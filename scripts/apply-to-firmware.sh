#!/usr/bin/env bash
# Copy GeofenceBeaconModule sources into a meshtastic/firmware checkout and
# print the remaining hand-edits (Modules.cpp registration + Router::send gate).
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 /path/to/meshtastic/firmware" >&2
  exit 1
fi

FW=$(cd "$1" && pwd)
HERE=$(cd "$(dirname "$0")/.." && pwd)

echo "Firmware: $FW"
echo "SHA: $(git -C "$FW" log -1 --oneline || true)"

mkdir -p "$FW/src/modules" "$FW/src/mesh"
cp -v "$HERE/src/modules/"GeofenceBeacon*.{h,cpp} "$FW/src/modules/"
cp -v "$HERE/src/mesh/GeofenceBeaconTxGate.h" "$FW/src/mesh/"

echo
echo "Next (manual):"
echo "  1. Register the module — see $HERE/patches/Modules.cpp.registration.txt"
echo "  2. Apply Router gate — try: git -C \"$FW\" apply $HERE/patches/Router.send.geofence-beacon.diff"
echo "     (line numbers may drift; paste by hand from the diff if needed)"
echo "  3. Edit $FW/src/modules/GeofenceBeaconConfig.h (nav point, message, window)"
echo "  4. pio run -e heltec-v4   # or t-echo / rak4631"
