# shellcheck shell=sh

# kiosk mode covers the deletes inside plugin screens, with simple-mode,
# presser and each of screenshot-viewer and save-state-manager. With no
# PIN, Y on the screenshot (screenshot-viewer.delete) and on Alpha's state
# (save-state-manager.delete) is refused with simple mode's toast before
# any confirm, and nothing is deleted; with a PIN, Y asks for it, and the
# right one leads to the confirm, whose A deletes. Simple mode off asks no
# PIN. /tmp/last.txt, the PIN and the screenshots are put back afterwards
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
make -s -C "$ROOT/plugins" simple-mode ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building simple-mode"
KS_SHOTS="$CARD/Screenshots"
KS_SHOT="$KS_SHOTS/Alpha.2026-11-09-08-30-00.png"
KS_ROMS="$CARD/Roms/Kiosk Screens (KST)"
KS_EMU="$CARD/Emus/desktop/KST.pak"
KS_CORE="$CARD/.userdata/shared/KST-core"
KS_STATE="$KS_CORE/Alpha.gb.st0"
KS_PIN="$STATE/pin.txt"
[ -e "$KS_SHOTS" ] && mv "$KS_SHOTS" "$WORK/kiosk-screens-shots.saved"
[ -f "$KS_PIN" ] && mv "$KS_PIN" "$WORK/kiosk-screens-pin.saved"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/kiosk-screens-last.saved"
mkdir -p "$KS_SHOTS" "$KS_ROMS" "$KS_EMU" "$KS_CORE" "$STATE"
: >"$KS_ROMS/Alpha.gb"
: >"$KS_EMU/launch.sh"
# typing 4826 from where the keyboard starts (q), then START
TYPE_PIN="up right right right a right right right right a left left left left left left a right right right right a start"

# ks_run <name> <keys>: starts on Alpha and presses keys; what was drawn
# goes to $WORK/<name>.texts
ks_run() {
	printf '%s' "$KS_ROMS/Alpha.gb" >/tmp/last.txt
	texts="$WORK/$1.texts"
	: >"$texts"
	PRESSER_KEYS="$2" PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER"
	export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	run_launcher "$WORK/$1.log" "presser: done" || fail "the launcher exited early in $1"
	unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	log="$WORK/$1.log"
}
shown() {
	grep -qxF -- "$1" "$texts" || fail "\"$1\" wasn't drawn in $log"
}
refused() {
	grep -qF "plugins: $1 was refused by a plugin's action_gate" "$log" || fail "$1 wasn't refused without a PIN"
	shown "Set a PIN to make changes in kiosk mode"
	grep -qF "asking the PIN" "$log" && fail "the PIN was asked for with none set"
}
pin_asked() {
	grep -qF "plugins: asking the PIN before $1" "$log" || fail "the PIN wasn't asked for before $1"
	shown "PIN to make changes"
	grep -qF "4826" "$log" && fail "the PIN was logged"
}

# screenshot-viewer, on SELECT
make -s -C "$ROOT/plugins" screenshot-viewer ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building screenshot-viewer"
printf 'simple-mode=on\nscreenshot-viewer=on\n' >"$STATE/plugins.txt"
printf 'button=SELECT\ntile=off\n' >"$STATE/screenshot-viewer.cfg"
cp "$ROOT/tests/plugins/peeker/art.png" "$KS_SHOT"

# kiosk with no PIN: refused before the confirm, and kept
printf 'on=on\nkiosk=on\n' >"$STATE/simple-mode.cfg"
ks_run kiosk-shot-nopin "select y a"
refused screenshot-viewer.delete
grep -qF "plugin screenshot-viewer: not deleting Alpha.2026-11-09-08-30-00.png: refused" "$log" || fail "screenshot-viewer didn't hear the refusal"
grep -qF "Delete this screenshot?" "$texts" && fail "screenshot-viewer confirmed a refused delete"
[ -f "$KS_SHOT" ] || fail "kiosk mode let screenshot-viewer delete the screenshot"

# kiosk with a PIN: the right one, then the confirm, deletes it
printf '4826\n' >"$KS_PIN"
ks_run kiosk-shot-pin "select y $TYPE_PIN a"
pin_asked screenshot-viewer.delete
grep -qF "plugin screenshot-viewer: deleted Alpha.2026-11-09-08-30-00.png, 0 left" "$log" || fail "screenshot-viewer didn't delete after the PIN"
[ -e "$KS_SHOT" ] && fail "screenshot-viewer didn't delete the screenshot after the PIN"

# simple mode off: no PIN, the confirm deletes it
cp "$ROOT/tests/plugins/peeker/art.png" "$KS_SHOT"
printf 'on=off\nkiosk=on\n' >"$STATE/simple-mode.cfg"
ks_run kiosk-shot-off "select y a"
grep -qF "asking the PIN" "$log" && fail "the PIN was asked for with simple mode off"
[ -e "$KS_SHOT" ] && fail "screenshot-viewer didn't delete the screenshot with simple mode off"
rm -rf "$WORK/plugins/screenshot-viewer.plugin" "$STATE/screenshot-viewer.cfg"

# save-state-manager, on Y
make -s -C "$ROOT/plugins" save-state-manager ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building save-state-manager"
printf 'simple-mode=on\nsave-state-manager=on\n' >"$STATE/plugins.txt"
printf 'tile=off\n' >"$STATE/save-state-manager.cfg"
printf state >"$KS_STATE"

# kiosk with no PIN: refused before the confirm, and kept
rm -f "$KS_PIN"
printf 'on=on\nkiosk=on\n' >"$STATE/simple-mode.cfg"
ks_run kiosk-state-nopin "y y a"
refused save-state-manager.delete
grep -qF "plugin save-state-manager: not deleting a state of Alpha.gb: refused" "$log" || fail "save-state-manager didn't hear the refusal"
grep -qF "Delete this save state?" "$texts" && fail "save-state-manager confirmed a refused delete"
[ -f "$KS_STATE" ] || fail "kiosk mode let save-state-manager delete the state"

# kiosk with a PIN: the right one, then the confirm, deletes it
printf '4826\n' >"$KS_PIN"
ks_run kiosk-state-pin "y y $TYPE_PIN a"
pin_asked save-state-manager.delete
grep -qF "plugin save-state-manager: deleted Slot 1 of Alpha.gb" "$log" || fail "save-state-manager didn't delete after the PIN"
[ -e "$KS_STATE" ] && fail "save-state-manager didn't delete the state after the PIN"

rm -f "$KS_PIN"
[ -f "$WORK/kiosk-screens-pin.saved" ] && mv "$WORK/kiosk-screens-pin.saved" "$KS_PIN"
if [ -f "$WORK/kiosk-screens-last.saved" ]; then cp "$WORK/kiosk-screens-last.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
rm -rf "$KS_SHOTS" "$KS_ROMS" "$KS_EMU" "$KS_CORE"
[ -e "$WORK/kiosk-screens-shots.saved" ] && mv "$WORK/kiosk-screens-shots.saved" "$KS_SHOTS"
rm -rf "$PRESSER" "$WORK/kiosk-screens-last.saved" "$WORK/plugins/save-state-manager.plugin" "$WORK/plugins/simple-mode.plugin" \
	"$STATE/save-state-manager.cfg" "$STATE/simple-mode.cfg" "$STATE/plugins.txt"
case_log=""
