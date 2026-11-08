#!/bin/sh
#
# Loads test plugins into the desktop build and checks what happened:
# a plugin that loads and uses the host table, one with the wrong API
# version, one that replaces the root and opens entries, one that sorts
# and renames a folder and moves the selection, one that adds quick menu
# tiles and shares a button, one that declines after registering things,
# one that powers off, one that sets stock toggles, one that reads theme
# colours and sets an LED colour, one that asks about Bluetooth, one that
# draws, one that draws the game list as a grid, and one that crashes
# during startup.
#
# Each case waits up to PLUGIN_TEST_SECONDS (10 by default) for the log
# lines it checks, and stops as soon as they're in, so a slow machine only
# makes the run slower.
#
#   tests/plugins/run.sh

set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$ROOT/build/tests/plugins"
CARD=/var/tmp/nextui/sdcard
STATE="$CARD/.userdata/desktop/launcher"
ARCH="desktop-$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
CC="${CC:-cc}"
WAIT="${PLUGIN_TEST_SECONDS:-10}"
failures=0
case_log=""

# fail <message>: names the case whose log is $case_log, if there is one
fail() {
	if [ -n "$case_log" ]; then
		echo "FAIL: $(basename "$case_log" .log): $*; log in $case_log" >&2
	else
		echo "FAIL: $*" >&2
	fi
	failures=$((failures + 1))
}

# build_plugin <name> <manifest text>
build_plugin() {
	case_log=""
	dir="$WORK/plugins/$1.plugin"
	mkdir -p "$dir/lib/$ARCH"
	printf '%b' "$2" >"$dir/plugin.txt"
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -I"$ROOT/sdk" \
		"$ROOT/tests/plugins/$1/$1.c" -o "$dir/lib/$ARCH/plugin.so" || fail "building $1"
}

# clear_guard: forgets earlier starts' crashes, so one slow or failed case
# can't start the next ones with plugins paused
clear_guard() {
	rm -f "$STATE/loading" "$STATE/plugins_paused" "$STATE/crashes" "$STATE/fallback"
}

# start_launcher <log> [line...]: starts the launcher, keeping the crash
# guard's state. With lines, stops once the log has all of them and the
# first frame is up (the loading marker is gone); without, runs until it
# exits or $WAIT seconds pass. 0 if it ran until stopped, 1 if it exited on
# its own, 2 (also a failure here) if it had to be killed after ignoring
# SIGTERM. Not getting the lines in time is a failure too, and returns 0.
start_launcher() {
	case_log="$1"
	shift
	until=$(printf '%s\n' "$@")
	MOSAIC_LAUNCHER_PLUGIN_PATH="$WORK/plugins" SMOKE_UNTIL="$until" SMOKE_UNTIL_GONE="$STATE/loading" \
		"$ROOT/scripts/desktop-smoke.sh" "$case_log" "$WAIT" >/dev/null 2>"$WORK/smoke.err"
	status=$?
	[ "$status" -eq 2 ] && fail "the launcher ignored SIGTERM and was killed"
	if [ "$status" -eq 3 ]; then
		why=$(sed -n 's/^  //p' "$WORK/smoke.err" | tr '\n' ',')
		fail "not ready after ${WAIT}s (${why%,})"
		return 0
	fi
	return "$status"
}

# run_launcher <log> [line...]: start_launcher from a clear crash guard
run_launcher() {
	clear_guard
	start_launcher "$@"
}

# expect <line>: plugin $plugin logged line in $log
expect() {
	grep -qF "plugin $plugin: $1" "$log" || fail "$plugin didn't log \"$1\""
}

rm -rf "$WORK" "$STATE"
build_plugin hello 'id=hello\nname=Hello\napi=1\n'

# loads, logs through the host, and saves its config
run_launcher "$WORK/hello.log" "plugin hello: hello from desktop" || fail "the launcher exited early with hello loaded"
grep -q "plugins: loaded hello" "$WORK/hello.log" || fail "hello didn't load"
grep -q "plugin hello: hello from desktop" "$WORK/hello.log" || fail "hello's log line is missing"
grep -q "^started=yes$" "$STATE/hello.cfg" 2>/dev/null || fail "hello's cfg_set didn't save"
[ ! -f "$STATE/loading" ] || fail "the loading marker outlived the first frame"

# reads its config back on the next start
printf 'greeting=hi\nstarted=yes\n' >"$STATE/hello.cfg"
run_launcher "$WORK/hello-cfg.log" "plugin hello: hi from desktop" || fail "the launcher exited early on the second start"
grep -q "plugin hello: hi from desktop" "$WORK/hello-cfg.log" || fail "hello didn't read its config"

# turned off in plugins.txt
echo "hello=off" >"$STATE/plugins.txt"
run_launcher "$WORK/off.log" "plugins: not loading hello: turned off" || fail "the launcher exited early with hello off"
grep -q "plugins: not loading hello: turned off" "$WORK/off.log" || fail "hello loaded while turned off"
rm "$STATE/plugins.txt"

# wrong API version is skipped, and the launcher still starts
printf 'id=hello\nname=Hello\napi=99\n' >"$WORK/plugins/hello.plugin/plugin.txt"
run_launcher "$WORK/api.log" "plugins: hello targets API 99" || fail "the launcher exited early with a mismatched plugin"
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
run_launcher "$WORK/rooter.log" "plugin rooter: selected after reload" || fail "the launcher exited early with rooter loaded"
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
run_launcher "$WORK/rooter-unslotted.log" "plugins: rooter has root_replace but no slots=root" || fail "the launcher exited early with rooter unslotted"
grep -q "plugins: rooter has root_replace but no slots=root" "$WORK/rooter-unslotted.log" || fail "the missing root slot wasn't reported"
grep -q "plugin rooter: root_replace called" "$WORK/rooter-unslotted.log" && fail "root_replace ran without the root slot"

rm -rf "$WORK/plugins/rooter.plugin" "$(dirname "$ROM")" "$STATE/rooter.cfg"
if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi

# a plugin orders and renames a folder's entries, then reads the list and
# moves the selection
build_plugin sorter 'id=sorter\nname=Sorter\napi=1\n'
run_launcher "$WORK/sorter.log" "plugin sorter: next frame selected" || fail "the launcher exited early with sorter loaded"
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
run_launcher "$WORK/menus.log" "plugin menus: choose 0" "quick menu: top row Tools,Menus" || fail "the launcher exited early with menus loaded"
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
run_launcher "$WORK/menus-alone.log" "plugin menus: choose 0" "quick menu: top row Tools,Menus" || fail "the launcher exited early with menus keeping its root to itself"
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
expect "repeat 1 0, bind repeat -1"
expect "replace from init -1"
expect "rename 0, bottom row -1"
expect "override from tick -1"
expect "pre_launch $ROM"
expect "replace missing -1"
expect "replace 0"
grep -qF "plugins: remapper overrides A press on the game list with gamelist.resume" "$log" || fail "the A override wasn't logged"
grep -qF "plugins: remapper overrides Y press on the game list with nothing" "$log" || fail "the Y override wasn't logged"
grep -qF "plugins: remapper overrides L1 repeat on the game list with gamelist.prev_letter" "$log" || fail "the L1 repeat override wasn't logged"
grep -qF "plugins: rival can't override A on the game list, remapper already has it" "$log" || fail "the A conflict wasn't logged"
grep -qF "plugins: remapper overrides Y, so rival.y bound to it doesn't run" "$log" || fail "the overridden Y binding wasn't logged"
grep -qF "plugins: rival can't replace the launch, remapper already did" "$log" || fail "the launch conflict wasn't logged"
grep -qF "plugins: rival returned MOSAIC_LAUNCH_REPLACE without a replace_launch command" "$log" || fail "rival's empty replace wasn't logged"
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
run_launcher "$WORK/remap-return.log" "plugin remapper: override from tick" || fail "the launcher exited early after the replaced launch"
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
run_launcher "$WORK/decline.log" "plugin powerer: show_recents 1" "quick menu: bottom row" || fail "the launcher exited early after decliner declined"
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
run_launcher "$WORK/no-recents.log" "plugin powerer: show_recents 0" "quick menu: bottom row" || fail "the launcher exited early with no recents"
log="$WORK/no-recents.log"
expect "show_recents 0"

rm -rf "$WORK/plugins/powerer.plugin" "$WORK/plugins/decliner.plugin" "$(dirname "$ROM")" "$EMU_PAK" "$STATE/plugins.txt"
rmdir "$CARD/Emus/desktop" 2>/dev/null
if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi

# toggler reads stock toggles, has a name off the list refused, and flips
# Show clock, which stock saves to minuisettings.txt (in the folder named by
# SHARED_USERDATA_PATH, which stock's launch.sh sets on a device)
SETTINGS="$CARD/.userdata/shared/minuisettings.txt"
mkdir -p "$(dirname "$SETTINGS")" "$STATE"
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
printf 'showclock=0\nhaptics=1\n' >"$SETTINGS"
printf 'toggler=on\n' >"$STATE/plugins.txt"
build_plugin toggler 'id=toggler\nname=Toggler\napi=1\n'
SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/toggler.log" "plugin toggler: clock now" || fail "the launcher exited early with toggler loaded"
log="$WORK/toggler.log"
plugin=toggler
expect "clock 0, haptics 1, wifi -1"
expect "set clock 0, set wifi -1, set clock to 2 -1"
expect "clock now 1"
grep -q "plugins: refused to set stock toggle wifi to 1" "$log" || fail "the refused toggle wasn't logged"
grep -q "^showclock=1$" "$SETTINGS" || fail "Show clock wasn't saved: $(grep showclock "$SETTINGS")"
grep -q "^haptics=1$" "$SETTINGS" || fail "another toggle changed: $(grep haptics "$SETTINGS")"
rm -rf "$WORK/plugins/toggler.plugin" "$STATE/plugins.txt"

# lighter reads the theme colours stock loaded as 0xRRGGBB, and has its LED
# colour refused, since desktop has no LEDs
printf 'color1=0xFFFFFFFF\ncolor2=0x9B2257FF\ncolor7=0x10203080\n' >"$SETTINGS"
printf 'lighter=on\n' >"$STATE/plugins.txt"
build_plugin lighter 'id=lighter\nname=Lighter\napi=1\n'
SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/lighter.log" "plugin lighter: leds" || fail "the launcher exited early with lighter loaded"
log="$WORK/lighter.log"
plugin=lighter
expect "main FFFFFF, accent 9B2257, background 102030, 0 -1, 8 -1"
expect "leds 0, set -1, clear -1"
grep -q "plugins: refused LED colour FF0000 for lighter" "$log" || fail "the refused LED colour wasn't logged"
grep -q "LED colour" "$WORK/hello.log" && fail "the LED colour changed without a plugin asking"
rm -rf "$WORK/plugins/lighter.plugin" "$STATE/plugins.txt"

# bluer finds no Bluetooth on desktop, even with the toggle on, and its
# connects are refused without a worker thread or anything run
printf 'bluetooth=1\n' >"$SETTINGS"
printf 'bluer=on\n' >"$STATE/plugins.txt"
build_plugin bluer 'id=bluer\nname=Bluer\napi=1\n'
SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/bluer.log" "plugin bluer: tick:" || fail "the launcher exited early with bluer loaded"
log="$WORK/bluer.log"
plugin=bluer
expect "init: supported 0, enabled 0, connected 0 \"\", connect -1, bad -1"
expect "tick: supported 0, enabled 0, connected 0 \"\", connect -1, bad -1"
grep -q "plugins: refused Bluetooth connect to AA:BB:CC:DD:EE:FF: no Bluetooth" "$log" || fail "the refused connect wasn't logged"
grep -q "plugins: refused Bluetooth connect to (not an address): no Bluetooth" "$log" || fail "the refused bad address wasn't logged"
grep -q "reboot" "$log" && fail "a refused address reached the log"
grep -q "bluetooth: worker" "$log" && fail "the Bluetooth worker started on desktop"
grep -q "Bluetooth connect\|bluetooth: worker" "$WORK/hello.log" && fail "Bluetooth was used without a plugin asking"
rm -rf "$WORK/plugins/bluer.plugin" "$STATE/plugins.txt"
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi

# drawer tries every drawing call on surfaces of its own, reading pixels
# back, and has bad arguments, a FIFO, a folder and a file that isn't an
# image refused; the surfaces it leaves are freed after its quit, and when
# its init declines
DRAW_DIR="$WORK/drawer"
mkdir -p "$DRAW_DIR" "$STATE"
mkfifo "$DRAW_DIR/fifo"
echo "not an image" >"$DRAW_DIR/text.png"
printf 'art=%s\ndir=%s\ntext=%s\nfifo=%s\n' "$ROOT/tests/plugins/drawer/art.png" "$DRAW_DIR" "$DRAW_DIR/text.png" "$DRAW_DIR/fifo" >"$STATE/drawer.cfg"
printf 'drawer=on\n' >"$STATE/plugins.txt"
build_plugin drawer 'id=drawer\nname=Drawer\napi=1\n'
run_launcher "$WORK/drawer.log" "plugin drawer: leaving 7 surfaces" "plugins: loaded drawer" || fail "the launcher exited early with drawer loaded"
log="$WORK/drawer.log"
plugin=drawer
expect "screen 1024x768 scale 3"
expect "new ok, zero NULL, too wide NULL, not loaded NULL, no id NULL"
expect "size 0 40x20, null -1, unknown -1, no out -1"
expect "new pixel 000000 00, outside - -"
expect "budget first ok, second NULL, freed 0"
expect "fill 0, pixel FF0000 FF"
expect "blend 0, half 1, outside FF0000 FF"
expect "fill bad rect -1, null -1, unknown -1, clear 0"
expect "off the edge 0, huge -1, pixel FF0000 FF 00FF00 FF"
expect "round 0, corner 000000 00, edge 00FF00 FF, middle 00FF00 FF, negative -1"
expect "theme fill 1"
expect "image ok 4x2, left FF0000 FF, right 0000FF FF"
expect "image dir NULL, missing NULL, not an image NULL, fifo NULL, null NULL, not loaded NULL"
expect "contain ok, bar 000000 00, left FF0000 FF, right 0000FF FF"
expect "cover ok, left FF0000 FF, right 0000FF FF"
expect "stretch ok, FF0000 FF 0000FF FF"
expect "scale bad fit NULL, zero NULL, null NULL"
expect "blit 0 FF0000 FF 0000FF FF, stretched 0 FF0000 FF 0000FF FF, part 0 0000FF FF, outside 000000 00"
expect "blit onto itself -1, null -1, bad rect -1"
expect "text width 1, height 1, smaller 1, two lines 1, bad font -1 -1, null -1"
expect "truncate fits 1, dots 1, whole 1, small out 1"
expect "truncate no room -1, negative -1, null -1"
expect "draw text 1, inked 1, truncated 1, null -1, bad font -1, negative -1"
expect "free 0, again -1, not mine -1, null -1, then fill -1, then blit -1"
grep -qF "plugins: refused a 2048x2048 surface for drawer, its surfaces would take over 32 MiB" "$log" || fail "the refused surface wasn't logged"
grep -qF "plugins: drawer can't load $DRAW_DIR/fifo" "$log" || fail "the refused FIFO wasn't logged"
grep -q "plugins: freed .* surface\|plugins: refused a .* surface" "$WORK/hello.log" && fail "surfaces were made without a plugin asking"

# a launch quits the launcher, running plugins' quit; the card's recents
# and /tmp/next are put back afterwards, and /tmp/last.txt after the next case
PAK="$DRAW_DIR/Drawn.pak"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/recent.txt.saved"
[ -f /tmp/next ] && cp /tmp/next "$WORK/next.saved"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/last.txt.saved"
mkdir -p "$PAK"
: >"$PAK/launch.sh"
echo "pak=$PAK" >>"$STATE/drawer.cfg"
run_launcher "$WORK/drawer-quit.log" && fail "the launcher didn't quit to launch drawer's pak"
log="$WORK/drawer-quit.log"
expect "open pak 1"
quit_line=$(grep -nF "plugin drawer: quit" "$log" | cut -d: -f1)
freed_line=$(grep -nF "plugins: freed 7 surfaces drawer left" "$log" | cut -d: -f1)
if [ -z "$quit_line" ] || [ -z "$freed_line" ]; then
	fail "drawer's quit didn't run, or its surfaces weren't freed after it"
elif [ "$freed_line" -lt "$quit_line" ]; then
	fail "drawer's surfaces were freed before its quit"
fi

if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
if [ -f "$WORK/next.saved" ]; then cp "$WORK/next.saved" /tmp/next; else rm -f /tmp/next; fi
grep -qxF "$PAK" /tmp/last.txt 2>/dev/null || fail "opening drawer's pak didn't save it as the last path"
rm -f "$WORK/recent.txt.saved" "$WORK/next.saved"
printf 'pak=\ndecline=1\n' >>"$STATE/drawer.cfg"
run_launcher "$WORK/drawer-decline.log" "plugins: drawer declined to start" || fail "the launcher exited early after drawer declined"
log="$WORK/drawer-decline.log"
expect "declining"
grep -qF "plugins: freed 7 surfaces drawer left" "$log" || fail "drawer's surfaces weren't freed when it declined"
grep -qF "plugin drawer: quit" "$log" && fail "a declined plugin's quit ran"
rm -rf "$WORK/plugins/drawer.plugin" "$DRAW_DIR" "$STATE/drawer.cfg" "$STATE/plugins.txt"

# gridder owns the layout slot and draws the game list as a grid on the
# screen it's lent, inside the area above the hints; its input moves the
# selection through the launcher, which keeps it
mkdir -p "$STATE"
printf 'gridder=on\n' >"$STATE/plugins.txt"
build_plugin gridder 'id=gridder\nname=Gridder\napi=1\nslots=layout\n'
run_launcher "$WORK/gridder.log" "plugin gridder: renders" || fail "the launcher exited early with gridder loaded"
log="$WORK/gridder.log"
plugin=gridder
grep -qF "plugins: gridder draws the game list" "$log" || fail "gridder didn't take the game list"
# on the root, whose count depends on the card
grep -qE "plugin gridder: layout_init count [0-9]+ selected 0 depth 1, area 0,0 1024x663, status inside 1, view size 1$" "$log" \
	|| fail "gridder's first view is wrong: $(grep "plugin gridder: layout_init" "$log")"
expect "input first: pressed 0 repeated 0 released 0 held 0 consumed 0, input size 1"
expect "render depth 1 count "
expect "render clipped 1 (fill 0), select -1, free -1"
expect "after render fill -1"
expect "open folder 1"
expect "render depth 2 count 7 selected 0 Apple, window 0-7 of 7 rows, letter 0 of 7, names 1, art 1 \"\""
expect "render lit FF0000, background 000080"
expect "input move 0 to 4: 0, bad -1, now 4"
expect "render depth 2 count 7 selected 4 Elder, window 0-7 of 7 rows, letter 4 of 7, names 1, art 1 \"\""
grep -qF "plugins: gridder's layout" "$log" && fail "gridder's layout was turned off"
[ "$(grep -c "plugin gridder: layout_init" "$log")" -eq 1 ] || fail "layout_init didn't run once"

# its errors hand the game list back to the built-in list, which keeps
# drawing; the plugin's other hooks keep running
for hook in init input render; do
	echo "fail=$hook" >"$STATE/gridder.cfg"
	run_launcher "$WORK/gridder-$hook.log" "plugin gridder: renders" || fail "the launcher exited early with gridder's $hook failing"
	log="$WORK/gridder-$hook.log"
	case "$hook" in
	init) grep -qF "plugins: gridder declined the layout slot (layout_init returned 1), using the built-in list" "$log" || fail "the declined layout wasn't logged" ;;
	*) grep -qF "plugins: gridder's layout_$hook returned -1, using the built-in list until the launcher restarts" "$log" || fail "the failed layout_$hook wasn't logged" ;;
	esac
	expect "open folder 1"
	# how many times each hook ran by the 60th tick: none after the error
	case "$hook" in
	init) want="renders 0, inputs 0" ;;
	input) want="renders [0-9]+, inputs 3" ;;
	render) want="renders 3, inputs [0-9]+" ;;
	esac
	grep -qE "plugin gridder: $want\$" "$log" || fail "gridder's hooks didn't stop at the error: $(grep "plugin gridder: renders" "$log")"
done
rm -f "$STATE/gridder.cfg"

# another plugin can't have the slot while gridder does, and the one
# plugins.txt picks wins it; an owner without layout_render leaves the
# built-in list
build_plugin hello 'id=hello\nname=Hello\napi=1\nslots=layout\n'
rm -f "$STATE/hello.cfg" # an earlier case's greeting
printf 'gridder=on\nhello=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/gridder-rival.log" "plugin gridder: renders" || fail "the launcher exited early with two layouts"
grep -qF "plugins: not loading hello: the layout slot belongs to gridder" "$WORK/gridder-rival.log" || fail "the layout slot conflict wasn't logged"
printf 'gridder=on\nhello=on\nslot.layout=hello\n' >"$STATE/plugins.txt"
run_launcher "$WORK/gridder-picked.log" "plugin hello: hello from desktop" "plugins: hello owns the layout slot but has no layout_render" || fail "the launcher exited early with hello picked"
log="$WORK/gridder-picked.log"
grep -qF "plugins: not loading gridder: the layout slot belongs to hello" "$log" || fail "the picked owner didn't win the layout slot"
grep -qF "plugin gridder:" "$log" && fail "gridder ran without the layout slot"
rm -rf "$WORK/plugins/hello.plugin" "$STATE/hello.cfg"

# without slots=layout it isn't asked to draw
printf 'id=gridder\nname=Gridder\napi=1\n' >"$WORK/plugins/gridder.plugin/plugin.txt"
printf 'gridder=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/gridder-unslotted.log" "plugin gridder: renders" || fail "the launcher exited early with gridder unslotted"
log="$WORK/gridder-unslotted.log"
grep -qF "plugins: gridder has a layout but no slots=layout, so it won't be called" "$log" || fail "the missing layout slot wasn't reported"
expect "renders 0, inputs 0"

# a layout that crashes drawing the first frame pauses plugins on the next
# start, as any crash while starting does
printf 'id=gridder\nname=Gridder\napi=1\nslots=layout\n' >"$WORK/plugins/gridder.plugin/plugin.txt"
echo "fail=crash" >"$STATE/gridder.cfg"
run_launcher "$WORK/gridder-crash.log" && fail "gridder's crashing render didn't crash the launcher"
grep -qF "plugin gridder: crashing in render" "$WORK/gridder-crash.log" || fail "gridder's render didn't run before the crash"
start_launcher "$WORK/gridder-after-crash.log" "pausing them" || fail "the launcher crashed again instead of pausing plugins"
grep -q "pausing them" "$WORK/gridder-after-crash.log" || fail "plugins weren't paused after gridder's crash"
rm -rf "$WORK/plugins/gridder.plugin" "$STATE/gridder.cfg" "$STATE/plugins.txt"

# a saved last path the launcher can't follow up to the card's root, such as
# the pak off the card drawer's launch left, starts at the root rather than
# crashing; one missing from the card is followed as far as it goes, as stock
build_plugin hello 'id=hello\nname=Hello\napi=1\n'
rm -f "$STATE/hello.cfg"
i=0
for last in "$PAK" "" "garbage" "${CARD}X/Game.gb" "$CARD/Roms/Missing (MISS)/Game.gb"; do
	i=$((i + 1))
	# the first is what drawer's launch saved
	[ "$i" -gt 1 ] && printf '%s' "$last" >/tmp/last.txt
	run_launcher "$WORK/last-$i.log" "plugin hello: hello from desktop" || fail "the launcher exited with \"$last\" as the last path"
	case "$last" in
	"$CARD"/*) grep -qF "loadLast: ignoring" "$WORK/last-$i.log" && fail "\"$last\" was ignored as the last path" ;;
	*) grep -qF "loadLast: ignoring \"$last\", it isn't on the card" "$WORK/last-$i.log" || fail "\"$last\" wasn't ignored as the last path" ;;
	esac
done
rm -rf "$WORK/plugins/hello.plugin" "$STATE/hello.cfg"
if [ -f "$WORK/last.txt.saved" ]; then cp "$WORK/last.txt.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
rm -f "$WORK/last.txt.saved"

# the bundled plugins load and start cleanly
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building the bundled plugins"
run_launcher "$WORK/bundled.log" "plugins: loaded favorites" "quick menu: bottom row" || fail "the launcher exited early with the bundled plugins"
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
run_launcher "$WORK/template.log" "plugins: loaded example" || fail "the launcher exited early with the template plugin"
grep -q "plugins: loaded example" "$WORK/template.log" || fail "the template plugin didn't load"
rm -rf "$template"

# /tmp/stay_awake holds off auto-sleep
touch /tmp/stay_awake
run_launcher "$WORK/awake.log" "awake: holding off auto-sleep" || fail "the launcher exited early with /tmp/stay_awake"
rm -f /tmp/stay_awake
grep -q "awake: holding off auto-sleep" "$WORK/awake.log" || fail "/tmp/stay_awake didn't hold off auto-sleep"
grep -q "awake: holding off auto-sleep" "$WORK/hello.log" && fail "auto-sleep was held off without /tmp/stay_awake"

# so does /tmp/stay_alive
touch /tmp/stay_alive
run_launcher "$WORK/alive.log" "awake: holding off auto-sleep" || fail "the launcher exited early with /tmp/stay_alive"
rm -f /tmp/stay_alive
grep -q "awake: holding off auto-sleep" "$WORK/alive.log" || fail "/tmp/stay_alive didn't hold off auto-sleep"

# a crash while starting pauses plugins on the next start
build_plugin crasher 'id=crasher\nname=Crasher\napi=1\n'
run_launcher "$WORK/crash.log" && fail "the crashing plugin didn't crash the launcher"
[ -f "$STATE/loading" ] || fail "no loading marker after the crash"
start_launcher "$WORK/after-crash.log" "pausing them" || fail "the launcher crashed again instead of pausing plugins"
grep -q "pausing them" "$WORK/after-crash.log" || fail "plugins weren't paused after the crash"
[ -f "$STATE/plugins_paused" ] || fail "no plugins_paused marker"
start_launcher "$WORK/paused.log" "paused after a crash" || fail "the launcher crashed while plugins were paused"
grep -q "paused after a crash" "$WORK/paused.log" || fail "plugins didn't stay paused"

rm -rf "$STATE"
if [ "$failures" -gt 0 ]; then
	echo "$failures plugin check(s) failed" >&2
	exit 1
fi
echo "ok plugins"
