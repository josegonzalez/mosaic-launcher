#!/bin/sh
#
# Loads test plugins into the desktop build and checks what happened:
# a plugin that loads and uses the host table, one with the wrong API
# version, one that replaces the root and opens entries, one that sorts
# and renames a folder and moves the selection, one that adds quick menu
# tiles and shares a button, one that declines after registering things,
# one that powers off, one that sets stock toggles, one that reads theme
# colours and sets an LED colour, one that asks about Bluetooth, one that
# draws, one that draws the game list as a grid, two that decorate rows and
# add header widgets, two that supply art and load it through the art
# cache, one that hears about the selection, one that reads .info
# sidecars, one that picks transition styles, one that generates art, one
# that draws the background, one that looks at the bundled box art
# provider's art, and one that crashes during startup. It also
# checks the status pill shell paks' status files show.
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
expect "render decorated 0, bad index -1 -1, no rect -1, outside -1, not the screen -1"
expect "render header 0x0, beside status 1"
expect "after render fill -1, decorate -1"
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

# badger decorates the built-in list's rows and adds a header widget beside
# the status pill; its drawing lands in the row or widget and is clipped to
# it, select_index refuses during both, and the top row stops short of the
# widget
mkdir -p "$STATE"
printf 'badger=on\n' >"$STATE/plugins.txt"
build_plugin badger 'id=badger\nname=Badger\napi=1\n'
run_launcher "$WORK/badger.log" "plugin badger: rows " || fail "the launcher exited early with badger loaded"
log="$WORK/badger.log"
plugin=badger
expect "open folder 1"
expect "decorate outside a layout -1"
grep -qE "plugin badger: width first: room [0-9]+ height 90$" "$log" || fail "badger wasn't asked its width: $(grep "plugin badger: width" "$log")"
grep -qE "plugin badger: widget rect [0-9]+,15 60x90 fill 0 0000FF, clipped 1 \(fill 0\), select -1$" "$log" \
	|| fail "badger's widget is wrong: $(grep "plugin badger: widget" "$log")"
grep -qE "plugin badger: row Apple index 0 selected 1 rect 15,15 [0-9]+x90 name 15,15 [0-9]+x90, row size 1$" "$log" \
	|| fail "badger's selected row is wrong: $(grep "plugin badger: row Apple index" "$log")"
grep -qE "plugin badger: row Banana index 1 selected 0 rect 15,105 [0-9]+x90 name 15,105 [0-9]+x90, row size 1$" "$log" \
	|| fail "badger's second row is wrong: $(grep "plugin badger: row Banana index" "$log")"
expect "row Apple dot 0 FF0000, clipped 1 (fill 0), select -1"
expect "row Fig dot 0 FF0000, clipped 1 (fill 0), select -1"
expect "row Apple clear of the widget 1, widget 0000FF"
expect "rows 4, widths 3, widgets 3"
grep -qF "plugins: badger's" "$log" && fail "badger's hooks were turned off"
# the widget's room is half the header left of the pill (past the
# padding), less the gap
pill_x=$(($(sed -n 's/.*plugin badger: widget rect \([0-9]*\),.*/\1/p' "$log") + 60 + 15))
grep -qF "plugin badger: width first: room $(((pill_x - 15) / 2 - 15)) height 90" "$log" || fail "badger's room isn't half the header"
# the widget takes its width and the gap off the top row only
top_row() { sed -n "s/.*plugin badger: row $1 index [0-9]* selected [01] rect [0-9]*,[0-9]* \([0-9]*\)x90 .*/\1/p" "$2"; }
with_widget=$(top_row Apple "$log")
other_row=$(top_row Banana "$log")
grep -qF "plugins: badger has" "$log" && fail "badger's hooks were reported as unpaired"
grep -qE "decorate_row|header widget" "$WORK/hello.log" && fail "a plugin without decorators logged them"

# a widget needs both hooks; with one, it's reported and never asked
# (exported and unset, since an assignment before a function call outlives it in sh)
export BADGER_NO_WIDTH=1
run_launcher "$WORK/badger-unpaired.log" "plugin badger: rows " || fail "the launcher exited early with badger unpaired"
unset BADGER_NO_WIDTH
log="$WORK/badger-unpaired.log"
grep -qF "plugins: badger has header_widget but no header_width, so its header widget won't show" "$log" || fail "the unpaired header hook wasn't reported"
expect "rows 4, widths 0, widgets 0"

# with no widget, the top row is stock's, as wide as without plugins
printf 'width=0\n' >"$STATE/badger.cfg"
run_launcher "$WORK/badger-nowidget.log" "plugin badger: rows " || fail "the launcher exited early with badger's widget off"
log="$WORK/badger-nowidget.log"
expect "rows 4, widths 3, widgets 0"
without=$(top_row Apple "$log")
if [ -z "$without" ] || [ -z "$with_widget" ] || [ $((without - with_widget)) -ne 75 ]; then
	fail "the widget didn't take 75 off the top row: $without without, $with_widget with"
fi
[ "$(top_row Banana "$log")" = "$other_row" ] || fail "the widget changed a row below the header"
grep -qF "row Apple clear of the widget" "$log" && fail "badger saw a widget it didn't ask for"

# an error turns that plugin's hook off until the launcher restarts, and
# the rest keep running
for hook in row width widget; do
	printf 'fail=%s\n' "$hook" >"$STATE/badger.cfg"
	run_launcher "$WORK/badger-$hook.log" "plugin badger: rows " || fail "the launcher exited early with badger's $hook failing"
	log="$WORK/badger-$hook.log"
	case "$hook" in
	row) line="plugins: badger's decorate_row returned -1, leaving its decorations out until the launcher restarts"; want="rows 3, widths 3, widgets 3" ;;
	width) line="plugins: badger's header_width returned -1, leaving its header widget out until the launcher restarts"; want="rows 4, widths 2, widgets 1" ;;
	widget) line="plugins: badger's header_widget returned 1, leaving its header widget out until the launcher restarts"; want="rows 4, widths 2, widgets 2" ;;
	esac
	grep -qF "$line" "$log" || fail "badger's failing $hook wasn't logged"
	[ "$(grep -cF "plugins: badger's" "$log")" -eq 1 ] || fail "badger's failing $hook was logged more than once"
	expect "$want"
done
rm -f "$STATE/badger.cfg"

# decorators and widgets are chained in load order: the first decorates
# each row first, and its widget is next to the pill
build_plugin captioner 'id=captioner\nname=Captioner\napi=1\n'
printf 'badger=on\ncaptioner=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/captioner.log" "plugin badger: rows " "plugin captioner: row Apple" || fail "the launcher exited early with captioner loaded"
log="$WORK/captioner.log"
plugin=captioner
expect "row Apple after badger 1"
badger_x=$(sed -n 's/.*plugin badger: widget rect \([0-9]*\),.*/\1/p' "$log")
grep -qF "plugin captioner: widget rect $((badger_x - 15 - 40)),15 40x90, badger on the right 1" "$log" \
	|| fail "captioner's widget isn't left of badger's: $(grep "plugin captioner: widget" "$log")"
printf 'captioner=on\nbadger=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/captioner-first.log" "plugin badger: rows " "plugin captioner: row Apple" || fail "the launcher exited early with captioner first"
log="$WORK/captioner-first.log"
expect "row Apple after badger 0"
grep -qE "plugin captioner: widget rect [0-9]+,15 40x90, badger on the right 0$" "$log" || fail "captioner's widget isn't next to the pill"
rm -rf "$WORK/plugins/captioner.plugin"

# under a plugin layout, the layout passes each cell to decorate_cell, which
# runs the decorators clipped to the cell, and the view has the widget
build_plugin gridder 'id=gridder\nname=Gridder\napi=1\nslots=layout\n'
printf 'gridder=on\nbadger=on\n' >"$STATE/plugins.txt"
printf 'open=0\nfolder=Gridder\n' >"$STATE/badger.cfg"
run_launcher "$WORK/badger-grid.log" "plugin gridder: renders" "plugin badger: rows " || fail "the launcher exited early with badger under gridder"
log="$WORK/badger-grid.log"
plugin=gridder
expect "render header 75x90, beside status 1"
expect "render decorated 0, bad index -1 -1, no rect -1, outside -1, not the screen -1"
expect "after render fill -1, decorate -1"
plugin=badger
expect "row Apple index 0 selected 1 rect 4,4 248x172 name 4,4 0x172, row size 1"
expect "row Elder index 4 selected 0 rect 4,184 248x172 name 4,184 0x172, row size 1"
expect "row Apple dot 0 FF0000, clipped 1 (fill 0), select -1"
grep -qF "plugins: badger's" "$log" && fail "badger's hooks were turned off under gridder"
grep -qF "plugins: gridder's" "$log" && fail "gridder's layout was turned off with badger"
rm -rf "$WORK/plugins/gridder.plugin" "$WORK/plugins/badger.plugin" "$STATE/badger.cfg" "$STATE/plugins.txt"

# artist supplies art of every kind and loads it through the art cache: a
# provider answering each kind, a load then a hit, the entries either side
# of the selection loaded ahead, loads asked for before the selection moved
# dropped, and the built-in list falling back from a thumbnail to a box, 3D
# box or screenshot. framer answers boxes too, so the first in load order
# wins. Then, with a cache that holds two images, the least recently used
# one goes.
ART_DIR="$WORK/art"
mkdir -p "$ART_DIR" "$STATE"
for name in Alpha-thumb Bravo-box Charlie-3d framer-Bravo plain; do
	cp "$ROOT/tests/plugins/drawer/art.png" "$ART_DIR/$name.png"
done
for game in Alpha Bravo Charlie Delta Echo Foxtrot; do
	cp "$ROOT/tests/plugins/drawer/art.png" "$ART_DIR/$game-shot.png"
	cp "$ROOT/tests/plugins/drawer/art.png" "$ART_DIR/$game-back.png"
done
printf 'dir=%s\nimage=%s\n' "$ART_DIR" "$ART_DIR/plain.png" >"$STATE/artist.cfg"
printf 'dir=%s\n' "$ART_DIR" >"$STATE/framer.cfg"
printf 'artist=on\nframer=on\n' >"$STATE/plugins.txt"
build_plugin artist 'id=artist\nname=Artist\napi=1\nslots=layout\n'
build_plugin framer 'id=framer\nname=Framer\napi=1\n'
run_launcher "$WORK/artist.log" "plugin artist: done" || fail "the launcher exited early with artist loaded"
log="$WORK/artist.log"
plugin=artist
expect "open folder 1"
expect 'list art Alpha "Alpha-thumb.png"'
expect 'find Alpha thumbnail 1 "Alpha-thumb.png"'
expect 'find Bravo box 1 "Bravo-box.png"'
expect 'find Charlie box3d 1 "Charlie-3d.png"'
expect 'find Alpha logo 0 "Alpha-logo.png"'
expect 'find Delta screenshot 1 "Delta-shot.png"'
expect 'find Echo backdrop 1 "Echo-back.png"'
expect 'find Delta box3d 0 ""'
expect 'find Alpha unknown 0 ""'
expect "refused get -1 -1, cleared 1, missing -1, load -1 -1"
expect "shot Alpha first 0"
expect "shot Alpha in 1"
expect "hit 1 same 1, 4x2, pixel FF0000, blit 0, copy 1, free -1, fill -1, text -1"
expect "still FF0000, copy free 0"
expect "ahead Bravo 1 Foxtrot 1 Delta 0"
expect "before move Delta 0 Echo 0 Foxtrot 0"
expect "move 0"
expect "after move Alpha 0"
expect "dropped Delta 0 Echo 0 Foxtrot 0, kept Alpha 1"
expect 'list art Charlie "Charlie-3d.png"'
expect 'list art Bravo "Bravo-box.png"'
expect 'list art Delta "Delta-shot.png"'
expect "load first 0"
expect "load in 1, pixel FF0000"

printf 'framer=on\nartist=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/artist-framer.log" "plugin artist: done" || fail "the launcher exited early with framer first"
log="$WORK/artist-framer.log"
expect 'find Bravo box 1 "framer-Bravo.png"'
expect 'list art Bravo "framer-Bravo.png"'
# framer's missing box for Charlie still wins the box, and the list goes on to the 3D box
expect 'list art Charlie "Charlie-3d.png"'

printf 'mode=evict\n' >>"$STATE/artist.cfg"
printf 'artist=on\n' >"$STATE/plugins.txt"
# each 4x2 image decodes to 32 bytes
MOSAIC_LAUNCHER_ART_CACHE_BYTES=80 run_launcher "$WORK/artist-evict.log" "plugin artist: done" \
	|| fail "the launcher exited early with a small art cache"
unset MOSAIC_LAUNCHER_ART_CACHE_BYTES
log="$WORK/artist-evict.log"
expect "fill Charlie in 1"
expect "fill Delta in 1"
expect "touch Charlie 1"
expect "fill Echo in 1"
expect "kept Charlie 1 Echo 1, evicted Delta 0"
rm -rf "$WORK/plugins/artist.plugin" "$WORK/plugins/framer.plugin" "$ART_DIR" "$STATE/artist.cfg" "$STATE/framer.cfg" "$STATE/plugins.txt"

# selector hears about the selection once it settles: the starting one, a
# folder it opens, the end of a scroll through it a row a frame, nothing
# for a reload that keeps the same game, and the root's when it goes back.
# select_index is refused during the event. With fail=1, an error on the
# second event turns its selection_changed off.
build_plugin selector 'id=selector\nname=Selector\napi=1\n'
run_launcher "$WORK/selector.log" "plugin selector: done" || fail "the launcher exited early with selector loaded"
log="$WORK/selector.log"
plugin=selector
expect "settle 250"
expect "open folder 1"
grep -qE "plugin selector: event 1 .* settled 1, select -1$" "$log" || fail "selector's first event is missing"
expect "event 2 Apple in Selector, index 0, settled 1, select -1"
expect "event 3 Grape in Selector, index 6, settled 1, select -1"
expect "after scrolling 3 events"
expect "after reload 3 events"
expect "open root 1"
grep -qE "plugin selector: event 4 .* in root, index [0-9]+, settled 1, select -1$" "$log" || fail "selector didn't hear of the root's selection"
expect "done"
grep -qF "plugin selector: event 5" "$log" && fail "selector heard of the selection more than once a settle"
printf 'fail=1\n' >"$STATE/selector.cfg"
run_launcher "$WORK/selector-fail.log" "plugin selector: done" || fail "the launcher exited early with selector failing"
log="$WORK/selector-fail.log"
expect "event failing"
expect "after scrolling 2 events"
grep -qF "plugins: selector's selection_changed returned -1, no longer telling it about the selection until the launcher restarts" "$log" \
	|| fail "selector's failing selection_changed wasn't logged"
grep -qF "plugin selector: event 3" "$log" && fail "selector's selection_changed was called after it failed"
# open_path from tick, from a folder's last game into folders of one game,
# a subfolder and one elsewhere, leaves each on its own game
printf 'jump=1\n' >"$STATE/selector.cfg"
run_launcher "$WORK/selector-jump.log" "plugin selector: done" || fail "the launcher exited early with selector opening smaller folders"
log="$WORK/selector-jump.log"
expect "open sub 1"
expect "in Selector/Tiny, index 0 of 1"
expect "open tiny 1"
expect "in Tiny, index 0 of 1"
# open_path into a folder with no games keeps the launcher up through its
# frames, and the folder's event has no entry
printf 'empty=1\n' >"$STATE/selector.cfg"
run_launcher "$WORK/selector-empty.log" "plugin selector: done" || fail "the launcher exited early with selector opening an empty folder"
log="$WORK/selector-empty.log"
expect "open empty 1"
expect "event 3 NULL in Selector/Empty, index -1, settled 1, select -1"
expect "in Selector/Empty, 0 games, selected NULL, after 3 events"
expect "open folder 1"
expect "back in Selector, index 0 of 7"
rm -rf "$WORK/plugins/selector.plugin" "$STATE/selector.cfg"

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

# infoer reads .info sidecars through entry_info: display_name names games
# by them, each entry reads its sidecar once, and an oversized sidecar or a
# folder in its place is ignored and logged
INFO_DIR="$CARD/Infoer"
mkdir -p "$INFO_DIR/.media/Delta.info" "$STATE"
for game in Alpha Bravo Charlie Delta Echo; do : >"$INFO_DIR/$game.gb"; done
printf '# scraped\r\nname=Alpha Prime\r\nyear=1990\r\ngenre=Platformer\r\n' >"$INFO_DIR/.media/Alpha.info"
printf 'YEAR = 1985 \ngenre=Puzzle\nbroken line\n' >"$INFO_DIR/.media/Bravo.info"
{ echo "year=1999"; head -c 5000 /dev/zero | tr '\0' '#'; } >"$INFO_DIR/.media/Charlie.info"
printf 'infoer=on\n' >"$STATE/plugins.txt"
build_plugin infoer 'id=infoer\nname=Infoer\napi=1\n'
run_launcher "$WORK/infoer.log" "plugin infoer: done" || fail "the launcher exited early with infoer loaded"
log="$WORK/infoer.log"
plugin=infoer
expect "open folder 1"
grep -qE "plugin infoer: named [1-9][0-9]*$" "$log" || fail "display_name didn't name a game from its sidecar"
expect 'game Alpha Prime year 1 "1990" genre "Platformer" players 0 ""'
expect 'game Bravo year 1 "1985" genre "Puzzle" players 0 ""'
expect 'game Charlie year 0 "" genre "" players 0 ""'
expect 'game Delta year 0 "" genre "" players 0 ""'
expect 'game Echo year 0 "" genre "" players 0 ""'
expect "bad args -1 -1 -1 -1"
expect 'cut 1 "Alp"'
expect "kept 1990"
expect "fresh 2000"
grep -qF "plugins: ignoring $INFO_DIR/.media/Charlie.info: it's over 4096 bytes" "$log" || fail "the oversized sidecar wasn't logged"
grep -qF "plugins: ignoring $INFO_DIR/.media/Delta.info: it isn't a regular file" "$log" || fail "the folder sidecar wasn't logged"
grep -qF "Echo.info" "$log" && fail "a missing sidecar was logged"
rm -rf "$WORK/plugins/infoer.plugin" "$INFO_DIR" "$STATE/plugins.txt"

# shell paks' status files show as a pill in the header: the newest valid
# one that isn't stale, with files that are rejected logged once, and links,
# dotfiles and old files left out
STATUS_DIR="$STATE/status.d"
mkdir -p "$STATUS_DIR"
printf 'state=running\nlabel=Backup\n' >"$STATUS_DIR/backup.status"
printf 'state=running\n' >"$STATUS_DIR/old.status"
touch -t 200001010000 "$STATUS_DIR/old.status"
sleep 1
printf 'state=running\ndone=3\ntotal=10\nlabel=Syncing\n' >"$STATUS_DIR/sync.status"
printf 'state=maybe\n' >"$STATUS_DIR/bad.status"
head -c 2000 /dev/zero | tr '\0' '#' >"$STATUS_DIR/big.status"
printf 'state=running\nlabel=Linked\n' >"$WORK/outside.status"
ln -s "$WORK/outside.status" "$STATUS_DIR/link.status"
printf 'state=running\nlabel=Hidden\n' >"$STATUS_DIR/.tmp.status"
run_launcher "$WORK/status.log" "status: showing sync: Syncing 30%" "status: pill at " || fail "the launcher exited early with status files"
log="$WORK/status.log"
grep -qF "status: ignoring bad.status: state isn't running, done or error" "$log" || fail "the bad status file wasn't logged"
grep -qF "status: ignoring big.status: it's over 1024 bytes" "$log" || fail "the oversized status file wasn't logged"
grep -qF "status: ignoring link.status: it isn't a regular file" "$log" || fail "the linked status file wasn't refused"
[ "$(grep -cF "status: ignoring bad.status" "$log")" -eq 1 ] || fail "the bad status file was logged more than once"
grep -qE "status: showing (backup|old|link|\.tmp)" "$log" && fail "a status file other than the newest showed"
grep -qE "status: pill at [0-9]+,15 [0-9]+x90$" "$log" || fail "the pill isn't in the header: $(grep "status: pill" "$log")"

# the pill goes through the header row, so plugins' widgets sit left of it
# and have less room
printf 'badger=on\n' >"$STATE/plugins.txt"
build_plugin badger 'id=badger\nname=Badger\napi=1\n'
run_launcher "$WORK/status-badger.log" "status: pill at " "plugin badger: widget rect " || fail "the launcher exited early with badger and a status file"
log="$WORK/status-badger.log"
pill_x=$(sed -n 's/.*status: pill at \([0-9]*\),15 .*/\1/p' "$log" | tail -n 1)
pill_w=$(sed -n 's/.*status: pill at [0-9]*,15 \([0-9]*\)x90$/\1/p' "$log" | tail -n 1)
widget_x=$(sed -n 's/.*plugin badger: widget rect \([0-9]*\),.*/\1/p' "$log")
room=$(sed -n 's/.*plugin badger: width first: room \([0-9]*\) .*/\1/p' "$log")
if [ -z "$pill_x" ] || [ -z "$widget_x" ] || [ $((widget_x + 60 + 15)) -ne "$pill_x" ]; then
	fail "badger's widget isn't left of the status pill: pill at $pill_x, widget at $widget_x"
fi
# the stock pill starts a gap right of the status pill, and the widgets
# together keep to half the header left of it
stock_x=$((pill_x + pill_w + 15))
if [ -z "$room" ] || [ "$room" -ne $(((pill_x - 15) - (stock_x - (stock_x - 15) / 2))) ]; then
	fail "badger's room doesn't leave out the status pill: $room"
fi
rm -rf "$WORK/plugins/badger.plugin" "$STATE/plugins.txt"

# a file rewritten while the launcher runs is read again; done shows for a
# few seconds and then the pill goes
rm -rf "$STATUS_DIR" "$WORK/outside.status"
mkdir -p "$STATUS_DIR"
printf 'state=running\ndone=1\ntotal=4\nlabel=Scraping\n' >"$STATUS_DIR/scrape.status"
(
	sleep 2
	printf 'state=done\nlabel=Scraped\n' >"$STATUS_DIR/.scrape.tmp"
	mv "$STATUS_DIR/.scrape.tmp" "$STATUS_DIR/scrape.status"
) &
writer=$!
# done stays up for 5 seconds, so this case gets longer
wait_was=$WAIT
WAIT=$((WAIT + 10))
run_launcher "$WORK/status-done.log" "status: showing scrape: Scraped done" "status: none" || fail "the launcher exited early with a status file changing"
WAIT=$wait_was
wait "$writer"
log="$WORK/status-done.log"
grep -qF "status: showing scrape: Scraping 25%" "$log" || fail "the running status file didn't show"
rm -rf "$STATUS_DIR"
grep -qE "status: (showing|pill|ignoring|none)" "$WORK/hello.log" && fail "a status pill showed with no status files"

# rect_at <kind> <old|new> [log]: slider's line for where the selection is
# in that frame of the first transition of kind in log ($log by default), as
# "x y w h pill_x pill_y pill_w pill_h on beside cell", or another with
# RECT_NTH (a sed line address, such as 2 or $)
rect_at() {
	grep "plugin slider: begin $1 $2 row " "${3:-$log}" | sed -n "${RECT_NTH:-1}p" \
		| sed 's/.* row //; s/ pill / /; s/ on / /; s/ beside / /; s/ cell / /; s/[,x]/ /g'
}

# check_pill <what> <rect_at line>: the row is known and the pill is on it,
# from its left and as tall, and what's under the pill isn't what's beside it
check_pill() {
	# shellcheck disable=SC2086
	set -- "$1" $2
	what=$1
	shift
	if [ "$#" -ne 11 ]; then
		fail "slider didn't log where the selection is in $what"
	elif [ "$3" -le 0 ] || [ "$4" -le 0 ] || [ "$7" -le 0 ] || [ "$7" -gt "$3" ]; then
		fail "the selection in $what isn't on a row: $*"
	elif [ "$5" -ne "$1" ] || [ "$6" -ne "$2" ] || [ "$8" -ne "$4" ]; then
		fail "the pill in $what isn't on its row: $*"
	elif [ "$9" = "-" ] || [ "$9" = "${10}" ]; then
		fail "the pill isn't where $what says: $*"
	fi
}

# slider owns the transition slot and draws both pushes into its folders,
# opened with open_path from tick, with frames from progress 0 to exactly 1
# on a lent screen that starts black, the old and new frames read-only and
# taken back afterwards, and blit_alpha fading by half. Each says where the
# selection is in both frames, and the pill is there: Before on the first
# push's new frame, and Inner, the row under it, on the second's old one.
build_plugin slider 'id=slider\nname=Slider\napi=1\nslots=transition\n'
printf 'slider=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/slider.log" "plugin slider: done at" || fail "the launcher exited early with slider loaded"
log="$WORK/slider.log"
plugin=slider
grep -qF "plugins: slider picks the transition styles" "$log" || fail "slider didn't take the transition slot"
[ "$(grep -c "plugin slider: begin push stock" "$log")" -eq 2 ] || fail "slider wasn't asked about both pushes"
grep -qE "plugin slider: begin push stock [0-9]+ duration [0-9]+ ease 2, size 1, old [1-9][0-9]*x[1-9][0-9]* new [1-9][0-9]*x[1-9][0-9]*, max 1000$" "$log" \
	|| fail "slider's transition is wrong: $(grep "plugin slider: begin push stock" "$log" | head -1)"
expect "begin push read-only: fill -1, blit onto -1, free -1, select -1"
[ "$(grep -c "plugin slider: frame first push progress 0.000 duration 200, screen 1024x768 000000FF, select -1" "$log")" -eq 2 ] \
	|| fail "slider's first frames are wrong: $(grep "plugin slider: frame first" "$log" | head -1)"
grep -qE "plugin slider: frame alpha 0 [78][0-9A-F]0000FF, none 0 000000FF, bad -1 -1, new [0-9A-F]{6}FF$" "$log" \
	|| fail "blit_alpha didn't blend by half: $(grep "plugin slider: frame alpha" "$log" | head -1)"
[ "$(grep -cE "plugin slider: frame last push progress 1.000 frames [0-9]+ backwards 0 took [0-9]+ ms" "$log")" -eq 2 ] \
	|| fail "slider's transitions didn't end at progress 1: $(grep "plugin slider: frame last" "$log")"
expect "after: old - new -"
expect "done at $CARD/Slider/Inner"
grep -qF "transitions until the launcher restarts" "$log" && fail "slider's transitions were turned off"
expect "select inner 0"
check_pill "the first push's old frame" "$(rect_at push old)"
check_pill "the first push's new frame" "$(rect_at push new)"
check_pill "the second push's old frame" "$(RECT_NTH=2 rect_at push old)"
check_pill "the second push's new frame" "$(RECT_NTH=2 rect_at push new)"
# shellcheck disable=SC2046
set -- $(rect_at push new) $(RECT_NTH=2 rect_at push old)
if [ "$#" -ne 22 ] || [ "${13}" -ne $(($2 + $4)) ] || [ "${12}" -ne "$1" ]; then
	fail "Inner isn't the row under Before: $(grep "plugin slider: begin push .* row " "$log")"
fi

# under a plugin layout, the row is the last cell gridder passed to
# decorate_cell for the selected entry, the lit one, and there's no pill;
# Inner is the cell beside Before. Where the stock pill would rest on each
# push's new frame (the first entry, 40,60 inside its left end) is the lit
# cell, not the pill.
build_plugin gridder 'id=gridder\nname=Gridder\napi=1\nslots=layout\n'
printf 'slider=on\ngridder=on\n' >"$STATE/plugins.txt"
echo "probe=40,60" >"$STATE/slider.cfg"
run_launcher "$WORK/slider-layout.log" "plugin slider: done at" || fail "the launcher exited early with slider and gridder loaded"
log="$WORK/slider-layout.log"
expect "done at $CARD/Slider/Inner"
grep "plugin slider: begin push \(old\|new\) row " "$log" | grep -v " pill 0,0 0x0 on - beside - cell FF0000FF$" \
	&& fail "a push's selection under gridder isn't its lit cell"
[ "$(grep -c "plugin slider: begin push \(old\|new\) row [0-9]*,[0-9]* [1-9][0-9]*x[1-9][0-9]* " "$log")" -ge 4 ] \
	|| fail "slider wasn't told where gridder's cells are: $(grep "plugin slider: begin push .* row " "$log")"
# shellcheck disable=SC2046
set -- $(RECT_NTH='$' rect_at push new) $(RECT_NTH='$' rect_at push old)
if [ "$#" -ne 22 ] || [ "${12}" -le "$1" ] || [ "${13}" -ne "$2" ]; then
	fail "Inner isn't the cell beside Before under gridder: $(grep "plugin slider: begin push .* row " "$log")"
fi
[ "$(grep -c "plugin slider: begin push probe 40,60 old [0-9A-F]*FF new " "$log")" -ge 2 ] \
	|| fail "slider didn't read the stock pill's spot under gridder: $(grep "plugin slider: begin push probe" "$log")"
grep "plugin slider: begin push probe " "$log" | grep -v " new FF0000FF$" \
	&& fail "the stock pill was drawn over gridder's layout on a push's new frame"
rm -rf "$WORK/plugins/gridder.plugin" "$STATE/gridder.cfg" "$STATE/slider.cfg"
printf 'slider=on\n' >"$STATE/plugins.txt"

# off: no frames, and the folders still open
echo "style=off" >"$STATE/slider.cfg"
run_launcher "$WORK/slider-off.log" "plugin slider: done at" || fail "the launcher exited early with slider's transitions off"
log="$WORK/slider-off.log"
[ "$(grep -c "plugin slider: begin push stock" "$log")" -eq 2 ] || fail "slider wasn't asked about both pushes with transitions off"
grep -qF "plugin slider: frame" "$log" && fail "slider drew frames with its transitions off"
expect "done at $CARD/Slider/Inner"

# stock's own slide, with no frames from slider
echo "style=stock" >"$STATE/slider.cfg"
run_launcher "$WORK/slider-stock.log" "plugin slider: done at" || fail "the launcher exited early with slider choosing stock's"
log="$WORK/slider-stock.log"
grep -qF "plugin slider: frame" "$log" && fail "slider drew frames after choosing stock's"
expect "done at $CARD/Slider/Inner"

# a duration past the cap runs for the cap, logged once
printf 'duration=5000\nease=3\n' >"$STATE/slider.cfg"
run_launcher "$WORK/slider-cap.log" "plugin slider: done at" || fail "the launcher exited early with slider's long transition"
log="$WORK/slider-cap.log"
[ "$(grep -cF "plugins: slider asked for a 5000 ms transition, running it for 1000 ms" "$log")" -eq 1 ] || fail "the capped transition wasn't logged once"
expect "frame first push progress 0.000 duration 1000"
took=$(sed -n 's/.*plugin slider: frame last push progress 1.000 frames [0-9]* backwards 0 took \([0-9]*\) ms.*/\1/p' "$log" | head -1)
if [ -z "$took" ]; then
	fail "slider's capped transition didn't end"
elif [ "$took" -lt 900 ] || [ "$took" -gt 2500 ]; then
	fail "slider's capped transition took $took ms"
fi

# errors turn slider's transitions off for the run: the second push is stock's
for mode in bad frame noframe; do
	case "$mode" in
	frame) printf 'fail=frame\n' >"$STATE/slider.cfg" ;;
	*) printf 'style=%s\n' "$mode" >"$STATE/slider.cfg" ;;
	esac
	run_launcher "$WORK/slider-$mode.log" "plugin slider: done at" || fail "the launcher exited early with slider's $mode error"
	log="$WORK/slider-$mode.log"
	case "$mode" in
	bad) want="plugins: slider's transition_begin returned 7, using stock's transitions until the launcher restarts" ;;
	frame) want="plugins: slider's transition_frame returned -1, using stock's transitions until the launcher restarts" ;;
	noframe) want="plugins: slider chose a custom transition but has no transition_frame, using stock's transitions until the launcher restarts" ;;
	esac
	grep -qF "$want" "$log" || fail "slider's $mode error wasn't logged"
	[ "$(grep -c "plugin slider: begin push stock" "$log")" -eq 1 ] || fail "slider was asked again after its $mode error"
	expect "done at $CARD/Slider/Inner"
done
rm -f "$STATE/slider.cfg"

# with stock's Menu transitions off there are none: slider isn't asked about
# the pushes, and the folders still open; the card's settings are put back
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
printf 'menutransitions=0\n' >"$SETTINGS"
SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/slider-setting.log" "plugin slider: done at" \
	|| fail "the launcher exited early with Menu transitions off"
log="$WORK/slider-setting.log"
grep -qF "plugin slider: begin" "$log" && fail "slider was asked with Menu transitions off"
expect "done at $CARD/Slider/Inner"
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi

# without a slot it's never asked
printf 'id=slider\nname=Slider\napi=1\n' >"$WORK/plugins/slider.plugin/plugin.txt"
run_launcher "$WORK/slider-unslotted.log" "plugin slider: done at" || fail "the launcher exited early with slider unslotted"
log="$WORK/slider-unslotted.log"
grep -qF "plugins: slider has transitions but no slots=transition or slots=launch, so they won't be called" "$log" || fail "the missing transition slot wasn't reported"
grep -qF "plugin slider: begin" "$log" && fail "slider was asked without a slot"

# with only the launch slot, the pushes are stock's and the launch of a pak
# is slider's, from the last frame into black; the launch quits the
# launcher, and the card's recents, /tmp/next and /tmp/last.txt are put back
SLIDE_PAK="$WORK/slider/Slid.pak"
mkdir -p "$SLIDE_PAK"
: >"$SLIDE_PAK/launch.sh"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/recent.txt.saved"
[ -f /tmp/next ] && cp /tmp/next "$WORK/next.saved"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/last.txt.saved"
printf 'id=slider\nname=Slider\napi=1\nslots=launch\n' >"$WORK/plugins/slider.plugin/plugin.txt"
echo "pak=$SLIDE_PAK" >"$STATE/slider.cfg"
run_launcher "$WORK/slider-launch.log" && fail "the launcher didn't quit to launch slider's pak"
log="$WORK/slider-launch.log"
grep -qF "plugins: slider picks the launch style" "$log" || fail "slider didn't take the launch slot"
grep -qF "plugin slider: begin push" "$log" && fail "slider was asked about a push with only the launch slot"
expect "open pak 1"
grep -qE "plugin slider: begin launch stock 0 duration 0 ease 2, size 1, old [1-9][0-9]*x[1-9][0-9]* new [1-9][0-9]*x[1-9][0-9]*, max 1000$" "$log" \
	|| fail "slider's launch transition is wrong: $(grep "plugin slider: begin launch" "$log")"
grep -qE "plugin slider: frame alpha 0 [78][0-9A-F]0000FF, none 0 000000FF, bad -1 -1, new 000000FF$" "$log" \
	|| fail "the launch's new frame isn't black: $(grep "plugin slider: frame alpha" "$log")"
grep -qE "plugin slider: frame last launch progress 1.000 frames [0-9]+ backwards 0" "$log" || fail "slider's launch transition didn't end"
# the launch starts from Inner's list with Game selected, where the
# transition slot's second push left it; the black frame it goes into has
# none
check_pill "the launch's old frame" "$(rect_at launch old)"
[ "$(rect_at launch old | cut -d' ' -f1-8)" = "$(RECT_NTH=2 rect_at push new "$WORK/slider.log" | cut -d' ' -f1-8)" ] \
	|| fail "the launch's selection isn't Game's row: $(rect_at launch old)"
expect "begin launch new row 0,0 0x0 pill 0,0 0x0 on - beside -"
if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
if [ -f "$WORK/next.saved" ]; then cp "$WORK/next.saved" /tmp/next; else rm -f /tmp/next; fi
if [ -f "$WORK/last.txt.saved" ]; then cp "$WORK/last.txt.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
rm -f "$WORK/recent.txt.saved" "$WORK/next.saved" "$WORK/last.txt.saved"
rm -rf "$WORK/plugins/slider.plugin" "$WORK/slider" "$STATE/slider.cfg" "$STATE/plugins.txt"
grep -q "plugin slider:\|transition styles\|launch style" "$WORK/hello.log" && fail "transitions were asked about with no plugin taking them"

# painter generates art with art_surface: art_get hands it out read-only,
# off the plugin's budget, asked for once; a real file wins and art_find
# reports files only (writing where stock would look); the built-in list shows a generated thumbnail for a
# game with no art file, seen in the push's old frame; and art_surface
# can't ask for generated art or move the selection
PAINT_DIR="$WORK/paint"
mkdir -p "$PAINT_DIR" "$STATE"
cp "$ROOT/tests/plugins/drawer/art.png" "$PAINT_DIR/Real-thumb.png"
printf 'dir=%s\n' "$PAINT_DIR" >"$STATE/painter.cfg"
printf 'painter=on\n' >"$STATE/plugins.txt"
build_plugin painter 'id=painter\nname=Painter\napi=1\nslots=transition\n'
run_launcher "$WORK/painter.log" "plugin painter: done" || fail "the launcher exited early with painter loaded"
log="$WORK/painter.log"
plugin=painter
expect "open folder 1"
expect "asked Blank 0"
expect "inside get -1 select -1"
expect "blank box 1 1 same 1, 64x64 FF00FF"
expect "blank box read-only: fill -1, free -1"
expect "blank thumbnail 1 00FF00"
expect "blank find 0 \"$CARD/Painter/.media/Blank.png\""
expect "shy box -1 -1"
expect "budget 1"
expect "real thumbnail 1 FF0000"
expect "asks 3"
expect "open next 1"
expect "push to next old thumbnail green 1"
[ "$(grep -cE "plugin painter: asked (Blank|Real|Shy) " "$log")" -eq 3 ] || fail "painter was asked more than once for the same art: $(grep "plugin painter: asked" "$log" | tr '\n' ',')"
grep -qF "plugin painter: asked Real" "$log" && fail "painter was asked for art Real has a file for"
grep -qF "plugins: painter's art_surface" "$log" && fail "painter's art_surface was turned off"

# handing over the same surface twice is an error: it's not asked again
printf 'dir=%s\nmode=again\n' "$PAINT_DIR" >"$STATE/painter.cfg"
run_launcher "$WORK/painter-again.log" "plugin painter: done" || fail "the launcher exited early with painter's error"
log="$WORK/painter-again.log"
grep -qF "plugins: painter's art_surface returned a surface it didn't make, no longer asking it for art until the launcher restarts" "$log" \
	|| fail "painter's error wasn't logged"
expect "blank box -1 -1 same 0"
expect "shy box -1 -1"
expect "asks 2"
rm -rf "$WORK/plugins/painter.plugin" "$PAINT_DIR" "$STATE/painter.cfg" "$STATE/plugins.txt"
grep -qF "plugin painter:" "$WORK/hello.log" && fail "art was generated with no plugin making it"

# backer owns the background slot: its background_render gets a lent
# screen-sized surface over stock's background, which it draws over, with
# NULL in an empty folder, and both pushes start from frames with its
# background on them, the new one this frame's
BACK_BG="$WORK/back.png"
cp "$ROOT/tests/plugins/drawer/art.png" "$BACK_BG"
printf 'bg=%s\n' "$BACK_BG" >"$STATE/backer.cfg"
printf 'backer=on\n' >"$STATE/plugins.txt"
build_plugin backer 'id=backer\nname=Backer\napi=1\nslots=background,transition\n'
run_launcher "$WORK/backer.log" "plugin backer: done at" || fail "the launcher exited early with backer loaded"
log="$WORK/backer.log"
plugin=backer
grep -qF "plugins: backer draws the background" "$log" || fail "backer didn't take the background slot"
grep -qE "plugin backer: render size ([0-9]+)x([0-9]+) of \1x\2, select -1, free -1$" "$log" \
	|| fail "backer's lent surface is wrong: $(grep "plugin backer: render size" "$log")"
# stock's background, Empty's from art_path, red on the left, under it once it has loaded
expect "render Empty under FF0000FF"
# the first push's old frame is from the startup fade, so only its new one
grep -qE "plugin backer: push to backer old [0-9A-F]{8} new 00FF00FF" "$log" || fail "the first push's new frame has no background from backer"
expect "push to empty old 00FF00FF new FFFF00FF left FF0000FF"
expect "render (none) under FF0000FF"
expect "done at $CARD/Backer/Empty"
grep -qF "stock's background until" "$log" && fail "backer's background was turned off"

# an error turns it off for the run: stock's background, blue on the right, in the new frame
printf 'bg=%s\nfail=empty\n' "$BACK_BG" >"$STATE/backer.cfg"
run_launcher "$WORK/backer-fail.log" "plugin backer: done at" || fail "the launcher exited early with backer's error"
log="$WORK/backer-fail.log"
grep -qF "plugins: backer's background_render returned -1, using stock's background until the launcher restarts" "$log" \
	|| fail "backer's error wasn't logged"
[ "$(grep -c "plugin backer: render failing" "$log")" -eq 1 ] || fail "backer was asked again after its error"
expect "push to empty old 00FF00FF new 0000FFFF"

# without the slot it's never called
rm -f "$STATE/backer.cfg"
printf 'id=backer\nname=Backer\napi=1\nslots=transition\n' >"$WORK/plugins/backer.plugin/plugin.txt"
run_launcher "$WORK/backer-unslotted.log" "plugin backer: done at" || fail "the launcher exited early with backer unslotted"
log="$WORK/backer-unslotted.log"
grep -qF "plugins: backer has background_render but no slots=background, so it won't be called" "$log" || fail "the missing background slot wasn't reported"
grep -qF "plugin backer: render" "$log" && fail "backer drew without the slot"
rm -rf "$WORK/plugins/backer.plugin" "$BACK_BG" "$STATE/plugins.txt"
grep -qF "draws the background" "$WORK/hello.log" && fail "a background was drawn with no plugin taking the slot"

# the bundled art-boxart answers art from each fork's folder convention, as
# looker sees it through art_find: nx-redux's .media/box, 3d and wheel,
# MyMinUI's Imgs, UnuOS's .res, visible Images folders, a folder's
# cover.png and a Pico-8 cart as its own art, in that order, with stock's
# .media thumbnail kept unless the thumbnail setting is on. The Imgs and
# Images folders are left out of the list. Then the settings turn each off.
BOX_DIR="$CARD/Boxart"
mkdir -p "$BOX_DIR/.media/box" "$BOX_DIR/.media/3d" "$BOX_DIR/.media/wheel" "$BOX_DIR/Imgs" "$BOX_DIR/.res" \
	"$BOX_DIR/Images" "$BOX_DIR/Cover" "$STATE"
for game in Alpha Bravo Charlie Delta Foxtrot; do : >"$BOX_DIR/$game.gb"; done
: >"$BOX_DIR/Cover/Game.gb"
for art in .media/Alpha .media/box/Alpha .media/3d/Alpha .media/wheel/Alpha Imgs/Alpha Imgs/Bravo .res/Charlie \
	Images/Delta Cover/cover Echo.p8; do
	cp "$ROOT/tests/plugins/drawer/art.png" "$BOX_DIR/$art.png"
done
make -s -C "$ROOT/plugins" art-boxart ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building art-boxart"
build_plugin looker 'id=looker\nname=Looker\napi=1\n'
printf 'art-boxart=on\nlooker=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/boxart.log" "plugin looker: done" || fail "the launcher exited early with art-boxart loaded"
log="$WORK/boxart.log"
plugin=looker
grep -qF "plugins: loaded art-boxart" "$log" || fail "art-boxart didn't load"
expect "open folder 1"
expect "entries Alpha.gb,Bravo.gb,Charlie.gb,Cover,Delta.gb,Echo.p8.png,Foxtrot.gb"
expect 'art Alpha.gb thumbnail 1 ".media/Alpha.png"'
expect 'art Alpha.gb box 1 ".media/box/Alpha.png"'
expect 'art Alpha.gb box3d 1 ".media/3d/Alpha.png"'
expect 'art Alpha.gb logo 1 ".media/wheel/Alpha.png"'
expect 'art Bravo.gb box 1 "Imgs/Bravo.png"'
expect 'art Bravo.gb thumbnail 0 ".media/Bravo.png"'
expect 'art Bravo.gb box3d 0 ""'
expect 'art Charlie.gb box 1 ".res/Charlie.png"'
expect 'art Delta.gb box 1 "Images/Delta.png"'
expect 'art Cover box 1 "Cover/cover.png"'
expect 'art Echo.p8.png box 1 "Echo.p8.png"'
expect 'art Foxtrot.gb box 0 ""'
expect 'art Foxtrot.gb logo 0 ""'

# the thumbnail setting puts this art before stock's, and with nx-redux's
# folders, covers and carts off the next convention answers; the art
# folders show with hide off
printf 'thumbnail=on\nmedia=off\ncover=off\nrom_art=off\nhide=off\n' >"$STATE/art-boxart.cfg"
run_launcher "$WORK/boxart-settings.log" "plugin looker: done" || fail "the launcher exited early with art-boxart's settings changed"
log="$WORK/boxart-settings.log"
expect "entries Alpha.gb,Bravo.gb,Charlie.gb,Cover,Delta.gb,Echo.p8.png,Foxtrot.gb,Images,Imgs"
expect 'art Alpha.gb thumbnail 1 "Imgs/Alpha.png"'
expect 'art Alpha.gb box 1 "Imgs/Alpha.png"'
expect 'art Alpha.gb box3d 0 ""'
expect 'art Alpha.gb logo 0 ""'
expect 'art Bravo.gb thumbnail 1 "Imgs/Bravo.png"'
expect 'art Cover box 0 ""'
expect 'art Echo.p8.png box 0 ""'

# with Imgs, .res and Images off, their art is gone and the folders show
# even with hide on
printf 'imgs=off\nres=off\nimages=off\n' >"$STATE/art-boxart.cfg"
run_launcher "$WORK/boxart-off.log" "plugin looker: done" || fail "the launcher exited early with art-boxart's folders off"
log="$WORK/boxart-off.log"
expect "entries Alpha.gb,Bravo.gb,Charlie.gb,Cover,Delta.gb,Echo.p8.png,Foxtrot.gb,Images,Imgs"
expect 'art Alpha.gb box 1 ".media/box/Alpha.png"'
expect 'art Bravo.gb box 0 ""'
expect 'art Charlie.gb box 0 ""'
expect 'art Delta.gb box 0 ""'
expect 'art Cover box 1 "Cover/cover.png"'
expect 'art Echo.p8.png box 1 "Echo.p8.png"'
rm -rf "$WORK/plugins/art-boxart.plugin" "$WORK/plugins/looker.plugin" "$BOX_DIR" "$STATE/art-boxart.cfg" "$STATE/plugins.txt"

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
