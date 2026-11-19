# shellcheck shell=sh

# quick-menu-plus's rows, labels, Restart launcher and Reset launcher
# settings, with presser. The quick menu draws the Restart launcher
# tile's label by default, and not with the top row or its labels hidden; with both rows off the bottom
# row stays and the plugin says why. Restart launcher, the last tile,
# restarts at once: the launcher exits on its own. Reset launcher settings
# asks first: B leaves everything, A removes plugins.txt and the settings
# in quick-menu-plus.cfg, keeping its other keys, the PIN, settings.txt and
# tags.txt, and the launcher exits. In simple mode's kiosk mode it asks
# for the PIN first. /tmp/last.txt and the PIN are put back afterwards
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
make -s -C "$ROOT/plugins" quick-menu-plus ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building quick-menu-plus"
make -s -C "$ROOT/plugins" simple-mode ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building simple-mode"
QO_CFG="$STATE/quick-menu-plus.cfg"
QO_PIN="$STATE/pin.txt"
mkdir -p "$STATE"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/qo-last.saved"
[ -f "$QO_PIN" ] && mv "$QO_PIN" "$WORK/qo-pin.saved"
[ -f "$STATE/settings.txt" ] && mv "$STATE/settings.txt" "$WORK/qo-settings.saved"

# qo_plugins [simple-mode]: quick-menu-plus on, and simple-mode when
# named; every other plugin an earlier case built into $WORK/plugins off
qo_plugins() {
	{
		printf 'quick-menu-plus=on\n'
		[ -n "${1:-}" ] && printf 'simple-mode=on\n'
		for qo_built in "$WORK"/plugins/*.plugin; do
			[ -e "$qo_built" ] || continue
			qo_id=$(basename "$qo_built" .plugin)
			case "$qo_id" in quick-menu-plus) ;; simple-mode) [ -n "${1:-}" ] || printf '%s=off\n' "$qo_id" ;; *) printf '%s=off\n' "$qo_id" ;; esac
		done
	} >"$STATE/plugins.txt"
}
# qo_cfg <key=value>...: quick-menu-plus's cfg, with only the given tiles
qo_cfg() {
	printf 'toggles=\nawake_tile=off\nstock_tile=off\n' >"$QO_CFG"
	for qo_line in "$@"; do printf '%s\n' "$qo_line" >>"$QO_CFG"; done
}

# qo_run <name> <keys> [line]: starts on the game list and presses keys;
# what was drawn goes to $WORK/<name>.texts. With a line, waits for it as
# well as presser's; without, runs until the launcher exits, and qo_status
# is 1 when it exited on its own
qo_run() {
	printf '%s' "$CARD/Roms" >/tmp/last.txt
	texts="$WORK/$1.texts"
	: >"$texts"
	PRESSER_KEYS="$2" PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER"
	export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	if [ "${3:-}" = exits ]; then
		run_launcher "$WORK/$1.log"
		qo_status=$?
	else
		run_launcher "$WORK/$1.log" "presser: done" || fail "the launcher exited early in $1"
	fi
	unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	log="$WORK/$1.log"
}
shown() {
	grep -qxF -- "$1" "$texts" || fail "\"$1\" wasn't drawn"
}
not_shown() {
	grep -qxF -- "$1" "$texts" && fail "\"$1\" was drawn"
}

# by default both rows and the labels show
qo_plugins
qo_cfg restart_tile=on
qo_run qo-default "wait menu left"
grep -qxF "[INFO] quick menu: top row Games,Tools,Restart launcher" "$log" || fail "the top row isn't Games, Tools and Restart launcher"
grep -qxF "[INFO] quick menu: bottom row Settings,Wifi,Reboot,Poweroff" "$log" || fail "the bottom row isn't stock's"
shown "Restart launcher"

# the top row hidden: none of its tiles
qo_cfg top_row=off restart_tile=on
qo_run qo-top "wait menu left"
grep -qxF "[INFO] quick menu: top row " "$log" || fail "the top row wasn't hidden"
grep -qxF "[INFO] quick menu: bottom row Settings,Wifi,Reboot,Poweroff" "$log" || fail "the bottom row didn't stay"
not_shown "Restart launcher"

# both rows off: the bottom row stays, and the plugin says why
qo_cfg top_row=off bottom_row=off restart_tile=on
qo_run qo-both "wait menu left"
grep -qF "the top and bottom rows can't both be hidden, so the bottom row stays" "$log" || fail "both rows off wasn't refused"
grep -qxF "[INFO] quick menu: top row " "$log" || fail "the top row wasn't hidden with both off"
grep -qxF "[INFO] quick menu: bottom row Settings,Wifi,Reboot,Poweroff" "$log" || fail "the bottom row didn't stay with both off"
not_shown "Restart launcher"

# labels hidden: the tiles are there, without their names
qo_cfg tile_labels=off restart_tile=on
qo_run qo-labels "menu left a" exits
grep -qxF "[INFO] quick menu: top row Games,Tools,Restart launcher" "$log" || fail "the top row's tiles went with their labels"
not_shown "Restart launcher"
[ "$qo_status" = 1 ] || fail "Restart launcher, unlabelled, didn't restart"

# Restart launcher restarts at once
qo_cfg restart_tile=on
qo_run qo-restart "menu left a" exits
shown "Restart launcher"
[ "$qo_status" = 1 ] || fail "the launcher didn't exit for Restart launcher"
grep -qF "plugins: a plugin asked to restart the launcher" "$log" || fail "Restart launcher didn't ask to restart"
grep -qF "restarting for a plugin" "$log" || fail "the launcher didn't restart"
grep -qF "reset plugin settings" "$log" && fail "Restart launcher reset the settings"

# Reset launcher settings: B leaves everything
printf '4826\n' >"$QO_PIN"
printf 'lock_chord=off\n' >"$STATE/settings.txt"
printf 'Kids\n' >"$STATE/tags.txt"
qo_cfg reset_tile=on top_row=on kept=yes
qo_run qo-reset-no "menu left a b"
shown "Reset launcher settings?"
grep -qF "reset plugin settings" "$log" && fail "backing out of Reset launcher settings reset them"
[ -f "$STATE/plugins.txt" ] || fail "backing out of Reset launcher settings removed plugins.txt"

# A resets: plugins.txt and the settings go, the rest stays
qo_run qo-reset "menu left a a" exits
[ "$qo_status" = 1 ] || fail "the launcher didn't exit for Reset launcher settings"
grep -qF "plugins: a plugin asked to reset plugin settings and restart the launcher" "$log" || fail "Reset launcher settings didn't ask"
grep -qF "plugins: reset plugin settings to their defaults" "$log" || fail "the settings weren't reset"
[ -f "$STATE/plugins.txt" ] && fail "plugins.txt wasn't removed"
[ "$(cat "$QO_CFG" 2>/dev/null)" = "kept=yes" ] || fail "quick-menu-plus.cfg wasn't reset to its other keys: $(cat "$QO_CFG" 2>/dev/null)"
[ "$(cat "$QO_PIN")" = 4826 ] || fail "the PIN changed"
[ "$(cat "$STATE/settings.txt")" = lock_chord=off ] || fail "settings.txt changed"
[ "$(cat "$STATE/tags.txt")" = Kids ] || fail "tags.txt changed"

# kiosk mode asks for the PIN first; cancelling leaves everything
qo_plugins simple-mode
printf 'on=on\nkiosk=on\n' >"$STATE/simple-mode.cfg"
qo_cfg reset_tile=on
qo_run qo-kiosk "menu left a b"
grep -qF "plugins: asking the PIN before quick-menu-plus.reset" "$log" || fail "kiosk mode didn't ask for the PIN"
not_shown "Reset launcher settings?"
[ -f "$STATE/plugins.txt" ] || fail "cancelling the PIN removed plugins.txt"

rm -f "$QO_PIN"
[ -f "$WORK/qo-pin.saved" ] && mv "$WORK/qo-pin.saved" "$QO_PIN"
rm -f "$STATE/settings.txt"
[ -f "$WORK/qo-settings.saved" ] && mv "$WORK/qo-settings.saved" "$STATE/settings.txt"
if [ -f "$WORK/qo-last.saved" ]; then cp "$WORK/qo-last.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
rm -rf "$PRESSER" "$WORK/qo-last.saved" "$WORK/plugins/quick-menu-plus.plugin" "$WORK/plugins/simple-mode.plugin" \
	"$QO_CFG" "$STATE/tags.txt" "$STATE/simple-mode.cfg" "$STATE/plugins.txt"
case_log=""
