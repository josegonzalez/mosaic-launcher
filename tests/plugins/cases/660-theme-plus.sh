# shellcheck shell=sh
# shard: 3

# the bundled theme-plus owns the theme slot: a bundled theme, a theme
# folder on the card with its own font, a palette and sizes (its wallpaper
# symlinked out of the folder is dropped), a palette from the card's
# Palettes as a colours-only theme, the UI size setting over stock's, and
# a theme name that isn't there, which leaves stock's theme.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" theme-plus >/dev/null || fail "building theme-plus"
printf 'theme-plus=on\n' >"$STATE/plugins.txt"
plugin=theme-plus
TP_DIR="$CARD/Themes/Case"
mkdir -p "$TP_DIR" "$CARD/Palettes" "$WORK/off-card"
cp "$CARD/.system/res/font2.ttf" "$TP_DIR/font.ttf"
: >"$WORK/off-card/bg.png"
ln -sf "$WORK/off-card/bg.png" "$TP_DIR/escape.png"
printf 'version=1\nname=Case\npalette=Case Palette\ncolor1=0x112233\nfont=font.ttf\nwallpaper=escape.png\npill_size=36\nbutton_size=24\npadding=8\nshadow=on\n' \
	>"$TP_DIR/theme.txt"
printf 'version=1\nname=Case Palette\ncolor1=0xCBA6F7FF\ncolor7=0x1E1E2EFF\n' >"$CARD/Palettes/Case_Palette.txt"

# tp_run <name> <config>: the launcher with theme-plus's config, until it's said which theme
tp_run() {
	log="$WORK/$1.log"
	printf '%b' "$2" >"$STATE/theme-plus.cfg"
	run_launcher "$log" "$3" || fail "the launcher exited early with theme-plus ($1)"
}
# tp_supplies <text>: the launcher put theme-plus's theme in place with these sizes
tp_supplies() {
	grep -qF "plugins: theme-plus supplies the theme: $1" "$log" || fail "theme-plus's theme wasn't \"$1\": $(grep "supplies the theme" "$log")"
}

tp_run theme-plus-bundled 'theme=high-contrast\n' "plugin theme-plus: theme high-contrast (bundled)"
tp_supplies "pill 30, button 20, padding 5, 7 rows, text step 2, font stock's"

tp_run theme-plus-card 'theme=Case\n' "plugin theme-plus: theme Case (card, palette Case Palette)"
tp_supplies "pill 36, button 24, padding 8, 5 rows, text step 0, font $(cd "$TP_DIR" && pwd -P)/font.ttf"
expect "$TP_DIR/theme.txt: line 10: unknown key shadow, ignored"
expect "$TP_DIR/theme.txt: wallpaper (escape.png) is outside the theme's folder, dropped"
grep -qF "so stock's is used" "$log" && fail "the launcher refused part of theme-plus's theme: $(grep "so stock's is used" "$log")"

tp_run theme-plus-palette 'theme=case palette\n' "plugin theme-plus: theme Case Palette (palette)"
tp_supplies "pill 30, button 20, padding 5, 7 rows, text step 0, font stock's"

tp_run theme-plus-ui 'theme=stock\nui_size=large\ntext_size=large\n' "plugin theme-plus: theme stock"
tp_supplies "pill 36, button 24, padding 12, 5 rows, text step 2, font stock's"

tp_run theme-plus-missing 'theme=Nope\n' "plugin theme-plus: theme stock"
expect "no theme or palette is called Nope, so stock's is used"
tp_supplies "pill 30, button 20, padding 5, 7 rows, text step 0, font stock's"

rm -rf "$WORK/plugins/theme-plus.plugin" "$CARD/Themes" "$CARD/Palettes/Case_Palette.txt" "$WORK/off-card" \
	"$STATE/theme-plus.cfg" "$STATE/plugins.txt"
