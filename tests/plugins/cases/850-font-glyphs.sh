# shellcheck shell=sh
# shard: 2

# the bundled localization checks the UI font has every character the
# language's translations use: with language=es and the bundled theme-plus
# on a theme whose font is glyphs.ttf (ASCII and two Hangul, no accents),
# a toast says the font lacks them, once, saved in its cfg, and the
# launcher falls back to stock's font for them. The next start with the
# same language and font doesn't toast again. With theme-plus off, stock's
# font has them all, which forgets the warning. Then screener picks the
# theme from theme-plus's list: on launcher.theme_changed the toast shows
# again. With the language off the warning is forgotten in init. Then the
# Launcher pak's pages show the note on localization's page and on
# theme-plus's, and on neither with stock's font.
for name in localization theme-plus; do
	make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" "$name" >/dev/null || fail "building $name"
done
build_plugin screener 'id=screener\nname=Screener\napi=1\n'
FG_THEME="$CARD/Themes/Glyph"
FG_CFG="$STATE/localization.cfg"
rm -rf "$CARD/Themes"
mkdir -p "$FG_THEME" "$STATE"
cp "$ROOT/tests/plugins/glypher/glyphs.ttf" "$FG_THEME/glyphs.ttf"
printf 'version=1\nname=Glyph\nfont=glyphs.ttf\n' >"$FG_THEME/theme.txt"
FG_STOCK="$CARD/.system/res"
plugin=localization

# fg_run <name> [line...]: the launcher until the lines are logged
fg_run() {
	log="$WORK/$1.log"
	shift
	run_launcher "$log" "$@" || fail "the launcher exited early with localization ($(basename "$log" .log))"
}
# fg_toasts: how many toasts localization showed in $log
fg_toasts() {
	grep -cF "plugin localization: toast: " "$log"
}

# a theme font without Spanish's accents: one toast, and stock's font for them
printf 'localization=on\ntheme-plus=on\nscreener=off\n' >"$STATE/plugins.txt"
printf 'language=es\n' >"$FG_CFG"
printf 'theme=Glyph\n' >"$STATE/theme-plus.cfg"
fg_run glyphs-start "plugin localization: toast: A la fuente le faltan "
expect "the font lacks "
grep "plugin localization: the font lacks " "$log" | grep -qF "characters es's translations use: " \
	|| fail "localization didn't say what the font lacks: $(grep "the font" "$log")"
[ "$(fg_toasts)" = 1 ] || fail "localization toasted $(fg_toasts) times"
grep -qF "plugins: stock's font, under theme-plus's theme, supplies the fallback font $FG_STOCK/" "$log" \
	|| fail "stock's font wasn't the fallback font under the theme's: $(grep "fallback font" "$log")"
grep -q '^warned=es:' "$FG_CFG" || fail "the warning wasn't saved: $(cat "$FG_CFG")"

# the same language and font again: no toast
fg_run glyphs-again "plugin localization: already warned about this language and font"
[ "$(fg_toasts)" = 0 ] || fail "localization toasted again for the same language and font"

# stock's font has every character: no toast, and the warning forgotten
printf 'localization=on\ntheme-plus=off\nscreener=off\n' >"$STATE/plugins.txt"
fg_run glyphs-stock "plugin localization: the font has every character es's translations use"
[ "$(fg_toasts)" = 0 ] || fail "localization toasted with stock's font"
grep -q '^warned=.' "$FG_CFG" && fail "the warning wasn't forgotten with stock's font: $(cat "$FG_CFG")"
grep -qF "supplies the fallback font" "$log" && fail "a fallback font was used with no theme: $(grep "fallback font" "$log")"

# the theme picked from theme-plus's list (Stock, then Glyph) toasts on the change
printf 'localization=on\ntheme-plus=on\nscreener=on\n' >"$STATE/plugins.txt"
printf 'theme=stock\n' >"$STATE/theme-plus.cfg"
printf 'steps=action:theme-plus.pick,down,a\n' >"$STATE/screener.cfg"
fg_run glyphs-picked "plugin screener: done" "plugin localization: toast: A la fuente le faltan "
expect "the font has every character es's translations use"
grep -qF "plugin theme-plus: picked Glyph" "$log" || fail "theme-plus didn't pick Glyph: $(grep "plugin theme-plus" "$log")"
[ "$(fg_toasts)" = 1 ] || fail "localization toasted $(fg_toasts) times on the theme change"
rm -f "$STATE/screener.cfg"

# the language off forgets the warning
printf 'localization=on\ntheme-plus=on\nscreener=off\n' >"$STATE/plugins.txt"
printf 'language=off\nwarned=es:1:00000000\n' >"$FG_CFG"
fg_run glyphs-off "plugin localization: language off, so nothing is translated"
grep -q '^warned=.' "$FG_CFG" && fail "the warning wasn't forgotten with the language off: $(cat "$FG_CFG")"
[ "$(fg_toasts)" = 0 ] || fail "localization toasted with the language off"

# the Launcher pak's pages: the note on localization's page and theme-plus's
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
FG_PLUGINS="$WORK/font-glyphs"
rm -rf "$FG_PLUGINS"
mkdir -p "$FG_PLUGINS"
mv "$WORK/plugins/localization.plugin" "$WORK/plugins/theme-plus.plugin" "$FG_PLUGINS/"
texts="$WORK/font-glyphs.texts"
# fg_page <name> <keys>: the Plugins page, with what it drew in $texts
fg_page() {
	case_log="$WORK/$1.log"
	log="$case_log"
	rm -f "$texts"
	: >"$texts"
	PRESSER_KEYS="$2" PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER" SMOKE_UNTIL="presser: done" \
		MOSAIC_LAUNCHER_PAK_DIR="$ROOT/pak" MOSAIC_LAUNCHER_PLUGIN_PATH="$FG_PLUGINS" \
		"$ROOT/scripts/desktop-smoke.sh" "$case_log" 40 --settings plugins >/dev/null 2>"$WORK/smoke.err" \
		|| fail "the Plugins page didn't run its presses ($1)"
}
# fg_note: the note's text drawn, if any
fg_note() {
	grep -F "A la fuente le falta" "$texts" | head -n 1
}
clear_guard
printf 'language=es\n' >"$FG_CFG"
printf 'theme=Glyph\n' >"$STATE/theme-plus.cfg"
printf 'localization=on\ntheme-plus=on\n' >"$STATE/plugins.txt"
# the Language group's Localization is selected first, then the Theme
# group's Theme plus; under glyphs.ttf the title's ó is a run of stock's
# font of its own, drawn apart
fg_page glyphs-page "a"
drew "Localizaci"
[ -n "$(fg_note)" ] || fail "localization's page had no note"
fg_page glyphs-theme-page "down a"
drew "Tema plus"
[ -n "$(fg_note)" ] || fail "theme-plus's page had no note"
grep -q '^warned=' "$FG_CFG" && fail "the pages saved a warning: $(cat "$FG_CFG")"
printf 'theme=stock\n' >"$STATE/theme-plus.cfg"
fg_page glyphs-stock-page "a"
drew "Localización"
[ -z "$(fg_note)" ] || fail "localization's page had a note with stock's font: $(fg_note)"

clear_guard
rm -rf "$FG_PLUGINS" "$WORK/plugins/screener.plugin" "$CARD/Themes" "$FG_CFG" "$STATE/theme-plus.cfg" "$STATE/screener.cfg" \
	"$STATE/plugins.txt" "$texts"
case_log=""
