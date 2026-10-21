#!/usr/bin/env bash
# Fetches Bluepad32 (with the BTstack commit it pins) into deps/ and installs
# BTstack as an ESP-IDF component inside it, as Bluepad32's ESP32 guide says.
# Run it once from any directory; it does nothing if deps/ is already there.
set -euo pipefail

BLUEPAD32_TAG=4.2.0
BLUEPAD32_COMMIT=6efa7123fe8badf5a40ad1205743a80b31c00ea4

HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="$HERE/deps/bluepad32"

if [ -d "$DEST/src/components/btstack" ]; then
	echo "deps.sh: Bluepad32 $BLUEPAD32_TAG is already in $DEST"
	exit 0
fi

rm -rf "$DEST"
git clone --quiet --depth 1 --branch "$BLUEPAD32_TAG" https://github.com/ricardoquesada/bluepad32.git "$DEST"
actual="$(git -C "$DEST" rev-parse HEAD)"
if [ "$actual" != "$BLUEPAD32_COMMIT" ]; then
	echo "deps.sh: tag $BLUEPAD32_TAG is $actual, expected $BLUEPAD32_COMMIT" >&2
	exit 1
fi
git -C "$DEST" submodule update --quiet --init --depth 1 external/btstack

# integrate_btstack.py copies BTstack into $IDF_PATH/components; pointing IDF_PATH at
# Bluepad32's src/ puts it next to the bluepad32 component instead.
(cd "$DEST/external/btstack/port/esp32" && IDF_PATH="$DEST/src" python3 ./integrate_btstack.py > /dev/null)
echo "deps.sh: Bluepad32 $BLUEPAD32_TAG and BTstack $(git -C "$DEST/external/btstack" rev-parse --short HEAD) ready in $DEST"
