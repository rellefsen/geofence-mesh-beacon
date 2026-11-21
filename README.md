# GeofenceBeacon — Meshtastic firmware module

Repo: https://github.com/rellefsen/geofence-mesh-beacon

Drop-in `GeofenceBeaconModule` for [meshtastic/firmware](https://github.com/meshtastic/firmware).
A node stays RX-only until it is inside a GPS radius and/or a time window, then
sends a canned text beacon. See `HANDOFF.md` for design, TX gate, fail-safes,
and the test plan. See `APPLIED.md` for what was verified against a recent
firmware tip.

## Clone this repo

```bash
git clone https://github.com/rellefsen/geofence-mesh-beacon.git
cd geofence-mesh-beacon
```

## Apply into firmware

```bash
git clone --recurse-submodules https://github.com/meshtastic/firmware.git
./scripts/apply-to-firmware.sh /path/to/firmware
# Then hand-edit Modules.cpp + Router::send (notes in patches/ and APPLIED.md)
# Edit src/modules/GeofenceBeaconConfig.h, then:
pio run -e heltec-v4
```

---


Drop-in notes for adding `GeofenceBeaconModule` to a local checkout of
[meshtastic/firmware](https://github.com/meshtastic/firmware). Start with
`HANDOFF.md` for the design, file-by-file change list, trigger logic, TX
gate, fail-safes, test plan, and legal notes.

The skeletons are compile-plausible against firmware `master` as researched
on 2026-11-20 (observed tip `6d41e279`, 2026-08-20 UTC) but **were not
built**. Treat every symbol as something to re-check after you clone.

## Drop into a firmware checkout

```bash
# Recurse submodules — protobufs lives at firmware/protobufs
# (https://github.com/meshtastic/protobufs.git).
git clone --recurse-submodules https://github.com/meshtastic/firmware.git
cd firmware
git log -1 --oneline   # record the SHA; re-verify names if it is not 6d41e279

# From this handoff directory:
cp src/modules/GeofenceBeaconModule.h   src/modules/GeofenceBeaconModule.cpp \
   src/modules/GeofenceBeaconConfig.h   /path/to/firmware/src/modules/
cp src/mesh/GeofenceBeaconTxGate.h      /path/to/firmware/src/mesh/

# Apply the Router::send gate (or paste the equivalent by hand;
# line numbers will drift):
#   git apply /path/to/geofence-beacon-handoff/patches/Router.send.geofence-beacon.diff
# Registration is a hand edit; see patches/Modules.cpp.registration.txt
```

Then edit `src/modules/GeofenceBeaconConfig.h` (nav point, radii, time
window, canned message) and build:

```bash
# User hardware, PlatformIO env names verified in variant platformio.ini:
pio run -e heltec-v4      # Heltec V4 OLED; also env:heltec-v4-tft
pio run -e t-echo         # LilyGO T-Echo (not t-echo-plus)
pio run -e rak4631        # RAK WisBlock RAK4631

pio run -e heltec-v4 -t upload
pio run -e t-echo -t upload
pio run -e rak4631 -t upload
```

Heltec V4 is marked `custom_meshtastic_requires_dfu = true`. If upload
fails, hold PRG, plug USB, release PRG, retry. T-Echo / RAK4631 are
nRF52840 and often need a double-reset UF2 / DFU path; the `.pio/build/<env>/`
output may include a `.uf2`. Re-check the official firmware build docs if
upload does not look like that on your checkout.

Heltec V4 has no onboard GNSS. The `heltec_v4` variant already defines
L76K-compatible pins (`GPS_TX_PIN` 38, `GPS_RX_PIN` 39, `PIN_GPS_EN` 34,
`PIN_GPS_RESET` 42). An external GPS on that header is the intended
source of a fix. T-Echo has onboard GPS. RAK4631 typically uses a
WisBlock GNSS module (e.g. RAK12500) when one is fitted.

Leave `config.lora.tx_enabled` **true**. The Router gate, plus role
`CLIENT_MUTE` and `rebroadcast_mode` `NONE`, is what silences everything
except the canned message. See `HANDOFF.md`.

Phase 1 is hard-coded config in the header. Do not turn on
`GEOFENCE_BEACON_FAKE_POSITION` except for a bench test.
