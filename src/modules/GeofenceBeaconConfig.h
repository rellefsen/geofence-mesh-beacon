#pragma once

/*
 * Phase-1 hard-coded config for GeofenceBeaconModule.
 *
 * Edit this header and rebuild. Phase 2 should move these fields into a
 * protobuf ModuleConfig block in the meshtastic/protobufs submodule
 * (https://github.com/meshtastic/protobufs.git). Do NOT reuse
 * ModuleConfig.mesh_beacon (field 17, since firmware 2.8.0) — that is a
 * different feature (channel/preset advertisement), and MeshBeaconModule
 * was not present under src/modules on firmware master as of the research
 * snapshot. See HANDOFF.md.
 *
 * UNTESTED. Names and units should be re-checked against the firmware
 * checkout you actually build.
 */

#ifndef MESHTASTIC_EXCLUDE_GEOFENCEBEACON
#define MESHTASTIC_EXCLUDE_GEOFENCEBEACON 0
#endif

// Master enable. If 0, Modules.cpp should not construct the module.
#ifndef GEOFENCE_BEACON_ENABLED
#define GEOFENCE_BEACON_ENABLED 1
#endif

// When 1, Router::send drops every originated packet except those
// pre-allowed by this module, and also drops relays. Leave
// config.lora.tx_enabled = true; RadioLibInterface will drop beacons too
// if that flag is false.
#ifndef GEOFENCE_BEACON_TX_GATE
#define GEOFENCE_BEACON_TX_GATE 1
#endif

// How often the OSThread evaluates the trigger, in milliseconds.
#ifndef GEOFENCE_BEACON_POLL_MS
#define GEOFENCE_BEACON_POLL_MS 5000
#endif

// Minimum time between successful beacon transmissions.
#ifndef GEOFENCE_BEACON_MIN_INTERVAL_MS
#define GEOFENCE_BEACON_MIN_INTERVAL_MS (5 * 60 * 1000UL)
#endif

// Nav-point latitude / longitude in degrees (WGS84). Converted to
// Meshtastic 1e-7 integer degrees at runtime.
#ifndef GEOFENCE_BEACON_LAT_DEG
#define GEOFENCE_BEACON_LAT_DEG 40.7608
#endif
#ifndef GEOFENCE_BEACON_LON_DEG
#define GEOFENCE_BEACON_LON_DEG -111.8910
#endif

// Hysteresis: enter at <= ENTER meters, remain inside until > EXIT meters.
#ifndef GEOFENCE_BEACON_ENTER_RADIUS_M
#define GEOFENCE_BEACON_ENTER_RADIUS_M 200.0f
#endif
#ifndef GEOFENCE_BEACON_EXIT_RADIUS_M
#define GEOFENCE_BEACON_EXIT_RADIUS_M 250.0f
#endif

// Time window in minutes past midnight. Inclusive. A window with
// start > end is treated as wrapping through midnight (e.g. 22:00–06:00).
// Set both to -1 to disable the time trigger (geo-only, if geo is on).
#ifndef GEOFENCE_BEACON_WINDOW_START_MIN
#define GEOFENCE_BEACON_WINDOW_START_MIN 8 * 60
#endif
#ifndef GEOFENCE_BEACON_WINDOW_END_MIN
#define GEOFENCE_BEACON_WINDOW_END_MIN 17 * 60
#endif

// Independent trigger enables. If both are on, COMBINE selects AND or OR.
#ifndef GEOFENCE_BEACON_USE_GEO
#define GEOFENCE_BEACON_USE_GEO 1
#endif
#ifndef GEOFENCE_BEACON_USE_TIME
#define GEOFENCE_BEACON_USE_TIME 1
#endif

// 0 = AND (geo and time must both be true), 1 = OR (either is enough).
// If only one of USE_GEO / USE_TIME is on, COMBINE is ignored.
#ifndef GEOFENCE_BEACON_COMBINE_OR
#define GEOFENCE_BEACON_COMBINE_OR 0
#endif

// 1 = evaluate the window in local time (getValidTime(..., /*local=*/true)).
// 0 = UTC. Requires the device TZ to be set for local to be meaningful.
#ifndef GEOFENCE_BEACON_TIME_LOCAL
#define GEOFENCE_BEACON_TIME_LOCAL 0
#endif

// Minimum RTC quality for a valid time trigger.
// RTCQualityGPS (4) is the strict "synced to our GPS" setting.
// RTCQualityFromNet (2) also accepts mesh-provided time (useful without GPS
// time, or for bench tests). See src/gps/RTC.h.
#ifndef GEOFENCE_BEACON_MIN_RTC_QUALITY
#define GEOFENCE_BEACON_MIN_RTC_QUALITY 4 /* RTCQualityGPS */
#endif

// If 1, a configured fixed_position (or the bench-test override below)
// counts as a usable position even without gpsStatus->getHasLock().
#ifndef GEOFENCE_BEACON_ALLOW_FIXED_POSITION
#define GEOFENCE_BEACON_ALLOW_FIXED_POSITION 1
#endif

// Bench-test hook: if 1, ignore GPS and use the nav point (or the
// override below) as the current position. Leave 0 for field use.
#ifndef GEOFENCE_BEACON_FAKE_POSITION
#define GEOFENCE_BEACON_FAKE_POSITION 0
#endif

// Optional fake-position override in degrees. If FAKE_POSITION is 1 and
// these are left at the nav point, the node looks "inside" the fence.
#ifndef GEOFENCE_BEACON_FAKE_LAT_DEG
#define GEOFENCE_BEACON_FAKE_LAT_DEG GEOFENCE_BEACON_LAT_DEG
#endif
#ifndef GEOFENCE_BEACON_FAKE_LON_DEG
#define GEOFENCE_BEACON_FAKE_LON_DEG GEOFENCE_BEACON_LON_DEG
#endif

// UTF-8 canned message. Truncated to meshtastic_Constants_DATA_PAYLOAD_LEN
// at send time. In ham licensed mode, include the callsign in this text.
#ifndef GEOFENCE_BEACON_MESSAGE
#define GEOFENCE_BEACON_MESSAGE "Geofence beacon"
#endif

// Destination node number. 0xFFFFFFFF is NODENUM_BROADCAST on current
// firmware (TODO-verify in MeshTypes.h).
#ifndef GEOFENCE_BEACON_DEST
#define GEOFENCE_BEACON_DEST 0xFFFFFFFF
#endif

// Channel index in the node's channel table (0 = primary).
#ifndef GEOFENCE_BEACON_CHANNEL
#define GEOFENCE_BEACON_CHANNEL 0
#endif

// If >= 0, override hop_limit on the beacon packet. -1 keeps the default
// from Router::allocForSending(). 0 is heard by neighbors only.
#ifndef GEOFENCE_BEACON_HOP_LIMIT
#define GEOFENCE_BEACON_HOP_LIMIT -1
#endif

// If 1, also copy the outgoing beacon to a connected phone/app.
#ifndef GEOFENCE_BEACON_CC_TO_PHONE
#define GEOFENCE_BEACON_CC_TO_PHONE 1
#endif
