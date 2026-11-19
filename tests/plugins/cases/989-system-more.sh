# shellcheck shell=sh

# system-info's Storage page and input tester, with its button on SELECT
# and screener logging each screen: SELECT opens system info, Y opens
# Storage, which counts a test system's games, art, saves and states, B
# goes back and Y opens it again with the sizes counted before. Then, with
# a stand-in left stick, X opens the input tester, which takes A, MENU, up,
# START and SELECT without the quick menu opening, and holding B and START
# leaves it, back on system info, where B closes it
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" system-info >/dev/null || fail "building system-info"
SM_ROMS="$CARD/Roms/Storage More (SMX)"
SM_SAVES="$CARD/Saves/SMX"
SM_STATES="$CARD/.userdata/shared/SMX-core"
mkdir -p "$SM_ROMS/.media" "$SM_SAVES" "$SM_STATES" "$STATE"
sm_size() { dd if=/dev/zero of="$1" bs=1 count="$2" 2>/dev/null || fail "writing $1"; }
sm_size "$SM_ROMS/One.smx" 1024
sm_size "$SM_ROMS/Two.smx" 2048
sm_size "$SM_ROMS/.media/One.png" 1024
sm_size "$SM_SAVES/One.sav" 1024
sm_size "$SM_STATES/One.st0" 1024
printf 'button=SELECT\n' >"$STATE/system-info.cfg"
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
printf 'steps=\n' >"$STATE/screener.cfg"

PRESSER_KEYS="select y wait wait wait b y b b" SMOKE_PRELOAD="$PRESSER"
export PRESSER_KEYS SMOKE_PRELOAD
run_launcher "$WORK/system-more.log" "presser: done" || fail "the launcher exited early with system-info's Storage page"
unset PRESSER_KEYS SMOKE_PRELOAD
log="$WORK/system-more.log"
plugin=system-info
expect "storage: counting"
expect "storage: Storage More (SMX): games 3.0 KB, saves 2.0 KB, art 1.0 KB, total 6.0 KB"
expect "storage: BIOS "
expect "storage: opened with the sizes counted before"
expect "storage: closed"
expect "closed"
[ "$(seen)" = "gamelist system-info.main system-info.storage system-info.main system-info.storage system-info.main gamelist " ] \
	|| fail "system-info's Storage page didn't open and close: $(seen)"

MOSAIC_LAUNCHER_STICK_FAKE="1000,-2000" PRESSER_KEYS="select x a menu up start select b+start/1500 b" SMOKE_PRELOAD="$PRESSER"
export MOSAIC_LAUNCHER_STICK_FAKE PRESSER_KEYS SMOKE_PRELOAD
run_launcher "$WORK/system-more-input.log" "presser: done" || fail "the launcher exited early with system-info's input tester"
unset MOSAIC_LAUNCHER_STICK_FAKE PRESSER_KEYS SMOKE_PRELOAD
log="$WORK/system-more-input.log"
expect "input tester: opened"
expect "input tester: left stick 1000, -2000; right stick none"
for button in A MENU UP START SELECT B; do expect "input tester: pressed $button"; done
expect "input tester: B and START held, leaving"
expect "input tester: closed"
# MENU didn't open the quick menu, and B closed system info after it
[ "$(seen)" = "gamelist system-info.main system-info.input system-info.main gamelist " ] \
	|| fail "the input tester let a button through: $(seen)"

rm -rf "$SM_ROMS" "$SM_SAVES" "$SM_STATES" "$PRESSER" "$WORK/plugins/system-info.plugin" \
	"$WORK/plugins/screener.plugin" "$STATE/system-info.cfg" "$STATE/screener.cfg"
case_log=""
