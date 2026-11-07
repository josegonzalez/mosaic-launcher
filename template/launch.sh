#!/bin/sh
# This pak is a plugin for the Launcher pak, which loads it on its own.
# Opening it from Tools shows the Launcher's Plugins page, to turn it on or off.

LAUNCHER="$SDCARD_PATH/Tools/$PLATFORM/Launcher.pak/launch.sh"
if [ -x "$LAUNCHER" ]; then
	exec "$LAUNCHER" plugins
fi
echo "Launcher.pak isn't installed; get it from the Pak Store" >&2
exit 1
