# shellcheck shell=sh
# shard: 2

# toggler reads stock toggles, has a name off the list refused, and flips
# Show clock, which stock saves to minuisettings.txt (in the folder named by
# SHARED_USERDATA_PATH, which stock's launch.sh sets on a device)
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
