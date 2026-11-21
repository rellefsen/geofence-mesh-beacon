#pragma once

/*
 * TX-gate helper shared by GeofenceBeaconModule and Router::send.
 *
 * Header-only so a firmware drop-in does not require a new compile unit
 * beyond the module .cpp and the Router.cpp patch.
 *
 * Why a packet-id allowlist, not a portnum check:
 *   The canned message should use meshtastic_PortNum_TEXT_MESSAGE_APP so
 *   other nodes and the official apps display it as ordinary text. A
 *   portnum check in Router::send would also pass phone-originated texts.
 *   After perhapsEncode(), RadioLibInterface only sees the encrypted
 *   packet, so a portnum hole there is also impossible.
 *
 * UNTESTED.
 */

#include "MeshTypes.h"
#include "modules/GeofenceBeaconConfig.h"
#include "configuration.h"

#ifndef GEOFENCE_BEACON_ALLOWLIST_SIZE
#define GEOFENCE_BEACON_ALLOWLIST_SIZE 4
#endif

#ifndef GEOFENCE_BEACON_ALLOWLIST_TTL_MS
#define GEOFENCE_BEACON_ALLOWLIST_TTL_MS 30000UL
#endif

struct GeofenceBeaconAllowSlot {
    PacketId id = 0;
    uint32_t expiresAtMs = 0;
};

inline GeofenceBeaconAllowSlot *geofenceBeaconAllowlist()
{
    static GeofenceBeaconAllowSlot slots[GEOFENCE_BEACON_ALLOWLIST_SIZE];
    return slots;
}

inline bool geofenceBeaconTxGateEnabled()
{
#if GEOFENCE_BEACON_ENABLED && GEOFENCE_BEACON_TX_GATE
    return true;
#else
    return false;
#endif
}

inline void geofenceBeaconAllowPacketId(PacketId id)
{
    if (id == 0) {
        return;
    }
    GeofenceBeaconAllowSlot *slots = geofenceBeaconAllowlist();
    const uint32_t now = millis();
    int evict = 0;
    for (int i = 0; i < GEOFENCE_BEACON_ALLOWLIST_SIZE; i++) {
        if (slots[i].id == 0 || (int32_t)(now - slots[i].expiresAtMs) >= 0) {
            evict = i;
            break;
        }
        // Evict the slot that expires soonest if all are occupied.
        if ((int32_t)(slots[i].expiresAtMs - slots[evict].expiresAtMs) < 0) {
            evict = i;
        }
    }
    slots[evict].id = id;
    slots[evict].expiresAtMs = now + GEOFENCE_BEACON_ALLOWLIST_TTL_MS;
}

inline bool geofenceBeaconIsAllowed(PacketId id)
{
    if (id == 0) {
        return false;
    }
    GeofenceBeaconAllowSlot *slots = geofenceBeaconAllowlist();
    const uint32_t now = millis();
    for (int i = 0; i < GEOFENCE_BEACON_ALLOWLIST_SIZE; i++) {
        if (slots[i].id == id && (int32_t)(now - slots[i].expiresAtMs) < 0) {
            // Do not consume: ReliableRouter may retry the same id.
            return true;
        }
    }
    return false;
}

