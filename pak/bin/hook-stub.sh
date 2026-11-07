#!/bin/sh
# Installed by Launcher.pak as $USERDATA_PATH/.hooks/boot.d/00-launcher.sync.sh.
#
# Runs the pak's boot logic. When the pak has been uninstalled, it undoes a
# copy-mode install and deletes itself, so nothing is left in the boot path.
# It must stay self-contained: by then the pak is gone.

STATE_DIR="$USERDATA_PATH/launcher"
pak_dir=""
[ -f "$STATE_DIR/pak_dir" ] && pak_dir="$(cat "$STATE_DIR/pak_dir")"

if [ -n "$pak_dir" ] && [ -x "$pak_dir/bin/boot.sh" ]; then
	exec "$pak_dir/bin/boot.sh" mount
fi

bin="$SYSTEM_PATH/bin"
if grep -q NEXTUI_LAUNCHER_WRAPPER "$bin/nextui.elf" 2>/dev/null && [ -f "$bin/nextui.stock.elf" ]; then
	mv -f "$bin/nextui.stock.elf" "$bin/nextui.elf"
fi
rm -f "$0"
