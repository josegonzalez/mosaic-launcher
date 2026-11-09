#!/bin/sh
#
# Loads test plugins into the desktop build and checks what happened:
# a plugin that loads and uses the host table, one with the wrong API
# version, one that replaces the root and opens entries, one that sorts
# and renames a folder and moves the selection, one that adds quick menu
# tiles and shares a button, one that declines after registering things,
# one that powers off, one that sets stock toggles, one that reads theme
# colours and sets an LED colour, one that asks about Bluetooth, one that
# draws, one that writes pixels into its surfaces, one that draws the game list as a grid, two that decorate rows and
# add header widgets, one that decorates the game's art, two that show their own surfaces in place of it, one that shows its own in the bundled grid and fancy layouts' art, one that plays audio and one refused the device meanwhile, one that reads back the dots the bundled row-badges
# puts on rows, two that supply art and load it through the art
# cache, one that hears about the selection, one that reads .info
# sidecars, one that reads the play history, one that reads local
# RetroAchievements progress, one that picks transition
# styles, one that generates art, one that draws the background, one that
# looks at the bundled box art provider's art, one that reads the bundled
# grid, carousel, hero rail and shelf layouts' cells back, one that looks
# at the bundled placeholder art plugin's cards, one that hides entries'
# names, one that reads the bundled art backdrop plugin's background and
# the hero rail's hero back, one that reads the bundled header indicators
# back, one that reads the bundled fancy layout's art panel back, one that
# reads the bundled play time captions back, one that reads the bundled
# metadata band back, one that launches a game past the bundled play time
# limit, one that lists the bundled collections plus plugin's collections,
# two that filter input, pressing buttons themselves, one that reads the
# bundled launch animation's frames back, one that reads the bundled
# transition styles' frames back, one that presses the bundled hold
# lock's chord, one that walks the selection through the bundled video
# previews' clips, one with screens of its own, which also owns the home
# and switcher slots, draws stock's button hints and presses the bundled
# game switcher plus's buttons, and one with an overlay,
# and one that crashes during startup. A preloaded library,
# presser, drives the Launcher pak's Plugins page through the bundled play
# time limit's protected settings, reads back those hints, and drives the
# bundled home dashboard's tabs and modes. It also checks the status pill shell
# paks' status files show.
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

# writer copies RGBA8888 pixels into surfaces of its own and reads them
# back; writes into its lent row, an art cache image and, under hello's id,
# its own surface are refused
mkdir -p "$STATE"
printf 'art=%s\nother=hello\n' "$ROOT/tests/plugins/drawer/art.png" >"$STATE/writer.cfg"
printf 'hello=on\nwriter=on\n' >"$STATE/plugins.txt"
build_plugin hello 'id=hello\nname=Hello\napi=1\n'
build_plugin writer 'id=writer\nname=Writer\napi=1\n'
run_launcher "$WORK/writer.log" "plugin writer: done" || fail "the launcher exited early with writer loaded"
log="$WORK/writer.log"
plugin=writer
expect "write 0, FF0000 FF 00FF00 FF 0000FF 80 FF0000 FF, FF0000 FF 123456 78 000000 00 FF0000 FF"
expect "whole 0, 112233 44 112233 44"
expect "clipped 0, 505050 FF 909090 FF 112233 44, off 0, 112233 44"
expect "blit 00FF00 FF FF0000 FF"
expect "null pixels -1, null surface -1, unknown -1, short pitch -1, negative -1, not loaded -1, no id -1"
expect "image 0 112233 44"
expect "freed -1"
expect "frame 720x480 written 0,"
expect "lent -1"
expect "art -1 FF0000 FF"
expect "other -1, own 0"
grep -F "plugin writer: frame" "$log"
rm -rf "$WORK/plugins/writer.plugin" "$WORK/plugins/hello.plugin" "$STATE/writer.cfg" "$STATE/hello.cfg" "$STATE/plugins.txt"

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

# bander decorates the built-in list's thumbnail: it's called once the
# selected game's art is in, with the art's rect as the list places it and
# the column under it, its drawing lands there and stops at the art, and
# select_index and nested decorating refuse. A game with no art, an empty
# folder and art still loading for another game get no call. As the
# transition slot's owner it reads the push's old frame, where the layers
# are drawn together: its band shows over the background, and the
# thumbnail over the screen, beside it
BAND_DIR="$CARD/Bander"
mkdir -p "$BAND_DIR/.media" "$STATE"
cp "$ROOT/tests/plugins/peeker/art.png" "$BAND_DIR/.media/Apple.png"
cp "$ROOT/tests/plugins/drawer/art.png" "$BAND_DIR/.media/Cherry.png"
build_plugin bander 'id=bander\nname=Bander\napi=1\nslots=transition\n'
printf 'bander=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/bander.log" "plugin bander: done" || fail "the launcher exited early with bander loaded"
log="$WORK/bander.log"
plugin=bander
expect "open folder 1"
expect "decorate art outside a layout -1"
expect "art Apple rect 519,154 460x460 below 519,614 460x49, band 0 FF00FF, clipped 1 (fill 0), select -1, nested -1 -1"
expect "art Cherry rect 519,269 460x230 below 519,499 460x164, band 0 FF00FF, clipped 1 (fill 0), select -1, nested -1 -1"
expect "open inner 1"
expect "push band FF00FF art FF0000"
expect "done: Apple 1, Banana 0, Cherry 1, inner 0"
grep -qF "plugins: bander's" "$log" && fail "bander's hooks were turned off"
grep -qF "decorate_art" "$WORK/hello.log" && fail "a plugin without art decorators logged them"

# an error turns that plugin's decorate_art off until the launcher restarts
printf 'fail=art\n' >"$STATE/bander.cfg"
run_launcher "$WORK/bander-fail.log" "plugin bander: done" || fail "the launcher exited early with bander failing"
log="$WORK/bander-fail.log"
grep -qF "plugins: bander's decorate_art returned -1, leaving its art decorations out until the launcher restarts" "$log" \
	|| fail "bander's failing decorate_art wasn't logged"
[ "$(grep -cF "plugins: bander's" "$log")" -eq 1 ] || fail "bander's failing decorate_art was logged more than once"
expect "done: Apple 0, Banana 0, Cherry 0, inner 0"
rm -f "$STATE/bander.cfg"

# with Show game art off no thumbnail shows, so nothing is decorated; the
# card's settings are put back
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
printf 'gameart=0\n' >"$SETTINGS"
SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/bander-gameart.log" "plugin bander: done" \
	|| fail "the launcher exited early with Show game art off"
log="$WORK/bander-gameart.log"
expect "done: Apple 0, Banana 0, Cherry 0, inner 0"
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -f "$WORK/minuisettings.txt.saved"

# art decorators are chained in load order: the first draws first
build_plugin captioner 'id=captioner\nname=Captioner\napi=1\n'
printf 'bander=on\ncaptioner=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/bander-chain.log" "plugin bander: done" "plugin captioner: art after" || fail "the launcher exited early with captioner after bander"
log="$WORK/bander-chain.log"
plugin=captioner
expect "art after bander 1"
printf 'captioner=on\nbander=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/bander-chain-first.log" "plugin bander: done" "plugin captioner: art after" || fail "the launcher exited early with captioner first"
log="$WORK/bander-chain-first.log"
expect "art after bander 0"
rm -rf "$WORK/plugins/captioner.plugin"

# under a plugin layout, the layout passes its art to decorate_art, which
# runs the decorators clipped to the area under it, as wide as the art
build_plugin gridder 'id=gridder\nname=Gridder\napi=1\nslots=layout\n'
printf 'gridder=on\nbander=on\n' >"$STATE/plugins.txt"
printf 'art=1\n' >"$STATE/gridder.cfg"
printf 'open=0\nfolder=Gridder\n' >"$STATE/bander.cfg"
run_launcher "$WORK/bander-grid.log" "plugin gridder: renders" "plugin bander: done" || fail "the launcher exited early with bander under gridder"
log="$WORK/bander-grid.log"
plugin=gridder
expect "render art 0"
expect "render art bad index -1 -1, no rect -1, outside -1, not the screen -1, at the bottom 0"
expect "after render art -1"
plugin=bander
expect "art Apple rect 4,4 248x86 below 4,90 248x573, band 0 FF00FF, clipped 1 (fill 0), select -1, nested -1 -1"
expect "done: Apple 1, Banana 0, Cherry 0"
grep -qF "plugins: bander's" "$log" && fail "bander's hooks were turned off under gridder"
grep -qF "plugins: gridder's" "$log" && fail "gridder's layout was turned off with bander"
rm -rf "$WORK/plugins/gridder.plugin" "$WORK/plugins/bander.plugin" "$BAND_DIR" "$STATE/gridder.cfg" "$STATE/bander.cfg" "$STATE/plugins.txt"

# swapper shows surfaces of its own in place of the built-in list's
# thumbnail with art_replace: placed as a thumbnail of their size, with its
# rounded corners, and art decorators under them. New frames of the same
# surface redraw only the thumbnail. It goes when the selection moves or
# the surface is freed, the first plugin's keeps the place over taker's,
# and bad calls are refused. As the transition slot's owner it reads each
# push's old frame, where the layers are drawn together
SWAP_DIR="$CARD/Swapper"
mkdir -p "$SWAP_DIR/.media" "$SWAP_DIR/Next/.media" "$SWAP_DIR/Next/Next/.media" "$STATE"
cp "$ROOT/tests/plugins/drawer/art.png" "$SWAP_DIR/.media/Apple.png"
cp "$ROOT/tests/plugins/drawer/art.png" "$SWAP_DIR/Next/.media/Cherry.png"
cp "$ROOT/tests/plugins/drawer/art.png" "$SWAP_DIR/Next/Next/.media/Dog.png"
build_plugin hello 'id=hello\nname=Hello\napi=1\n'
build_plugin swapper 'id=swapper\nname=Swapper\napi=1\nslots=transition\n'
build_plugin taker 'id=taker\nname=Taker\napi=1\n'
printf 'hello=on\nswapper=on\ntaker=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/swapper.log" "plugin swapper: done" || fail "the launcher exited early with swapper loaded"
log="$WORK/swapper.log"
plugin=swapper
expect "init -1"
expect "art Apple rect 519,269 460x230"
expect "in decorate_art -1"
expect "null id -1, not loaded -1, unknown -1, other's id -1, art cache -1, clear none 0"
expect "show 0"
expect "art Apple rect 519,231 460x306"
expect "frames 30 failed 0, list redraws 0"
expect "push 1 Apple left 0000FF corner "
grep -qF "plugin swapper: push 1 Apple left 0000FF corner 0000FF" "$log" && fail "the replacement's corners weren't rounded"
expect "show Banana 0"
expect "art Banana rect 519,231 460x306"
expect "select Cherry 0"
expect "art Cherry rect 519,269 460x230"
expect "push 2 Cherry left FF0000"
expect "show small 0"
expect "art Dog rect 519,231 460x306"
expect "free small 0"
expect "while taker's shows -1"
expect "art Dog rect 519,154 460x460"
expect "push 3 Dog left FF0000"
expect "empty -1"
plugin=taker
expect "refused -1"
expect "shown 0"
expect "cleared 0"
grep -qF "plugins: swapper's" "$log" && fail "swapper's hooks were turned off"
grep -F "plugin swapper: push" "$log"

# nothing shows with Show game art off; under a plugin layout it's the
# layout's to draw, which gridder does with swap=1 from art_replacement
printf 'once=1\n' >"$STATE/swapper.cfg"
printf 'swapper=on\n' >"$STATE/plugins.txt"
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
printf 'gameart=0\n' >"$SETTINGS"
SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/swapper-gameart.log" "plugin swapper: done" \
	|| fail "the launcher exited early with Show game art off"
log="$WORK/swapper-gameart.log"
plugin=swapper
expect "once -1"
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -f "$WORK/minuisettings.txt.saved"
build_plugin gridder 'id=gridder\nname=Gridder\napi=1\nslots=layout\n'
printf 'gridder=on\nswapper=on\n' >"$STATE/plugins.txt"
printf 'swap=1\n' >"$STATE/gridder.cfg"
run_launcher "$WORK/swapper-grid.log" "plugin gridder: renders" "plugin swapper: done" "plugin gridder: render replacement Apple 1" \
	|| fail "the launcher exited early with swapper under gridder"
log="$WORK/swapper-grid.log"
expect "once 0"
plugin=gridder
expect "after render replacement -1"
expect "render replacement Apple 1 lit 00FF00, other 0, bad index -1 -1, no out -1"
grep -qF "plugins: gridder's" "$log" && fail "gridder's layout was turned off with swapper"
rm -rf "$WORK/plugins/gridder.plugin" "$WORK/plugins/swapper.plugin" "$WORK/plugins/taker.plugin" "$WORK/plugins/hello.plugin" "$SWAP_DIR" \
	"$STATE/swapper.cfg" "$STATE/taker.cfg" "$STATE/hello.cfg" "$STATE/gridder.cfg" "$STATE/plugins.txt"

# reeler shows a surface in place of the selected game's art under the
# bundled layout-grid, which draws it in the selected tile, and
# layout-fancy, which draws it in its panel with art decorators under it:
# new frames of it redraw the layout with no request_redraw, and it goes
# when cleared or when the selection moves, leaving the game's own art.
# Under gridder, which doesn't draw it, new frames don't redraw the list
REEL_DIR="$CARD/Reeler"
mkdir -p "$REEL_DIR/.media" "$STATE"
cp "$ROOT/tests/plugins/peeker/art.png" "$REEL_DIR/.media/Apple.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$REEL_DIR/.media/Banana.png"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" layout-grid layout-fancy >/dev/null || fail "building the layouts"
build_plugin reeler 'id=reeler\nname=Reeler\napi=1\n'
# check_reeler <layout> <what reeler reads>: the case's lines in $log
check_reeler() {
	plugin=reeler
	expect "open folder 1"
	expect "$2 Apple 00FF00"
	expect "show 0"
	expect "$2 Apple FF0000"
	expect "shown FF0000"
	grep -qE "plugin reeler: frames 30 failed 0, redraws ([2-9][0-9]|[1-9][0-9][0-9]), now 0000FF, " "$log" \
		|| fail "new frames didn't redraw $1: $(grep "plugin reeler: frames" "$log")"
	expect "$2 Apple 0000FF"
	expect "clear 0"
	expect "cleared 00FF00"
	expect "show again 0"
	expect "select Banana 0"
	expect "moved Banana 00FF00"
	grep -qF "plugins: $1's" "$log" && fail "$1's layout was turned off with reeler"
	grep -qF "plugins: reeler's" "$log" && fail "reeler's decorators were turned off under $1"
	grep -F "plugin reeler: frames" "$log"
}
printf 'layout-grid=on\nreeler=on\nslot.layout=layout-grid\n' >"$STATE/plugins.txt"
run_launcher "$WORK/reeler-grid.log" "plugin reeler: done" || fail "the launcher exited early with reeler under layout-grid"
log="$WORK/reeler-grid.log"
check_reeler layout-grid cell
printf 'read=art\n' >"$STATE/reeler.cfg"
printf 'layout-fancy=on\nreeler=on\nslot.layout=layout-fancy\n' >"$STATE/plugins.txt"
run_launcher "$WORK/reeler-fancy.log" "plugin reeler: done" || fail "the launcher exited early with reeler under layout-fancy"
log="$WORK/reeler-fancy.log"
check_reeler layout-fancy art
# the panel's art, under which art decorators go: the thumbnail, then the frame fitted across the panel
grep -qE "plugin reeler: art Apple 00FF00 rect 519,[0-9]+ [0-9]+x[0-9]+$" "$log" || fail "layout-fancy's art rect is wrong: $(grep "plugin reeler: art Apple" "$log")"
grep -qE "plugin reeler: art Apple FF0000 rect 519,[0-9]+ 460x306$" "$log" || fail "layout-fancy's panel doesn't fit the frame: $(grep "plugin reeler: art Apple FF0000" "$log")"
rm -rf "$WORK/plugins/layout-grid.plugin" "$WORK/plugins/layout-fancy.plugin"
build_plugin gridder 'id=gridder\nname=Gridder\napi=1\nslots=layout\n'
printf 'plain=1\n' >"$STATE/reeler.cfg"
printf 'gridder=on\nreeler=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/reeler-gridder.log" "plugin reeler: done" || fail "the launcher exited early with reeler under gridder"
log="$WORK/reeler-gridder.log"
grep -qF "plugins: gridder draws the game list" "$log" || fail "gridder didn't take the game list"
expect "show 0"
grep -qE "plugin reeler: frames 30 failed 0, redraws [0-2], " "$log" \
	|| fail "new frames redrew a layout that doesn't draw them: $(grep "plugin reeler: frames" "$log")"
rm -rf "$WORK/plugins/reeler.plugin" "$WORK/plugins/gridder.plugin" "$WORK/plugins/layout-grid.plugin" \
	"$WORK/plugins/layout-fancy.plugin" "$REEL_DIR" "$STATE/reeler.cfg" "$STATE/plugins.txt"

# speaker plays audio through the host's audio calls with SDL's dummy
# driver: refusals before it opens, a queue from its own thread that falls
# as it plays and holds while paused, the queue's cap, closing, the
# main-thread-only calls refused from its thread, a reopen in another
# format, a thread queueing while the device opens and closes, and the
# launcher closing the device itself before a sleep (once what's queued
# plays out), before a launch and at quit; SDL's audio is started only
# while the device is open (until a sleep, after which stock starts it
# itself). hearer is refused the device meanwhile
SPEAK_DIR="$CARD/Speaker"
SPEAK_PAK="$SPEAK_DIR/Spoken.pak"
mkdir -p "$SPEAK_PAK" "$STATE"
: >"$SPEAK_PAK/launch.sh"
[ -f /tmp/next ] && cp /tmp/next "$WORK/speaker-next.saved"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/speaker-last.saved"
build_plugin speaker 'id=speaker\nname=Speaker\napi=1\n'
build_plugin hearer 'id=hearer\nname=Hearer\napi=1\n'
printf 'speaker=on\nhearer=on\n' >"$STATE/plugins.txt"
printf 'pak=%s\n' "$SPEAK_PAK" >"$STATE/speaker.cfg"
SDL_AUDIODRIVER=dummy run_launcher "$WORK/speaker.log" && fail "the launcher didn't quit to launch speaker's pak"
log="$WORK/speaker.log"
plugin=speaker
expect "sdl audio before open 0"
expect "before open: queue -1, queued -1, pause -1, close -1"
expect "bad rate -1, bad channels -1, unknown id -1, max ms 1000"
expect "open 0"
expect "open again 0, queued 0, empty queue 0, sdl audio 1"
expect "thread queued 37 of 37 chunks, null -1, over -1, queued 1"
expect "queued fell 1"
expect "pause 0"
expect "paused held 1"
expect "unpause 0"
expect "playing again 1"
expect "close 0, again -1, queued after -1, queue after -1, sdl audio 0"
expect "off main open -1, pause -1, close -1, queued -1"
expect "open 0, other format 0"
expect "race cycles 1, calls 1, ok 1, refused 1"
expect "queued for sleep 0"
expect "before_sleep queued 1"
expect "after_resume queued -1"
expect "open after sleep 0"
expect "open pak 1"
expect "after launch queued -1"
expect "open after launch 0"
expect "quit queued -1"
plugin=hearer
expect "open while taken -1, queue -1, queued -1, close -1"
grep -qF "plugins: speaker opened audio at 48000 Hz with 2 channels (dummy)" "$log" || fail "speaker's open wasn't logged"
grep -qF "plugins: hearer can't open audio, speaker has it" "$log" || fail "hearer's refusal wasn't logged"
grep -qF "plugins: speaker can't open audio at 1000 Hz with 2 channels" "$log" || fail "the bad rate wasn't logged"
[ "$(grep -cF "plugins: speaker queued more than 1000 ms of audio" "$log")" -eq 1 ] || fail "the queue's cap wasn't logged once an open"
grep -qF "plugins: speaker called audio_open off the main thread" "$log" || fail "the open off the main thread wasn't logged"
grep -qF "plugins: speaker reopened audio at 22050 Hz with 1 channel (dummy)" "$log" || fail "the reopen wasn't logged"
grep -qF "plugins: speaker closed audio" "$log" || fail "speaker's close wasn't logged"
drained=$(sed -n "s/.*plugins: closed speaker's audio before sleep, after \([0-9]*\) ms for it to play out.*/\1/p" "$log")
if [ -z "$drained" ]; then
	fail "the launcher didn't close speaker's audio before sleep"
elif [ "$drained" -lt 300 ] || [ "$drained" -gt 1100 ]; then
	fail "the launcher waited $drained ms for speaker's audio to play out, not most of 0.6 s"
fi
launch_line=$(grep -nF "plugins: closed speaker's audio before a launch" "$log" | cut -d: -f1)
cmd_line=$(grep -nF "cmd: '" "$log" | cut -d: -f1)
if [ -z "$launch_line" ] || [ -z "$cmd_line" ] || [ "$launch_line" -gt "$cmd_line" ]; then
	fail "the launcher didn't close speaker's audio before the launch"
fi
quit_line=$(grep -nF "plugin speaker: quit" "$log" | cut -d: -f1)
closed_line=$(grep -nF "plugins: closed speaker's audio at quit" "$log" | cut -d: -f1)
if [ -z "$quit_line" ] || [ -z "$closed_line" ] || [ "$closed_line" -gt "$quit_line" ]; then
	fail "the launcher didn't close speaker's audio at quit, before its quit"
fi
grep -qi "audio" "$WORK/hello.log" && fail "audio was touched with no plugin asking"

# a plugin that declines to start has its device closed, and so does
# powering off; /tmp/next and /tmp/last.txt are put back afterwards
printf 'speaker=on\n' >"$STATE/plugins.txt"
printf 'mode=decline\n' >"$STATE/speaker.cfg"
SDL_AUDIODRIVER=dummy run_launcher "$WORK/speaker-decline.log" "plugins: speaker declined to start" \
	|| fail "the launcher exited early after speaker declined"
log="$WORK/speaker-decline.log"
plugin=speaker
expect "open in init 0, declining"
grep -qF "plugins: closed speaker's audio as it declined to start" "$log" || fail "a declined plugin's audio stayed open"
printf 'mode=poweroff\n' >"$STATE/speaker.cfg"
SDL_AUDIODRIVER=dummy run_launcher "$WORK/speaker-poweroff.log" && fail "power_off didn't quit the launcher with speaker"
log="$WORK/speaker-poweroff.log"
expect "open 0, powering off"
grep -qF "plugins: closed speaker's audio before powering off" "$log" || fail "the launcher didn't close speaker's audio before powering off"
rm -rf "$WORK/plugins/speaker.plugin" "$WORK/plugins/hearer.plugin" "$SPEAK_DIR" "$STATE/speaker.cfg" "$STATE/hearer.cfg" \
	"$STATE/plugins.txt"
if [ -f "$WORK/speaker-next.saved" ]; then cp "$WORK/speaker-next.saved" /tmp/next; else rm -f /tmp/next; fi
if [ -f "$WORK/speaker-last.saved" ]; then cp "$WORK/speaker-last.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
rm -f "$WORK/speaker-next.saved" "$WORK/speaker-last.saved"

# row-badges puts an unplayed dot and tier dots after rows' names, which
# dotter, after it in load order, reads back off the screen. Alpha is
# unplayed with tier 3, Bravo has a save and tier 1, Charlie a resume slot,
# Delta a state and Echo is in recents, and Foxtrot is unplayed with a tier
# that isn't one
BADGE_DIR="$CARD/Roms/Row Badges (RBT)"
SHARED="$CARD/.userdata/shared"
mkdir -p "$BADGE_DIR/.media" "$CARD/Saves/RBT" "$SHARED/RBT-test" "$SHARED/.minui/RBT" "$STATE"
for game in Alpha Bravo Charlie Delta Echo Foxtrot; do : >"$BADGE_DIR/$game.gb"; done
printf 'tier=3\n' >"$BADGE_DIR/.media/Alpha.info"
printf 'tier=1\n' >"$BADGE_DIR/.media/Bravo.info"
printf 'tier=high\n' >"$BADGE_DIR/.media/Foxtrot.info"
: >"$CARD/Saves/RBT/Bravo.gb.sav"
printf '0' >"$SHARED/.minui/RBT/Charlie.gb.txt"
: >"$SHARED/RBT-test/Delta.gb.st0"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/recent.txt.saved"
echo "/Roms/Row Badges (RBT)/Echo.gb" >"$RECENTS"
make -s -C "$ROOT/plugins" row-badges ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building row-badges"
build_plugin dotter 'id=dotter\nname=Dotter\napi=1\n'
printf 'folder=Roms/Row Badges (RBT)\n' >"$STATE/dotter.cfg"
printf 'unplayed=on\ntiers=on\n' >"$STATE/row-badges.cfg"
printf 'row-badges=on\ndotter=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/row-badges.log" "plugin dotter: row Foxtrot" || fail "the launcher exited early with row-badges loaded"
log="$WORK/row-badges.log"
plugin=row-badges
expect "unplayed on, tiers on"
plugin=dotter
expect "open folder 1"
expect "row Alpha unplayed 1 tiers 3"
expect "row Bravo unplayed 0 tiers 1"
expect "row Charlie unplayed 0 tiers 0"
expect "row Delta unplayed 0 tiers 0"
expect "row Echo unplayed 0 tiers 0"
expect "row Foxtrot unplayed 1 tiers 0"
grep -qF "plugins: row-badges's" "$log" && fail "row-badges' decorations were turned off"

# both badges are off by default
rm -f "$STATE/row-badges.cfg"
run_launcher "$WORK/row-badges-off.log" "plugin dotter: row Foxtrot" || fail "the launcher exited early with row-badges' defaults"
log="$WORK/row-badges-off.log"
plugin=row-badges
expect "unplayed off, tiers off"
plugin=dotter
expect "row Alpha unplayed 0 tiers 0"
expect "row Foxtrot unplayed 0 tiers 0"

# under a layout, cells with no name get the dots in their top right corner
mkdir -p "$CARD/Gridder/.media"
printf 'tier=2\n' >"$CARD/Gridder/.media/Apple.info"
printf 'unplayed=on\ntiers=on\n' >"$STATE/row-badges.cfg"
printf 'open=0\nfolder=Gridder\n' >"$STATE/dotter.cfg"
build_plugin gridder 'id=gridder\nname=Gridder\napi=1\nslots=layout\n'
printf 'gridder=on\nrow-badges=on\ndotter=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/row-badges-grid.log" "plugin gridder: renders" "plugin dotter: row Banana" || fail "the launcher exited early with row-badges under gridder"
log="$WORK/row-badges-grid.log"
plugin=dotter
expect "row Apple unplayed 0 tiers 2"
expect "row Banana unplayed 0 tiers 0"
grep -qF "plugins: row-badges's" "$log" && fail "row-badges' decorations were turned off under gridder"

# under the bundled layout-grid, which names each cell, the dots go after
# the cell's name
rm -rf "$WORK/plugins/gridder.plugin" "$CARD/Gridder"
make -s -C "$ROOT/plugins" layout-grid ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building layout-grid"
printf 'folder=Roms/Row Badges (RBT)\n' >"$STATE/dotter.cfg"
printf 'layout-grid=on\nrow-badges=on\ndotter=on\nslot.layout=layout-grid\n' >"$STATE/plugins.txt"
run_launcher "$WORK/row-badges-layout-grid.log" "plugin dotter: row Foxtrot" || fail "the launcher exited early with row-badges under layout-grid"
log="$WORK/row-badges-layout-grid.log"
plugin=dotter
expect "row Alpha unplayed 1 tiers 3"
expect "row Bravo unplayed 0 tiers 1"
expect "row Charlie unplayed 0 tiers 0"
expect "row Foxtrot unplayed 1 tiers 0"
grep -qF "plugins: row-badges's" "$log" && fail "row-badges' decorations were turned off under layout-grid"
grep -qF "plugins: layout-grid's" "$log" && fail "layout-grid was turned off with row-badges"
rm -rf "$WORK/plugins/dotter.plugin" "$WORK/plugins/row-badges.plugin" "$WORK/plugins/layout-grid.plugin" \
	"$BADGE_DIR" "$CARD/Saves/RBT" "$SHARED/RBT-test" "$SHARED/.minui/RBT" \
	"$STATE/dotter.cfg" "$STATE/row-badges.cfg" "$STATE/plugins.txt"
if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi

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

# the bundled layout-grid draws peeker's folder as a grid: Apple's
# thumbnail from .media fills its cell's art, scaled from 8x8, and banana
# and Cherry, with no art, get letter tiles in the theme's accent colour
# with their letter across the middle; peeker reads each cell back as a
# row decorator. Its settings set the columns and rows, and with
# art-boxart on, Cherry's box stands in for a thumbnail.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" layout-grid art-boxart >/dev/null || fail "building layout-grid"
build_plugin peeker 'id=peeker\nname=Peeker\napi=1\n'
PEEK_DIR="$CARD/Peeker"
mkdir -p "$PEEK_DIR/.media" "$STATE"
cp "$ROOT/tests/plugins/peeker/art.png" "$PEEK_DIR/.media/Apple.png"
printf 'layout-grid=on\npeeker=on\nslot.layout=layout-grid\n' >"$STATE/plugins.txt"
run_launcher "$WORK/layout-grid.log" "plugin peeker: cell Apple middle 00FF00" "plugin peeker: cell banana" \
	"plugin peeker: cell Cherry" || fail "the launcher exited early with layout-grid loaded"
log="$WORK/layout-grid.log"
plugin=peeker
grep -qF "plugins: layout-grid draws the game list" "$log" || fail "layout-grid didn't take the game list"
# 4 columns at 1024 wide; the rows depend on whether the status pill shows
grep -qE "plugin layout-grid: grid of 4x[23] cells, 250 by [0-9]+$" "$log" || fail "layout-grid's grid is wrong: $(grep "plugin layout-grid:" "$log")"
expect "open folder 1"
expect "cell Apple middle 00FF00 top 00FF00, accent 0, ink 0, named 1"
grep -qE "plugin peeker: cell banana middle [0-9A-F]{6} top [0-9A-F]{6}, accent 1, ink 1, named 1$" "$log" \
	|| fail "banana has no letter tile: $(grep "plugin peeker: cell banana" "$log")"
grep -qE "plugin peeker: cell Cherry middle [0-9A-F]{6} top [0-9A-F]{6}, accent 1, ink 1, named 1$" "$log" \
	|| fail "Cherry has no letter tile: $(grep "plugin peeker: cell Cherry" "$log")"
grep -qF "plugins: layout-grid's" "$log" && fail "layout-grid's layout was turned off"
grep -qF "plugins: peeker's" "$log" && fail "peeker's decorator was turned off"

printf 'columns=3\nrows=1\n' >"$STATE/layout-grid.cfg"
mkdir -p "$PEEK_DIR/.media/boxart2d"
cp "$ROOT/tests/plugins/peeker/art.png" "$PEEK_DIR/.media/boxart2d/Cherry.png"
printf 'art-boxart=on\nlayout-grid=on\npeeker=on\nslot.layout=layout-grid\n' >"$STATE/plugins.txt"
run_launcher "$WORK/layout-grid-set.log" "plugin peeker: cell Apple middle 00FF00" "plugin peeker: cell Cherry middle 00FF00" \
	|| fail "the launcher exited early with layout-grid set to 3 columns"
log="$WORK/layout-grid-set.log"
grep -qF "plugin layout-grid: grid of 3x1 cells" "$log" || fail "layout-grid's settings weren't used: $(grep "plugin layout-grid:" "$log")"
# the tiles are taller than the art now, so only their middles are art
expect "cell Apple middle 00FF00 top"
expect "cell Cherry middle 00FF00 top"
rm -rf "$WORK/plugins/layout-grid.plugin" "$WORK/plugins/art-boxart.plugin" "$WORK/plugins/peeker.plugin" "$PEEK_DIR" "$STATE/layout-grid.cfg" "$STATE/plugins.txt"

# the bundled layout-carousel draws peeker's folder as a carousel: Apple,
# selected, in the middle with its thumbnail from .media filling its cover
# and its name under it, and banana and Cherry to its right on letter cards
# in the theme's accent colour; peeker reads each cover back as a row
# decorator. With art-boxart on, a box comes before a thumbnail: Apple's
# red and blue box stands in for its green thumbnail, letterboxed, and
# Cherry gets its green box.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" layout-carousel art-boxart >/dev/null || fail "building layout-carousel"
build_plugin peeker 'id=peeker\nname=Peeker\napi=1\n'
PEEK_DIR="$CARD/Peeker"
mkdir -p "$PEEK_DIR/.media" "$STATE"
cp "$ROOT/tests/plugins/peeker/art.png" "$PEEK_DIR/.media/Apple.png"
printf 'layout-carousel=on\npeeker=on\nslot.layout=layout-carousel\n' >"$STATE/plugins.txt"
run_launcher "$WORK/layout-carousel.log" "plugin peeker: cell Apple middle 00FF00" "plugin peeker: cell banana" \
	"plugin peeker: cell Cherry" || fail "the launcher exited early with layout-carousel loaded"
log="$WORK/layout-carousel.log"
plugin=peeker
grep -qF "plugins: layout-carousel draws the game list" "$log" || fail "layout-carousel didn't take the game list"
# 3/10 of the 1000 wide room, and two either side
grep -qF "plugin layout-carousel: strip of 5 covers, 300 by 300" "$log" || fail "layout-carousel's strip is wrong: $(grep "plugin layout-carousel:" "$log")"
expect "open folder 1"
expect "cell Apple middle 00FF00 top 00FF00, accent 0, ink 0, named 1"
grep -qE "plugin peeker: cell banana middle [0-9A-F]{6} top [0-9A-F]{6}, accent 1, ink 1, named 0$" "$log" \
	|| fail "banana has no letter card: $(grep "plugin peeker: cell banana" "$log")"
grep -qE "plugin peeker: cell Cherry middle [0-9A-F]{6} top [0-9A-F]{6}, accent 1, ink 1, named 0$" "$log" \
	|| fail "Cherry has no letter card: $(grep "plugin peeker: cell Cherry" "$log")"
grep -qF "plugins: layout-carousel's" "$log" && fail "layout-carousel's layout was turned off"
grep -qF "plugins: peeker's" "$log" && fail "peeker's decorator was turned off"

printf 'slide=off\n' >"$STATE/layout-carousel.cfg"
mkdir -p "$PEEK_DIR/.media/boxart2d"
cp "$ROOT/tests/plugins/drawer/art.png" "$PEEK_DIR/.media/boxart2d/Apple.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$PEEK_DIR/.media/boxart2d/Cherry.png"
printf 'art-boxart=on\nlayout-carousel=on\npeeker=on\nslot.layout=layout-carousel\n' >"$STATE/plugins.txt"
run_launcher "$WORK/layout-carousel-box.log" "plugin peeker: cell Cherry middle 00FF00" \
	|| fail "the launcher exited early with layout-carousel and art-boxart"
log="$WORK/layout-carousel-box.log"
expect "cell Cherry middle 00FF00 top 00FF00, accent 0, ink 0, named 0"
apple=$(grep "plugin peeker: cell Apple" "$log" | tail -n 1)
case "$apple" in
*"cell Apple middle 00FF00"* | *"accent 1"* | "") fail "Apple's box didn't stand in for its thumbnail: $apple" ;;
esac
rm -rf "$WORK/plugins/peeker.plugin" "$PEEK_DIR" "$STATE/layout-carousel.cfg"

# under layout-carousel, hider's selected Apple, whose name is hidden,
# shows its art without its name, and with no art keeps its name
HIDE_DIR="$CARD/Hider"
mkdir -p "$HIDE_DIR"
cp "$ROOT/tests/plugins/drawer/art.png" "$HIDE_DIR/art.png"
build_plugin hider 'id=hider\nname=Hider\napi=1\nslots=transition\n'
printf 'hider=on\nlayout-carousel=on\nslot.layout=layout-carousel\n' >"$STATE/plugins.txt"
run_launcher "$WORK/layout-carousel-hider.log" "plugin hider: done" || fail "the launcher exited early with hider under layout-carousel"
log="$WORK/layout-carousel-hider.log"
plugin=hider
grep -qE "plugin hider: row Apple selected 1 hidden 1 named 0 " "$log" || fail "Apple's hidden name was drawn: $(grep "plugin hider: row Apple" "$log")"
printf 'art=none\n' >"$STATE/hider.cfg"
run_launcher "$WORK/layout-carousel-hider-noart.log" "plugin hider: done" || fail "the launcher exited early with hider's art off under layout-carousel"
log="$WORK/layout-carousel-hider-noart.log"
grep -qE "plugin hider: row Apple selected 1 hidden 1 named 1 " "$log" || fail "Apple lost its name with no art: $(grep "plugin hider: row Apple" "$log")"
grep -qF "plugins: layout-carousel's" "$log" && fail "layout-carousel's layout was turned off"
rm -rf "$WORK/plugins/layout-carousel.plugin" "$WORK/plugins/art-boxart.plugin" "$WORK/plugins/hider.plugin" "$HIDE_DIR" \
	"$STATE/hider.cfg" "$STATE/plugins.txt"

# the bundled layout-shelf draws peeker's folder as GB cartridges on
# shelves, as its games are .gb: Apple's thumbnail from .media fills its
# label, and banana and Cherry, with no art, get letter labels; peeker
# reads each cartridge back as a row decorator, from the top of its grey
# body. With art-boxart on, Cherry's box stands in for a thumbnail, and
# its settings set three shelves and no lift animation.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" layout-shelf art-boxart >/dev/null || fail "building layout-shelf"
build_plugin peeker 'id=peeker\nname=Peeker\napi=1\n'
PEEK_DIR="$CARD/Peeker"
mkdir -p "$PEEK_DIR/.media" "$STATE"
cp "$ROOT/tests/plugins/peeker/art.png" "$PEEK_DIR/.media/Apple.png"
printf 'layout-shelf=on\npeeker=on\nslot.layout=layout-shelf\n' >"$STATE/plugins.txt"
run_launcher "$WORK/layout-shelf.log" "plugin peeker: cell Apple middle 00FF00" "plugin peeker: cell banana" \
	"plugin peeker: cell Cherry" || fail "the launcher exited early with layout-shelf loaded"
log="$WORK/layout-shelf.log"
plugin=peeker
grep -qF "plugins: layout-shelf draws the game list" "$log" || fail "layout-shelf didn't take the game list"
grep -qE "plugin layout-shelf: 2 shelves of [0-9]+ GB cartridges, [0-9]+ by [0-9]+$" "$log" \
	|| fail "layout-shelf's shelves are wrong: $(grep "plugin layout-shelf:" "$log")"
expect "open folder 1"
expect "cell Apple middle 00FF00 top B4B4BC, accent 0, ink 1, named 1"
grep -qE "plugin peeker: cell banana middle [0-9A-F]{6} top B4B4BC, accent 0, ink 1, named 1$" "$log" \
	|| fail "banana has no letter label: $(grep "plugin peeker: cell banana" "$log")"
grep -qF "plugin peeker: cell banana middle 00FF00" "$log" && fail "banana has Apple's art"
grep -qE "plugin peeker: cell Cherry middle [0-9A-F]{6} top B4B4BC, accent 0, ink 1, named 1$" "$log" \
	|| fail "Cherry has no letter label: $(grep "plugin peeker: cell Cherry" "$log")"
grep -qF "plugins: layout-shelf's" "$log" && fail "layout-shelf's layout was turned off"
grep -qF "plugins: peeker's" "$log" && fail "peeker's decorator was turned off"

printf 'shelves=3\nanimate=off\n' >"$STATE/layout-shelf.cfg"
mkdir -p "$PEEK_DIR/.media/boxart2d"
cp "$ROOT/tests/plugins/peeker/art.png" "$PEEK_DIR/.media/boxart2d/Cherry.png"
printf 'art-boxart=on\nlayout-shelf=on\npeeker=on\nslot.layout=layout-shelf\n' >"$STATE/plugins.txt"
run_launcher "$WORK/layout-shelf-set.log" "plugin peeker: cell Apple middle 00FF00" "plugin peeker: cell Cherry middle 00FF00" \
	|| fail "the launcher exited early with layout-shelf set to three shelves"
log="$WORK/layout-shelf-set.log"
grep -qE "plugin layout-shelf: 3 shelves of [0-9]+ GB cartridges" "$log" || fail "layout-shelf's settings weren't used: $(grep "plugin layout-shelf:" "$log")"
expect "cell Cherry middle 00FF00 top B4B4BC, accent 0, ink 1, named 1"
rm -rf "$WORK/plugins/peeker.plugin" "$PEEK_DIR" "$STATE/layout-shelf.cfg"

# under layout-shelf, hider's selected Apple, whose name is hidden, shows
# its art without its name tag, and with no art keeps it
HIDE_DIR="$CARD/Hider"
mkdir -p "$HIDE_DIR"
cp "$ROOT/tests/plugins/drawer/art.png" "$HIDE_DIR/art.png"
build_plugin hider 'id=hider\nname=Hider\napi=1\nslots=transition\n'
printf 'hider=on\nlayout-shelf=on\nslot.layout=layout-shelf\n' >"$STATE/plugins.txt"
run_launcher "$WORK/layout-shelf-hider.log" "plugin hider: done" || fail "the launcher exited early with hider under layout-shelf"
log="$WORK/layout-shelf-hider.log"
plugin=hider
grep -qE "plugin hider: row Apple selected 1 hidden 1 named 0 " "$log" || fail "Apple's hidden name was drawn: $(grep "plugin hider: row Apple" "$log")"
grep -qE "plugin hider: row Banana selected 0 hidden 0 named 1 " "$log" || fail "Banana lost its name: $(grep "plugin hider: row Banana" "$log")"
printf 'art=none\n' >"$STATE/hider.cfg"
run_launcher "$WORK/layout-shelf-hider-noart.log" "plugin hider: done" || fail "the launcher exited early with hider's art off under layout-shelf"
log="$WORK/layout-shelf-hider-noart.log"
grep -qE "plugin hider: row Apple selected 1 hidden 1 named 1 " "$log" || fail "Apple lost its name with no art: $(grep "plugin hider: row Apple" "$log")"
grep -qF "plugins: layout-shelf's" "$log" && fail "layout-shelf's layout was turned off"
rm -rf "$WORK/plugins/layout-shelf.plugin" "$WORK/plugins/art-boxart.plugin" "$WORK/plugins/hider.plugin" "$HIDE_DIR" \
	"$STATE/hider.cfg" "$STATE/plugins.txt"

# the bundled layout-fancy draws glancer's folder as stock's rows beside a
# panel, which glancer reads down a column on each selected row: Alpha has
# a green thumbnail and a blue preview of its last state (slot 1), which
# stands in for the thumbnail; Beta has only its thumbnail; Gamma's last
# slot has no preview and it has no art, so its panel is empty. With
# prefer_preview off, Alpha's thumbnail goes on top and its preview under it.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" layout-fancy >/dev/null || fail "building layout-fancy"
build_plugin glancer 'id=glancer\nname=Glancer\napi=1\n'
GLANCE_DIR="$CARD/Roms/Glancer (GLANCE)"
GLANCE_STATES="$CARD/.userdata/shared/.minui/GLANCE"
mkdir -p "$GLANCE_DIR/.media" "$GLANCE_STATES" "$STATE"
cp "$ROOT/tests/plugins/peeker/art.png" "$GLANCE_DIR/.media/Alpha.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$GLANCE_DIR/.media/Beta.png"
printf '1' >"$GLANCE_STATES/Alpha.gb.txt"
cp "$ROOT/tests/plugins/glancer/state.bmp" "$GLANCE_STATES/Alpha.gb.1.bmp"
cp "$ROOT/tests/plugins/glancer/state.bmp" "$GLANCE_STATES/Alpha.gb.0.bmp"
printf '3\n' >"$GLANCE_STATES/Gamma.gb.txt"
cp "$ROOT/tests/plugins/glancer/state.bmp" "$GLANCE_STATES/Gamma.gb.0.bmp"
printf 'layout-fancy=on\nglancer=on\nslot.layout=layout-fancy\n' >"$STATE/plugins.txt"
run_launcher "$WORK/layout-fancy.log" "plugin glancer: done" || fail "the launcher exited early with layout-fancy loaded"
log="$WORK/layout-fancy.log"
plugin=glancer
grep -qF "plugins: layout-fancy draws the game list" "$log" || fail "layout-fancy didn't take the game list"
grep -qF "plugin layout-fancy: rows 443 wide (443 at the top), panel 519," "$log" || fail "layout-fancy's layout is wrong: $(grep "plugin layout-fancy:" "$log")"
expect "open folder 1"
alpha=$(grep "plugin glancer: panel Alpha" "$log" | tail -n 1)
echo "$alpha" | grep -qE "panel Alpha runs 0000FF:[0-9]+-[0-9]+$" || fail "Alpha's state preview didn't stand in for its thumbnail: $alpha"
beta=$(grep "plugin glancer: panel Beta" "$log" | tail -n 1)
echo "$beta" | grep -qE "panel Beta runs 00FF00:[0-9]+-[0-9]+$" || fail "Beta's thumbnail isn't in the panel: $beta"
expect "panel Gamma runs none"
grep -qF "plugins: layout-fancy's" "$log" && fail "layout-fancy's layout was turned off"
grep -qF "plugins: glancer's" "$log" && fail "glancer's decorator was turned off"

printf 'prefer_preview=off\n' >"$STATE/layout-fancy.cfg"
run_launcher "$WORK/layout-fancy-both.log" "plugin glancer: done" || fail "the launcher exited early with layout-fancy's prefer_preview off"
log="$WORK/layout-fancy-both.log"
alpha=$(grep "plugin glancer: panel Alpha" "$log" | tail -n 1)
echo "$alpha" | grep -qE "panel Alpha runs 00FF00:[0-9]+-[0-9]+ 0000FF:[0-9]+-[0-9]+$" || fail "Alpha's thumbnail and preview aren't both in the panel: $alpha"
expect "panel Gamma runs none"
rm -rf "$WORK/plugins/layout-fancy.plugin" "$WORK/plugins/glancer.plugin" "$GLANCE_DIR" "$GLANCE_STATES" \
	"$STATE/layout-fancy.cfg" "$STATE/plugins.txt"

# the bundled transitions plugin draws wiper's push from Wiper's red
# background into Inner's blue one, and the pop back out when wiper
# presses B: wiper owns the transition slot and hands each transition to
# transitions, loaded without the slot, reading the screen back after each
# frame at its left, middle and right. Slide, the default, moves both
# frames left on the push and right on the pop; fade blends the whole
# screen; slide and fade blends all but the left edge; off and stock draw
# no frames.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" transitions >/dev/null || fail "building transitions"
TRANSITIONS_LIB="$WORK/plugins/transitions.plugin/lib/$ARCH/plugin.so"
sed '/^slots=/d' "$ROOT/plugins/transitions/plugin.txt" >"$WORK/plugins/transitions.plugin/plugin.txt"
build_plugin wiper 'id=wiper\nname=Wiper\napi=1\nslots=background,transition\n'
mkdir -p "$STATE"
printf 'transitions=on\nwiper=on\n' >"$STATE/plugins.txt"
printf 'lib=%s\n' "$TRANSITIONS_LIB" >"$STATE/wiper.cfg"
red=FF0000FF
blue=0000FFFF
any='[0-9A-F]{8}'
blend='[4-B][0-9A-F]00[4-B][0-9A-F]FF'
for style in default fade slide_fade off stock; do
	case "$style" in
	default) rm -f "$STATE/transitions.cfg" ;;
	# slow, so a frame comes near progress 0.5 on a slow machine too
	*) printf 'style=%s\nspeed=slow\nease=linear\n' "$style" >"$STATE/transitions.cfg" ;;
	esac
	run_launcher "$WORK/transitions-$style.log" "plugin wiper: done at" || fail "the launcher exited early with transitions' $style style"
	log="$WORK/transitions-$style.log"
	plugin=wiper
	expect "done at $CARD/Wiper"
	grep -qF "plugin wiper: done at $CARD/Wiper/Inner" "$log" && fail "B didn't back out of Inner"
	grep -qF "plugin wiper: no transitions plugin" "$log" && fail "wiper couldn't find the transitions plugin"
	grep -qF "transitions until the launcher restarts" "$log" && fail "transitions' $style style was turned off"
	expect "frames push to Inner old $red $red $red new $blue $blue $blue"
	expect "frames pop to Wiper old $blue $blue $blue new $red $red $red"
	case "$style" in
	default)
		grep -qF "plugin transitions: style slide, 250 ms, ease out" "$log" || fail "transitions' defaults are wrong: $(grep "plugin transitions:" "$log")"
		expect "begin push to Inner: style 2 duration 250 ease 2"
		expect "begin pop to Wiper: style 2 duration 250 ease 2"
		push_mid="$red $blue $blue"
		# the middle is on the seam at exactly 0.5
		pop_mid="$red $any $blue"
		;;
	off | stock)
		grep -qF "plugin transitions: style $style, 500 ms, ease linear" "$log" || fail "transitions' settings weren't used: $(grep "plugin transitions:" "$log")"
		want=1
		[ "$style" = stock ] && want=0
		for kind in "push to Inner" "pop to Wiper"; do
			grep -qE "plugin wiper: begin $kind: style $want duration [0-9]+ ease 2$" "$log" \
				|| fail "transitions' $style style didn't answer $want: $(grep "plugin wiper: begin $kind" "$log")"
		done
		grep -qF "plugin wiper: first " "$log" && fail "transitions drew frames in its $style style"
		continue
		;;
	*)
		grep -qF "plugin transitions: style $style, 500 ms, ease linear" "$log" || fail "transitions' settings weren't used: $(grep "plugin transitions:" "$log")"
		expect "begin push to Inner: style 2 duration 500 ease 0"
		expect "begin pop to Wiper: style 2 duration 500 ease 0"
		push_mid="$blend $blend $blend"
		[ "$style" = slide_fade ] && push_mid="$red $blend $blend"
		# the old blue fading out over the new red, the left edge red too
		pop_mid=$push_mid
		;;
	esac
	expect "first push to Inner progress 0.000: $red $red $red"
	expect "first pop to Wiper progress 0.000: $blue $blue $blue"
	grep -qE "plugin wiper: mid push to Inner progress 0\.[0-9]+: $push_mid$" "$log" \
		|| fail "transitions' $style push is wrong halfway: $(grep "plugin wiper: mid push to Inner" "$log")"
	grep -qE "plugin wiper: mid pop to Wiper progress 0\.[0-9]+: $pop_mid$" "$log" \
		|| fail "transitions' $style pop is wrong halfway: $(grep "plugin wiper: mid pop to Wiper" "$log")"
	grep -qE "plugin wiper: last push to Inner progress 1\.000 frames [0-9]+: $blue $blue $blue$" "$log" \
		|| fail "transitions' $style push didn't end on the new frame: $(grep "plugin wiper: last push to Inner" "$log")"
	grep -qE "plugin wiper: last pop to Wiper progress 1\.000 frames [0-9]+: $red $red $red$" "$log" \
		|| fail "transitions' $style pop didn't end on the new frame: $(grep "plugin wiper: last pop to Wiper" "$log")"
done

# owning the slot itself, it draws the push and pop with no errors
cp "$ROOT/plugins/transitions/plugin.txt" "$WORK/plugins/transitions.plugin/plugin.txt"
printf 'id=wiper\nname=Wiper\napi=1\nslots=background\n' >"$WORK/plugins/wiper.plugin/plugin.txt"
rm -f "$STATE/transitions.cfg"
run_launcher "$WORK/transitions-owner.log" "plugin wiper: done at" || fail "the launcher exited early with transitions owning the slot"
log="$WORK/transitions-owner.log"
grep -qF "plugins: transitions picks the transition styles" "$log" || fail "transitions didn't take the transition slot"
grep -qF "plugin wiper: begin" "$log" && fail "wiper was asked without the slot"
grep -qF "transitions until the launcher restarts" "$log" && fail "transitions' transitions were turned off"
expect "done at $CARD/Wiper"
rm -rf "$WORK/plugins/transitions.plugin" "$WORK/plugins/wiper.plugin" "$STATE/transitions.cfg" "$STATE/wiper.cfg" \
	"$STATE/plugins.txt"

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

# tracker reads the play history from a fixture game_logs.sqlite made with
# gametimectl's schema: per game totals, orphans by name, today's total,
# one read however often it asks, answers kept with the file moved away,
# and a fresh read after a game returns. Local time is set to about noon,
# so the sessions an hour ago are today's.
PLAY_DB="$CARD/.userdata/shared/game_logs.sqlite"
PLAY_ROMS="$CARD/Roms/Tracker"
mkdir -p "$(dirname "$PLAY_DB")" "$STATE"
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$WORK/game_logs.sqlite.saved"
# make_play_db: writes the SQL on stdin into a new $PLAY_DB
make_play_db() {
	rm -f "$PLAY_DB"
	if command -v sqlite3 >/dev/null; then
		sqlite3 "$PLAY_DB"
	else
		python3 -I -c 'import sqlite3, sys; db = sqlite3.connect(sys.argv[1]); db.executescript(sys.stdin.read()); db.commit()' "$PLAY_DB"
	fi
}
now=$(date +%s)
hours_behind=$(( $(date -u +%H | sed 's/^0//') - 12 ))
make_play_db <<SQL || fail "making the play history fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Tetris', 'Tracker/Tetris.gb');
INSERT INTO rom(id, type, name, file_path) VALUES(2, 'ORPHAN', 'Metroid', NULL);
INSERT INTO rom(id, type, name, file_path) VALUES(3, '', 'Unplayed', 'Tracker/Unplayed.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 1000, $((now - 3 * 86400)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 600, $((now - 3600)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(2, 50, $((now - 10 * 86400)));
SQL
printf 'game1=%s\ngame2=%s\ngame3=%s\ngame4=%s\ndb=%s\n' "$PLAY_ROMS/Tetris.gb" "$PLAY_ROMS/Metroid.nes" \
	"$PLAY_ROMS/Unplayed.gb" "$CARD/Tools/desktop/Files.pak" "$PLAY_DB" >"$STATE/tracker.cfg"
printf 'tracker=on\n' >"$STATE/plugins.txt"
build_plugin tracker 'id=tracker\nname=Tracker\napi=1\n'
TZ="TST$(printf '%+d' "$hours_behind")"
export TZ
run_launcher "$WORK/tracker.log" "plugin tracker: done" || fail "the launcher exited early with tracker loaded"
log="$WORK/tracker.log"
plugin=tracker
expect "game Tetris.gb time 1600 last $((now - 3600)) count 2"
expect "game Metroid.nes time 50 last $((now - 10 * 86400)) count 1"
expect "game Unplayed.gb time 0 last 0 count 0"
expect "game Files.pak time 0 last 0 count 0"
expect "game today 600"
expect "null -1 -1 -1"
expect "kept Tetris.gb time 1600 last $((now - 3600)) count 2"
expect "kept today 600"
[ "$(grep -c "play history: read " "$log")" -eq 1 ] || fail "the play history wasn't read just once: $(grep "play history:" "$log")"
grep -qF "play history: read 3 games and 1 session since midnight from $PLAY_DB" "$log" || fail "the read wasn't logged"
# read only once tracker asked
asked=$(grep -n "plugin tracker: asking" "$log" | cut -d: -f1)
read_at=$(grep -n "play history: read " "$log" | cut -d: -f1)
if [ -z "$asked" ] || [ -z "$read_at" ] || [ "$read_at" -le "$asked" ]; then
	fail "the play history was read before tracker asked"
fi
[ -f "$PLAY_DB" ] || fail "tracker didn't put the database back"

# after a game returns: post_return gets a fresh read, and so does the
# next ask after the launcher's stop_all
touch /tmp/mosaic-launcher-launched
run_launcher "$WORK/tracker-return.log" "plugin tracker: done" || fail "the launcher exited early with tracker after a launch"
log="$WORK/tracker-return.log"
expect "post_return today 600"
expect "game Tetris.gb time 1600 last $((now - 3600)) count 2"
[ "$(grep -c "play history: read " "$log")" -eq 2 ] || fail "post_return and the next ask didn't both read: $(grep "play history:" "$log")"
rm -f /tmp/mosaic-launcher-launched

# without sqlite: unknown, logged once
MOSAIC_LAUNCHER_SQLITE="$WORK/no-such-libsqlite3"
export MOSAIC_LAUNCHER_SQLITE
run_launcher "$WORK/tracker-nolib.log" "plugin tracker: done" || fail "the launcher exited early with tracker and no sqlite"
unset MOSAIC_LAUNCHER_SQLITE
log="$WORK/tracker-nolib.log"
expect "game Tetris.gb time -1 last -1 count -1"
expect "game today -1"
[ "$(grep -c "play history: unknown, no sqlite library" "$log")" -eq 1 ] || fail "the missing library wasn't logged once"

# without the database: unknown, logged once
rm -f "$PLAY_DB"
run_launcher "$WORK/tracker-nodb.log" "plugin tracker: done" || fail "the launcher exited early with tracker and no database"
log="$WORK/tracker-nodb.log"
expect "game Tetris.gb time -1 last -1 count -1"
expect "kept today -1"
[ "$(grep -c "play history: unknown, no $PLAY_DB" "$log")" -eq 1 ] || fail "the missing database wasn't logged once"
[ -e "$PLAY_DB" ] && fail "the launcher made a database"
unset TZ
[ -f "$WORK/game_logs.sqlite.saved" ] && mv "$WORK/game_logs.sqlite.saved" "$PLAY_DB"
rm -rf "$WORK/plugins/tracker.plugin" "$STATE/tracker.cfg" "$STATE/plugins.txt"

# achiever reads local RetroAchievements progress from fixture .ra files
# in minarch's format: Hit has a cached set of 3 (unlocks 1, and 2 in
# hardcore; 2 muted), Bare none, the A5200 game can't be hashed and
# Missing isn't there. Every answer is unknown at first, the hashes land in
# the launcher's cache, and a restart reads them back instead of hashing.
# Nothing is hashed, and no thread starts, when no plugin asks.
RA_DIR="$CARD/.userdata/shared/.ra"
RA_HASHES="$CARD/.userdata/shared/launcher/ra-hashes.txt"
RA_ROMS="$CARD/Roms/RA Test (GB)"
RA_5200="$CARD/Roms/RA Test 5200 (A5200)"
grep -qF "retroachievements:" "$WORK/tracker.log" && fail "the launcher read RetroAchievements progress with no plugin asking"
[ -e "$RA_HASHES" ] && fail "the launcher made $RA_HASHES with no plugin asking"
[ -d "$RA_DIR" ] && mv "$RA_DIR" "$WORK/ra.saved"
mkdir -p "$RA_ROMS" "$RA_5200" "$RA_DIR/offline/cache" "$RA_DIR/muted" "$STATE"
# md5_of <file>
md5_of() {
	if command -v md5sum >/dev/null; then md5sum "$1" | cut -d' ' -f1; else md5 -q "$1"; fi
}
# frame_json <json> <file>: minarch's [u32 length][json][sha256 of json]
frame_json() {
	len=$(printf '%s' "$1" | wc -c | tr -d ' ')
	# shellcheck disable=SC2059 # the format is the length's four bytes
	{
		printf "$(printf '\\%03o\\%03o\\%03o\\%03o' $((len & 255)) $((len >> 8 & 255)) $((len >> 16 & 255)) $((len >> 24 & 255)))"
		printf '%s' "$1"
		printf '%s' "$1" | openssl dgst -sha256 -binary
	} >"$2"
}
printf 'mosaic retroachievements test cart %.0s' 1 2 3 4 5 6 7 8 >"$RA_ROMS/Hit.gb"
printf 'another cart' >"$RA_ROMS/Bare.gb"
printf 'a 5200 cart' >"$RA_5200/Game.a52"
HIT_HASH=$(md5_of "$RA_ROMS/Hit.gb")
BARE_HASH=$(md5_of "$RA_ROMS/Bare.gb")
frame_json '{"Success":true,"GameId":99,"Sets":[{"Type":"core","Achievements":[{"ID":1,"Flags":3,"Points":5},{"ID":2,"Flags":3,"Points":10},{"ID":3,"Flags":3,"Points":20},{"ID":4,"Flags":5,"Points":50}]}]}' \
	"$RA_DIR/offline/cache/achievementsets_$HIT_HASH.bin"
frame_json '{"Success":true,"HardcoreUnlocks":[{"ID":2,"When":1}],"Unlocks":[{"ID":1,"When":2}],"ServerNow":3}' \
	"$RA_DIR/offline/cache/startsession_$HIT_HASH.bin"
printf '2\n' >"$RA_DIR/muted/$HIT_HASH.txt"
printf 'game1=%s\ngame2=%s\ngame3=%s\ngame4=%s\n' "$RA_ROMS/Hit.gb" "$RA_ROMS/Bare.gb" "$RA_5200/Game.a52" \
	"$RA_ROMS/Missing.gb" >"$STATE/achiever.cfg"
printf 'achiever=on\n' >"$STATE/plugins.txt"
build_plugin achiever 'id=achiever\nname=Achiever\napi=1\n'
run_launcher "$WORK/achiever.log" "plugin achiever: done" || fail "the launcher exited early with achiever loaded"
log="$WORK/achiever.log"
plugin=achiever
expect "first Hit.gb -1"
expect "first Missing.gb -1"
expect "game Hit.gb rc 1 total 3 unlocked 2 hardcore 1 points 15/35 muted 1 id 99 hash 1 $HIT_HASH"
expect "game Bare.gb rc 0 total 0 unlocked 0 hardcore 0 points 0/0 muted 0 id 0 hash 1 $BARE_HASH"
expect "game Game.a52 rc 0 total 0 unlocked 0 hardcore 0 points 0/0 muted 0 id 0 hash 0 -"
expect "game Missing.gb rc 0 total 0 unlocked 0 hardcore 0 points 0/0 muted 0 id 0 hash 0 -"
expect "null -1"
expect "short rc 1 total 3 rest untouched 1"
grep -qF "retroachievements: Roms/RA Test (GB)/Hit.gb is $HIT_HASH, hashed as console 4" "$log" || fail "Hit.gb's hash wasn't logged"
[ "$(grep -c ", hashed as console" "$log")" -eq 3 ] || fail "the games weren't hashed once each: $(grep "retroachievements:" "$log")"
grep -qF "$HIT_HASH	$(wc -c <"$RA_ROMS/Hit.gb" | tr -d ' ')	" "$RA_HASHES" || fail "Hit.gb's hash isn't in $RA_HASHES"
grep -qF "Roms/RA Test 5200 (A5200)/Game.a52" "$RA_HASHES" || fail "the unhashable game isn't in $RA_HASHES"
# a restart reads the hashes back
run_launcher "$WORK/achiever-cached.log" "plugin achiever: done" || fail "the launcher exited early with achiever after a restart"
log="$WORK/achiever-cached.log"
expect "game Hit.gb rc 1 total 3 unlocked 2 hardcore 1 points 15/35 muted 1 id 99 hash 1 $HIT_HASH"
grep -qF ", hashed as console" "$log" && fail "games were hashed again: $(grep "retroachievements:" "$log")"
# a damaged cache file says nothing
printf 'x' >>"$RA_DIR/offline/cache/achievementsets_$HIT_HASH.bin"
run_launcher "$WORK/achiever-damaged.log" "plugin achiever: done" || fail "the launcher exited early with a damaged cache file"
log="$WORK/achiever-damaged.log"
expect "game Hit.gb rc 0 total 0 unlocked 0 hardcore 0 points 0/0 muted 0 id 0 hash 1 $HIT_HASH"
grep -qF "retroachievements: ignoring" "$log" || fail "the damaged cache file wasn't logged"
rm -rf "$WORK/plugins/achiever.plugin" "$STATE/achiever.cfg" "$STATE/plugins.txt" "$RA_ROMS" "$RA_5200" "$RA_DIR" "$RA_HASHES"

# playtime-captions puts a caption at the right of played games' rows, which
# subtitler, after it in load order, reads back off the screen: Tetris was
# played 2h 15m over 2 sessions 3 days ago, Q 2h 5m an hour ago,
# Unplayed never, and the long name leaves no room. Local time is about
# noon, as for tracker.
CAPTION_DIR="$CARD/Roms/Play Captions (PCT)"
LONG_NAME="The Legend of a Game Whose Name Is Far Too Long for Any Row on the Screen"
mkdir -p "$CAPTION_DIR/Extras" "$CAPTION_DIR/.media" "$(dirname "$PLAY_DB")" "$STATE"
for game in Tetris Q Unplayed "$LONG_NAME"; do : >"$CAPTION_DIR/$game.gb"; done
: >"$CAPTION_DIR/Extras/Bonus.gb"
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$WORK/game_logs.sqlite.saved"
now=$(date +%s)
hours_behind=$(( $(date -u +%H | sed 's/^0//') - 12 ))
make_play_db <<SQL || fail "making the play captions fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Tetris', 'Play Captions (PCT)/Tetris.gb');
INSERT INTO rom(id, type, name, file_path) VALUES(2, '', 'Long', 'Play Captions (PCT)/$LONG_NAME.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 7500, $((now - 10 * 86400)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 600, $((now - 3 * 86400)));
INSERT INTO rom(id, type, name, file_path) VALUES(3, '', 'Q', 'Play Captions (PCT)/Q.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(2, 600, $((now - 3600)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(3, 7500, $((now - 3600)));
SQL
TZ="TST$(printf '%+d' "$hours_behind")"
export TZ
make -s -C "$ROOT/plugins" playtime-captions ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building playtime-captions"
build_plugin subtitler 'id=subtitler\nname=Subtitler\napi=1\n'
printf 'folder=Roms/Play Captions (PCT)\nTetris=2h 15m \302\267 3 days ago\n' >"$STATE/subtitler.cfg"
printf 'playtime-captions=on\nsubtitler=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/playtime-captions.log" "plugin subtitler: row Unplayed" "plugin subtitler: row Tetris" || fail "the launcher exited early with playtime-captions loaded"
log="$WORK/playtime-captions.log"
plugin=playtime-captions
expect "time on, last on, sessions off, format relative, never off"
plugin=subtitler
expect "open folder 1"
expect "row Tetris caption at right 1, clear of name 1, as wide as \"2h 15m · 3 days ago\" 1"
expect "row Unplayed caption none"
expect "row Extras caption none"
expect "row $LONG_NAME caption none"
grep -qF "plugins: playtime-captions's" "$log" && fail "playtime-captions' decorations were turned off"

# every part in the compact format, and "Never played"
printf 'sessions=on\nformat=compact\nnever=on\n' >"$STATE/playtime-captions.cfg"
printf 'folder=Roms/Play Captions (PCT)\nTetris=2h \302\267 3d \302\267 2x\nUnplayed=Never played\n' >"$STATE/subtitler.cfg"
run_launcher "$WORK/playtime-captions-set.log" "plugin subtitler: row Unplayed" "plugin subtitler: row Tetris" || fail "the launcher exited early with playtime-captions' settings"
log="$WORK/playtime-captions-set.log"
plugin=playtime-captions
expect "time on, last on, sessions on, format compact, never on"
plugin=subtitler
expect "row Tetris caption at right 1, clear of name 1, as wide as \"2h · 3d · 2x\" 1"
expect "row Unplayed caption at right 1, clear of name 1, as wide as \"Never played\" 1"
expect "row Extras caption none"

# with row-badges' dots after the names, both show, apart (row-badges
# goes by saves and recents, so Tetris is unplayed to it)
printf 'tier=3\n' >"$CAPTION_DIR/.media/Tetris.info"
printf 'unplayed=on\ntiers=on\n' >"$STATE/row-badges.cfg"
printf 'folder=Roms/Play Captions (PCT)\n' >"$STATE/dotter.cfg"
printf 'folder=Roms/Play Captions (PCT)\nTetris=2h 15m \302\267 3 days ago\n' >"$STATE/subtitler.cfg"
rm -f "$STATE/playtime-captions.cfg"
make -s -C "$ROOT/plugins" row-badges ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building row-badges"
build_plugin dotter 'id=dotter\nname=Dotter\napi=1\n'
printf 'row-badges=on\nplaytime-captions=on\ndotter=on\nsubtitler=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/playtime-captions-badges.log" "plugin subtitler: row Unplayed" "plugin subtitler: row Tetris" "plugin dotter: row Unplayed" || fail "the launcher exited early with playtime-captions and row-badges"
log="$WORK/playtime-captions-badges.log"
plugin=dotter
expect "row Tetris unplayed 1 tiers 3"
expect "row Unplayed unplayed 1 tiers 0"
plugin=subtitler
expect "row Tetris caption at right 1, clear of name 1, as wide as \"2h 15m · 3 days ago\" 1"
grep -qF "plugins: playtime-captions's" "$log" && fail "playtime-captions' decorations were turned off with row-badges"
grep -qF "plugins: row-badges's" "$log" && fail "row-badges' decorations were turned off with playtime-captions"
rm -rf "$WORK/plugins/dotter.plugin" "$WORK/plugins/row-badges.plugin" "$STATE/dotter.cfg" "$STATE/row-badges.cfg"

# under the bundled layout-grid, on the cells' name line, after the
# centred name: Q's short name leaves room for "2h", and Tetris' for
# nothing
make -s -C "$ROOT/plugins" layout-grid ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building layout-grid"
printf 'folder=Roms/Play Captions (PCT)\nQ=2h\n' >"$STATE/subtitler.cfg"
printf 'layout-grid=on\nplaytime-captions=on\nsubtitler=on\nslot.layout=layout-grid\n' >"$STATE/plugins.txt"
run_launcher "$WORK/playtime-captions-grid.log" "plugin subtitler: row Unplayed" "plugin subtitler: row Q" || fail "the launcher exited early with playtime-captions under layout-grid"
log="$WORK/playtime-captions-grid.log"
plugin=subtitler
expect "row Q caption at right 1, clear of name 1, as wide as \"2h\" 1"
expect "row Tetris caption none"
expect "row Unplayed caption none"
grep -qF "plugins: playtime-captions's" "$log" && fail "playtime-captions' decorations were turned off under layout-grid"
grep -qF "plugins: layout-grid's" "$log" && fail "layout-grid was turned off with playtime-captions"
rm -rf "$WORK/plugins/layout-grid.plugin"

# the achievements part: Tetris has a cached set of 3 with 1 unlocked.
# PCT is a tag minarch doesn't map, so the launcher takes rcheevos' guess
# that the card has a set for, as minarch takes the one the server knows.
printf 'Tetris cart %.0s' 1 2 3 4 >"$CAPTION_DIR/Tetris.gb"
TETRIS_HASH=$(md5_of "$CAPTION_DIR/Tetris.gb")
[ -d "$RA_DIR" ] && mv "$RA_DIR" "$WORK/ra.saved"
mkdir -p "$RA_DIR/offline/cache"
frame_json '{"Success":true,"GameId":7,"Sets":[{"Achievements":[{"ID":1,"Flags":3,"Points":1},{"ID":2,"Flags":3,"Points":1},{"ID":3,"Flags":3,"Points":1}]}]}' \
	"$RA_DIR/offline/cache/achievementsets_$TETRIS_HASH.bin"
frame_json '{"Success":true,"Unlocks":[{"ID":3,"When":1}]}' "$RA_DIR/offline/cache/startsession_$TETRIS_HASH.bin"
printf 'achievements=on\n' >"$STATE/playtime-captions.cfg"
printf 'folder=Roms/Play Captions (PCT)\nTetris=2h 15m \302\267 3 days ago \302\267 1/3 achievements\n' >"$STATE/subtitler.cfg"
printf 'playtime-captions=on\nsubtitler=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/playtime-captions-achievements.log" "plugin subtitler: row Unplayed" \
	"plugin subtitler: row Tetris caption at right 1, clear of name 1, as wide as \"2h 15m · 3 days ago · 1/3 achievements\" 1" ||
	fail "the launcher exited early with playtime-captions' achievements"
log="$WORK/playtime-captions-achievements.log"
plugin=playtime-captions
expect "time on, last on, sessions off, format relative, never off, achievements on"
plugin=subtitler
expect "row Tetris caption at right 1, clear of name 1, as wide as \"2h 15m · 3 days ago · 1/3 achievements\" 1"
expect "row Unplayed caption none"
grep -qF "retroachievements: Roms/Play Captions (PCT)/Tetris.gb is $TETRIS_HASH, guessed" "$log" || fail "Tetris.gb's guessed hash wasn't logged"
grep -qF "plugins: playtime-captions's" "$log" && fail "playtime-captions' decorations were turned off with achievements"
rm -rf "$RA_DIR" "$RA_HASHES"
# the folder the launcher made for its hash cache, if nothing else is in it
if [ -d "$(dirname "$RA_HASHES")" ]; then rmdir "$(dirname "$RA_HASHES")" 2>/dev/null; fi
[ -d "$WORK/ra.saved" ] && mv "$WORK/ra.saved" "$RA_DIR"
rm -f "$STATE/playtime-captions.cfg"

# with the history unknown, nothing, even with never on
printf 'never=on\n' >"$STATE/playtime-captions.cfg"
printf 'playtime-captions=on\nsubtitler=on\n' >"$STATE/plugins.txt"
MOSAIC_LAUNCHER_SQLITE="$WORK/no-such-libsqlite3"
export MOSAIC_LAUNCHER_SQLITE
run_launcher "$WORK/playtime-captions-unknown.log" "plugin subtitler: row Unplayed" "plugin subtitler: row Tetris" || fail "the launcher exited early with playtime-captions and no sqlite"
unset MOSAIC_LAUNCHER_SQLITE
log="$WORK/playtime-captions-unknown.log"
plugin=subtitler
expect "row Tetris caption none"
expect "row Unplayed caption none"
unset TZ
rm -f "$PLAY_DB"
[ -f "$WORK/game_logs.sqlite.saved" ] && mv "$WORK/game_logs.sqlite.saved" "$PLAY_DB"
rm -rf "$WORK/plugins/subtitler.plugin" "$WORK/plugins/playtime-captions.plugin" "$CAPTION_DIR" \
	"$STATE/subtitler.cfg" "$STATE/playtime-captions.cfg" "$STATE/plugins.txt"

# metadata-band draws the year, publisher and genre from a game's .info
# sidecar in a pill under its art, which dater, after it in load order,
# reads back off the screen. Apple has all three (its year a full date)
# under a square thumbnail, Cherry only a genre under a wide one, Banana a
# sidecar with none of the keys and Durian no sidecar, so neither gets a
# band, and Elder's long publisher leaves out what doesn't fit
BAND_DIR="$CARD/Roms/Metadata Band (MBD)"
mkdir -p "$BAND_DIR/.media" "$STATE"
for game in Apple Banana Cherry Durian Elder; do
	: >"$BAND_DIR/$game.gb"
	cp "$ROOT/tests/plugins/peeker/art.png" "$BAND_DIR/.media/$game.png"
done
cp "$ROOT/tests/plugins/drawer/art.png" "$BAND_DIR/.media/Cherry.png"
printf 'year=1994-11-21\npublisher=Nintendo\ngenre=Platformer\ndeveloper=Nintendo\n' >"$BAND_DIR/.media/Apple.info"
printf 'tier=3\nname=Banana\n' >"$BAND_DIR/.media/Banana.info"
printf 'genre= Puzzle \n' >"$BAND_DIR/.media/Cherry.info"
printf 'year=2001\npublisher=A Publisher With a Name Much Too Long to Fit Under Any Art at All\ngenre=Racing\n' >"$BAND_DIR/.media/Elder.info"
make -s -C "$ROOT/plugins" metadata-band ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building metadata-band"
build_plugin dater 'id=dater\nname=Dater\napi=1\n'
printf 'folder=Roms/Metadata Band (MBD)\nApple=1994 \302\267 Nintendo \302\267 Platformer\nCherry=Puzzle\nElder=2001\n' >"$STATE/dater.cfg"
printf 'metadata-band=on\ndater=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/metadata-band.log" "plugin dater: done" || fail "the launcher exited early with metadata-band loaded"
log="$WORK/metadata-band.log"
plugin=metadata-band
expect "fields year,publisher,genre"
plugin=dater
expect "open folder 1"
expect "band Apple under art 1, centred 1, text 1, as wide as \"1994 · Nintendo · Platformer\" 1"
expect "band Banana none"
expect "band Cherry under art 1, centred 1, text 1, as wide as \"Puzzle\" 1"
expect "band Durian none"
expect "band Elder under art 1, centred 1, text 1, as wide as \"2001\" 1"
grep -qF "plugins: metadata-band's" "$log" && fail "metadata-band's decorations were turned off"

# the fields setting picks the keys and their order, skipping unknown ones
printf 'fields=genre, Developer ,bogus,year\n' >"$STATE/metadata-band.cfg"
printf 'folder=Roms/Metadata Band (MBD)\nApple=Platformer \302\267 Nintendo \302\267 1994\nCherry=Puzzle\nElder=Racing \302\267 2001\n' >"$STATE/dater.cfg"
run_launcher "$WORK/metadata-band-fields.log" "plugin dater: done" || fail "the launcher exited early with metadata-band's fields set"
log="$WORK/metadata-band-fields.log"
plugin=metadata-band
expect "fields genre,developer,year"
plugin=dater
expect "band Apple under art 1, centred 1, text 1, as wide as \"Platformer · Nintendo · 1994\" 1"
expect "band Elder under art 1, centred 1, text 1, as wide as \"Racing · 2001\" 1"
expect "band Durian none"

# with no field it knows, no band
printf 'fields=bogus\n' >"$STATE/metadata-band.cfg"
run_launcher "$WORK/metadata-band-none.log" "plugin dater: done" || fail "the launcher exited early with metadata-band's fields empty"
log="$WORK/metadata-band-none.log"
plugin=metadata-band
expect "fields none"
plugin=dater
expect "band Apple none"
rm -rf "$WORK/plugins/dater.plugin" "$WORK/plugins/metadata-band.plugin" "$BAND_DIR" \
	"$STATE/dater.cfg" "$STATE/metadata-band.cfg" "$STATE/plugins.txt"

# with no plugin asking, nothing reads the play history
grep -q "play history:" "$WORK/hello.log" "$WORK/off.log" && fail "the play history was read with no plugin asking"

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

# the bundled header-indicators shows free storage and a profile badge in a
# pill in the theme's accent with hint-coloured text, a gap left of the
# status pill; spotter, a widget after it in load order, reads the header
# back. The SSH dot stays off on desktop, the FPS shows once a second has
# passed, and with nothing to show there's no widget.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" header-indicators >/dev/null || fail "building header-indicators"
build_plugin spotter 'id=spotter\nname=Spotter\napi=1\n'
mkdir -p "$STATE"
printf 'header-indicators=on\nspotter=on\n' >"$STATE/plugins.txt"
printf 'profile=Kid\nssh=on\n' >"$STATE/header-indicators.cfg"
run_launcher "$WORK/header-indicators.log" "plugin header-indicators: widget at " "plugin spotter: runs " \
	|| fail "the launcher exited early with header-indicators loaded"
log="$WORK/header-indicators.log"
plugin=header-indicators
expect "unsupported on desktop: the SSH dot stays off"
grep -qE "plugin header-indicators: widget at [0-9]+,15 [0-9]+x90: profile storage$" "$log" \
	|| fail "header-indicators' widget is wrong: $(grep "plugin header-indicators: widget" "$log")"
hi_x=$(sed -n 's/.*plugin header-indicators: widget at \([0-9]*\),.*/\1/p' "$log" | tail -n 1)
hi_w=$(sed -n 's/.*plugin header-indicators: widget at [0-9]*,15 \([0-9]*\)x90:.*/\1/p' "$log" | tail -n 1)
# runs <first start> <first end> <second start>, the edges blended by up to 2 pixels
runs=$(grep "plugin spotter: runs " "$log" | tail -n 1 | sed 's/.*: runs //; s/,.*//; s/-/ /g')
# shellcheck disable=SC2086
set -- $runs
if [ -z "$hi_x" ] || [ -z "$hi_w" ] || [ "$#" -lt 4 ]; then
	fail "spotter didn't see header-indicators' widget and the status pill: $(grep "plugin spotter: runs" "$log" | tail -n 1)"
elif [ $(($1 - hi_x)) -gt 2 ] || [ $((hi_x - $1)) -gt 2 ] || [ $(($2 - hi_x - hi_w)) -gt 2 ] || [ $((hi_x + hi_w - $2)) -gt 2 ]; then
	fail "header-indicators didn't draw at $hi_x, $hi_w wide: spotter saw $runs"
elif [ "$3" -lt $((hi_x + hi_w + 10)) ]; then
	fail "header-indicators' widget isn't a gap left of the status pill: $runs"
fi
grep "plugin spotter: runs " "$log" | tail -n 1 | grep -qF ", accent 1 hint 1" \
	|| fail "header-indicators' pill isn't in the theme's colours: $(grep "plugin spotter: runs" "$log" | tail -n 1)"
grep -qF "plugins: header-indicators'" "$log" && fail "header-indicators' widget was turned off"

printf 'storage=off\nfps=on\n' >"$STATE/header-indicators.cfg"
run_launcher "$WORK/header-indicators-fps.log" "plugin header-indicators: fps first " "plugin header-indicators: widget at " \
	|| fail "the launcher exited early with header-indicators showing the FPS"
log="$WORK/header-indicators-fps.log"
fps=$(sed -n 's/.*plugin header-indicators: fps first \([0-9]*\)$/\1/p' "$log")
if [ -z "$fps" ] || [ "$fps" -lt 10 ] || [ "$fps" -gt 250 ]; then
	fail "header-indicators' FPS is wrong: $fps"
fi
grep -qE "plugin header-indicators: widget at [0-9]+,15 [0-9]+x90: fps$" "$log" \
	|| fail "header-indicators didn't show only the FPS: $(grep "plugin header-indicators: widget" "$log")"
grep -qF "SSH dot" "$log" && fail "header-indicators logged the SSH dot with it off"

printf 'storage=off\n' >"$STATE/header-indicators.cfg"
run_launcher "$WORK/header-indicators-off.log" "plugin spotter: runs " || fail "the launcher exited early with header-indicators showing nothing"
grep -qF "plugin header-indicators: widget at" "$WORK/header-indicators-off.log" && fail "header-indicators drew a widget with nothing to show"
rm -rf "$WORK/plugins/header-indicators.plugin" "$WORK/plugins/spotter.plugin" "$STATE/header-indicators.cfg" "$STATE/plugins.txt"

# the bundled playtime-limit refuses a game once today's play, from a
# fixture game_logs.sqlite with 40 minutes played an hour ago, reaches the
# daily limit, offering the code; allows it under the limit, counting
# today's grant, and when the history can't be read; and shows the time
# left as a header widget, which spotter reads back. opener launches the
# game; a launch that goes ahead quits the launcher, and the card's
# recents and the launch's /tmp files are put back afterwards.
LIMIT_ROM="$CARD/Roms/.limit-test/Tetris.gb"
mkdir -p "$(dirname "$LIMIT_ROM")" "$(dirname "$PLAY_DB")" "$STATE"
: >"$LIMIT_ROM"
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$WORK/game_logs.sqlite.saved"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/recent.txt.saved"
for f in next last.txt resume_slot.txt; do
	[ -f "/tmp/$f" ] && cp "/tmp/$f" "$WORK/$f.saved"
done
now=$(date +%s)
hours_behind=$(( $(date -u +%H | sed 's/^0//') - 12 ))
TZ="TST$(printf '%+d' "$hours_behind")"
export TZ
make_play_db <<SQL || fail "making the play time limit fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Tetris', '.limit-test/Tetris.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 2400, $((now - 3600)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 5000, $((now - 3 * 86400)));
SQL
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" playtime-limit >/dev/null || fail "building playtime-limit"
build_plugin opener 'id=opener\nname=Opener\napi=1\n'
printf 'playtime-limit=on\nopener=on\n' >"$STATE/plugins.txt"
printf 'rom=%s\n' "$LIMIT_ROM" >"$STATE/opener.cfg"
plugin=playtime-limit

# over the limit: refused, with the code offered, and the code never logged
printf 'limit=30\ncode=Sekr1t\n' >"$STATE/playtime-limit.cfg"
run_launcher "$WORK/limit-over.log" "plugin opener: done" || fail "the launcher exited early with a refused launch"
log="$WORK/limit-over.log"
expect "daily limit 30 minutes, warning at 10, 15 more behind the code"
expect "refused Tetris.gb: 2400 seconds played today, the limit is 30 minutes; offering extra time behind the code"
grep -qF "plugin opener: open rom 0" "$log" || fail "the refused launch went ahead"
grep -qF "plugins: launch of $LIMIT_ROM cancelled by a plugin" "$log" || fail "the refused launch wasn't cancelled"
grep -qF "Sekr1t" "$log" && fail "playtime-limit logged the code"

# under the limit, with today's grant counted: allowed
printf 'limit=30\ncode=Sekr1t\ngrant_day=%s\ngrant_minutes=15\n' "$(date +%Y-%m-%d)" >"$STATE/playtime-limit.cfg"
run_launcher "$WORK/limit-grant.log" && fail "the launcher didn't quit to launch the game with today's grant"
log="$WORK/limit-grant.log"
expect "allowed Tetris.gb: 5 of 45 minutes left today"
rm -f /tmp/mosaic-launcher-launched

# under the limit: allowed
printf 'limit=60\n' >"$STATE/playtime-limit.cfg"
run_launcher "$WORK/limit-under.log" && fail "the launcher didn't quit to launch the game under the limit"
log="$WORK/limit-under.log"
expect "allowed Tetris.gb: 20 of 60 minutes left today"
grep -qF "plugin opener: open rom 1" "$log" || fail "the launch under the limit didn't go ahead"
rm -f /tmp/mosaic-launcher-launched

# the time left as a header widget, in the theme's colours, then the
# warning colour once it's at or under the threshold
rm -rf "$STATE/opener.cfg" "$WORK/plugins/opener.plugin"
build_plugin spotter 'id=spotter\nname=Spotter\napi=1\n'
printf 'playtime-limit=on\nspotter=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/limit-widget.log" "plugin playtime-limit: widget at " "plugin spotter: runs " \
	|| fail "the launcher exited early with playtime-limit's widget"
log="$WORK/limit-widget.log"
grep -qE "plugin playtime-limit: widget at [0-9]+,15 [0-9]+x90: 20m left$" "$log" \
	|| fail "playtime-limit's widget is wrong: $(grep "plugin playtime-limit: widget" "$log")"
pl_x=$(sed -n 's/.*plugin playtime-limit: widget at \([0-9]*\),.*/\1/p' "$log" | tail -n 1)
pl_w=$(sed -n 's/.*plugin playtime-limit: widget at [0-9]*,15 \([0-9]*\)x90:.*/\1/p' "$log" | tail -n 1)
runs=$(grep "plugin spotter: runs " "$log" | tail -n 1 | sed 's/.*: runs //; s/,.*//; s/-/ /g')
# shellcheck disable=SC2086
set -- $runs
if [ -z "$pl_x" ] || [ -z "$pl_w" ] || [ "$#" -lt 4 ]; then
	fail "spotter didn't see playtime-limit's widget and the status pill: $(grep "plugin spotter: runs" "$log" | tail -n 1)"
elif [ $(($1 - pl_x)) -gt 2 ] || [ $((pl_x - $1)) -gt 2 ] || [ $(($2 - pl_x - pl_w)) -gt 2 ] || [ $((pl_x + pl_w - $2)) -gt 2 ]; then
	fail "playtime-limit didn't draw at $pl_x, $pl_w wide: spotter saw $runs"
fi
grep "plugin spotter: runs " "$log" | tail -n 1 | grep -qF ", accent 1 hint 1" \
	|| fail "playtime-limit's pill isn't in the theme's colours: $(grep "plugin spotter: runs" "$log" | tail -n 1)"

printf 'limit=45\n' >"$STATE/playtime-limit.cfg"
run_launcher "$WORK/limit-warn.log" "plugin playtime-limit: widget at " "plugin spotter: runs " \
	|| fail "the launcher exited early with playtime-limit's warning"
log="$WORK/limit-warn.log"
grep -qE "plugin playtime-limit: widget at [0-9]+,15 [0-9]+x90: 5m left, warning$" "$log" \
	|| fail "playtime-limit's warning is wrong: $(grep "plugin playtime-limit: widget" "$log")"
grep "plugin spotter: runs " "$log" | tail -n 1 | grep -qF ", accent 0 " \
	|| fail "playtime-limit's warning is in the theme's colours: $(grep "plugin spotter: runs" "$log" | tail -n 1)"

# off by default: nothing shows
rm -f "$STATE/playtime-limit.cfg"
run_launcher "$WORK/limit-off.log" "plugin playtime-limit: no daily limit set" "plugin spotter: runs " \
	|| fail "the launcher exited early with playtime-limit off by default"
grep -qF "plugin playtime-limit: widget at" "$WORK/limit-off.log" && fail "playtime-limit drew a widget with no limit"
rm -rf "$WORK/plugins/spotter.plugin"

# with no history: allowed, saying why once
rm -f "$PLAY_DB"
build_plugin opener 'id=opener\nname=Opener\napi=1\n'
printf 'playtime-limit=on\nopener=on\n' >"$STATE/plugins.txt"
printf 'rom=%s\n' "$LIMIT_ROM" >"$STATE/opener.cfg"
printf 'limit=30\n' >"$STATE/playtime-limit.cfg"
run_launcher "$WORK/limit-unknown.log" && fail "the launcher didn't quit to launch the game with no history"
log="$WORK/limit-unknown.log"
[ "$(grep -c "plugin playtime-limit: the play history can't be read" "$log")" -eq 1 ] \
	|| fail "playtime-limit didn't say once why it allowed the launch"
grep -qF "plugin opener: open rom 1" "$log" || fail "the launch with no history didn't go ahead"
rm -f /tmp/mosaic-launcher-launched

unset TZ
[ -f "$WORK/game_logs.sqlite.saved" ] && mv "$WORK/game_logs.sqlite.saved" "$PLAY_DB"
if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
for f in next last.txt resume_slot.txt; do
	if [ -f "$WORK/$f.saved" ]; then mv "$WORK/$f.saved" "/tmp/$f"; else rm -f "/tmp/$f"; fi
done
rm -f "$WORK/recent.txt.saved"
rm -rf "$WORK/plugins/playtime-limit.plugin" "$WORK/plugins/opener.plugin" "$(dirname "$LIMIT_ROM")" \
	"$STATE/playtime-limit.cfg" "$STATE/opener.cfg" "$STATE/plugins.txt"

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

# launch-animation (built in whole by glider, which reads its frames back)
# owns the launch slot: launching a pak from the root, the selection pill
# glides from its row to the middle as the rest dims to black, then fades,
# and the last frame is black; under gridder the selected cell glides, at
# the duration and ease settings. Each launch quits the launcher; the
# card's recents, /tmp/next and /tmp/last.txt are put back
GLIDE_PAK="$WORK/glider/Glide.pak"
mkdir -p "$GLIDE_PAK"
: >"$GLIDE_PAK/launch.sh"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/recent.txt.saved"
[ -f /tmp/next ] && cp /tmp/next "$WORK/next.saved"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/last.txt.saved"
build_plugin glider "$(cat "$ROOT/plugins/launch-animation/plugin.txt")\n"
build_plugin gridder 'id=gridder\nname=Gridder\napi=1\nslots=layout\n'
plugin=glider
for mode in list grid; do
	case "$mode" in
	list)
		printf 'launch-animation=on\ngridder=off\n' >"$STATE/plugins.txt"
		printf 'glider_pak=%s\n' "$GLIDE_PAK" >"$STATE/launch-animation.cfg"
		what=pill; want_duration=350; want_ease=2
		;;
	grid)
		printf 'launch-animation=on\ngridder=on\n' >"$STATE/plugins.txt"
		printf 'glider_pak=%s\nduration=200\nease=in-out\n' "$GLIDE_PAK" >"$STATE/launch-animation.cfg"
		what=cell; want_duration=200; want_ease=3
		;;
	esac
	run_launcher "$WORK/launch-animation-$mode.log" && fail "the launcher didn't quit to launch glider's pak"
	log="$WORK/launch-animation-$mode.log"
	grep -qF "plugins: launch-animation picks the launch style" "$log" || fail "launch-animation didn't take the launch slot"
	expect "duration $want_duration ease $want_ease"
	expect "open pak 1"
	grep -qE "plugin launch-animation: gliding the $what at [0-9]+,[0-9]+ [1-9][0-9]*x[1-9][0-9]* to the middle over $want_duration ms$" "$log" \
		|| fail "launch-animation didn't glide the $what: $(grep "plugin launch-animation: " "$log")"
	# the pill or cell is on something the frame beside it isn't, and a probe was found
	begin=$(grep "plugin glider: begin " "$log")
	echo "$begin" | grep -qE "style 2 gliding 1 from .* on ([0-9A-F]{6}) beside ([0-9A-F]{6}) probe [0-9]+,[0-9]+$" \
		|| fail "glider's begin is wrong: $begin"
	if [ "$mode" = list ]; then
		on=$(echo "$begin" | sed 's/.* on \([0-9A-F]*\) beside.*/\1/')
		beside=$(echo "$begin" | sed 's/.* beside \([0-9A-F]*\) probe.*/\1/')
		[ "$on" != "$beside" ] || fail "glider's pill isn't on the pill: $begin"
	fi
	# every frame up to the glide's end had the pill where the model puts it,
	# the rest dimmed by the model's amount, and the last was black (the
	# ease can reach 1 a frame early, so the last summary counts)
	last=$(grep "plugin glider: last " "$log" | tail -1)
	echo "$last" | grep -qE "^.*last frames ([0-9]+) pill ([3-9]|[1-9][0-9]+) of ([0-9]+) dim ([0-9]+) of ([0-9]+) black 1$" \
		|| fail "launch-animation's frames are wrong: ${last:-no last frame}"
	# shellcheck disable=SC2046
	set -- $(echo "$last" | sed 's/.* pill \([0-9]*\) of \([0-9]*\) dim \([0-9]*\) of \([0-9]*\) black.*/\1 \2 \3 \4/')
	{ [ "${1:-0}" -eq "${2:-1}" ] && [ "${3:-0}" -eq "${4:-1}" ] && [ "${4:-0}" -gt 0 ]; } \
		|| fail "launch-animation's frames don't match the model: $last"
	grep -qF "using stock's transitions until the launcher restarts" "$log" && fail "launch-animation's launch fell back to stock's"
done
if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
if [ -f "$WORK/next.saved" ]; then cp "$WORK/next.saved" /tmp/next; else rm -f /tmp/next; fi
if [ -f "$WORK/last.txt.saved" ]; then cp "$WORK/last.txt.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
rm -f "$WORK/recent.txt.saved" "$WORK/next.saved" "$WORK/last.txt.saved"
rm -rf "$WORK/plugins/glider.plugin" "$WORK/plugins/gridder.plugin" "$WORK/glider" "$STATE/launch-animation.cfg" "$STATE/plugins.txt"

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

# hider hides Apple's and Cherry's names, which have art, in its own
# folder: the built-in list leaves those rows blank, the selected Apple
# with no pill on the push into the folder, and still draws Banana's name;
# decorators are told which are hidden. hide_name is asked once an entry.
HIDE_DIR="$CARD/Hider"
mkdir -p "$HIDE_DIR" "$STATE"
cp "$ROOT/tests/plugins/drawer/art.png" "$HIDE_DIR/art.png"
printf 'hider=on\n' >"$STATE/plugins.txt"
build_plugin hider 'id=hider\nname=Hider\napi=1\nslots=transition\n'
run_launcher "$WORK/hider.log" "plugin hider: done" || fail "the launcher exited early with hider loaded"
log="$WORK/hider.log"
plugin=hider
expect "open folder 1"
expect "push to hider row 1 pill 0"
expect "row Apple selected 1 hidden 1 named 0 ink 0"
expect "row Banana selected 0 hidden 0 named 1 ink 1"
expect "row Cherry selected 0 hidden 1 named 0 ink 0"
grep -qE "plugin hider: done, asked 3$" "$log" || fail "hider wasn't asked once an entry: $(grep "plugin hider: done" "$log")"
grep -qE "plugin hider: row (Apple|Cherry) .* ink 1$" "$log" && fail "a hidden name was drawn: $(grep "plugin hider: row" "$log")"

# with no art to stand in for it, the selected Apple keeps its name and pill
printf 'art=none\n' >"$STATE/hider.cfg"
run_launcher "$WORK/hider-noart.log" "plugin hider: done" || fail "the launcher exited early with hider's art off"
log="$WORK/hider-noart.log"
expect "push to hider row 1 pill 1"
expect "row Apple selected 1 hidden 1 named 1 ink 1"
expect "row Cherry selected 0 hidden 1 named 0 ink 0"

# with Show game art off, every name shows; the card's settings are put back
rm -f "$STATE/hider.cfg"
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
printf 'gameart=0\n' >"$SETTINGS"
SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/hider-gameart.log" "plugin hider: done" \
	|| fail "the launcher exited early with Show game art off"
log="$WORK/hider-gameart.log"
expect "push to hider row 1 pill 1"
expect "row Apple selected 1 hidden 1 named 1 ink 1"
expect "row Cherry selected 0 hidden 1 named 1 ink 1"
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -f "$WORK/minuisettings.txt.saved"

# hiding nothing draws the list as stock does
printf 'hide=none\n' >"$STATE/hider.cfg"
run_launcher "$WORK/hider-none.log" "plugin hider: done" || fail "the launcher exited early with hider hiding nothing"
log="$WORK/hider-none.log"
expect "push to hider row 1 pill 1"
expect "row Apple selected 1 hidden 0 named 1 ink 1"
expect "row Banana selected 0 hidden 0 named 1 ink 1"
expect "row Cherry selected 0 hidden 0 named 1 ink 1"

# a plugin layout gets the answers in its view, and decorators in the
# cells it passes to decorate_cell
rm -f "$STATE/hider.cfg"
printf 'id=hider\nname=Hider\napi=1\nslots=layout\n' >"$WORK/plugins/hider.plugin/plugin.txt"
run_launcher "$WORK/hider-layout.log" "plugin hider: done" || fail "the launcher exited early with hider's layout"
log="$WORK/hider-layout.log"
expect "view names_hidden 101"
grep -qE "plugin hider: row Apple selected 1 hidden 1 named 0 " "$log" || fail "Apple's cell isn't hidden: $(grep "plugin hider: row" "$log")"
grep -qE "plugin hider: row Banana selected 0 hidden 0 named 0 " "$log" || fail "Banana's cell is hidden: $(grep "plugin hider: row" "$log")"
printf 'hide=none\n' >"$STATE/hider.cfg"
run_launcher "$WORK/hider-layout-none.log" "plugin hider: done" || fail "the launcher exited early with hider's layout hiding nothing"
log="$WORK/hider-layout-none.log"
expect "view names_hidden none"
rm -rf "$WORK/plugins/hider.plugin" "$HIDE_DIR" "$STATE/hider.cfg" "$STATE/plugins.txt"

# the bundled art-boxart answers art from each fork's folder convention, as
# looker sees it through art_find: nx-redux's .media/boxart2d and
# .media/boxart (a .png, then a .jpg or .jpeg), MyMinUI's Imgs, UnuOS's
# .res, visible Images folders, a folder's cover.png and a Pico-8 cart as
# its own art, in that order, with nx-redux's .media/boxart as the 3D box,
# .media/wheel as the logo and .media/screenshot as the screenshot, and
# stock's .media thumbnail kept unless the thumbnail setting is on. The
# Imgs and Images folders are left out of the list, and no names are
# hidden until cover_name hides the covered folder's. Then the settings
# turn each off.
BOX_DIR="$CARD/Boxart"
mkdir -p "$BOX_DIR/.media/boxart2d" "$BOX_DIR/.media/boxart" "$BOX_DIR/.media/wheel" "$BOX_DIR/.media/screenshot" \
	"$BOX_DIR/.media/mix" "$BOX_DIR/Imgs" "$BOX_DIR/.res" "$BOX_DIR/Images" "$BOX_DIR/Cover" "$STATE"
for game in Alpha Bravo Charlie Delta Foxtrot Golf; do : >"$BOX_DIR/$game.gb"; done
: >"$BOX_DIR/Cover/Game.gb"
for art in .media/Alpha .media/boxart2d/Alpha .media/boxart/Alpha .media/wheel/Alpha .media/screenshot/Alpha \
	Imgs/Alpha Imgs/Bravo .res/Charlie Images/Delta Cover/cover Echo.p8 .media/mix/Foxtrot; do
	cp "$ROOT/tests/plugins/drawer/art.png" "$BOX_DIR/$art.png"
done
# art_find only names files, so a .png copy stands in for the .jpg and .jpeg art
for art in .media/boxart/Golf.jpg .media/wheel/Golf.jpeg .media/screenshot/Golf.jpg; do
	cp "$ROOT/tests/plugins/drawer/art.png" "$BOX_DIR/$art"
done
make -s -C "$ROOT/plugins" art-boxart ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building art-boxart"
build_plugin looker 'id=looker\nname=Looker\napi=1\n'
printf 'art-boxart=on\nlooker=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/boxart.log" "plugin looker: done" || fail "the launcher exited early with art-boxart loaded"
log="$WORK/boxart.log"
plugin=looker
grep -qF "plugins: loaded art-boxart" "$log" || fail "art-boxart didn't load"
expect "open folder 1"
expect "entries Alpha.gb,Bravo.gb,Charlie.gb,Cover,Delta.gb,Echo.p8.png,Foxtrot.gb,Golf.gb"
expect 'art Alpha.gb thumbnail 1 ".media/Alpha.png"'
expect 'art Alpha.gb box 1 ".media/boxart2d/Alpha.png"'
expect 'art Alpha.gb box3d 1 ".media/boxart/Alpha.png"'
expect 'art Alpha.gb logo 1 ".media/wheel/Alpha.png"'
expect 'art Alpha.gb screenshot 1 ".media/screenshot/Alpha.png"'
expect 'art Bravo.gb box 1 "Imgs/Bravo.png"'
expect 'art Bravo.gb thumbnail 0 ".media/Bravo.png"'
expect 'art Bravo.gb box3d 0 ""'
expect 'art Charlie.gb box 1 ".res/Charlie.png"'
expect 'art Delta.gb box 1 "Images/Delta.png"'
expect 'art Cover box 1 "Cover/cover.png"'
expect 'art Echo.p8.png box 1 "Echo.p8.png"'
# nx-redux's mix composite isn't any one kind
expect 'art Foxtrot.gb box 0 ""'
expect 'art Foxtrot.gb logo 0 ""'
expect 'art Foxtrot.gb screenshot 0 ""'
expect 'art Golf.gb box 1 ".media/boxart/Golf.jpg"'
expect 'art Golf.gb box3d 1 ".media/boxart/Golf.jpg"'
expect 'art Golf.gb logo 1 ".media/wheel/Golf.jpeg"'
expect 'art Golf.gb screenshot 1 ".media/screenshot/Golf.jpg"'
# folders keep their names by default
expect "hidden none"

# with cover_name on, the folder with a cover shows as its cover, its name
# hidden, and nothing else's is
printf 'cover_name=on\n' >"$STATE/art-boxart.cfg"
run_launcher "$WORK/boxart-cover-name.log" "plugin looker: done" || fail "the launcher exited early with art-boxart hiding covered folders' names"
log="$WORK/boxart-cover-name.log"
expect "hidden Cover"

# the thumbnail setting puts this art before stock's, and with nx-redux's
# folders, covers and carts off the next convention answers; the art
# folders show with hide off
printf 'thumbnail=on\nmedia=off\ncover=off\nrom_art=off\nhide=off\n' >"$STATE/art-boxart.cfg"
run_launcher "$WORK/boxart-settings.log" "plugin looker: done" || fail "the launcher exited early with art-boxart's settings changed"
log="$WORK/boxart-settings.log"
expect "entries Alpha.gb,Bravo.gb,Charlie.gb,Cover,Delta.gb,Echo.p8.png,Foxtrot.gb,Golf.gb,Images,Imgs"
expect 'art Alpha.gb thumbnail 1 "Imgs/Alpha.png"'
expect 'art Alpha.gb box 1 "Imgs/Alpha.png"'
expect 'art Alpha.gb box3d 0 ""'
expect 'art Alpha.gb logo 0 ""'
expect 'art Alpha.gb screenshot 0 ""'
expect 'art Golf.gb box 0 ""'
expect 'art Bravo.gb thumbnail 1 "Imgs/Bravo.png"'
expect 'art Cover box 0 ""'
expect 'art Echo.p8.png box 0 ""'

# with Imgs, .res and Images off, their art is gone and the folders show
# even with hide on
printf 'imgs=off\nres=off\nimages=off\n' >"$STATE/art-boxart.cfg"
run_launcher "$WORK/boxart-off.log" "plugin looker: done" || fail "the launcher exited early with art-boxart's folders off"
log="$WORK/boxart-off.log"
expect "entries Alpha.gb,Bravo.gb,Charlie.gb,Cover,Delta.gb,Echo.p8.png,Foxtrot.gb,Golf.gb,Images,Imgs"
expect 'art Alpha.gb box 1 ".media/boxart2d/Alpha.png"'
expect 'art Bravo.gb box 0 ""'
expect 'art Charlie.gb box 0 ""'
expect 'art Delta.gb box 0 ""'
expect 'art Cover box 1 "Cover/cover.png"'
expect 'art Echo.p8.png box 1 "Echo.p8.png"'
rm -rf "$WORK/plugins/art-boxart.plugin" "$WORK/plugins/looker.plugin" "$BOX_DIR" "$STATE/art-boxart.cfg" "$STATE/plugins.txt"

# the bundled collections-plus puts every collection on the root in the
# order setting's order, with the Collections folder off it and the
# Games tile off the quick menu, as collector reads the lists back: the
# Collections folder in the same order, and Alpha in its file's order,
# though Collections/map.txt renames one of its games, with its own map's
# names (Secret hidden) and stock's shared name for the rest. Alpha's art
# is its thumbnail and box. Then with the defaults, the root and the
# folder are stock's, and the file order and own map still hold.
COLL_DIR="$CARD/Collections"
# a system of its own, so stock keeps the collections in their folder
COLL_GAMES="$CARD/Roms/Collector (GB)"
[ -e "$COLL_DIR" ] && mv "$COLL_DIR" "$WORK/Collections.saved"
mkdir -p "$COLL_DIR/.media" "$COLL_GAMES" "$STATE"
for game in Zed Apple Mango Secret; do : >"$COLL_GAMES/$game.gb"; done
for game in Zed Apple Mango Secret; do echo "/Roms/Collector (GB)/$game.gb"; done >"$COLL_DIR/Alpha.txt"
echo "/Roms/Collector (GB)/Apple.gb" >"$COLL_DIR/Beta.txt"
echo "/Roms/Collector (GB)/Apple.gb" >"$COLL_DIR/Gamma.txt"
printf 'Beta.txt\tBee\nMango.gb\tMango Shared\n' >"$COLL_DIR/map.txt"
printf 'Zed.gb\tZed Prime\nSecret.gb\t.secret\n' >"$COLL_DIR/.media/Alpha.map.txt"
cp "$ROOT/tests/plugins/drawer/art.png" "$COLL_DIR/.media/Alpha.png"
make -s -C "$ROOT/plugins" collections-plus ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building collections-plus"
build_plugin collector 'id=collector\nname=Collector\napi=1\n'
printf 'collection=Collections/Alpha.txt\n' >"$STATE/collector.cfg"
printf 'promote=*\norder=Gamma, Bee\nhide_folder=on\nhide_games=on\n' >"$STATE/collections-plus.cfg"
printf 'collections-plus=on\ncollector=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/collections-plus.log" "plugin collector: done" "quick menu: top row" || fail "the launcher exited early with collections-plus loaded"
log="$WORK/collections-plus.log"
plugin=collections-plus
expect "order 2, promote all 0, hide folder on, file order on, own map on, art on, hide games on"
plugin=collector
expect "open root 1"
grep -qE "plugin collector: root .*Collector,Gamma,Bee,Alpha(,|$)" "$log" || fail "the collections aren't on the root in order: $(grep "plugin collector: root" "$log")"
grep -qE "plugin collector: root (.*,)?Collections(,|$)" "$log" && fail "the Collections folder stayed on the root"
expect "art Gamma thumbnail 0 box 0"
expect "art Alpha thumbnail 1 box 1"
expect "folder Gamma,Bee,Alpha"
expect "collection Zed Prime,Apple,Mango Shared"
top=$(grep "quick menu: top row" "$log" | tail -n 1)
case "$top" in *Games*) fail "the Games tile stayed: $top" ;; esac
case "$top" in *Collections*) ;; *) fail "the Collections tile went: $top" ;; esac

rm -f "$STATE/collections-plus.cfg"
run_launcher "$WORK/collections-plus-defaults.log" "plugin collector: done" || fail "the launcher exited early with collections-plus' defaults"
log="$WORK/collections-plus-defaults.log"
grep -qE "plugin collector: root (.*,)?Collections(,|$)" "$log" || fail "the Collections folder isn't on the root: $(grep "plugin collector: root" "$log")"
grep -qE "plugin collector: root .*(Gamma|Bee|Alpha)" "$log" && fail "a collection is on the root by default"
expect "folder Alpha,Bee,Gamma"
expect "collection Zed Prime,Apple,Mango Shared"
grep "quick menu: top row" "$log" | grep -qv "Games" && fail "the Games tile went by default"

# with file_order and own_map off, stock's names and order: sorted by
# the shared map's rename
printf 'file_order=off\nown_map=off\n' >"$STATE/collections-plus.cfg"
run_launcher "$WORK/collections-plus-stock.log" "plugin collector: done" || fail "the launcher exited early with collections-plus' options off"
log="$WORK/collections-plus-stock.log"
expect "collection Apple,Mango Shared,Secret,Zed"
rm -rf "$WORK/plugins/collections-plus.plugin" "$WORK/plugins/collector.plugin" "$COLL_DIR" "$COLL_GAMES" \
	"$STATE/collections-plus.cfg" "$STATE/collector.cfg" "$STATE/plugins.txt"
[ -e "$WORK/Collections.saved" ] && mv "$WORK/Collections.saved" "$COLL_DIR"

# the bundled art-placeholder makes a thumbnail and a box for games with no
# art: by default a title card in the theme's colours with the name on it,
# or with the style setting a pattern from the rom's path, the same pixels
# on every start and different for each game. A game with a real thumbnail
# gets no box either, and the launcher never asks for generated art a file
# answers. carder, loaded first, logs each ask and makes nothing.
HOLD_DIR="$CARD/Placeholder"
mkdir -p "$HOLD_DIR/.media" "$STATE"
for game in Alpha Bravo Charlie; do : >"$HOLD_DIR/$game.gb"; done
cp "$ROOT/tests/plugins/drawer/art.png" "$HOLD_DIR/.media/Bravo.png"
make -s -C "$ROOT/plugins" art-placeholder ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building art-placeholder"
build_plugin carder 'id=carder\nname=Carder\napi=1\n'
printf 'carder=on\nart-placeholder=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/placeholder.log" "plugin carder: done" || fail "the launcher exited early with art-placeholder loaded"
log="$WORK/placeholder.log"
plugin=carder
grep -qF "plugins: loaded art-placeholder" "$log" || fail "art-placeholder didn't load"
expect "open folder 1"
expect "card Alpha 0 1 460x460 edge 1 panel 1 text 1 sig "
expect "card Alpha 3 1 345x460 edge 1 panel 1 text 1 sig "
expect "card Charlie 0 1 460x460 edge 1 panel 1 text 1 sig "
expect "card Charlie 3 1 345x460 edge 1 panel 1 text 1 sig "
expect "card Bravo 3 -1"
expect "real Bravo 1 FF0000"
expect "asked Alpha 0"
expect "asked Bravo 3"
grep -qF "plugin carder: asked Bravo 0" "$log" && fail "generated art was asked for Bravo's real thumbnail"

# sig <name> <kind> <log>: a card's pixel signature
sig() { sed -n "s/.*plugin carder: card $1 $2 1 .* sig \([0-9A-F]*\)$/\1/p" "$3"; }
title_alpha=$(sig Alpha 0 "$log")
printf 'style=pattern\n' >"$STATE/art-placeholder.cfg"
run_launcher "$WORK/placeholder-pattern.log" "plugin carder: done" || fail "the launcher exited early with art-placeholder's patterns"
log="$WORK/placeholder-pattern.log"
expect "card Alpha 0 1 460x460 "
expect "card Alpha 3 1 345x460 "
expect "card Bravo 3 -1"
run_launcher "$WORK/placeholder-again.log" "plugin carder: done" || fail "the launcher exited early with art-placeholder started again"
for card in "Alpha 0" "Alpha 3" "Charlie 0" "Charlie 3"; do
	# shellcheck disable=SC2086 # name and kind
	first=$(sig $card "$WORK/placeholder-pattern.log")
	# shellcheck disable=SC2086
	again=$(sig $card "$WORK/placeholder-again.log")
	[ -n "$first" ] || fail "no $card pattern"
	[ "$first" = "$again" ] || fail "the $card pattern changed between starts: $first, then $again"
done
pattern_alpha=$(sig Alpha 0 "$WORK/placeholder-pattern.log")
[ "$pattern_alpha" != "$(sig Charlie 0 "$WORK/placeholder-pattern.log")" ] || fail "Alpha and Charlie have the same pattern"
[ "$pattern_alpha" != "$title_alpha" ] || fail "the pattern style made a title card"
rm -rf "$WORK/plugins/art-placeholder.plugin" "$WORK/plugins/carder.plugin" "$HOLD_DIR" "$STATE/art-placeholder.cfg" "$STATE/plugins.txt"

# the bundled art-backdrop draws the selected game's art behind the list,
# which dimmer reads back from each push's old frame down the left edge and
# at the top right. By default it's a gradient over the whole screen,
# dimmed by half: Art's backdrop, red on the left and blue on the right,
# at full strength over the top half and fading out below it; Shot falls
# back to its screenshot, green; and Bare, with no art, keeps stock's
# background, the theme's colour.
DIM_DIR="$WORK/dimmer"
mkdir -p "$DIM_DIR"
cp "$ROOT/tests/plugins/drawer/art.png" "$DIM_DIR/backdrop.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$DIM_DIR/shot.png"
make -s -C "$ROOT/plugins" art-backdrop ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building art-backdrop"
build_plugin dimmer 'id=dimmer\nname=Dimmer\napi=1\nslots=transition\n'
printf 'backdrop=%s\nshot=%s\n' "$DIM_DIR/backdrop.png" "$DIM_DIR/shot.png" >"$STATE/dimmer.cfg"
printf 'dimmer=on\nart-backdrop=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/backdrop.log" "plugin dimmer: done" || fail "the launcher exited early with art-backdrop loaded"
log="$WORK/backdrop.log"
plugin=dimmer
grep -qF "plugins: art-backdrop draws the background" "$log" || fail "art-backdrop didn't take the background slot"
grep -qF "plugin art-backdrop: gradient, dimmed by 128 of 255, over 1/1 of the screen, from 4 kinds of art" "$log" \
	|| fail "art-backdrop's defaults are wrong: $(grep "plugin art-backdrop:" "$log")"
grep -qE "plugin dimmer: from Art top [78][0-9A-F]0000FF quarter [78][0-9A-F]0000FF fade [456][0-9A-F]0000FF low [123][0-9A-F]0000FF right 0000[78][0-9A-F]FF, " "$log" \
	|| fail "Art's backdrop isn't a dimmed gradient: $(grep "plugin dimmer: from Art" "$log")"
grep -qE "plugin dimmer: from Shot top 00[78][0-9A-F]00FF quarter 00[78][0-9A-F]00FF fade 00[456][0-9A-F]00FF low 00[123][0-9A-F]00FF " "$log" \
	|| fail "Shot's screenshot isn't behind the list: $(grep "plugin dimmer: from Shot" "$log")"
# stock_is <game> <log>: every pixel dimmer read for game is the theme's colour
stock_is() {
	line=$(grep "plugin dimmer: from $1 " "$2")
	theme=$(echo "$line" | sed -n 's/.*, theme \([0-9A-F]*\)$/\1/p')
	[ -n "$theme" ] || { fail "dimmer didn't read $1's background"; return; }
	want="from $1 top ${theme}FF quarter ${theme}FF fade ${theme}FF low ${theme}FF right ${theme}FF, theme $theme"
	echo "$line" | grep -qF "$want" || fail "$1 doesn't have stock's background: $line"
}
stock_is Bare "$log"
grep -qF "stock's background until" "$log" && fail "art-backdrop's background was turned off"

# full-bleed over the top third, dimmed by three quarters, without the
# screenshot: Art's backdrop is darker and stops a third of the way down,
# and Shot keeps stock's background
printf 'style=full\nheight=third\ndim=75\nscreenshot=off\n' >"$STATE/art-backdrop.cfg"
run_launcher "$WORK/backdrop-third.log" "plugin dimmer: done" || fail "the launcher exited early with art-backdrop's third"
log="$WORK/backdrop-third.log"
grep -qF "plugin art-backdrop: full-bleed, dimmed by 191 of 255, over 1/3 of the screen, from 3 kinds of art" "$log" \
	|| fail "art-backdrop's settings weren't read: $(grep "plugin art-backdrop:" "$log")"
theme=$(sed -n 's/.*plugin dimmer: from Art .*, theme \([0-9A-F]*\)$/\1/p' "$log")
grep -qE "plugin dimmer: from Art top [34][0-9A-F]0000FF quarter [34][0-9A-F]0000FF fade ${theme}FF low ${theme}FF right 0000[34][0-9A-F]FF, " "$log" \
	|| fail "Art's backdrop isn't full-bleed over a third: $(grep "plugin dimmer: from Art" "$log")"
stock_is Shot "$log"
stock_is Bare "$log"
rm -rf "$WORK/plugins/art-backdrop.plugin" "$WORK/plugins/dimmer.plugin" "$DIM_DIR" "$STATE/art-backdrop.cfg" "$STATE/dimmer.cfg" "$STATE/plugins.txt"

# the bundled layout-hero-rail draws peeker's folder as a rail of cards
# under the selected game's hero: Apple's thumbnail from .media fills its
# card, scaled from 8x8, on a tile of the theme's background colour (so
# its middle differs from its top), and
# banana and Cherry, with no art, get letter tiles in the theme's accent
# colour with their letter across the middle; peeker reads each card back
# as a row decorator. Its settings set the cards' size.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" layout-hero-rail art-boxart >/dev/null || fail "building layout-hero-rail"
build_plugin peeker 'id=peeker\nname=Peeker\napi=1\n'
PEEK_DIR="$CARD/Peeker"
mkdir -p "$PEEK_DIR/.media" "$STATE"
cp "$ROOT/tests/plugins/peeker/art.png" "$PEEK_DIR/.media/Apple.png"
printf 'layout-hero-rail=on\npeeker=on\nslot.layout=layout-hero-rail\n' >"$STATE/plugins.txt"
run_launcher "$WORK/hero-rail.log" "plugin peeker: cell Apple middle 00FF00" "plugin peeker: cell banana" \
	"plugin peeker: cell Cherry" || fail "the launcher exited early with layout-hero-rail loaded"
log="$WORK/hero-rail.log"
plugin=peeker
grep -qF "plugins: layout-hero-rail draws the game list" "$log" || fail "layout-hero-rail didn't take the game list"
grep -qF "plugin layout-hero-rail: cards 28% of the height, tinted by 51 of 255, logo fading in over 300 ms" "$log" \
	|| fail "layout-hero-rail's defaults are wrong: $(grep "plugin layout-hero-rail:" "$log")"
grep -qE "plugin layout-hero-rail: hero 1024x[0-9]+, 6 cards of 138 by 185, logo in " "$log" \
	|| fail "layout-hero-rail's rail is wrong: $(grep "plugin layout-hero-rail:" "$log")"
expect "open folder 1"
grep -qE "plugin peeker: cell Apple middle 00FF00 top [0-9A-F]{6}, accent 0, ink 1, named 1$" "$log" \
	|| fail "Apple's card isn't its art: $(grep "plugin peeker: cell Apple" "$log")"
grep -qE "plugin peeker: cell banana middle [0-9A-F]{6} top [0-9A-F]{6}, accent 1, ink 1, named 1$" "$log" \
	|| fail "banana has no letter tile: $(grep "plugin peeker: cell banana" "$log")"
grep -qE "plugin peeker: cell Cherry middle [0-9A-F]{6} top [0-9A-F]{6}, accent 1, ink 1, named 1$" "$log" \
	|| fail "Cherry has no letter tile: $(grep "plugin peeker: cell Cherry" "$log")"
grep -qF "plugins: layout-hero-rail's" "$log" && fail "layout-hero-rail's layout was turned off"
grep -qF "plugins: peeker's" "$log" && fail "peeker's decorator was turned off"

printf 'cards=small\ntint=0\nfade=0\n' >"$STATE/layout-hero-rail.cfg"
run_launcher "$WORK/hero-rail-set.log" "plugin peeker: cell Apple middle 00FF00" "plugin peeker: cell Cherry" \
	|| fail "the launcher exited early with layout-hero-rail's small cards"
log="$WORK/hero-rail-set.log"
grep -qF "plugin layout-hero-rail: cards 22% of the height, tinted by 0 of 255, logo fading in over 0 ms" "$log" \
	|| fail "layout-hero-rail's settings weren't read: $(grep "plugin layout-hero-rail:" "$log")"
grep -qE "plugin layout-hero-rail: hero 1024x[0-9]+, 8 cards of 108 by 145, " "$log" \
	|| fail "layout-hero-rail's small cards are wrong: $(grep "plugin layout-hero-rail:" "$log")"
rm -rf "$WORK/plugins/layout-hero-rail.plugin" "$WORK/plugins/art-boxart.plugin" "$WORK/plugins/peeker.plugin" "$PEEK_DIR" \
	"$STATE/layout-hero-rail.cfg" "$STATE/plugins.txt"

# hider's hidden names under layout-hero-rail: Apple's and Cherry's
# cards, once their art is in, have no name under them, and Banana's,
# which isn't hidden, keeps its
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" layout-hero-rail >/dev/null || fail "building layout-hero-rail"
build_plugin hider 'id=hider\nname=Hider\napi=1\n'
HIDE_DIR="$CARD/Hider"
mkdir -p "$HIDE_DIR"
cp "$ROOT/tests/plugins/peeker/art.png" "$HIDE_DIR/art.png"
printf 'layout-hero-rail=on\nhider=on\nslot.layout=layout-hero-rail\n' >"$STATE/plugins.txt"
run_launcher "$WORK/hero-rail-hidden.log" "plugin hider: row Apple selected 1 hidden 1 named 0" \
	"plugin hider: row Banana selected 0 hidden 0 named 1" "plugin hider: row Cherry selected 0 hidden 1 named 0" \
	|| fail "the launcher exited early with hider and layout-hero-rail"
log="$WORK/hero-rail-hidden.log"
grep -qF "plugins: layout-hero-rail draws the game list" "$log" || fail "layout-hero-rail didn't take the game list"
grep -qF "plugin hider: row Banana selected 0 hidden 1" "$log" && fail "Banana's name was hidden"
grep -qE "plugin hider: row Banana .* named 0" "$log" && fail "Banana's card has no name: $(grep "plugin hider: row Banana" "$log")"
rm -rf "$WORK/plugins/layout-hero-rail.plugin" "$WORK/plugins/hider.plugin" "$HIDE_DIR" "$STATE/plugins.txt"

# the hero, which dimmer reads back from each push's old frame: Art's
# backdrop, red on the left and blue on the right, tinted toward the
# theme's accent across the top and fading into the theme's background
# colour above the cards, with its logo from art-boxart's wheel folder,
# green, faded in at the bottom left once the selection settled; Shot's
# screenshot, green; and Bare, with no art, keeps stock's background
DIM_DIR="$WORK/dimmer"
mkdir -p "$DIM_DIR" "$CARD/Dimmer/.media/wheel"
cp "$ROOT/tests/plugins/drawer/art.png" "$DIM_DIR/backdrop.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$DIM_DIR/shot.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$CARD/Dimmer/.media/wheel/Art.png"
make -s -C "$ROOT/plugins" layout-hero-rail art-boxart ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building layout-hero-rail"
build_plugin dimmer 'id=dimmer\nname=Dimmer\napi=1\nslots=transition\n'
printf 'backdrop=%s\nshot=%s\nprobe=173,289\n' "$DIM_DIR/backdrop.png" "$DIM_DIR/shot.png" >"$STATE/dimmer.cfg"
printf 'dimmer=on\nart-boxart=on\nlayout-hero-rail=on\nslot.layout=layout-hero-rail\n' >"$STATE/plugins.txt"
run_launcher "$WORK/hero-rail-hero.log" "plugin dimmer: done" || fail "the launcher exited early with layout-hero-rail's hero"
log="$WORK/hero-rail-hero.log"
plugin=dimmer
grep -qF "plugins: layout-hero-rail draws the game list" "$log" || fail "layout-hero-rail didn't take the game list"
theme=$(sed -n 's/.*plugin dimmer: from Art .*, theme \([0-9A-F]*\)$/\1/p' "$log")
[ -n "$theme" ] || fail "dimmer didn't read Art's hero"
grep -qE "plugin dimmer: from Art top [D-F][0-9A-F]0[0-9A-F]{3}FF quarter [D-F][0-9A-F]0[0-9A-F]{3}FF fade [0-3][0-9A-F]0[0-9A-F]{3}FF low ${theme}FF right [0-3][0-9A-F]0[0-9A-F][C-F][0-9A-F]FF, " "$log" \
	|| fail "Art's hero isn't its tinted backdrop fading out: $(grep "plugin dimmer: from Art" "$log")"
expect "probe Art 00FF00FF"
grep -qE "plugin dimmer: from Shot top [0-3][0-9A-F][C-F][0-9A-F][0-3][0-9A-F]FF quarter [0-3][0-9A-F][C-F][0-9A-F][0-3][0-9A-F]FF fade [0-3][0-9A-F]{5}FF low ${theme}FF " "$log" \
	|| fail "Shot's hero isn't its screenshot: $(grep "plugin dimmer: from Shot" "$log")"
grep -qF "plugin dimmer: probe Shot 00FF00FF" "$log" && fail "Shot has a logo"
expect "from Bare top ${theme}FF quarter ${theme}FF fade ${theme}FF low ${theme}FF right ${theme}FF, theme $theme"
grep -qF "plugins: layout-hero-rail's" "$log" && fail "layout-hero-rail's layout was turned off"
rm -rf "$WORK/plugins/layout-hero-rail.plugin" "$WORK/plugins/art-boxart.plugin" "$WORK/plugins/dimmer.plugin" "$DIM_DIR" \
	"$CARD/Dimmer" "$STATE/dimmer.cfg" "$STATE/plugins.txt"

# filterer's input_filter runs every frame, before its tick; the desktop
# has no hold switch; the new buttons follow the d-pad; and bind_button and
# override_button refuse them
build_plugin filterer 'id=filterer\nname=Filterer\napi=1\n'
build_plugin sifter 'id=sifter\nname=Sifter\napi=1\n'
mkdir -p "$STATE"
printf 'filterer=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/filterer.log" "plugin filterer: done" || fail "the launcher exited early with filterer loaded"
log="$WORK/filterer.log"
plugin=filterer
expect "hold_switch -1"
expect "buttons MENU 14 POWER 15 FN1 16 FN2 17 FN3 18"
expect "bind MENU -1 POWER -1 FN1 -1 FN3 -1 UP -1"
expect "override MENU -1 POWER -1 FN2 -1"
grep -qE "plugin filterer: done: .*, calls [1-9][0-9]+, missed 0, doubled 0, sleeps 0$" "$log" \
	|| fail "filterer's filter didn't run once a frame before its tick: $(grep "plugin filterer: done" "$log")"
grep -qF "plugin filterer: input size" "$log" && fail "filterer's input has the wrong size"

# moved <log>: 1 when filterer's done line has the selection moving
moved() {
	sed -n 's/.*plugin filterer: done: index \([0-9-]*\) to \([0-9-]*\),.*/\1 \2/p' "$1" | {
		read -r before after
		[ -n "$before" ] && [ "$before" != "$after" ]
	}
}

# on the game list, down moves the selection, and sifter, after filterer
# in load order, sees the press
printf 'filterer=on\nsifter=on\n' >"$STATE/plugins.txt"
printf 'press=down\nhold=100\n' >"$STATE/filterer.cfg"
run_launcher "$WORK/filterer-down.log" "plugin filterer: done" || fail "the launcher exited early pressing down"
log="$WORK/filterer-down.log"
expect "filter pressed 800 held 800"
expect "filter released 800"
moved "$log" || fail "down didn't move the selection: $(grep "plugin filterer: done" "$log")"
grep -qF "plugin sifter: sees pressed 800 held 800" "$log" || fail "sifter didn't see down pressed"

# taken by filterer, it doesn't move, and sifter sees it held but not pressed
printf 'press=down\nhold=100\ntake=down\n' >"$STATE/filterer.cfg"
run_launcher "$WORK/filterer-take-down.log" "plugin filterer: done" || fail "the launcher exited early taking down"
log="$WORK/filterer-take-down.log"
expect "filter pressed 800 held 800"
moved "$log" && fail "down moved the selection while taken: $(grep "plugin filterer: done" "$log")"
grep -qF "plugin sifter: sees pressed 0 held 800" "$log" || fail "sifter didn't see down held without the press"
grep -qF "plugin sifter: sees pressed 800" "$log" && fail "sifter saw a press filterer took"

# a filter's error turns it off for the run: it takes down only until then
printf 'press=down\nhold=100\ntake=down\nfail=1\n' >"$STATE/filterer.cfg"
printf 'filterer=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/filterer-fail.log" "plugin filterer: done" || fail "the launcher exited early with filterer failing"
log="$WORK/filterer-fail.log"
[ "$(grep -cF "plugins: filterer's input_filter returned -1, no longer filtering input with it until the launcher restarts" "$log")" -eq 1 ] \
	|| fail "filterer's failing filter wasn't logged once"
grep -qE "plugin filterer: done: .*, calls 3, " "$log" || fail "filterer's filter was called after its error: $(grep "plugin filterer: done" "$log")"
moved "$log" || fail "down didn't move the selection after the filter's error"

# on the quick menu and the game switcher (Settings' start screen), B goes
# back to the game list, which then draws its rows, unless filterer takes it
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
for view in 1 2; do
	printf 'defaultView=%s\n' "$view" >"$SETTINGS"
	printf 'press=b\nhold=100\n' >"$STATE/filterer.cfg"
	SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/filterer-view$view.log" "plugin filterer: done" \
		|| fail "the launcher exited early starting on screen $view"
	log="$WORK/filterer-view$view.log"
	expect "filter pressed 2 held 2"
	grep -qE "plugin filterer: done: .*, rows [1-9]" "$log" || fail "B didn't leave screen $view: $(grep "plugin filterer: done" "$log")"
	printf 'press=b\nhold=100\ntake=b\n' >"$STATE/filterer.cfg"
	SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/filterer-view$view-take.log" "plugin filterer: done" \
		|| fail "the launcher exited early starting on screen $view taking B"
	log="$WORK/filterer-view$view-take.log"
	expect "filter pressed 2 held 2"
	grep -qE "plugin filterer: done: .*, rows 0, " "$log" || fail "B left screen $view while taken: $(grep "plugin filterer: done" "$log")"
done
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -f "$WORK/minuisettings.txt.saved"

# in a confirm dialog, A says yes, unless filterer takes it
printf 'press=a\nhold=100\nconfirm=1\n' >"$STATE/filterer.cfg"
run_launcher "$WORK/filterer-confirm.log" "plugin filterer: done" || fail "the launcher exited early with a dialog"
log="$WORK/filterer-confirm.log"
expect "filter pressed 1 held 1"
grep -qE "plugin filterer: done: .*, yes 1, " "$log" || fail "A didn't say yes: $(grep "plugin filterer: done" "$log")"
printf 'press=a\nhold=100\nconfirm=1\ntake=a\n' >"$STATE/filterer.cfg"
run_launcher "$WORK/filterer-confirm-take.log" "plugin filterer: done" || fail "the launcher exited early with a dialog taking A"
log="$WORK/filterer-confirm-take.log"
grep -qE "plugin filterer: done: .*, yes 0, " "$log" || fail "A said yes while taken: $(grep "plugin filterer: done" "$log")"

# a power press filterer takes doesn't sleep
printf 'press=power\nhold=200\ntake=all\n' >"$STATE/filterer.cfg"
run_launcher "$WORK/filterer-tap.log" "plugin filterer: done" || fail "the launcher exited early with a taken power tap"
log="$WORK/filterer-tap.log"
expect "filter pressed 8000 held 8000"
grep -qE "plugin filterer: done: .*, sleeps 0$" "$log" || fail "a taken power tap slept: $(grep "plugin filterer: done" "$log")"
grep -qF "Entering hybrid sleep" "$log" && fail "a taken power tap slept"
grep -qF "before_poweroff" "$log" && fail "a taken power tap powered off"

# but held for a second it powers off all the same, through the host
printf 'press=power\ntake=all\n' >"$STATE/filterer.cfg"
run_launcher "$WORK/filterer-hold.log" "plugin filterer: before_poweroff"
log="$WORK/filterer-hold.log"
expect "before_poweroff, held a second 1"
grep -qF "plugins: power held through an input filter, powering off" "$log" || fail "the host didn't power off for a taken long press"
grep -qF "plugin filterer: before_sleep" "$log" && fail "a taken long press ran before_sleep"

# and with nothing taken, stock powers off for it itself
printf 'press=power\n' >"$STATE/filterer.cfg"
run_launcher "$WORK/filterer-hold-stock.log" "plugin filterer: before_poweroff"
log="$WORK/filterer-hold-stock.log"
expect "before_poweroff, held a second 1"
grep -qF "plugins: power held through an input filter" "$log" && fail "the host powered off for a press stock had"
rm -rf "$WORK/plugins/filterer.plugin" "$WORK/plugins/sifter.plugin" "$STATE/filterer.cfg" "$STATE/plugins.txt"

# the bundled hold-lock: holding SELECT and START locks every button, so
# locker's down doesn't move the selection and its own filter, after
# hold-lock's, sees no presses; holding them again unlocks, and the
# chord's releases don't reach the game list, so the next down moves it.
# Desktop has no hold switch, which hold-lock logs once
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" hold-lock >/dev/null || fail "building hold-lock"
build_plugin locker 'id=locker\nname=Locker\napi=1\n'
printf 'hold-lock=on\nlocker=on\n' >"$STATE/plugins.txt"
printf 'switch=on\n' >"$STATE/hold-lock.cfg"
run_launcher "$WORK/hold-lock.log" "plugin locker: done" || fail "the launcher exited early with hold-lock loaded"
log="$WORK/hold-lock.log"
plugin=hold-lock
[ "$(grep -cF "plugin hold-lock: unsupported on desktop: no hold switch, so only the chord locks" "$log")" -eq 1 ] \
	|| fail "hold-lock didn't log the missing hold switch once"
expect "locked by the chord"
expect "unlocked by the chord"
grep -qF "plugin hold-lock: locked by the switch" "$log" && fail "hold-lock locked by a switch desktop doesn't have"
indexes=$(sed -n 's/.*plugin locker: done: index \([0-9-]*\) \([0-9-]*\) \([0-9-]*\),.*/\1 \2 \3/p' "$log")
# shellcheck disable=SC2086
set -- $indexes
if [ "$#" -ne 3 ]; then
	fail "locker didn't finish: $(grep "plugin locker: done" "$log")"
elif [ "$2" -ne "$1" ]; then
	fail "down moved the selection while locked: $indexes"
elif [ "$3" -ne $(($1 + 1)) ]; then
	fail "down didn't move the selection once unlocked: $indexes"
fi
grep -qE "plugin locker: done: .*, locked presses 0, after presses [1-9][0-9]*, leaked 0$" "$log" \
	|| fail "hold-lock let presses through while locked, or the chord's releases after: $(grep "plugin locker: done" "$log")"

# while locked, the padlock shows left of the status pill in the theme's
# colours, which spotter, after it in load order, reads back
build_plugin spotter 'id=spotter\nname=Spotter\napi=1\n'
printf 'hold-lock=on\nlocker=on\nspotter=on\n' >"$STATE/plugins.txt"
printf 'unlock=0\n' >"$STATE/locker.cfg"
rm -f "$STATE/hold-lock.cfg"
run_launcher "$WORK/hold-lock-widget.log" "plugin locker: done" "plugin hold-lock: padlock at " \
	|| fail "the launcher exited early with hold-lock's padlock"
log="$WORK/hold-lock-widget.log"
grep -qE "plugin hold-lock: padlock at [0-9]+,15 90x90$" "$log" \
	|| fail "hold-lock's padlock is wrong: $(grep "plugin hold-lock: padlock" "$log")"
grep -qF "plugin hold-lock: unlocked" "$log" && fail "hold-lock unlocked without the chord"
hl_x=$(sed -n 's/.*plugin hold-lock: padlock at \([0-9]*\),.*/\1/p' "$log" | tail -n 1)
runs=$(grep "plugin spotter: runs " "$log" | tail -n 1 | sed 's/.*: runs //; s/,.*//; s/-/ /g')
# shellcheck disable=SC2086
set -- $runs
if [ -z "$hl_x" ] || [ "$#" -lt 4 ]; then
	fail "spotter didn't see hold-lock's padlock and the status pill: $(grep "plugin spotter: runs" "$log" | tail -n 1)"
elif [ $(($1 - hl_x)) -gt 2 ] || [ $((hl_x - $1)) -gt 2 ] || [ $(($2 - hl_x - 90)) -gt 2 ] || [ $((hl_x + 90 - $2)) -gt 2 ]; then
	fail "hold-lock didn't draw at $hl_x, 90 wide: spotter saw $runs"
fi
grep "plugin spotter: runs " "$log" | tail -n 1 | grep -qF ", accent 1 hint 1" \
	|| fail "hold-lock's padlock isn't in the theme's colours: $(grep "plugin spotter: runs" "$log" | tail -n 1)"
grep -qF "plugins: hold-lock's" "$log" && fail "hold-lock's widget or filter was turned off"
rm -rf "$WORK/plugins/hold-lock.plugin" "$WORK/plugins/locker.plugin" "$WORK/plugins/spotter.plugin" \
	"$STATE/hold-lock.cfg" "$STATE/locker.cfg" "$STATE/plugins.txt"

# the bundled video-previews plays a game's .videos clip a second (its
# delay setting) after the selection settles, in place of the thumbnail,
# with its sound through SDL's dummy driver, and stops when the selection
# moves, going round again at each clip's end: Alpha's half-second clip
# has sound, Bravo's has none, Charlie's isn't a clip and Delta has none.
# clipper walks the selection down the folder. Under a plugin layout that
# asks for it, the layout draws the clip in its own art spot.
# Built without ffmpeg's libraries, it only says it's unavailable; the
# clips are made with the ffmpeg command, and without it the cases that
# play them are skipped
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" video-previews >/dev/null || fail "building video-previews"
VP_DIR="$CARD/Roms/Video Previews (VPT)"
mkdir -p "$VP_DIR/.videos" "$STATE"
for game in Alpha Bravo Charlie Delta; do : >"$VP_DIR/$game.gb"; done
printf 'not a clip\n' >"$VP_DIR/.videos/Charlie.mp4"
build_plugin clipper 'id=clipper\nname=Clipper\napi=1\n'
printf 'folder=Roms/Video Previews (VPT)\nstep=3000\n' >"$STATE/clipper.cfg"
printf 'delay=1\n' >"$STATE/video-previews.cfg"
printf 'video-previews=on\nclipper=on\n' >"$STATE/plugins.txt"
vp_clips=0
if ! pkg-config --exists libavformat libavcodec libavutil 2>/dev/null; then
	SDL_AUDIODRIVER=dummy run_launcher "$WORK/video-previews.log" "plugin video-previews: unavailable" \
		|| fail "the launcher exited early with video-previews loaded"
	log="$WORK/video-previews.log"
	plugin=video-previews
	expect "unavailable: built without ffmpeg, so no clip plays"
elif ! command -v ffmpeg >/dev/null; then
	echo "note: skipping video-previews' clips: no ffmpeg command to make them with" >&2
elif ffmpeg -v error -y -f lavfi -i "testsrc=size=64x48:rate=30" -f lavfi -i "sine=frequency=440:sample_rate=48000" -t 0.5 \
	-c:v libx264 -pix_fmt yuv420p -c:a aac -ac 2 -shortest "$VP_DIR/.videos/Alpha.mp4" </dev/null \
	&& ffmpeg -v error -y -f lavfi -i "testsrc=size=64x48:rate=30" -t 0.5 -c:v libx264 -pix_fmt yuv420p -an \
		"$VP_DIR/.videos/Bravo.mp4" </dev/null; then
	vp_clips=1
else
	echo "note: skipping video-previews' clips: the ffmpeg command couldn't make them" >&2
fi
if [ "$vp_clips" -eq 1 ]; then
	vp_wait=$WAIT
	WAIT=$((WAIT + 10))
	SDL_AUDIODRIVER=dummy run_launcher "$WORK/video-previews.log" "plugin clipper: done" \
		|| fail "the launcher exited early with video-previews loaded"
	WAIT=$vp_wait
	log="$WORK/video-previews.log"
	plugin=video-previews
	expect "clips play 1000 ms after the selection settles, with sound"
	expect "loaded ffmpeg "
	expect "playing Alpha.mp4: 64x48, with sound"
	grep -qF "plugins: video-previews opened audio at 48000 Hz with 2 channels (dummy)" "$log" || fail "video-previews didn't open audio"
	expect "Bravo.mp4 has no sound"
	expect "playing Bravo.mp4: 64x48, silent"
	expect "can't open Charlie.mp4"
	for game in Alpha Bravo; do
		frames=$(sed -n "s|.*plugin video-previews: stopped $VP_DIR/$game.gb after showing \([0-9]*\) frames.*|\1|p" "$log")
		if [ -z "$frames" ]; then
			fail "video-previews didn't stop $game's clip when the selection moved"
		elif [ "$frames" -lt 5 ]; then
			fail "video-previews showed only $frames frames of $game's clip"
		fi
		passes=$(sed -n "s|.*plugin video-previews: stopped $game.mp4 after [0-9]* frames, \([0-9]*\) passes.*|\1|p" "$log")
		[ "${passes:-0}" -ge 2 ] || fail "video-previews didn't go round $game's clip again: ${passes:-no} passes"
	done
	grep -qF "plugin video-previews: stopped $VP_DIR/Charlie.gb" "$log" && fail "video-previews played Charlie's broken clip"
	grep -F "plugin video-previews: " "$log" | grep -qF "Delta" && fail "video-previews played a clip Delta doesn't have"
	grep -qF "plugins: video-previews's" "$log" && fail "video-previews' hooks were turned off"
	grep -qF "art_replace refused" "$log" && fail "art_replace refused video-previews on the game list"

	# under a plugin layout that asks for it, the layout draws the clip in
	# its own art spot: gridder with swap=1 draws it in the selected cell
	mkdir -p "$CARD/Gridder/.videos"
	cp "$VP_DIR/.videos/Alpha.mp4" "$CARD/Gridder/.videos/Apple.mp4"
	cp "$VP_DIR/.videos/Alpha.mp4" "$CARD/Gridder/.videos/Banana.mp4"
	build_plugin gridder 'id=gridder\nname=Gridder\napi=1\nslots=layout\n'
	printf 'swap=1\n' >"$STATE/gridder.cfg"
	printf 'gridder=on\nvideo-previews=on\n' >"$STATE/plugins.txt"
	SDL_AUDIODRIVER=dummy run_launcher "$WORK/video-previews-grid.log" "plugin gridder: renders" \
		"plugin video-previews: playing " || fail "the launcher exited early with video-previews under gridder"
	log="$WORK/video-previews-grid.log"
	grep -qE "plugin video-previews: playing (Alpha|Apple|Banana).mp4: 64x48, with sound" "$log" \
		|| fail "video-previews didn't play under a layout: $(grep "plugin video-previews: " "$log")"
	grep -qE "plugin gridder: render replacement [A-Za-z]+ 1 " "$log" \
		|| fail "gridder didn't draw video-previews' clip: $(grep "plugin gridder: render replacement" "$log")"
	grep -qF "art_replace refused" "$log" && fail "art_replace refused video-previews under a layout that shows it"
	grep -qF "plugins: gridder's" "$log" && fail "gridder's layout was turned off with video-previews"
	rm -rf "$WORK/plugins/gridder.plugin" "$CARD/Gridder" "$STATE/gridder.cfg"
fi
rm -rf "$WORK/plugins/video-previews.plugin" "$WORK/plugins/clipper.plugin" "$VP_DIR" \
	"$STATE/video-previews.cfg" "$STATE/clipper.cfg" "$STATE/plugins.txt"

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

# the Launcher pak's Plugins page (nextui.elf --settings plugins), driven
# by presser, which presses buttons and writes down every text drawn, with
# the bundled playtime-limit as the only plugin: its settings are protected
# and its code secret. Without a PIN they show and change as usual, and the
# code shows only as Set; the PIN row sets the launcher's PIN; with it, the
# settings show Locked until it's typed, a wrong one changes nothing, and
# turning the plugin off asks for it too. Neither the PIN nor the code is
# ever drawn once set, or logged. Simple mode's old PIN becomes the
# launcher's.
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
PIN_PLUGINS="$WORK/pin-plugins"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$PIN_PLUGINS" CC="$CC" playtime-limit >/dev/null || fail "building playtime-limit"
LIMIT_CFG="$STATE/playtime-limit.cfg"
PIN_FILE="$STATE/pin.txt"
# typing 4826 from where the keyboard starts (q), then START
TYPE_PIN="up right right right a right right right right a left left left left left left a right right right right a start"

# plugins_page <name> <keys>: opens the Plugins page and presses keys; what
# it drew goes to $WORK/<name>.texts
plugins_page() {
	case_log="$WORK/$1.log"
	texts="$WORK/$1.texts"
	rm -f "$texts"
	: >"$texts"
	PRESSER_KEYS="$2" PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER" SMOKE_UNTIL="presser: done" \
		MOSAIC_LAUNCHER_PAK_DIR="$ROOT/pak" MOSAIC_LAUNCHER_PLUGIN_PATH="$PIN_PLUGINS" \
		"$ROOT/scripts/desktop-smoke.sh" "$case_log" 40 --settings plugins >/dev/null 2>"$WORK/smoke.err"
	status=$?
	[ "$status" -eq 0 ] || fail "the Plugins page didn't run its presses (status $status)"
	log="$case_log"
}

# drew <text>: the page drew exactly this text
drew() {
	grep -qxF -- "$1" "$texts" || fail "the page didn't show \"$1\""
}

# never_shown <text>: neither drawn nor logged, even as part of something
never_shown() {
	grep -qF -- "$1" "$texts" && fail "the page showed \"$1\""
	grep -qF -- "$1" "$log" && fail "\"$1\" was logged"
}

rm -f "$PIN_FILE" "$LIMIT_CFG" "$STATE/simple-mode.cfg"
printf 'playtime-limit=on\n' >"$STATE/plugins.txt"

# no PIN: the values show as usual, and the code is typed on a keyboard
# that starts empty
plugins_page pin-none "a down down down down a a right a right a right a right a start"
drew "Daily limit (minutes)"
drew "Not set"
drew "Set"
grep -qxF "Locked" "$texts" && fail "a setting showed Locked with no PIN set"
grep -qxF "code=qwert" "$LIMIT_CFG" 2>/dev/null || fail "the code wasn't saved: $(cat "$LIMIT_CFG" 2>/dev/null)"
grep -qF "qwert" "$log" && fail "the code was logged"

# once set, the code is never shown, even on its keyboard, which Y
# cancels; with no PIN, turning the plugin off needs nothing
printf 'limit=30\ncode=qwert\n' >"$LIMIT_CFG"
plugins_page pin-none-secret "a wait down down down down a y b right"
drew "30"
drew "Set"
never_shown "qwert"
grep -qxF "code=qwert" "$LIMIT_CFG" || fail "cancelling the code's keyboard changed it"
grep -qxF "playtime-limit=off" "$STATE/plugins.txt" || fail "playtime-limit wasn't turned off without a PIN"

# the PIN row sets the launcher's PIN
printf 'playtime-limit=on\n' >"$STATE/plugins.txt"
plugins_page pin-set "up a $TYPE_PIN a"
drew "Not set"
drew "New PIN, empty for none"
[ "$(cat "$PIN_FILE" 2>/dev/null)" = "4826" ] || fail "the PIN wasn't saved"
grep -qF "4826" "$log" && fail "the PIN was logged"

# with the PIN: Locked, a wrong PIN changes nothing, then the right one
# opens the limit's choices, and the code still shows only as Set
plugins_page pin-locked "a down a up a start a a $TYPE_PIN down a"
drew "Locked"
drew "Wrong PIN"
drew "*_"
drew "****_"
drew "Set"
never_shown "4826"
never_shown "qwert"
grep -qxF "limit=45" "$LIMIT_CFG" || fail "the limit didn't change after the PIN: $(grep limit "$LIMIT_CFG")"

# turning the plugin off asks for the PIN: Y cancels it, and it stays on
plugins_page pin-off-cancel "right y"
drew "_"
grep -qxF "playtime-limit=on" "$STATE/plugins.txt" || fail "playtime-limit was turned off without the PIN"
plugins_page pin-off "right $TYPE_PIN"
drew "****_"
never_shown "4826"
grep -qxF "playtime-limit=off" "$STATE/plugins.txt" || fail "playtime-limit wasn't turned off after the PIN"

# simple mode's old PIN, from its cfg, becomes the launcher's
rm -f "$PIN_FILE"
printf 'on=on\npin=2468\n' >"$STATE/simple-mode.cfg"
plugins_page pin-adopt "wait"
[ "$(cat "$PIN_FILE" 2>/dev/null)" = "2468" ] || fail "simple mode's PIN didn't move to the launcher's"
grep -qxF "pin=" "$STATE/simple-mode.cfg" || fail "simple mode's PIN was left in its cfg: $(cat "$STATE/simple-mode.cfg")"
grep -qF "simple mode's PIN is the launcher's PIN now" "$log" || fail "moving simple mode's PIN wasn't logged"
never_shown "2468"
rm -rf "$PIN_PLUGINS" "$PIN_FILE" "$LIMIT_CFG" "$STATE/simple-mode.cfg" "$STATE/plugins.txt"
case_log=""

# plugin screens: screener's Y action opens screener.main over the game
# list, sliding in as a push whose old frame has layerer's overlay over the
# list and whose new frame is the screen before overlays; B closes it, a
# pop whose old frame has the overlay over the screen
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
build_plugin layerer 'id=layerer\nname=Layerer\napi=1\n'
mkdir -p "$STATE"
printf 'screener=on\nlayerer=on\n' >"$STATE/plugins.txt"
printf 'steps=y,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/screener.log" "plugin screener: done" || fail "the launcher exited early with screener loaded"
log="$WORK/screener.log"
plugin=screener
expect "refused dup -1 builtin -1 empty -1 no render -1 open -1 close -1"
expect "action open 0"
expect "enter screener.main over gamelist"
expect "now screener.main"
expect "transition push old FF00FFFF new 0000FFFF"
grep -qE "plugin screener: render screener.main ([0-9]+)x([0-9]+) area \1x\2 status [1-9][0-9]* drawn 1 current screener.main open -1 select -1$" "$log" \
	|| fail "screener.main wasn't lent the screen with its header: $(grep "plugin screener: render" "$log")"
expect "input screener.main pressed 2"
expect "exit screener.main"
expect "screener.main drew every frame"
expect "transition pop old FF00FFFF"
[ "$(grep -cF "plugin screener: now gamelist" "$log")" -eq 2 ] || fail "B didn't go back to the game list"
grep -qF "plugins: closed screen screener.main" "$log" || fail "closing screener.main wasn't logged"
plugin=layerer
expect "over gamelist clear 1 size 1 1 select -1 open -1"
expect "over screener.main clear 1 size 1 1 select -1 open -1"

# open_screen opens screens over each other, one at a time; close_screen
# closes the ones above too, top first; registering after init is refused;
# and without an input hook, B closes a screen
printf 'steps=open:screener.bare,open:screener.bare,open:screener.main,close:screener.bare,register,open:screener.bare,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/screener-stack.log" "plugin screener: done" || fail "the launcher exited early stacking screens"
log="$WORK/screener-stack.log"
plugin=screener
expect "open screener.bare 0"
expect "enter screener.bare over gamelist"
expect "open screener.bare -1"
expect "enter screener.main over screener.bare"
expect "open screener.main 0"
expect "now screener.main"
expect "close screener.bare 0"
main_at=$(grep -nF "plugin screener: exit screener.main" "$log" | head -1 | cut -d: -f1)
bare_at=$(grep -nF "plugin screener: exit screener.bare" "$log" | head -1 | cut -d: -f1)
if [ -z "$main_at" ] || [ -z "$bare_at" ] || [ "$main_at" -gt "$bare_at" ]; then
	fail "close_screen didn't close the top screen first"
fi
expect "register from tick -1"
[ "$(grep -cF "plugin screener: exit screener.bare" "$log")" -eq 2 ] || fail "B didn't close screener.bare"
[ "$(grep -cF "plugin screener: now gamelist" "$log")" -eq 3 ] || fail "screener didn't see the game list again after each close"

# a dialog opened from a screen's input shows over it and goes back to it;
# no screen opens while it shows, and overlays leave it alone
printf 'steps=open:screener.main,a,open:screener.bare,a,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/screener-dialog.log" "plugin screener: done" || fail "the launcher exited early with a dialog over a screen"
log="$WORK/screener-dialog.log"
expect "now dialog"
expect "open screener.bare -1"
expect "yes, now screener.main"
expect "exit screener.main"
grep -qF "plugin layerer: over dialog" "$log" && fail "layerer's overlay was drawn over a dialog"

# a MOSAIC_SCREEN_FULL screen has no header and no overlays
printf 'steps=open:screener.full,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/screener-full.log" "plugin screener: done" || fail "the launcher exited early with a full screen"
log="$WORK/screener-full.log"
grep -qE "plugin screener: render screener.full [0-9]+x[0-9]+ area [0-9]+x[0-9]+ status 0 drawn 0 " "$log" \
	|| fail "screener.full had a header: $(grep "plugin screener: render" "$log")"
expect "transition pop old 0000FFFF"
grep -qF "plugin layerer: over screener.full" "$log" && fail "layerer's overlay was drawn over a full screen"

# button hints on plugin screens: draw_hints draws stock's pills where
# stock's own screens put them, with the labels presser writes down, and
# only on the screen lent to a screen's render
# hinted <name> <hints> <steps>: screener with that hints setting, its
# drawn text in $texts
hinted() {
	texts="$WORK/$1.texts"
	: >"$texts"
	printf 'hints=%s\nsteps=%s\n' "$2" "$3" >"$STATE/screener.cfg"
	PRESSER_LOG="$texts"
	export PRESSER_LOG
	SMOKE_PRELOAD="$PRESSER"
	export SMOKE_PRELOAD
	run_launcher "$WORK/$1.log" "plugin screener: done" || fail "the launcher exited early drawing hints ($2)"
	unset PRESSER_LOG SMOKE_PRELOAD
	log="$WORK/$1.log"
	plugin=screener
}
# hints_line <hints> <screen> <left rc> <right rc>: the first render's line,
# with the pills where stock puts them, in its accent colour and no
# further; the calls it refuses; and no render with a group refused
hints_line() {
	grep -qE "plugin screener: hints $1 on $2 left $3 [0-9-]+,[0-9-]+ [0-9]+x[0-9]+ right $4 [0-9-]+,[0-9-]+ [0-9]+x[0-9]+ stock 1 " "$log" \
		|| fail "screener's $1 hints on $2 weren't where stock puts them: $(grep "plugin screener: hints $1" "$log")"
	grep -qE "plugin screener: hints $1 .* pill ([0-9A-F]{8}) accent \1 outside clear 1$" "$log" \
		|| fail "screener's $1 pill wasn't the accent colour, or went past its edge: $(grep "plugin screener: hints $1" "$log")"
	expect "hints refused again -1 wide -1 own -1 group -1 none -1 three -1 dpad -1 fn -1 null -1 empty -1"
	grep -qE "plugin screener: hints drawn [1-9][0-9]* refused 0$" "$log" \
		|| fail "screener's $1 hints were refused: $(grep "plugin screener: hints drawn" "$log")"
}
hinted hints-none "" "open:screener.main,b"
grep -qxF "REMOVE" "$texts" && fail "something drew REMOVE without screener's hints"
grep -qF "plugin screener: hints" "$log" && fail "screener drew hints without its hints setting"
hinted hints-switcher switcher "open:screener.main,b"
hints_line switcher screener.main 0 0
expect "hints from tick -1"
expect "hints on the overlay over gamelist -1"
for label in BACK REMOVE RESUME; do drew "$label"; done
expect "exit screener.main"
# a full screen, and the right group alone
hinted hints-full open "open:screener.full,b"
hints_line open screener.full 0 0
hinted hints-back back "open:screener.bare,b"
hints_line back screener.bare 1 0
# every button stock names, from a different set each frame
hinted hints-names names "open:screener.main,b"
hints_line names screener.main 0 0
for label in L1 R1 L2 R2 START SELECT INFO SLEEP DONE MORE PREV NEXT UP DOWN SORT FIND PICK; do drew "$label"; done
rm -f "$PRESSER"

# a screen whose render or input fails closes, back to the game list, and
# can't open again until the launcher restarts
for hook in render input; do
	printf 'steps=open:screener.fail,b,open:screener.fail\nfail=%s\n' "$hook" >"$STATE/screener.cfg"
	run_launcher "$WORK/screener-fail-$hook.log" "plugin screener: done" || fail "the launcher exited early with a failing $hook"
	log="$WORK/screener-fail-$hook.log"
	grep -qE "plugins: screener's screen screener.fail's $hook returned -[23], closing it until the launcher restarts" "$log" \
		|| fail "screener.fail's $hook error wasn't logged"
	expect "exit screener.fail"
	expect "open screener.fail -1"
	[ "$(grep -cF "plugin screener: now gamelist" "$log")" -eq 2 ] || fail "screener.fail's $hook error didn't go back to the game list"
done

# from the quick menu and the game switcher (Settings' start screen), a
# screen goes back to where it opened
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
for view in 1 2; do
	name=switcher
	[ "$view" -eq 2 ] && name=quickmenu
	printf 'defaultView=%s\n' "$view" >"$SETTINGS"
	printf 'steps=open:screener.bare,b\n' >"$STATE/screener.cfg"
	SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/screener-view$view.log" "plugin screener: done" \
		|| fail "the launcher exited early opening a screen over the $name"
	log="$WORK/screener-view$view.log"
	expect "enter screener.bare over $name"
	expect "exit screener.bare"
	[ "$(grep -cF "plugin screener: now $name" "$log")" -eq 2 ] || fail "closing screener.bare didn't go back to the $name"
	grep -qF "plugin layerer: over $name" "$log" || fail "layerer's overlay wasn't drawn over the $name"
done
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -f "$WORK/minuisettings.txt.saved"

# an overlay that fails is left out from then on
printf 'steps=open:screener.bare,b\n' >"$STATE/screener.cfg"
printf 'fail=1\n' >"$STATE/layerer.cfg"
run_launcher "$WORK/layerer-fail.log" "plugin screener: done" || fail "the launcher exited early with a failing overlay"
log="$WORK/layerer-fail.log"
[ "$(grep -cF "plugins: layerer's overlay_render returned -1, leaving its overlay out until the launcher restarts" "$log")" -eq 1 ] \
	|| fail "layerer's failing overlay wasn't logged once"
rm -f "$STATE/layerer.cfg"

# the home slot: a screen marked home without slots=home is an ordinary one
HOME_LEFT=/tmp/mosaic-launcher-home
[ -f "$HOME_LEFT" ] && cp "$HOME_LEFT" "$WORK/home-left.saved"
rm -f "$HOME_LEFT"
printf 'home=main\nsteps=b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/home-noslot.log" "plugin screener: done" || fail "the launcher exited early with a home screen but no slot"
log="$WORK/home-noslot.log"
grep -qF "plugins: screener's screen screener.main is marked home but screener has no slots=home, so it's an ordinary screen" "$log" \
	|| fail "the home flag without the slot wasn't logged"
grep -qF "plugin screener: enter screener.main" "$log" && fail "screener.main opened as home without the slot"
[ -f "$HOME_LEFT" ] && fail "the home file was written without a home screen"

# the slot with no screen marked home keeps the root list
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition,home\n'
printf 'steps=b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/home-none.log" "plugin screener: done" || fail "the launcher exited early owning the home slot with no home screen"
log="$WORK/home-none.log"
grep -qF "plugins: screener owns the home slot but registered no MOSAIC_SCREEN_HOME screen, using the root list" "$log" \
	|| fail "the home slot with no home screen wasn't logged"
grep -qF "plugin screener: enter screener.main" "$log" && fail "a screen opened as home with none marked"

# the home screen shows first, with no slide; closing it goes on to the
# root list as a push, B at the root goes back to it as a pop, and closing
# it again pushes the list back in
printf 'home=main\nsteps=b,b,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/home.log" "plugin screener: done" || fail "the launcher exited early with a home screen"
log="$WORK/home.log"
plugin=screener
grep -qF "plugins: screener's screen screener.main is the home screen" "$log" || fail "the home screen wasn't claimed"
[ "$(sed -n 's/.*plugin screener: now //p' "$log" | head -1)" = "screener.main" ] || fail "the launcher didn't start on the home screen"
expect "enter screener.main over gamelist"
[ "$(grep -cF "plugin screener: enter screener.main over gamelist" "$log")" -eq 2 ] || fail "B at the root didn't go back to the home screen"
[ "$(grep -cF "plugin screener: now gamelist" "$log")" -eq 2 ] || fail "closing the home screen didn't show the root list twice"
[ "$(sed -n 's/.*plugin screener: transition \([a-z]*\) .*/\1/p' "$log" | tr '\n' ' ')" = "push pop push " ] \
	|| fail "home's slides weren't push, pop, push: $(grep "plugin screener: transition" "$log")"
expect "run from init -1"

# run_action opens the quick menu and the game switcher from the home
# screen, closing it, and B on them goes back to it; unknown and
# unavailable actions are refused
printf 'home=main\nsteps=action:nope,action:gamelist.back,action:gamelist.quick_menu,b,action:gamelist.switcher,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/home-actions.log" "plugin screener: done" || fail "the launcher exited early running actions from the home screen"
log="$WORK/home-actions.log"
expect "run nope -1"
expect "run gamelist.back -1"
expect "run gamelist.quick_menu 0"
expect "run gamelist.switcher 0"
[ "$(sed -n 's/.*plugin screener: now //p' "$log" | tr '\n' ' ')" = "screener.main quickmenu screener.main switcher screener.main " ] \
	|| fail "the quick menu and switcher didn't go back to the home screen: $(grep "plugin screener: now" "$log")"
[ "$(grep -cF "plugin screener: exit screener.main" "$log")" -ge 2 ] || fail "the quick menu and switcher didn't close the home screen"
grep -qF "plugins: closed screen screener.main, as the quickmenu opened over it" "$log" || fail "the quick menu over the home screen wasn't logged"

# a launch from the home screen comes back to it, and one from the list
# comes back to the list; /tmp/next and /tmp/last.txt are put back after
PAK="$WORK/home/Home.pak"
mkdir -p "$PAK"
: >"$PAK/launch.sh"
[ -f /tmp/next ] && cp /tmp/next "$WORK/home-next.saved"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/home-last.saved"
printf 'home=main\nsteps=pak:%s\n' "$PAK" >"$STATE/screener.cfg"
run_launcher "$WORK/home-launch.log" && fail "the launcher didn't quit to launch a pak from the home screen"
log="$WORK/home-launch.log"
expect "open pak 1"
[ "$(cat "$HOME_LEFT" 2>/dev/null)" = "home" ] || fail "a launch from the home screen didn't save home: $(cat "$HOME_LEFT" 2>/dev/null)"
printf 'home=main\nsteps=\n' >"$STATE/screener.cfg"
run_launcher "$WORK/home-return.log" "plugin screener: done" || fail "the launcher exited early back from the home screen's pak"
log="$WORK/home-return.log"
[ "$(sed -n 's/.*plugin screener: now //p' "$log" | head -1)" = "screener.main" ] || fail "back from a pak, the launcher didn't start on the home screen"
[ -f "$HOME_LEFT" ] && fail "the home file outlived the start it was for"
printf 'home=main\nsteps=b,pak:%s\n' "$PAK" >"$STATE/screener.cfg"
run_launcher "$WORK/home-list-launch.log" && fail "the launcher didn't quit to launch a pak from the list"
[ "$(cat "$HOME_LEFT" 2>/dev/null)" = "list" ] || fail "a launch from the list didn't save list: $(cat "$HOME_LEFT" 2>/dev/null)"
printf 'home=main\nsteps=\n' >"$STATE/screener.cfg"
run_launcher "$WORK/home-list-return.log" "plugin screener: done" || fail "the launcher exited early back from the list's pak"
log="$WORK/home-list-return.log"
[ "$(sed -n 's/.*plugin screener: now //p' "$log" | head -1)" = "gamelist" ] || fail "back from a pak launched from the list, the launcher didn't start on the list"
grep -qF "plugin screener: enter screener.main" "$log" && fail "the home screen opened back from a pak launched from the list"
if [ -f "$WORK/home-next.saved" ]; then cp "$WORK/home-next.saved" /tmp/next; else rm -f /tmp/next; fi
if [ -f "$WORK/home-last.saved" ]; then cp "$WORK/home-last.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
rm -rf "${WORK:?}/home" "$WORK/home-next.saved" "$WORK/home-last.saved"

# starting on the game switcher or the quick menu (Settings' start
# screen), B goes back to the home screen rather than the list
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
for view in 1 2; do
	name=switcher
	[ "$view" -eq 2 ] && name=quickmenu
	printf 'defaultView=%s\n' "$view" >"$SETTINGS"
	printf 'home=main\nsteps=b\n' >"$STATE/screener.cfg"
	SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/home-view$view.log" "plugin screener: done" \
		|| fail "the launcher exited early starting on the $name with a home screen"
	log="$WORK/home-view$view.log"
	grep -qF "plugins: the $name goes back to the home screen" "$log" || fail "the $name didn't note the home screen under it"
	[ "$(sed -n 's/.*plugin screener: now //p' "$log" | tr '\n' ' ')" = "$name screener.main " ] \
		|| fail "B on the $name didn't go to the home screen: $(grep "plugin screener: now" "$log")"
	expect "enter screener.main over gamelist"
done
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -f "$WORK/minuisettings.txt.saved"

# a home screen that fails leaves the root list for the rest of the run
printf 'home=fail\nfail=render\nsteps=b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/home-fail.log" "plugin screener: done" || fail "the launcher exited early with a failing home screen"
log="$WORK/home-fail.log"
grep -qF "plugins: using the root list in place of the home screen until the launcher restarts" "$log" \
	|| fail "the failing home screen's fallback wasn't logged"
expect "exit screener.fail"
[ "$(grep -cF "plugin screener: enter screener.fail" "$log")" -eq 1 ] || fail "B at the root opened the failed home screen again"

# the switcher slot: recents for it (one with a save state and its
# preview, one without, one whose emulator isn't installed), put back after
seen() { sed -n 's/.*plugin screener: now //p' "$log" | tr '\n' ' '; }
SWITCHED="$CARD/.userdata/shared/.minui/game_switcher.txt"
SW_ROMS="$CARD/Roms/Switcher Test (SWT)"
SW_GONE="$CARD/Roms/Switcher Gone (SWGONE)"
SW_EMU="$CARD/Emus/desktop/SWT.pak"
SW_STATES="$CARD/.userdata/shared/.minui/SWT"
mkdir -p "$SW_ROMS" "$SW_GONE" "$SW_EMU" "$SW_STATES"
: >"$SW_ROMS/Game.gb"
: >"$SW_ROMS/Other.gb"
: >"$SW_GONE/Gone.gb"
: >"$SW_EMU/launch.sh"
printf '2' >"$SW_STATES/Game.gb.txt"
: >"$SW_STATES/Game.gb.2.bmp"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/switcher-recent.saved"
for saved in /tmp/next /tmp/last.txt /tmp/resume_slot.txt "$SWITCHED"; do
	[ -f "$saved" ] && cp "$saved" "$WORK/switcher-$(basename "$saved").saved"
done
rm -f "$SWITCHED" "$HOME_LEFT"
sw_recents() {
	printf '/Roms/Switcher Test (SWT)/Game.gb\tSaved Game\n/Roms/Switcher Test (SWT)/Other.gb\n/Roms/Switcher Gone (SWGONE)/Gone.gb\n' >"$RECENTS"
}
sw_recents

# a screen marked switcher without slots=switcher is an ordinary one
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
printf 'switcher=bare\nsteps=action:gamelist.switcher,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-noslot.log" "plugin screener: done" || fail "the launcher exited early with a switcher screen but no slot"
log="$WORK/switcher-noslot.log"
grep -qF "plugins: screener's screen screener.bare is marked switcher but screener has no slots=switcher, so it's an ordinary screen" "$log" \
	|| fail "the switcher flag without the slot wasn't logged"
[ "$(seen)" = "gamelist switcher gamelist " ] || fail "the built-in switcher didn't show without the slot: $(seen)"

# the slot with no screen marked switcher keeps the built-in one
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition,switcher\n'
printf 'steps=action:gamelist.switcher,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-none.log" "plugin screener: done" || fail "the launcher exited early owning the switcher slot with no screen"
log="$WORK/switcher-none.log"
grep -qF "plugins: screener owns the switcher slot but registered no MOSAIC_SCREEN_GAME_SWITCHER screen other than its home screen, using the built-in switcher" "$log" \
	|| fail "the switcher slot with no switcher screen wasn't logged"
[ "$(seen)" = "gamelist switcher gamelist " ] || fail "the built-in switcher didn't show with no switcher screen: $(seen)"

# SELECT and gamelist.switcher open the switcher screen over the game
# list, at once; screens open over it and close back to it without
# sliding, open_screen can't open it, it reads the recents, and B goes
# back to the list
printf 'switcher=bare\nsteps=select,b,action:gamelist.switcher,open:screener.main,open:screener.bare,b,recents,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher.log" "plugin screener: done" || fail "the launcher exited early with a switcher screen"
log="$WORK/switcher.log"
plugin=screener
grep -qF "plugins: screener's screen screener.bare is the game switcher" "$log" || fail "the switcher screen wasn't claimed"
[ "$(seen)" = "gamelist screener.bare gamelist screener.bare screener.main screener.bare gamelist " ] \
	|| fail "the switcher screen didn't open and close as the switcher: $(seen)"
[ "$(grep -cF "plugin screener: enter screener.bare over gamelist" "$log")" -eq 2 ] || fail "the switcher screen didn't open over the game list twice"
expect "enter screener.main over screener.bare"
expect "open screener.bare -1"
grep -qF "plugins: can't open screen screener.bare: it's the game switcher" "$log" || fail "open_screen on the switcher screen wasn't refused"
grep -qF "plugin screener: transition" "$log" && fail "something slid to or from the switcher screen: $(grep "plugin screener: transition" "$log")"
expect "recents from init info -1 null -1 open 0"
expect "recents 3"
expect "recent 0 0 name Saved Game type 2 available 1 resume 1 preview Game.gb.2.bmp"
expect "recent 1 0 name Other type 2 available 1 resume 0 preview -"
expect "recent 2 0 name Gone type 2 available 0 resume 0 preview -"
expect "recent 3 -1 name  type 0 available 0 resume 0 preview -"
grep -qE "plugin screener: old size [0-9]+ name Saved Game type 2$" "$log" || fail "a smaller MosaicRecent wasn't filled as far as it goes"
grep -qF "plugin layerer: over screener.bare" "$log" || fail "layerer's overlay wasn't drawn over the switcher screen"

# remove takes a recent out; one whose emulator isn't installed doesn't open
printf 'switcher=bare\nsteps=action:gamelist.switcher,play:2:1,remove:1,recents,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-remove.log" "plugin screener: done" || fail "the launcher exited early removing a recent"
log="$WORK/switcher-remove.log"
expect "play 2 0"
expect "remove 1 0"
expect "recents 2"
expect "recent 1 0 name Gone type 2 available 0 resume 0 preview -"
[ "$(grep -c . "$RECENTS")" -eq 2 ] || fail "the removed recent wasn't saved: $(cat "$RECENTS")"
sw_recents

# a game resumed from the switcher screen comes back to it
printf 'switcher=bare\nsteps=action:gamelist.switcher,play:0:1\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-play.log" && fail "the launcher didn't quit to resume a recent"
log="$WORK/switcher-play.log"
expect "play 0 1"
grep -qE "SWT\.pak/launch\.sh.*Game\.gb" /tmp/next 2>/dev/null || fail "the recent didn't launch with its emulator: $(cat /tmp/next 2>/dev/null)"
[ "$(cat /tmp/resume_slot.txt 2>/dev/null)" = "2" ] || fail "the recent didn't resume its state's slot: $(cat /tmp/resume_slot.txt 2>/dev/null)"
[ -f "$SWITCHED" ] || fail "a launch from the switcher screen didn't save that it came from the switcher"
grep -qF "Saved Game" "$RECENTS" || fail "the recent lost its name: $(cat "$RECENTS")"
printf 'switcher=bare\nsteps=b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-return.log" "plugin screener: done" || fail "the launcher exited early back from a game from the switcher"
log="$WORK/switcher-return.log"
[ "$(seen)" = "screener.bare gamelist " ] || fail "back from a game, the launcher didn't start on the switcher screen: $(seen)"
expect "enter screener.bare over gamelist"
[ -f "$SWITCHED" ] && fail "the switcher file outlived the start it was for"

# fresh from the switcher screen, a game starts over; from the list, it
# doesn't come back to the switcher
printf 'switcher=bare\nsteps=action:gamelist.switcher,play:0:0\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-fresh.log" && fail "the launcher didn't quit to start a recent"
[ "$(cat /tmp/resume_slot.txt 2>/dev/null)" = "8" ] || fail "a fresh start resumed a state: $(cat /tmp/resume_slot.txt 2>/dev/null)"
rm -f "$SWITCHED"
printf 'switcher=bare\nsteps=play:1:1\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-list-play.log" && fail "the launcher didn't quit to open a recent from the list"
log="$WORK/switcher-list-play.log"
expect "play 1 1"
[ -f "$SWITCHED" ] && fail "a recent opened from the list came back to the switcher"
sw_recents

# Settings' start screen opens the switcher screen
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
printf 'defaultView=1\n' >"$SETTINGS"
printf 'switcher=bare\nsteps=b\n' >"$STATE/screener.cfg"
SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/switcher-view.log" "plugin screener: done" \
	|| fail "the launcher exited early starting on the switcher screen"
log="$WORK/switcher-view.log"
[ "$(seen)" = "screener.bare gamelist " ] || fail "the start screen wasn't the switcher screen: $(seen)"
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -f "$WORK/minuisettings.txt.saved"

# an enter that declines shows the built-in switcher that time
printf 'switcher=bare\ndecline=1\nsteps=action:gamelist.switcher,b,action:gamelist.switcher,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-decline.log" "plugin screener: done" || fail "the launcher exited early with a declining switcher screen"
log="$WORK/switcher-decline.log"
expect "declined screener.bare over gamelist"
[ "$(seen)" = "gamelist switcher gamelist screener.bare gamelist " ] || fail "a declined switcher screen didn't give way once: $(seen)"

# a switcher screen that fails gives way to the built-in switcher for the run
printf 'switcher=fail\nfail=render\nsteps=action:gamelist.switcher,b,action:gamelist.switcher,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-fail.log" "plugin screener: done" || fail "the launcher exited early with a failing switcher screen"
log="$WORK/switcher-fail.log"
grep -qF "plugins: using the built-in game switcher in place of screener.fail until the launcher restarts" "$log" \
	|| fail "the failing switcher screen's fallback wasn't logged"
expect "exit screener.fail"
[ "$(seen)" = "gamelist screener.fail switcher gamelist switcher gamelist " ] || fail "the failed switcher screen didn't give way: $(seen)"
[ "$(grep -cF "plugin screener: enter screener.fail" "$log")" -eq 1 ] || fail "the failed switcher screen opened again"

# with a home screen: the switcher screen over it closes it, and B, or the
# quick menu's B over the switcher screen, goes back to it
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition,home,switcher\n'
printf 'home=main\nswitcher=bare\nsteps=action:gamelist.switcher,b,action:gamelist.switcher,action:gamelist.quick_menu,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-home.log" "plugin screener: done" || fail "the launcher exited early with home and switcher screens"
log="$WORK/switcher-home.log"
[ "$(seen)" = "screener.main screener.bare screener.main screener.bare quickmenu screener.main " ] \
	|| fail "the switcher screen didn't go back to the home screen: $(seen)"
grep -qF "plugins: closed screen screener.main, as the switcher opened over it" "$log" || fail "the switcher screen didn't close the home screen"
grep -qF "plugins: closed screen screener.bare, as the quickmenu opened over it" "$log" || fail "the quick menu didn't close the switcher screen"

# a screen marked both home and switcher is only the home screen
printf 'home=main\nswitcher=main\nsteps=\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-both.log" "plugin screener: done" || fail "the launcher exited early with one screen marked home and switcher"
log="$WORK/switcher-both.log"
grep -qF "plugins: screener's screen screener.main is the home screen" "$log" || fail "the screen marked both wasn't the home screen"
grep -qF "is the game switcher" "$log" && fail "the home screen was the switcher screen too"

# a game from the switcher screen over the home screen comes back to the
# switcher screen, and B on it to the home screen
printf 'home=main\nswitcher=bare\nsteps=action:gamelist.switcher,play:1:1\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-home-play.log" && fail "the launcher didn't quit to open a recent over the home screen"
[ "$(cat "$HOME_LEFT" 2>/dev/null)" = "home" ] || fail "a launch from the switcher screen over home didn't save home: $(cat "$HOME_LEFT" 2>/dev/null)"
printf 'home=main\nswitcher=bare\nsteps=b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-home-return.log" "plugin screener: done" || fail "the launcher exited early back from a game over the home screen"
log="$WORK/switcher-home-return.log"
[ "$(seen)" = "screener.bare screener.main " ] || fail "back from a game, the switcher screen didn't go back to the home screen: $(seen)"

# the bundled switcher-plus, in stock's style: SELECT opens it in place of
# the built-in switcher, showing the first recent's save state preview (a
# real image this time) and stock's hints; right moves on, Y removes, and
# B goes back to the list. Screener, without the switcher slot, presses the
# buttons and logs what shows
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" switcher-plus >/dev/null || fail "building switcher-plus"
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
rm -rf "$WORK/plugins/layerer.plugin"
rm -f "$HOME_LEFT" "$SWITCHED"
cp "$ROOT/tests/plugins/peeker/art.png" "$SW_STATES/Game.gb.2.bmp"
sw_recents
printf 'steps=select,right,y,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus.log" "plugin screener: done" || fail "the launcher exited early with switcher-plus"
log="$WORK/switcher-plus.log"
plugin=switcher-plus
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher is the game switcher" "$log" || fail "switcher-plus didn't take the switcher"
[ "$(seen)" = "gamelist switcher-plus.switcher gamelist " ] || fail "switcher-plus didn't open and close as the switcher: $(seen)"
expect "style full"
expect "showing 0 of 3: Saved Game, preview drawn, resume, style full"
expect "showing 1 of 3: Other, preview none, open, style full"
expect "removed 1, 2 left"
expect "showing 0 of 2: Saved Game, preview drawn, resume, style full"
[ "$(grep -c . "$RECENTS")" -eq 2 ] || fail "switcher-plus's removal wasn't saved: $(cat "$RECENTS")"
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher's" "$log" && fail "switcher-plus's screen was turned off"
sw_recents

# A resumes the selected recent, and the launcher comes back to switcher-plus
printf 'steps=action:gamelist.switcher,a\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus-play.log" && fail "the launcher didn't quit to resume a recent from switcher-plus"
log="$WORK/switcher-plus-play.log"
expect "opening 0: Saved Game"
grep -qE "SWT\.pak/launch\.sh.*Game\.gb" /tmp/next 2>/dev/null || fail "switcher-plus's recent didn't launch: $(cat /tmp/next 2>/dev/null)"
[ "$(cat /tmp/resume_slot.txt 2>/dev/null)" = "2" ] || fail "switcher-plus's recent didn't resume: $(cat /tmp/resume_slot.txt 2>/dev/null)"
printf 'steps=b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus-return.log" "plugin screener: done" || fail "the launcher exited early back from a game from switcher-plus"
log="$WORK/switcher-plus-return.log"
[ "$(seen)" = "switcher-plus.switcher gamelist " ] || fail "back from a game, the launcher didn't start on switcher-plus: $(seen)"
sw_recents

# the strip and list styles; in the list, down past the last recent wraps,
# and a recent whose emulator isn't installed doesn't open
printf 'style=strip\n' >"$STATE/switcher-plus.cfg"
printf 'steps=action:gamelist.switcher,right,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus-strip.log" "plugin screener: done" || fail "the launcher exited early with switcher-plus's strip"
log="$WORK/switcher-plus-strip.log"
expect "showing 0 of 3: Saved Game, preview drawn, resume, style strip"
expect "showing 1 of 3: Other, preview none, open, style strip"
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher's" "$log" && fail "switcher-plus's strip was turned off"
printf 'style=list\n' >"$STATE/switcher-plus.cfg"
printf 'steps=action:gamelist.switcher,down,down,a,down,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus-list.log" "plugin screener: done" || fail "the launcher exited early with switcher-plus's list"
log="$WORK/switcher-plus-list.log"
expect "showing 0 of 3: Saved Game, preview drawn, resume, style list"
expect "showing 2 of 3: Gone, preview none, open, style list"
expect "can't open 2: Gone"
[ "$(grep -cF "plugin switcher-plus: showing 0 of 3: Saved Game, preview drawn" "$log")" -eq 2 ] || fail "down past the last recent didn't wrap"
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher's" "$log" && fail "switcher-plus's list was turned off"
: >"$SW_STATES/Game.gb.2.bmp"
rm -rf "$WORK/plugins/switcher-plus.plugin" "$STATE/switcher-plus.cfg"

rm -rf "$SW_ROMS" "$SW_GONE" "$SW_EMU" "$SW_STATES"
rmdir "$CARD/Emus/desktop" 2>/dev/null
if [ -f "$WORK/switcher-recent.saved" ]; then cp "$WORK/switcher-recent.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
for saved in /tmp/next /tmp/last.txt /tmp/resume_slot.txt "$SWITCHED"; do
	if [ -f "$WORK/switcher-$(basename "$saved").saved" ]; then cp "$WORK/switcher-$(basename "$saved").saved" "$saved"; else rm -f "$saved"; fi
	rm -f "$WORK/switcher-$(basename "$saved").saved"
done
rm -f "$WORK/switcher-recent.saved"

rm -f "$HOME_LEFT"
if [ -f "$WORK/home-left.saved" ]; then cp "$WORK/home-left.saved" "$HOME_LEFT"; fi
rm -f "$WORK/home-left.saved"
rm -rf "$WORK/plugins/screener.plugin" "$WORK/plugins/layerer.plugin" "$STATE/screener.cfg" "$STATE/layerer.cfg" "$STATE/plugins.txt"
case_log=""

# home-dashboard owns the home slot: the launcher starts on it, with a
# continue card for the last game (which has a save state to resume) and
# favorites' game on the Pinned tab. presser switches the tabs with R1 and
# L1, opens the switcher with SELECT and comes back with B, still on the tab
# it left, and X closes it on to the game list. The other modes each draw
# their own home; what they all draw goes to $texts.
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
make -s -C "$ROOT/plugins" home-dashboard favorites ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building home-dashboard"
HD_ROMS="$CARD/Roms/Home Test (HDT)"
HD_PICO="$CARD/Roms/Home Pico (P8)"
HD_EMU="$CARD/Emus/desktop/HDT.pak"
HD_STATES="$CARD/.userdata/shared/.minui/HDT"
HD_FAVORITES="$CARD/Collections/Favorites.txt"
mkdir -p "$HD_ROMS" "$HD_PICO" "$HD_EMU" "$HD_STATES" "$(dirname "$HD_FAVORITES")" "$(dirname "$RECENTS")"
: >"$HD_ROMS/Hit Game.gb"
: >"$HD_ROMS/Second.gb"
: >"$HD_PICO/Cart.p8"
: >"$HD_EMU/launch.sh"
printf '1' >"$HD_STATES/Hit Game.gb.txt"
: >"$HD_STATES/Hit Game.gb.1.bmp"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/home-dashboard-recent.saved"
[ -f "$HD_FAVORITES" ] && cp "$HD_FAVORITES" "$WORK/home-dashboard-favorites.saved"
printf '/Roms/Home Test (HDT)/Hit Game.gb\n/Roms/Home Test (HDT)/Second.gb\n' >"$RECENTS"
printf '/Roms/Home Test (HDT)/Second.gb\n' >"$HD_FAVORITES"
rm -f "$HOME_LEFT"

# home_case <name> <keys>: home-dashboard with favorites, presser pressing
# keys; what it drew goes to $texts
home_case() {
	texts="$WORK/$1.texts"
	: >"$texts"
	printf 'favorites=on\nhome-dashboard=on\n' >"$STATE/plugins.txt"
	PRESSER_KEYS="$2" PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER"
	export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	run_launcher "$WORK/$1.log" "presser: done" || fail "the launcher exited early with home-dashboard ($1)"
	unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	log="$WORK/$1.log"
	plugin=home-dashboard
	grep -qF "plugins: home-dashboard's screen home-dashboard.main is the home screen" "$log" \
		|| fail "home-dashboard's screen wasn't the home screen ($1)"
	grep -qF "plugins: home-dashboard's screen home-dashboard.main's" "$log" \
		&& fail "home-dashboard's screen was turned off: $(grep -F "home-dashboard.main's" "$log")"
}
# drew_home <text>: home-dashboard drew exactly this text
drew_home() {
	grep -qxF -- "$1" "$texts" || fail "home-dashboard didn't draw \"$1\" ($log)"
}

home_case home-dashboard "r1 r1 r1 l1 select b x"
expect "mode dashboard, pinned on, stats on, shelf on, favorites on"
expect "home: mode dashboard, tab Home of 3, recents 2, pinned 1, carts 0, history"
[ "$(sed -n 's/.*plugin home-dashboard: tab //p' "$log" | tr '\n' ' ')" = "Pinned Stats Home Stats " ] \
	|| fail "R1 and L1 didn't step through the tabs: $(grep "plugin home-dashboard: tab" "$log")"
expect "home: mode dashboard, tab Stats of 3"
expect "games"
for text in CONTINUE "Hit Game" Second Home Pinned Stats GAMES RESUME; do drew_home "$text"; done
grep -qxE "Played today|No play history yet" "$texts" || fail "the Stats tab drew neither its play time nor that there's none"
grep -qF "plugins: closed screen home-dashboard.main, as the switcher opened over it" "$log" \
	|| fail "SELECT didn't open the switcher over the home screen"

for hd_mode in minui retroachievements pico8; do
	printf 'mode=%s\n' "$hd_mode" >"$STATE/home-dashboard.cfg"
	home_case "home-dashboard-$hd_mode" "down x"
	expect "home: mode $hd_mode,"
	expect "games"
	case "$hd_mode" in
		minui) drew_home "Recently played"; drew_home "Games"; drew_home "Second" ;;
		retroachievements) drew_home "Achievements"; grep -qxE "Checking achievements|No achievements on the card yet" "$texts" \
			|| fail "the retroachievements mode drew neither its rows' state" ;;
		pico8) drew_home "Pico-8"; drew_home "Cart"; grep -qF ", carts 1, " "$log" || fail "the pico8 mode didn't find its cart" ;;
	esac
done
rm -f "$STATE/home-dashboard.cfg"

rm -f "$PRESSER" "$HOME_LEFT"
if [ -f "$WORK/home-dashboard-recent.saved" ]; then cp "$WORK/home-dashboard-recent.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
if [ -f "$WORK/home-dashboard-favorites.saved" ]; then cp "$WORK/home-dashboard-favorites.saved" "$HD_FAVORITES"; else rm -f "$HD_FAVORITES"; fi
rm -rf "$HD_ROMS" "$HD_PICO" "$HD_EMU" "$HD_STATES" "$WORK/home-dashboard-recent.saved" "$WORK/home-dashboard-favorites.saved" \
	"$WORK/plugins/home-dashboard.plugin" "$WORK/plugins/favorites.plugin" "$STATE/plugins.txt"
case_log=""

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
