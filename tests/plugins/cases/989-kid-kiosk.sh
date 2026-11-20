# shellcheck shell=sh

# simple mode's kid tag and kiosk mode, with tags and presser. With the
# kid tag Kids, the launcher starts on Kid Games (the last path), where A
# opens it with Alpha, the only Kids game, and without Bravo; B goes back
# to the root, which has Kid Games but not Other Games, which has no Kids
# game. With
# simple mode off, both systems and both games show again. Then kiosk
# mode: with no PIN, Y on Alpha (tags.edit, marked as editing) is refused
# with a toast; with a PIN, Y asks for it, a cancel leaves the checklist
# shut and the right PIN opens it; with simple mode off, Y opens it
# straight away, each starting on Alpha. /tmp/last.txt is put back
# afterwards
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
make -s -C "$ROOT/plugins" tags ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building tags"
make -s -C "$ROOT/plugins" simple-mode ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building simple-mode"
KK_KIDS="$CARD/Roms/Kid Games (GB)"
KK_OTHER="$CARD/Roms/Other Games (FC)"
KK_GB="$CARD/Emus/desktop/GB.pak"
KK_FC="$CARD/Emus/desktop/FC.pak"
KK_GB_MADE=""
KK_FC_MADE=""
KK_PIN="$STATE/pin.txt"
mkdir -p "$KK_KIDS" "$KK_OTHER" "$STATE"
[ -d "$KK_GB" ] || { mkdir -p "$KK_GB" && : >"$KK_GB/launch.sh" && KK_GB_MADE=1; }
[ -d "$KK_FC" ] || { mkdir -p "$KK_FC" && : >"$KK_FC/launch.sh" && KK_FC_MADE=1; }
: >"$KK_KIDS/Alpha.gb"
: >"$KK_KIDS/Bravo.gb"
: >"$KK_OTHER/Charlie.nes"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/kid-last.saved"
[ -f "$KK_PIN" ] && mv "$KK_PIN" "$WORK/kid-pin.saved"
printf 'Kids\nKids\t/Roms/Kid Games (GB)/Alpha.gb\n' >"$STATE/tags.txt"
printf 'tags=on\nsimple-mode=on\n' >"$STATE/plugins.txt"
# typing 4826 from where the keyboard starts (q), then START
TYPE_PIN="up right right right a right right right right a left left left left left left a right right right right a start"

# kid_run <name> <keys> [last path]: starts on Alpha, or the last path
# given, and presses keys; what was drawn goes to $WORK/<name>.texts. The
# rows of the list it starts on are drawn before presser writes them down
kid_run() {
	printf '%s' "${3:-$KK_KIDS/Alpha.gb}" >/tmp/last.txt
	texts="$WORK/$1.texts"
	: >"$texts"
	PRESSER_KEYS="$2" PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER"
	export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	run_launcher "$WORK/$1.log" "presser: done" || fail "the launcher exited early in $1"
	unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	log="$WORK/$1.log"
}
shown() {
	grep -qxF -- "$1" "$texts" || fail "\"$1\" wasn't drawn"
}
not_shown() {
	grep -qxF -- "$1" "$texts" && fail "\"$1\" was drawn"
}

# the kid tag: only its games, and the systems with one
printf 'on=on\nkid=kids\n' >"$STATE/simple-mode.cfg"
kid_run kid-on "a b" "$KK_KIDS"
shown "Alpha"
not_shown "Bravo"
shown "Kid Games"
not_shown "Other Games"

# simple mode off: every game, as before
printf 'on=off\nkid=kids\n' >"$STATE/simple-mode.cfg"
kid_run kid-off "a b" "$KK_KIDS"
shown "Alpha"
shown "Bravo"
shown "Kid Games"
shown "Other Games"

# kiosk with no PIN: tags.edit is refused
rm -f "$KK_PIN"
printf 'on=on\nkiosk=on\n' >"$STATE/simple-mode.cfg"
kid_run kiosk-nopin "y"
grep -qF "plugins: tags.edit was refused by a plugin's action_gate" "$log" || fail "tags.edit wasn't refused without a PIN"
shown "Set a PIN to make changes in kiosk mode"
not_shown "New tag"

# kiosk with a PIN: cancelled stays shut, the right PIN opens it
printf '4826\n' >"$KK_PIN"
kid_run kiosk-cancel "y y"
grep -qF "plugins: asking the PIN before tags.edit" "$log" || fail "the PIN wasn't asked for before tags.edit"
shown "PIN to make changes"
not_shown "New tag"
kid_run kiosk-pin "y $TYPE_PIN"
grep -qF "plugins: asking the PIN before tags.edit" "$log" || fail "the PIN wasn't asked for before tags.edit"
shown "New tag"
grep -qF "4826" "$log" && fail "the PIN was logged"

# simple mode off: no PIN asked
printf 'on=off\nkiosk=on\n' >"$STATE/simple-mode.cfg"
kid_run kiosk-off "y"
grep -qF "asking the PIN" "$log" && fail "the PIN was asked for with simple mode off"
shown "New tag"

rm -f "$KK_PIN"
[ -f "$WORK/kid-pin.saved" ] && mv "$WORK/kid-pin.saved" "$KK_PIN"
if [ -f "$WORK/kid-last.saved" ]; then cp "$WORK/kid-last.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
[ -n "$KK_GB_MADE" ] && rm -rf "$KK_GB"
[ -n "$KK_FC_MADE" ] && rm -rf "$KK_FC"
rm -rf "$KK_KIDS" "$KK_OTHER" "$PRESSER" "$WORK/kid-last.saved" "$WORK/plugins/tags.plugin" "$WORK/plugins/simple-mode.plugin" \
	"$STATE/tags.txt" "$STATE/status.txt" "$STATE/simple-mode.cfg" "$STATE/plugins.txt"
case_log=""
