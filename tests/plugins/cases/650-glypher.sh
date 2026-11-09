# shellcheck shell=sh
# shard: 2

# glypher owns the strings and theme slots and supplies fallback fonts
# (glyphs.ttf has boxes for ASCII and for Hangul, which the stock font
# lacks): the strings slot owner's draws Hangul in the fallback font while
# ASCII stays pixel for pixel stock's, mixed text is as wide as its runs
# and truncates between characters, the theme's wins when both are set, a
# refused one is logged and the other or none used, a reload drops one the
# owner no longer gives, and without slots=strings the hook isn't asked.
build_plugin glypher 'id=glypher\nname=Glypher\napi=1\nslots=strings,theme\n'
printf 'glypher=on\n' >"$STATE/plugins.txt"
plugin=glypher
GL_DIR="$CARD/Fonts"
mkdir -p "$GL_DIR"
cp "$ROOT/tests/plugins/glypher/glyphs.ttf" "$GL_DIR/glyphs.ttf"
cp "$CARD/.system/res/font2.ttf" "$GL_DIR/latin.ttf"
printf 'not a font\n' >"$GL_DIR/text.ttf"
mkdir -p "$WORK/off-card"
cp "$GL_DIR/glyphs.ttf" "$WORK/off-card/glyphs.ttf"
ln -sf "$WORK/off-card/glyphs.ttf" "$GL_DIR/escape.ttf"
GL_REAL=$(cd "$GL_DIR" && pwd -P)

# gl_run <name> <config>: the launcher with glypher's config, until it's done
gl_run() {
	log="$WORK/$1.log"
	printf '%b' "$2" >"$STATE/glypher.cfg"
	run_launcher "$log" "plugin glypher: done" || fail "the launcher exited early with glypher ($1)"
}
# gl_value <what>: what glypher first logged after "<what> "
gl_value() { sed -n "s/.*plugin glypher: $1 //p" "$log" | head -n 1; }
# gl_alpha: the alpha glypher read halfway down "한"
gl_alpha() { gl_value hangul | cut -d' ' -f2; }

# none: what stock draws
gl_run glypher-none ''
grep -qF "fallback font" "$log" && fail "a fallback font was used with none given: $(grep -F "fallback font" "$log")"
ascii=$(gl_value ascii)
hangul=$(gl_value hangul)
if [ -z "$ascii" ] || [ -z "$hangul" ]; then fail "glypher didn't report its text"; fi

# the strings slot owner's
gl_run glypher-strings "strings_font=$GL_DIR/glyphs.ttf\n"
grep -qF "plugins: glypher supplies the fallback font $GL_REAL/glyphs.ttf" "$log" || fail "the strings owner's fallback font wasn't used"
expect "ascii $ascii"
[ "$(gl_alpha)" -ge 200 ] || fail "Hangul wasn't drawn with the fallback font: $(gl_value hangul), $hangul without it"
[ "$(gl_value hangul)" != "$hangul" ] || fail "Hangul was drawn as without a fallback font"
expect "mixed same"
[ "$(gl_value truncated | cut -d' ' -f2)" = ok ] || fail "Hangul wasn't truncated between characters: $(gl_value truncated)"

# the theme's wins, even with one that lacks Hangul too
gl_run glypher-theme "theme_font=$GL_DIR/latin.ttf\nstrings_font=$GL_DIR/glyphs.ttf\n"
grep -qF "plugins: glypher's theme supplies the fallback font $GL_REAL/latin.ttf" "$log" || fail "the theme's fallback font didn't win"
expect "ascii $ascii"
expect "hangul $hangul"
gl_run glypher-theme-glyphs "theme_font=$GL_DIR/glyphs.ttf\n"
grep -qF "plugins: glypher's theme supplies the fallback font $GL_REAL/glyphs.ttf" "$log" || fail "the theme's fallback font wasn't used"
expect "ascii $ascii"
[ "$(gl_alpha)" -ge 200 ] || fail "Hangul wasn't drawn with the theme's fallback font: $(gl_value hangul)"

# a refused theme's leaves the strings owner's; refused ones leave none
gl_run glypher-theme-bad "theme_font=$GL_DIR/text.ttf\nstrings_font=$GL_DIR/glyphs.ttf\n"
grep -qF "plugins: glypher's theme fallback_font ($GL_REAL/text.ttf) won't open, so stock's is used" "$log" \
	|| fail "the theme's file that isn't a font wasn't refused"
grep -qF "plugins: glypher supplies the fallback font $GL_REAL/glyphs.ttf" "$log" || fail "the strings owner's didn't stand in for the theme's"
gl_run glypher-escape "strings_font=$GL_DIR/escape.ttf\n"
grep -qF "plugins: glypher's fallback font ($GL_DIR/escape.ttf) isn't on the card, so none is used" "$log" \
	|| fail "a fallback font off the card wasn't refused"
grep -qF "supplies the fallback font" "$log" && fail "a refused fallback font was used"
expect "hangul $hangul"
gl_run glypher-text "strings_font=$GL_DIR/text.ttf\n"
grep -qF "plugins: glypher's fallback font ($GL_REAL/text.ttf) won't open, so none is used" "$log" \
	|| fail "a fallback font that isn't a font wasn't refused"
expect "hangul $hangul"

# a reload asks again
gl_run glypher-reload "strings_font=$GL_DIR/glyphs.ttf\nreload=1\n"
grep -qF "plugins: no fallback font" "$log" || fail "the fallback font stayed after the owner dropped it"
after=$(sed -n '/plugin glypher: reloaded/,$s/.*plugin glypher: hangul //p' "$log" | head -n 1)
[ "$after" = "$hangul" ] || fail "Hangul after the reload was $after, not $hangul as without a fallback font"

# without slots=strings the hook isn't asked
printf 'id=glypher\nname=Glypher\napi=1\nslots=theme\n' >"$WORK/plugins/glypher.plugin/plugin.txt"
gl_run glypher-unslotted "strings_font=$GL_DIR/glyphs.ttf\n"
grep -qF "plugins: glypher has a fallback_font but no slots=strings, so it won't be asked" "$log" || fail "the missing strings slot wasn't reported"
expect "hangul $hangul"

rm -rf "$WORK/plugins/glypher.plugin" "$GL_DIR" "$WORK/off-card" "$STATE/glypher.cfg" "$STATE/plugins.txt"
grep -qF "fallback font" "$WORK/hello.log" && fail "a fallback font was used with no plugin taking a slot"
