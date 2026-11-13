# shellcheck shell=sh
# shard: 2

# stock's palettes through the host: paletter's list is the one stock's
# Settings builds (its own, then the card's, a card palette among them, in
# the order each folder is read) with the user's Settings palette selected;
# theme-plus lists them in that order with the user's marked, and picking
# the card's sets the launcher's colours from it without writing stock's
# settings; a theme.txt naming it keeps its own colour over the palette's.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" theme-plus >/dev/null || fail "building theme-plus"
build_plugin paletter 'id=paletter\nname=Paletter\napi=1\n'
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
mkdir -p "$STATE" "$CARD/Palettes" "$CARD/Themes/Mixed"
printf 'paletter=on\ntheme-plus=on\n' >"$STATE/plugins.txt"
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/palette-settings.saved"
PAL_FILE="$CARD/Palettes/Case_730.txt"
printf 'version=1\nname=Case 730\ncolor1=0x102030FF\ncolor2=0x405060\ncolor7=0x0A0B0C\n' >"$PAL_FILE"
printf 'version=1\nname=Mixed\npalette=Case 730\ncolor1=0x112233\n' >"$CARD/Themes/Mixed/theme.txt"
PAL_COLORS="102030 405060 1E2329 FFFFFF 000000 FFFFFF 0A0B0C"

# Settings' list, and where its Default and the card's palette are
order="$WORK/palette-order.txt"
python3 -I "$ROOT/tests/plugins/paletter/order.py" "$CARD/.system/res/palettes" "$CARD/Palettes" >"$order" \
	|| fail "listing stock's palettes"
default_at=$(awk '$1 == 1 && substr($0, 3) == "Default" { print NR - 1; exit }' "$order")
case_at=$(awk 'substr($0, 3) == "Case 730" { print NR - 1; exit }' "$order")
if [ -z "$default_at" ] || [ -z "$case_at" ]; then
	fail "the fake card has no built-in Default or no Case 730: $(cat "$order")"
fi

# pal_run <name> <theme-plus config> [presser keys]: until paletter logged the list
pal_run() {
	log="$WORK/$1.log"
	printf '%b' "$2" >"$STATE/theme-plus.cfg"
	printf 'palette=Default\n' >"$SETTINGS"
	if [ -n "${3:-}" ]; then
		: >"$WORK/palettes.texts"
		PRESSER_KEYS="$3" PRESSER_LOG="$WORK/palettes.texts" PRESSER_STEP=150 SMOKE_PRELOAD="$PRESSER"
		export PRESSER_KEYS PRESSER_LOG PRESSER_STEP SMOKE_PRELOAD
		saved_wait=$WAIT
		WAIT=$((WAIT + 20))
		SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$log" "presser: done" "plugin theme-plus: theme Case 730 (palette)" \
			|| fail "the launcher exited early ($1)"
		WAIT=$saved_wait
		unset PRESSER_KEYS PRESSER_LOG PRESSER_STEP SMOKE_PRELOAD
	else
		SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$log" "plugin paletter: colors" \
			|| fail "the launcher exited early ($1)"
	fi
}
# pal_colors: the colours paletter logged last
pal_colors() { sed -n 's/.*plugin paletter: colors //p' "$log" | tail -n 1; }

# stock's theme: paletter's list is Settings', with Default selected
pal_run palettes-stock 'theme=stock\n'
plugin=paletter
count=$(wc -l <"$order" | tr -d ' ')
expect "palettes $count selected $default_at"
sed -n 's/.*plugin paletter: palette \([0-9]*\) \([01]\) [0-9A-F,]* \(.*\)$/\2 \3/p' "$log" >"$WORK/palette-listed.txt"
cmp -s "$order" "$WORK/palette-listed.txt" || fail "the host's palettes aren't in Settings' order: $(diff "$order" "$WORK/palette-listed.txt" | head -n 10)"
expect "palette $case_at 0 $(echo "$PAL_COLORS" | tr ' ' ',') Case 730"
expect "refused -1 -1 -1"
expect "small $(sed -n '1s/^[01] //p' "$order") 1"
stock_colors=$(pal_colors)
[ -n "$stock_colors" ] || fail "paletter logged no colours"
settings_before=$(cat "$SETTINGS")

# theme-plus's picker: Stock, its 5 bundled themes and Mixed, then the palettes;
# the cursor starts on Stock, so the card's palette is 7 + its place down
downs=$((7 + case_at))
keys="y"
i=0
while [ "$i" -lt "$downs" ]; do
	keys="$keys down"
	i=$((i + 1))
done
pal_run palettes-pick 'theme=stock\nbutton=Y\n' "$keys a"
plugin=theme-plus
expect "picked Case 730"
expect "theme Case 730 (palette)"
grep -qxF "Default (in Settings)" "$WORK/palettes.texts" || fail "theme-plus didn't mark Settings' palette: $(grep -F "Default" "$WORK/palettes.texts" | head -n 3)"
grep -qxF "Case 730" "$WORK/palettes.texts" || fail "theme-plus didn't list the card's palette"
[ "$(pal_colors)" = "$PAL_COLORS" ] || fail "picking the palette didn't set the launcher's colours: $(pal_colors), want $PAL_COLORS"
[ "$(cat "$SETTINGS")" = "$settings_before" ] || fail "stock's settings were written: $(cat "$SETTINGS")"
grep -qx "theme=Case 730" "$STATE/theme-plus.cfg" || fail "theme-plus didn't save the pick: $(cat "$STATE/theme-plus.cfg")"

# a theme naming the palette keeps its own colour1 over it
pal_run palettes-mixed 'theme=Mixed\n'
plugin=theme-plus
expect "theme Mixed (card, palette Case 730)"
want="112233 ${PAL_COLORS#* }"
[ "$(pal_colors)" = "$want" ] || fail "the theme's colour didn't win over its palette's: $(pal_colors), want $want"
[ "$(cat "$SETTINGS")" = "$settings_before" ] || fail "stock's settings were written by the theme"

if [ -f "$WORK/palette-settings.saved" ]; then mv "$WORK/palette-settings.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -rf "$WORK/plugins/theme-plus.plugin" "$WORK/plugins/paletter.plugin" "$CARD/Themes/Mixed" "$PAL_FILE" "$PRESSER" \
	"$order" "$WORK/palette-listed.txt" "$STATE/theme-plus.cfg" "$STATE/plugins.txt"
rmdir "$CARD/Themes" 2>/dev/null || :
case_log=""
