# Applied against meshtastic/firmware

Local integration checkout: `~/git/meshtastic-firmware`

- Tip at apply time: `fe89ac1 Update meshtastic/device-ui digest to fb3c069 (#12445)`
- Handoff research tip was `6d41e279` (older). Router::send and includes moved;
  patches were applied by hand.
- Fixes vs raw handoff skeletons:
  - `#include "gps/RTC.h"` (not `"RTC.h"`)
  - `GeofenceBeaconModule.h` includes `GeofenceBeaconConfig.h` so
    `GEOFENCE_BEACON_ENABLED` is visible from `Modules.cpp`
  - TX gate inserted after transparent-mode check in `Router::send`

## Local commands

```bash
cd ~/git/geofence-mesh-beacon/geofence-mesh-beacon
git pull

# Re-copy into a firmware tree:
./scripts/apply-to-firmware.sh ~/git/meshtastic-firmware

# Then build (PlatformIO required):
cd ~/git/meshtastic-firmware
pio run -e heltec-v4
```

Questionnaire defaults still in `GeofenceBeaconConfig.h` (edit before field use):

| Knob | Value |
|------|-------|
| Nav point | 40.7608, -111.8910 (SLC placeholder) |
| Enter / exit | 200 m / 250 m |
| Window | 08:00–17:00 UTC |
| Combine | AND (geo + time) |
| Message | `Geofence beacon` |
| Boards | heltec-v4, t-echo, rak4631 |
