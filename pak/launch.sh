#!/bin/sh
# Launcher.pak - turns the plugin-capable launcher on or off, and sets up its
# plugins.
#
# The launcher itself takes over from stock nextui.elf through boot.sh. Its
# settings screens are drawn by the same binary, in settings mode; plugin
# paks open the plugin list with: launch.sh plugins

PAK_DIR="$(cd "$(dirname "$0")" && pwd)"
PAK_NAME="$(basename "$PAK_DIR" .pak)"
# shellcheck source=pak/bin/common.sh
. "$PAK_DIR/bin/common.sh"

mkdir -p "$LOGS_PATH" "$STATE_DIR"
exec >>"$LOGS_PATH/$PAK_NAME.txt" 2>&1
echo "$0 $*"

exec "$OUR_BIN" --settings "$@"
