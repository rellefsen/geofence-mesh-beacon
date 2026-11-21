# GeofenceBeaconModule — Cursor handoff

Firmware modification for [meshtastic/firmware](https://github.com/meshtastic/firmware).
Research was done against **`master` on 2026-11-20** by fetching raw GitHub
files. The tip of `master` reported by the GitHub API at that time was
**`6d41e279f1f51bd59f687b9d441c1bf47b1594fc`** (2026-08-20 UTC). The
`develop` branch and later tags (including 2.8.x) may have moved; after
you clone, run `git log -1 --oneline` and re-verify every symbol marked
**TODO-verify**.

The skeletons in `src/` are compile-plausible but **were not built** in
the environment that produced this package. Do not claim they compile
until you have a successful `pio run`.

## Goal

Add `GeofenceBeaconModule`:

- Normal transmit is disabled (this node stays in RX).
- A canned text message is transmitted only when the node is inside a
  radius of a GPS nav point and/or inside a time window.
- AND/OR combination of those two triggers is configurable.
- Phase 1: hard-coded settings in `GeofenceBeaconConfig.h`.
- Phase 2: a new protobuf `ModuleConfig` block, configurable from the
  app / CLI.

Target hardware (user):

| Board | PlatformIO env (verified in variant ini on master) | GPS |
|---|---|---|
| Heltec V4 (two units) | `heltec-v4` (OLED), also `heltec-v4-tft` | No onboard GNSS. Variant already defines L76K-compatible pins: `GPS_TX_PIN` 38, `GPS_RX_PIN` 39, `PIN_GPS_EN` 34, `PIN_GPS_RESET` 42, `PIN_GPS_PPS` 41, `PIN_GPS_STANDBY` 40. Fit an external GPS on that header. There have been third-party GPS polarity issues on V4; confirm `GPS_EN` if the module does not power. |
| LilyGO T-Echo | `t-echo` (not `t-echo-plus`, not `t-echo-inkhud` unless that is the firmware flavor you actually run) | Onboard GNSS |
| RAK WisBlock RAK4631 | `rak4631` | Typically a WisBlock GNSS module (e.g. RAK12500) when fitted |

## Design summary

One module class, following `DetectionSensorModule` (a `SinglePortModule`
plus a private `concurrency::OSThread`) rather than the two-class
`RangeTestModule` / `RangeTestModuleRadio` split.

- Port: `meshtastic_PortNum_TEXT_MESSAGE_APP` so other nodes and the
  official apps show a normal text message.
- Timer: `runOnce()` every `GEOFENCE_BEACON_POLL_MS` (default 5 s).
- Position: `gpsStatus->getHasLock()`, `getLatitude()`, `getLongitude()`
  (`src/GPSStatus.h`). Those getters already return the NodeDB
  coordinates when `config.position.fixed_position` is set.
- Distance: `GeoCoord::latLongToMeter(...)` (`src/gps/GeoCoord.h`), same
  helper `RangeTestModule` uses. Inputs are decimal degrees; Meshtastic
  stores lat/lon as `int32` 1e-7 degrees.
- Time: `getValidTime(RTCQuality, bool local)` in `src/gps/RTC.h`.
  Returns `0` when `currentQuality < minQuality`. Qualities observed:
  `RTCQualityNone=0`, `RTCQualityDevice=1`, `RTCQualityFromNet=2`,
  `RTCQualityNTP=3`, `RTCQualityGPS=4`.
- Send path: `allocDataPacket()` (sets `decoded.portnum` to our port)
  then `service->sendToMesh(p, RX_SRC_LOCAL, ccToPhone)`
  (`MeshService.h`). That calls `router->sendLocal()`, which calls
  `Router::send()` for off-node destinations.
- TX gate: a short-lived packet-id allowlist in
  `src/mesh/GeofenceBeaconTxGate.h`, checked at the top of
  `Router::send()`. See [TX gate](#tx-gate) below.

Do not name this `MeshBeacon` and do not reuse `ModuleConfig.mesh_beacon`
(protobuf field 17, `since_firmware: 2.8.0`). That is a different
feature (broadcast a `MESH_BEACON_APP` offer of channel / preset /
region). `MeshBeaconModule` was **not** in `src/modules/` on firmware
`master` at the research snapshot; it may exist on `develop`. **TODO-verify**
after clone.

## File-by-file change list

Copy from this package, then edit firmware files.

| Path in firmware | Action |
|---|---|
| `src/modules/GeofenceBeaconConfig.h` | **Add.** Phase-1 knobs (nav point, radii, window, message, AND/OR, fail-safe quality, fake-position bench hook). |
| `src/modules/GeofenceBeaconModule.h` | **Add.** `SinglePortModule` + `OSThread` declaration. |
| `src/modules/GeofenceBeaconModule.cpp` | **Add.** Trigger logic, hysteresis, send path. |
| `src/mesh/GeofenceBeaconTxGate.h` | **Add.** Header-only allowlist used by the module and `Router::send`. |
| `src/mesh/Router.cpp` | **Patch.** Include the gate header. At the start of `Router::send()`, after the `isToUs(p)` check, drop packets the gate does not allow. See `patches/Router.send.geofence-beacon.diff`. Line numbers will drift. |
| `src/modules/Modules.cpp` | **Patch by hand.** Include the module header; `new GeofenceBeaconModule()` in `setupModules()`. See `patches/Modules.cpp.registration.txt`. The existing comment in this file is "If you are adding a new module, you must 'new' it here (or somewhere else)". RangeTest is a good neighbor. |
| `src/modules/Modules.h` | **Maybe.** Only if you want an `extern` there. The skeleton already has `extern GeofenceBeaconModule *geofenceBeaconModule` in its own header. **TODO-verify** whether other modules are also listed in `Modules.h`. |
| `platformio.ini` / variant ini | **Usually no change.** These envs compile `src/`. **TODO-verify** if a custom `build_src_filter` on your env would exclude the new files. Optional: `-DGEOFENCE_BEACON_ENABLED=1` etc. |

Phase 2 only (not in this package):

| Path | Action |
|---|---|
| `protobufs/` submodule (`https://github.com/meshtastic/protobufs.git`) | Add a new `ModuleConfig.GeofenceBeaconConfig` message and a **new field number** on `ModuleConfig`. Do **not** reuse field 17 (`mesh_beacon`). |
| Regenerated `src/mesh/generated/meshtastic/module_config.pb.h` (and friends) | Regen after the proto change. **TODO-verify** the current regen script name in the firmware repo (often a `bin/` python script or a PIO pre-step). |
| `src/modules/AdminModule.cpp` | Get/set handlers so the app/CLI can write the new block, same pattern as range test / detection sensor. |
| Android / Apple / web / Python clients | Consume the new protobuf. Out of scope for phase 1. |

## Trigger logic

Evaluated every poll interval. Defaults are in `GeofenceBeaconConfig.h`.

### Distance (haversine)

`GeoCoord::latLongToMeter(lat_a, lng_a, lat_b, lng_b)` already implements
the meter distance used elsewhere in firmware. Convert stored `latitude_i`
/ `longitude_i` with `* 1e-7` before calling it.

### Hysteresis

- Become **inside** when `distance <= ENTER_RADIUS_M` (default 200 m).
- Stay inside until `distance > EXIT_RADIUS_M` (default 250 m).
- If the usable position is lost, force `inside = false`. The next valid
  fix must re-enter; do not stick "inside" on a stale state.

If you set `EXIT < ENTER`, the skeleton still compiles but the edge will
flutter. Keep `EXIT >= ENTER`.

### Minimum beacon interval

`Throttle::isWithinTimespanMs(lastBeaconMs, GEOFENCE_BEACON_MIN_INTERVAL_MS)`
(included as `<Throttle.h>` by `RangeTestModule`; path was
`src/mesh/Throttle.h` on later commits, and a fetch of
`src/concurrency/Throttle.h` on master 404'd). **TODO-verify** the
include path on your SHA.

Default interval is 5 minutes. The trigger can stay true; we just do not
send again until the interval elapses. `lastBeaconMs` is updated when we
hand the packet to `sendToMesh`, not when we merely decide we would like
to send.

### Time window, including midnight wrap

Minutes past midnight, inclusive on both ends:

- If `start <= end` (e.g. 08:00–17:00): `start <= now <= end`.
- If `start > end` (e.g. 22:00–06:00): `now >= start || now <= end`.
- If either bound is `-1`, the time trigger is false.

`getValidTime(minQuality, local)` already applies TZ when `local` is
true (`getTZOffset()` in `RTC.cpp`). Default in the header is UTC
(`GEOFENCE_BEACON_TIME_LOCAL=0`) and `minQuality = RTCQualityGPS`.

### AND / OR

| `USE_GEO` | `USE_TIME` | Result |
|---|---|---|
| 1 | 0 | geo only |
| 0 | 1 | time only |
| 1 | 1, `COMBINE_OR=0` | geo **AND** time |
| 1 | 1, `COMBINE_OR=1` | geo **OR** time |
| 0 | 0 | never transmit |

### Fail-safes

- **No GPS fix → no geo trigger.** `hasUsablePosition()` requires
  `gpsStatus->getHasLock()` (or, if `GEOFENCE_BEACON_ALLOW_FIXED_POSITION`,
  a non-zero `config.position.fixed_position`), and non-zero lat/lon.
  `GEOFENCE_BEACON_FAKE_POSITION` is a bench-only override; leave it 0
  in the field.
- **No valid time → time trigger is false.** `getValidTime(...) == 0`
  means the RTC is below the configured quality. The geo trigger can
  still fire in OR mode, or alone if time is disabled.
- If you need "time trigger stays off until *our* GPS has set the
  clock", keep `GEOFENCE_BEACON_MIN_RTC_QUALITY` at `RTCQualityGPS` (4).
  Lower it to `RTCQualityFromNet` (2) only if mesh time is acceptable.

## TX gate

`config.lora.tx_enabled` is the wrong lever for "silence everything
except the beacon":

- `RadioLibInterface::send()` refuses the packet when
  `disabled || !config.lora.tx_enabled` and returns `ERRNO_DISABLED`.
- `RadioLibInterface::startSend()` does the same check again.
- By the time the packet reaches the radio interface,
  `Router::send()` has already run `perhapsEncode()`, so
  `decoded.portnum` is no longer visible. A portnum exception at the
  radio layer will not work.

So: **leave `config.lora.tx_enabled` true** and filter in
`Router::send()` *before* encryption, using a packet-id allowlist
the module fills immediately before `service->sendToMesh()`.

A portnum check in `Router::send()` is also a poor fit. The canned
message uses `TEXT_MESSAGE_APP`, which is the same port as
phone-originated chats.

The allowlist keeps an id for ~30 s so a `ReliableRouter` retry of the
same id can still pass. **TODO-verify** that `FloodingRouter::send()`
(which calls `Router::send(p)` after `wasSeenRecently`) and any
`ReliableRouter::send` override still go through this function.

### Rebroadcast

`FloodingRouter::isRebroadcaster()` on master:

```cpp
return config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE &&
       config.device.rebroadcast_mode != meshtastic_Config_DeviceConfig_RebroadcastMode_NONE;
```

Set **both** `role = CLIENT_MUTE` and `rebroadcast_mode = NONE` as
defense in depth. The Router gate also drops `!isFromUs(p)` while it is
on, so relays should not go out even if role/mode are wrong. Still set
them: other code paths (cancel-dupe, late rebroadcast, etc.) look at
`isRebroadcaster()`.

Also turn off periodic originations you do not want wasting work
(position broadcast interval, nodeinfo broadcast, telemetry, neighbor
info, MQTT, range test, detection sensor, mesh beacon if present). The
gate will drop them if they still try.

### Implications (read this before flashing a mesh you care about)

While the gate is on, this node will **not** originate:

- NodeInfo / Position / Telemetry / NeighborInfo
- Phone- or serial-originated text, admin replies, traceroute replies
- ACKs / NAKs generated here (`RoutingModule`)

Effects:

- Other nodes may not "see" this unit as a full peer except when a
  canned message is actually sent (`from` is still this node's id, so a
  node entry can appear from that text alone).
- A station that DMs this node will not get an ACK over the air. They
  may retry.
- This node will **not** extend the mesh for anyone else (intentional).
- RX is unchanged: incoming packets are still received and processed.
- MQTT uplink of our own packets, if enabled, would only see the
  allowed beacons (`Router::send` publishes to MQTT only for `isFromUs`
  after encode — and only if the packet was not dropped first).

`ERRNO_DISABLED` is used as the drop return. **TODO-verify** that
`sendLocal` / `sendToMesh` handle that the same way as a radio-disabled
send (they must not leak the packet; `Router::send` already
`packetPool.release(p)` before returning). `MeshService::sendToMesh`
releases on `ERRNO_SHOULD_RELEASE`; if `ERRNO_DISABLED` is the wrong
code, the packet could be leaked or double-freed. Check this first if
you see heap or "packet already released" issues.

## Device config to use with the firmware change

Recommended settings, applied from the app or CLI after flash:

- `device.role` = `CLIENT_MUTE`
- `device.rebroadcast_mode` = `NONE`
- `lora.tx_enabled` = **true** (see above)
- Position / GPS enabled on Heltec V4 once the external GPS is attached;
  T-Echo and RAK as usual
- `position.fixed_position` only for bench tests
- Disable or lengthen NodeInfo / Position / Telemetry broadcasts

## Phase 1 vs phase 2

**Phase 1:** edit `GeofenceBeaconConfig.h`, rebuild, flash. This is
enough to prove the trigger and the gate on the user's three board
types.

**Phase 2:** promote the same fields into protobufs
(`https://github.com/meshtastic/protobufs.git`, submodule path
`firmware/protobufs`). Add a new `ModuleConfig` field; do not reuse
`mesh_beacon = 17`. Wire `AdminModule` get/set, then the official apps
once the proto is published. Until then, any protobuf bump has to stay
on a private fork or the stock apps will not know the field.

## Test plan

Serial logging is already in the skeleton (`LOG_INFO` / `LOG_DEBUG` with
the `GeofenceBeacon:` prefix). Use the USB console at the usual
Meshtastic baud.

1. **Bench, fake / fixed position.** Set
   `GEOFENCE_BEACON_FAKE_POSITION=1` (or set a fixed position in the app
   at the nav point). Put the time window around "now" or disable time
   and use geo only. Confirm a canned message goes out once, then is
   held off for `MIN_INTERVAL`. Watch the log for `TX id=...`.
2. **Outside the fence.** Move the fake lat/lon well beyond `EXIT_RADIUS`.
   Confirm `inside=0` and no further TX.
3. **Hysteresis.** Place the fake position between ENTER and EXIT after
   having been inside; it should stay inside. After exiting, place it
   between ENTER and EXIT again; it should stay outside until `<= ENTER`.
4. **Time window.** Disable geo, enable time. Confirm TX only inside the
   window. Test a midnight wrap (`start > end`).
5. **AND vs OR.** With geo false and time true, AND must not TX, OR must.
6. **No GPS fail-safe.** `FAKE_POSITION=0`, no fixed position, GPS
   unlocked. Geo trigger must stay false. Log should show `pos=0`.
7. **No valid time fail-safe.** Require `RTCQualityGPS` with GPS
   unpowered / no time pulse. Time trigger must stay false (`time=0`).
8. **TX gate.** From the app, try to send a regular text, or wait for a
   NodeInfo / position broadcast. Confirm the log line
   `GeofenceBeacon TX gate dropped` and that a second node does not
   hear those packets. Confirm the second node *does* hear the canned
   message when the trigger is live.
9. **RX still works.** Have the second node send a text; this node
   should display / log it.
10. **Rebroadcast off.** Have a third node talk through this one. This
    node must not relay, with `CLIENT_MUTE` + `NONE` and with the gate.
11. **Hardware GPS.** Repeat 1–7 with a real fix on T-Echo, RAK+GNSS,
    and Heltec V4 + external GPS. Do not leave `FAKE_POSITION` on.
12. **Interval and duty cycle.** Confirm we do not flood the channel if
    the trigger stays true (walk around inside the radius).

## Build and flash

Official background: <https://meshtastic.org/docs/development/firmware/build/>
(re-check; docs move). After the files are in the checkout:

```bash
pio run -e heltec-v4
pio run -e heltec-v4-tft    # only if the unit is the TFT variant
pio run -e t-echo
pio run -e rak4631

pio run -e heltec-v4 -t upload
pio run -e t-echo -t upload
pio run -e rak4631 -t upload
```

Notes:

- Heltec V4: `custom_meshtastic_requires_dfu = true` in
  `variants/esp32s3/heltec_v4/platformio.ini`. If the port is not found,
  hold PRG, connect USB, release PRG, retry.
- T-Echo / RAK4631: nRF52840. Upload may be UF2 (double-click reset,
  copy `.pio/build/<env>/firmware.uf2`) or a debug probe. **TODO-verify**
  the exact artifact name and upload method on your PlatformIO version.
- Always `--recurse-submodules` when cloning, or `git submodule update
  --init --recursive` afterwards. A missing `protobufs/` checkout will
  not generate headers.

## Legal note

This is not legal advice; it is a firmware/ops reminder.

- **Ham / licensed Meshtastic mode:** the firmware flag observed in
  current sources is `owner.is_licensed` on the `meshtastic_User`
  record (not a `config.device.is_licensed` field). **TODO-verify** the
  exact field on your protobuf revision. When licensed mode is on,
  channel PSKs are cleared and traffic is sent in the clear. Encryption
  must stay off. Put the operator's callsign in the canned message
  (and typically in the long name) so every beacon identifies the
  station.
- **ISM / unlicensed (e.g. US 915 MHz Part 15):** licensed-mode rules
  do not apply. Channel encryption may be used. Stay within the
  region's duty cycle and power limits; `Router::send` already aborts
  when the duty cycle is exceeded unless `config.lora.override_duty_cycle`
  is set.

## Grounded names (research snapshot)

Use these as the starting point; re-check after clone.

- Registration: `void setupModules()` in `src/modules/Modules.cpp`.
- `CannedMessageModule` is constructed only when `HAS_SCREEN` and
  display mode is not `COLOR`. Do not depend on it for the beacon text.
- `RangeTestModule` is an `OSThread`; its radio half is a
  `SinglePortModule` on `meshtastic_PortNum_RANGE_TEST_APP`. Sender uses
  `allocDataPacket()`, fills `decoded.payload`, `service->sendToMesh(p)`.
- `DetectionSensorModule` is the closer one-class pattern:
  `SinglePortModule("detection", meshtastic_PortNum_DETECTION_SENSOR_APP),
  OSThread("DetectionSensor")`.
- `SinglePortModule::allocDataPacket()` is in `src/mesh/SinglePortModule.h`:
  `router->allocForSending()` then `p->decoded.portnum = ourPortNum`.
- `Router::allocForSending()` sets `from`, `to=NODENUM_BROADCAST`,
  default hop limit, a unique `id`, and `rx_time = getValidTime(RTCQualityFromNet)`.
- `Router::sendLocal()` delivers local dests to the receive queue;
  otherwise calls `send(p)`.
- `Router::send()` (around line 295 on the fetched file): duty cycle,
  clears `want_ack` on broadcasts, `p->from = getFrom(p)`, encode,
  `iface->send(p)`.
- Position: `extern meshtastic::GPSStatus *gpsStatus` in `src/GPSStatus.h`.
  `extern meshtastic_Position localPosition` and
  `NodeDB::getMeshNode` / `getNodeNum` / `setLocalPosition` /
  `hasLocalPositionSinceBoot` in `src/mesh/NodeDB.h`.
  `MeshService::onGPSChanged` updates `nodeDB->updatePosition(...)`.
  `nodeDB->hasValidPosition(node)` is used by `trySendPosition`.
- `extern MeshService *service;` `extern Router *router;`
  `extern NodeDB *nodeDB;`
- `Throttle::isWithinTimespanMs(start, span)` — true while still inside
  the span since `start`.
- Roles / rebroadcast: `meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE`,
  `meshtastic_Config_DeviceConfig_RebroadcastMode_NONE`.
- Private app port range, if you ever stop using `TEXT_MESSAGE_APP`, is
  256–511 per `portnums.proto`. Not needed for phase 1.

## TODO-verify checklist

Copy this into the Cursor task and tick items against the checkout.

- [ ] Firmware SHA vs `6d41e279`; if `develop` / a 2.8.x tag, re-read
      `Router::send`, `GPSStatus`, `NodeDB`, `Modules.cpp`.
- [ ] Whether `MeshBeaconModule` now exists and how it is named, so this
      module does not collide.
- [ ] `NodeInfoLite.position` still present after any NodeDB "position
      decoupling" work; fallback to `localPosition` / `copyNodePosition`.
- [ ] `Throttle.h` include path.
- [ ] `meshtastic_Constants_DATA_PAYLOAD_LEN` vs `MAX_LORA_PAYLOAD_LEN`.
- [ ] `NODENUM_BROADCAST` is `0xFFFFFFFF`.
- [ ] `ERRNO_DISABLED` is a safe return from `Router::send` (no leak /
      double-free via `sendToMesh`).
- [ ] `ReliableRouter::send` (if it exists) still ends in `Router::send`.
- [ ] `owner.is_licensed` field name and licensed-mode encryption
      behavior on this protobuf revision.
- [ ] Regenerated protobuf workflow for phase 2; next free
      `ModuleConfig` field number (do not reuse 17).
- [ ] Heltec V4 env: OLED `heltec-v4` vs `heltec-v4-tft` for the actual
      units; GPS_EN polarity with the chosen external GPS.
- [ ] T-Echo env: `t-echo` vs `t-echo-inkhud` vs `t-echo-plus`.
- [ ] nRF52 upload artifact / DFU steps.
- [ ] `setStartDelay()` vs a hard-coded 5 s first-run delay.
- [ ] Channel 0 default/public channel policy (DetectionSensor refuses
      to send on the default public channel). Decide whether this beacon
      should do the same.
- [ ] Duty-cycle / `airTime->isTxAllowedChannelUtil` courtesy check
      before send (RangeTest uses this). Optional; do not fail silently.

## Suggested Cursor implementation order

1. Clone firmware with submodules, note SHA, tick the checklist above.
2. Copy the four new source files.
3. Register the module in `Modules.cpp`.
4. Apply the `Router::send` patch and inspect the surrounding function.
5. Fill in real nav-point / message values in the config header, with
   `FAKE_POSITION=1` for the first bench build.
6. `pio run -e heltec-v4` (or the board you have in hand). Fix compile
   errors (includes, enum names, ErrorCode) before flashing.
7. Walk the test plan on USB serial, then repeat with a real GPS fix
   and `FAKE_POSITION=0`.
8. Only then start phase 2 protobuf work if app-side config is needed.
