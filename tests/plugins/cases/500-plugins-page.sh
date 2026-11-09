# shellcheck shell=sh
# shard: 2

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
