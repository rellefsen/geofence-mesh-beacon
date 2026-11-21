/*
 * GeofenceBeaconModule.cpp
 *
 * UNTESTED skeleton. Intended to compile against meshtastic/firmware
 * after the Modules.cpp registration and Router::send patch in
 * ../patches/ are applied, but this was not built in the handoff
 * environment.
 */

#include "GeofenceBeaconModule.h"
#include "GeofenceBeaconConfig.h"
#include "mesh/GeofenceBeaconTxGate.h"

#include "GPSStatus.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "gps/GeoCoord.h"
#include "mesh-pb-constants.h"

#include <Arduino.h>
#include <Throttle.h>
#include <math.h>
#include <string.h>

GeofenceBeaconModule *geofenceBeaconModule;

GeofenceBeaconModule::GeofenceBeaconModule()
    : SinglePortModule("GeofenceBeacon", meshtastic_PortNum_TEXT_MESSAGE_APP),
      concurrency::OSThread("GeofenceBeacon")
{
}

int32_t GeofenceBeaconModule::runOnce()
{
#if MESHTASTIC_EXCLUDE_GEOFENCEBEACON
    return disable();
#else
    if (!GEOFENCE_BEACON_ENABLED) {
        LOG_INFO("GeofenceBeacon: disabled by GeofenceBeaconConfig.h");
        return disable();
    }

    if (firstTime) {
        firstTime = false;
        LOG_INFO("GeofenceBeacon: init poll=%u ms enter=%.0f m exit=%.0f m "
                 "geo=%d time=%d combine=%s gate=%d",
                 (unsigned)GEOFENCE_BEACON_POLL_MS, GEOFENCE_BEACON_ENTER_RADIUS_M,
                 GEOFENCE_BEACON_EXIT_RADIUS_M, GEOFENCE_BEACON_USE_GEO, GEOFENCE_BEACON_USE_TIME,
                 GEOFENCE_BEACON_COMBINE_OR ? "OR" : "AND", GEOFENCE_BEACON_TX_GATE);
        // Give GPS / RTC a moment after boot, same idea as MeshModule's
        // setStartDelay() (TODO-verify if you prefer that helper).
        return 5000;
    }

    int32_t lat_i = 0;
    int32_t lon_i = 0;
    const bool havePos = hasUsablePosition(lat_i, lon_i);

    uint32_t epochSecs = 0;
    const bool haveTime = hasUsableTime(epochSecs);

    bool geoActive = false;
    float dist = -1.0f;
    if (GEOFENCE_BEACON_USE_GEO) {
        if (!havePos) {
            // Fail-safe: no GPS fix (and no allowed fixed/fake position) =>
            // the geo trigger is false. Hysteresis is cleared so a later
            // fix must re-enter the fence.
            if (insideFence) {
                LOG_INFO("GeofenceBeacon: lost usable position, leaving fence");
            }
            insideFence = false;
            geoActive = false;
        } else {
            dist = distanceMeters(lat_i, lon_i);
            updateHysteresis(dist);
            geoActive = insideFence;
        }
    }

    bool timeActive = false;
    if (GEOFENCE_BEACON_USE_TIME) {
        // Fail-safe: no valid time => time trigger is false.
        timeActive = haveTime && inTimeWindow(epochSecs);
    }

    const bool shouldTx = triggersSatisfied(geoActive, timeActive);

    LOG_INFO("GeofenceBeacon: pos=%d lat=%d lon=%d dist=%.1f inside=%d "
             "time=%d epoch=%u window=%d geo=%d timeTrig=%d shouldTx=%d",
             havePos, (int)lat_i, (int)lon_i, (double)dist, insideFence, haveTime,
             (unsigned)epochSecs, timeActive, geoActive, timeActive, shouldTx);

    if (shouldTx) {
        if (!intervalElapsed()) {
            LOG_DEBUG("GeofenceBeacon: trigger live, waiting for min interval");
        } else {
            sendCannedMessage();
        }
    }

    return GEOFENCE_BEACON_POLL_MS;
#endif
}

bool GeofenceBeaconModule::hasUsablePosition(int32_t &lat_i, int32_t &lon_i) const
{
#if GEOFENCE_BEACON_FAKE_POSITION
    lat_i = (int32_t)lround(GEOFENCE_BEACON_FAKE_LAT_DEG * 1e7);
    lon_i = (int32_t)lround(GEOFENCE_BEACON_FAKE_LON_DEG * 1e7);
    return (lat_i != 0 || lon_i != 0);
#else
    // gpsStatus->getLatitude/Longitude already return the NodeDB
    // position when config.position.fixed_position is set (src/GPSStatus.h).
    if (gpsStatus) {
        const bool locked = gpsStatus->getHasLock();
        const bool fixed = config.position.fixed_position && GEOFENCE_BEACON_ALLOW_FIXED_POSITION;
        if (locked || fixed) {
            lat_i = gpsStatus->getLatitude();
            lon_i = gpsStatus->getLongitude();
            if (lat_i != 0 || lon_i != 0) {
                return true;
            }
        }
    }

    // Fallback: localPosition / our NodeInfoLite slot.
    // TODO-verify: some newer branches have started decoupling position
    // from NodeInfoLite. If getMeshNode(...)->position disappears, use
    // localPosition (extern in NodeDB.h) or nodeDB->copyNodePosition().
    if (GEOFENCE_BEACON_ALLOW_FIXED_POSITION && config.position.fixed_position && nodeDB) {
        meshtastic_NodeInfoLite *self = nodeDB->getMeshNode(nodeDB->getNodeNum());
        if (self && (self->position.latitude_i != 0 || self->position.longitude_i != 0)) {
            lat_i = self->position.latitude_i;
            lon_i = self->position.longitude_i;
            return true;
        }
        if (localPosition.latitude_i != 0 || localPosition.longitude_i != 0) {
            lat_i = localPosition.latitude_i;
            lon_i = localPosition.longitude_i;
            return true;
        }
    }

    return false;
#endif
}

bool GeofenceBeaconModule::hasUsableTime(uint32_t &epochSecs) const
{
    // getValidTime returns 0 when currentQuality < minQuality (src/gps/RTC.cpp).
    epochSecs = getValidTime((RTCQuality)GEOFENCE_BEACON_MIN_RTC_QUALITY,
                             (bool)GEOFENCE_BEACON_TIME_LOCAL);
    return epochSecs != 0;
}

bool GeofenceBeaconModule::inTimeWindow(uint32_t epochSecs) const
{
    const int startMin = GEOFENCE_BEACON_WINDOW_START_MIN;
    const int endMin = GEOFENCE_BEACON_WINDOW_END_MIN;
    if (startMin < 0 || endMin < 0) {
        return false;
    }

    // epochSecs is already local or UTC based on the getValidTime flag.
    const int nowMin = (int)((epochSecs % 86400UL) / 60UL);

    if (startMin <= endMin) {
        // Normal window, e.g. 08:00–17:00 inclusive of both ends.
        return nowMin >= startMin && nowMin <= endMin;
    }
    // Midnight wrap, e.g. 22:00–06:00.
    return nowMin >= startMin || nowMin <= endMin;
}

float GeofenceBeaconModule::distanceMeters(int32_t lat_i, int32_t lon_i) const
{
    // GeoCoord::latLongToMeter takes decimal degrees (see RangeTestModule).
    const double lat = lat_i * 1e-7;
    const double lon = lon_i * 1e-7;
    return GeoCoord::latLongToMeter(lat, lon, GEOFENCE_BEACON_LAT_DEG, GEOFENCE_BEACON_LON_DEG);
}

void GeofenceBeaconModule::updateHysteresis(float distMeters)
{
    if (!insideFence) {
        if (distMeters <= GEOFENCE_BEACON_ENTER_RADIUS_M) {
            insideFence = true;
            LOG_INFO("GeofenceBeacon: entered fence dist=%.1f m", (double)distMeters);
        }
    } else {
        if (distMeters > GEOFENCE_BEACON_EXIT_RADIUS_M) {
            insideFence = false;
            LOG_INFO("GeofenceBeacon: exited fence dist=%.1f m", (double)distMeters);
        }
    }
}

bool GeofenceBeaconModule::triggersSatisfied(bool geoActive, bool timeActive) const
{
    const bool useGeo = GEOFENCE_BEACON_USE_GEO;
    const bool useTime = GEOFENCE_BEACON_USE_TIME;
    if (!useGeo && !useTime) {
        return false;
    }
    if (useGeo && !useTime) {
        return geoActive;
    }
    if (!useGeo && useTime) {
        return timeActive;
    }
    if (GEOFENCE_BEACON_COMBINE_OR) {
        return geoActive || timeActive;
    }
    return geoActive && timeActive;
}

bool GeofenceBeaconModule::intervalElapsed() const
{
    if (lastBeaconMs == 0) {
        return true;
    }
    // isWithinTimespanMs is true when (now - start) is still inside the span.
    return !Throttle::isWithinTimespanMs(lastBeaconMs, GEOFENCE_BEACON_MIN_INTERVAL_MS);
}

void GeofenceBeaconModule::sendCannedMessage()
{
    if (!service || !router) {
        LOG_ERROR("GeofenceBeacon: service/router not ready");
        return;
    }

    meshtastic_MeshPacket *p = allocDataPacket();
    if (!p) {
        LOG_ERROR("GeofenceBeacon: allocDataPacket failed");
        return;
    }

    p->to = (NodeNum)GEOFENCE_BEACON_DEST;
    p->channel = (ChannelIndex)GEOFENCE_BEACON_CHANNEL;
    p->want_ack = false;
    p->decoded.want_response = false;
#if GEOFENCE_BEACON_HOP_LIMIT >= 0
    p->hop_limit = (uint8_t)GEOFENCE_BEACON_HOP_LIMIT;
#endif

    const char *msg = GEOFENCE_BEACON_MESSAGE;
    // TODO-verify meshtastic_Constants_DATA_PAYLOAD_LEN vs MAX_LORA_PAYLOAD_LEN
    // if the constant name has changed in your checkout.
    size_t len = strlen(msg);
    if (len > meshtastic_Constants_DATA_PAYLOAD_LEN) {
        len = meshtastic_Constants_DATA_PAYLOAD_LEN;
    }
    p->decoded.payload.size = len;
    memcpy(p->decoded.payload.bytes, msg, len);

    // Allow this packet id through Router::send *before* sendToMesh.
    geofenceBeaconAllowPacketId(p->id);

    LOG_INFO("GeofenceBeacon: TX id=0x%08x dest=0x%x ch=%u msg=%.*s", p->id, p->to,
             (unsigned)p->channel, (int)p->decoded.payload.size, p->decoded.payload.bytes);

    lastBeaconMs = millis();
    service->sendToMesh(p, RX_SRC_LOCAL, (bool)GEOFENCE_BEACON_CC_TO_PHONE);
}
