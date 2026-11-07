#!/bin/sh
#
# Loads test plugins into the desktop build and checks what happened:
# a plugin that loads and uses the host table, one with the wrong API
# version, one that replaces the root and opens entries, one that sorts
# and renames a folder and moves the selection, one that adds quick menu
# tiles and shares a button, one that declines after registering things,
# one that powers off, and one that crashes during startup.
#
#   tests/plugins/run.sh

set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$ROOT/build/tests/plugins"
CARD=/var/tmp/nextui/sdcard
STATE="$CARD/.userdata/desktop/launcher"
ARCH="desktop-$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
CC="${CC:-cc}"
failures=0

fail() {
	echo "FAIL: $*" >&2
	failures=$((failures + 1))
}

# build_plugin <name> <manifest text>
build_plugin() {
	dir="$WORK/plugins/$1.plugin"
	mkdir -p "$dir/lib/$ARCH"
	printf '%b' "$2" >"$dir/plugin.txt"
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -I"$ROOT/sdk" \
		"$ROOT/tests/plugins/$1/$1.c" -o "$dir/lib/$ARCH/plugin.so" || fail "building $1"
}

run_launcher() {
	NEXTUI_LAUNCHER_PLUGIN_PATH="$WORK/plugins" "$ROOT/scripts/desktop-smoke.sh" "$1" 3 >/dev/null 2>&1
}

# expect <line>: plugin $plugin logged line in $log
expect() {
	grep -qF "plugin $plugin: $1" "$log" || fail "$plugin didn't log \"$1\""
}

rm -rf "$WORK" "$STATE"
build_plugin hello 'id=hello\nname=Hello\napi=1\n'

# loads, logs through the host, and saves its config
run_launcher "$WORK/hello.log" || fail "the launcher exited early with hello loaded"
grep -q "plugins: loaded hello" "$WORK/hello.log" || fail "hello didn't load"
grep -q "plugin hello: hello from desktop" "$WORK/hello.log" || fail "hello's log line is missing"
grep -q "^started=yes$" "$STATE/hello.cfg" 2>/dev/null || fail "hello's cfg_set didn't save"
[ ! -f "$STATE/loading" ] || fail "the loading marker outlived the first frame"

# reads its config back on the next start
printf 'greeting=hi\nstarted=yes\n' >"$STATE/hello.cfg"
run_launcher "$WORK/hello-cfg.log" || fail "the launcher exited early on the second start"
grep -q "plugin hello: hi from desktop" "$WORK/hello-cfg.log" || fail "hello didn't read its config"

# turned off in plugins.txt
echo "hello=off" >"$STATE/plugins.txt"
run_launcher "$WORK/off.log" || fail "the launcher exited early with hello off"
grep -q "plugins: not loading hello: turned off" "$WORK/off.log" || fail "hello loaded while turned off"
rm "$STATE/plugins.txt"

# wrong API version is skipped, and the launcher still starts
printf 'id=hello\nname=Hello\napi=99\n' >"$WORK/plugins/hello.plugin/plugin.txt"
run_launcher "$WORK/api.log" || fail "the launcher exited early with a mismatched plugin"
grep -q "plugins: hello targets API 99" "$WORK/api.log" || fail "the API mismatch wasn't reported"
grep -q "plugins: loaded hello" "$WORK/api.log" && fail "a mismatched plugin loaded"
rm -rf "$WORK/plugins/hello.plugin"

# the root slot owner replaces the root, and the open, recents and reload
# calls work; the card's own recents are put back afterwards
RECENTS="$CARD/.userdata/shared/.minui/recent.txt"
ROM="$CARD/Roms/.rooter-test/Game.gb"
mkdir -p "$(dirname "$RECENTS")" "$(dirname "$ROM")" "$STATE"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/recent.txt.saved"
: >"$ROM"
echo "/Roms/.rooter-test/Game.gb" >"$RECENTS"
echo "rom=$ROM" >"$STATE/rooter.cfg"
build_plugin rooter 'id=rooter\nname=Rooter\napi=1\nslots=root\n'
run_launcher "$WORK/rooter.log" || fail "the launcher exited early with rooter loaded"
plugin=rooter
log="$WORK/rooter.log"
expect "open from init 0"
expect "root_replace called"
expect "selected $CARD/Rooter"
expect "recents 1, first $ROM"
expect "past the end (null)"
expect "pre_launch $ROM"
expect "open rom 0"
expect "open missing 0"
expect "open folder 1, now in $CARD/Rooter"
expect "removed 0, recents 0, bad index -1"
expect "cleared, recents 0"
expect "reload asked, listed 1"
expect "reloaded, in $CARD/Rooter, listed 2"
expect "selected after reload $ROM"
[ -s "$RECENTS" ] && fail "recents_clear didn't save an empty list"

# a plugin that doesn't claim the root slot isn't asked for the root
printf 'id=rooter\nname=Rooter\napi=1\n' >"$WORK/plugins/rooter.plugin/plugin.txt"
run_launcher "$WORK/rooter-unslotted.log" || fail "the launcher exited early with rooter unslotted"
grep -q "plugins: rooter has root_replace but no slots=root" "$WORK/rooter-unslotted.log" || fail "the missing root slot wasn't reported"
grep -q "plugin rooter: root_replace called" "$WORK/rooter-unslotted.log" && fail "root_replace ran without the root slot"

rm -rf "$WORK/plugins/rooter.plugin" "$(dirname "$ROM")" "$STATE/rooter.cfg"
if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi

# a plugin orders and renames a folder's entries, then reads the list and
# moves the selection
build_plugin sorter 'id=sorter\nname=Sorter\napi=1\n'
run_launcher "$WORK/sorter.log" || fail "the launcher exited early with sorter loaded"
plugin=sorter
log="$WORK/sorter.log"
expect "current list from init (null)"
expect "select from init -1"
expect "set_name outside list_ready -1"
expect "open folder 1"
expect "list_ready 5 entries"
expect "renamed Tetris (gbc) 0"
expect "renamed Tetris (gb) 0"
expect "order Pinned,Alpha,Beta,Tetris (gb),Tetris (gbc)"
expect "selected index 0"
expect "select 3 0, bad -1 -1"
expect "selected 3 Tetris (gb)"
expect "next frame selected 3 Tetris (gb)"
rm -rf "$WORK/plugins/sorter.plugin"

# two actions share Y, plugin tiles join the quick menu and Games is hidden,
# a replaced root takes other plugins' entries only when its owner asks,
# recent_set rewrites a recent, a reload rebuilds the quick menu without
# Recents, and a choice list opens
ROM="$CARD/Roms/.menus-test/Game.gb"
MOVED="$CARD/Roms/.menus-test/Moved.gb"
mkdir -p "$(dirname "$RECENTS")" "$(dirname "$ROM")" "$STATE"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/recent.txt.saved"
: >"$ROM"
echo "/Roms/.menus-test/Game.gb" >"$RECENTS"
echo "moved=$MOVED" >"$STATE/menus.cfg"
build_plugin menus 'id=menus\nname=Menus\napi=1\nslots=root\n'
build_plugin adder 'id=adder\nname=Adder\napi=1\n'
run_launcher "$WORK/menus.log" || fail "the launcher exited early with menus loaded"
plugin=menus
log="$WORK/menus.log"
expect "bound 0 0"
grep -qF "plugins: Y on the game list is shared by 2 actions" "$log" || fail "the shared Y wasn't reported"
expect "tiles 0 0, unknown -1, bad row -1"
expect "hide 0, empty -1"
expect "root Menus Root,Added"
expect "recent_set 0, first $MOVED"
expect "recent_set bad index -1, off the card -1"
expect "choose none -1"
expect "choose 0"
top_rows=$(grep "quick menu: top row" "$log")
first_top=$(printf '%s\n' "$top_rows" | head -n 1)
last_top=$(printf '%s\n' "$top_rows" | tail -n 1)
case "$first_top" in *"row Recents,"*Menus) ;; *) fail "the first top row isn't Recents ... Menus: $first_top" ;; esac
case "$first_top" in *Games*) fail "Games wasn't hidden: $first_top" ;; esac
case "$last_top" in *Recents*) fail "the reload didn't drop the Recents tile: $last_top" ;; *Menus) ;; *) fail "the rebuilt top row lost the plugin tile: $last_top" ;; esac
grep -q "quick menu: bottom row .*Light$" "$log" || fail "the bottom row has no Light tile"
[ -s "$RECENTS" ] && fail "recents_clear didn't save an empty list"

echo "keep=0" >>"$STATE/menus.cfg"
run_launcher "$WORK/menus-alone.log" || fail "the launcher exited early with menus keeping its root to itself"
log="$WORK/menus-alone.log"
expect "root Menus Root"
grep -qF "plugin menus: root Menus Root,Added" "$log" && fail "the root took other plugins' entries without being asked"

rm -rf "$WORK/plugins/menus.plugin" "$WORK/plugins/adder.plugin" "$(dirname "$ROM")" "$STATE/menus.cfg"
if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi

# remapper overrides buttons ahead of rival, relabels a tile, and replaces a
# launch, which rival can't also do; the launcher writes the replaced
# command and quits, and post_return runs on the next start
ROM="$CARD/Roms/.remap-test/It's.gb"
EMU="$CARD/Roms/.remap-test/remap-emu.sh"
mkdir -p "$(dirname "$RECENTS")" "$(dirname "$ROM")" "$STATE"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/recent.txt.saved"
[ -f /tmp/next ] && cp /tmp/next "$WORK/next.saved"
: >"$ROM"
: >"$EMU"
printf 'rom=%s\nemu=%s\n' "$ROM" "$EMU" >"$STATE/remapper.cfg"
printf 'emu=%s\n' "$EMU" >"$STATE/rival.cfg"
printf 'remapper=on\nrival=on\n' >"$STATE/plugins.txt"
build_plugin remapper 'id=remapper\nname=Remapper\napi=1\n'
build_plugin rival 'id=rival\nname=Rival\napi=1\n'
run_launcher "$WORK/remap.log" && fail "the launcher didn't quit to run the replaced launch"
plugin=remapper
log="$WORK/remap.log"
expect "swap 0 0"
expect "again 0"
expect "nothing 0"
expect "unknown -1, bad screen -1, bad trigger -1"
expect "replace from init -1"
expect "rename 0, bottom row -1"
expect "override from tick -1"
expect "pre_launch $ROM"
expect "replace missing -1"
expect "replace 0"
grep -qF "plugins: remapper overrides A press on the game list with gamelist.resume" "$log" || fail "the A override wasn't logged"
grep -qF "plugins: remapper overrides Y press on the game list with nothing" "$log" || fail "the Y override wasn't logged"
grep -qF "plugins: rival can't override A on the game list, remapper already has it" "$log" || fail "the A conflict wasn't logged"
grep -qF "plugins: remapper overrides Y, so rival.y bound to it doesn't run" "$log" || fail "the overridden Y binding wasn't logged"
grep -qF "plugins: rival can't replace the launch, remapper already did" "$log" || fail "the launch conflict wasn't logged"
grep -qF "plugins: rival returned NUI_LAUNCH_REPLACE without a replace_launch command" "$log" || fail "rival's empty replace wasn't logged"
grep -qF "plugins: remapper replaced the launch of $ROM" "$log" || fail "the replaced launch wasn't logged"
grep -q "quick menu: top row .*Games (Library)" "$log" || fail "the Games tile wasn't relabelled"
plugin=rival
expect "override A -1, B 0"
expect "bind Y 0"
expect "replace -1"
# the program, its args and then the game, each quoted
quoted_rom=$(printf '%s' "$ROM" | sed "s/'/'\\\\''/g")
grep -qF "/remap-emu.sh' '--core' 'x'\\''s.so' '$quoted_rom'" /tmp/next 2>/dev/null || fail "the replaced command isn't in /tmp/next: $(cat /tmp/next 2>/dev/null)"
grep -qF "cmd: '" "$log" || fail "the replaced command wasn't queued"
run_launcher "$WORK/remap-return.log" || fail "the launcher exited early after the replaced launch"
log="$WORK/remap-return.log"
plugin=remapper
expect "post_return"
grep -qF "plugin remapper: open rom" "$log" && fail "remapper launched again"

rm -rf "$WORK/plugins/remapper.plugin" "$WORK/plugins/rival.plugin" "$(dirname "$ROM")" \
	"$STATE/remapper.cfg" "$STATE/rival.cfg" "$STATE/plugins.txt"
if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
if [ -f "$WORK/next.saved" ]; then cp "$WORK/next.saved" /tmp/next; else rm -f /tmp/next; fi

# decliner registers actions, a binding, an override, tiles, a sleep hold
# and a power-off, then declines; none of it stays behind, where its
# action's available would run in unloaded code. powerer reads the power
# fields (desktop has no battery or LEDs) and show_recents, and powers off
# and reboots through the quick menu's path, which quits on desktop
# (a recent counts only when its emulator is installed, as for stock)
ROM="$CARD/Roms/.power-test (PWRT)/Game.gb"
EMU_PAK="$CARD/Emus/desktop/PWRT.pak"
mkdir -p "$(dirname "$RECENTS")" "$(dirname "$ROM")" "$EMU_PAK" "$STATE"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/recent.txt.saved"
: >"$ROM"
: >"$EMU_PAK/launch.sh"
echo "/Roms/.power-test (PWRT)/Game.gb" >"$RECENTS"
printf 'powerer=on\ndecliner=on\n' >"$STATE/plugins.txt"
build_plugin powerer 'id=powerer\nname=Powerer\napi=1\n'
build_plugin decliner 'id=decliner\nname=Decliner\napi=1\n'
run_launcher "$WORK/decline.log" || fail "the launcher exited early after decliner declined"
log="$WORK/decline.log"
plugin=decliner
expect "declining"
grep -q "plugins: decliner declined to start" "$log" || fail "decliner didn't decline"
plugin=powerer
expect "battery -1, charging -1, leds 0, show_recents 0"
expect "bind declined -1, tile declined -1, tile own 0"
expect "show_recents 1"
grep -q "quick menu: top row .*Games,Tools" "$log" || fail "decliner's hidden or renamed tile stayed: $(grep 'quick menu: top row' "$log")"
grep -q "quick menu: .*DECLINED" "$log" && fail "decliner's tile stayed"
grep -q "quick menu: bottom row .*Power$" "$log" || fail "powerer's action wasn't put back: $(grep 'quick menu: bottom row' "$log" | tail -n 1)"
grep -q "awake: holding off auto-sleep" "$log" && fail "decliner's sleep hold stayed"
grep -q "plugin powerer: before_poweroff" "$log" && fail "decliner's power-off stayed"

echo "power=off" >"$STATE/powerer.cfg"
run_launcher "$WORK/poweroff.log" && fail "power_off didn't quit the launcher"
log="$WORK/poweroff.log"
grep -q "plugins: a plugin asked to power off" "$log" || fail "the power-off wasn't logged"
expect "before_poweroff"

echo "power=reboot" >"$STATE/powerer.cfg"
run_launcher "$WORK/reboot.log" && fail "power_off(1) didn't quit the launcher"
log="$WORK/reboot.log"
grep -q "plugins: a plugin asked to reboot" "$log" || fail "the reboot wasn't logged"
expect "before_poweroff"

: >"$RECENTS"
rm "$STATE/powerer.cfg"
run_launcher "$WORK/no-recents.log" || fail "the launcher exited early with no recents"
log="$WORK/no-recents.log"
expect "show_recents 0"

rm -rf "$WORK/plugins/powerer.plugin" "$WORK/plugins/decliner.plugin" "$(dirname "$ROM")" "$EMU_PAK" "$STATE/plugins.txt"
rmdir "$CARD/Emus/desktop" 2>/dev/null
if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi

# the bundled plugins load and start cleanly
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building the bundled plugins"
run_launcher "$WORK/bundled.log" || fail "the launcher exited early with the bundled plugins"
grep -q "plugins: loaded favorites" "$WORK/bundled.log" || fail "favorites didn't load"
grep -q "couldn't bind Y" "$WORK/bundled.log" && fail "favorites couldn't bind Y"
rm -rf "$WORK/plugins"/*.plugin

# the plugin template builds and loads as it ships
template="$WORK/plugins/example.plugin"
mkdir -p "$template"
cp "$ROOT/template/plugin.txt" "$template/"
(cd "$ROOT/template" && make -s desktop CC="$CC" >/dev/null) || fail "building the template"
cp -R "$ROOT/template/lib" "$template/"
rm -rf "$ROOT/template/lib"
run_launcher "$WORK/template.log" || fail "the launcher exited early with the template plugin"
grep -q "plugins: loaded example" "$WORK/template.log" || fail "the template plugin didn't load"
rm -rf "$template"

# /tmp/stay_awake holds off auto-sleep
touch /tmp/stay_awake
run_launcher "$WORK/awake.log" || fail "the launcher exited early with /tmp/stay_awake"
rm -f /tmp/stay_awake
grep -q "awake: holding off auto-sleep" "$WORK/awake.log" || fail "/tmp/stay_awake didn't hold off auto-sleep"
grep -q "awake: holding off auto-sleep" "$WORK/hello.log" && fail "auto-sleep was held off without /tmp/stay_awake"

# so does /tmp/stay_alive
touch /tmp/stay_alive
run_launcher "$WORK/alive.log" || fail "the launcher exited early with /tmp/stay_alive"
rm -f /tmp/stay_alive
grep -q "awake: holding off auto-sleep" "$WORK/alive.log" || fail "/tmp/stay_alive didn't hold off auto-sleep"

# a crash while starting pauses plugins on the next start
build_plugin crasher 'id=crasher\nname=Crasher\napi=1\n'
run_launcher "$WORK/crash.log" && fail "the crashing plugin didn't crash the launcher"
[ -f "$STATE/loading" ] || fail "no loading marker after the crash"
run_launcher "$WORK/after-crash.log" || fail "the launcher crashed again instead of pausing plugins"
grep -q "pausing them" "$WORK/after-crash.log" || fail "plugins weren't paused after the crash"
[ -f "$STATE/plugins_paused" ] || fail "no plugins_paused marker"
run_launcher "$WORK/paused.log" || fail "the launcher crashed while plugins were paused"
grep -q "paused after a crash" "$WORK/paused.log" || fail "plugins didn't stay paused"

rm -rf "$STATE"
if [ "$failures" -gt 0 ]; then
	echo "$failures plugin check(s) failed" >&2
	exit 1
fi
echo "ok plugins"
