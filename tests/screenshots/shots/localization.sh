# shellcheck shell=sh

begin
on localization
cfg localization language=es
press "down down"
shot spanish

begin
on localization
cfg localization language=es
press "menu"
shot spanish-quick-menu

# a theme whose font, NextUI's reduced Rounded M+ for its boot screen, has
# none of Spanish's accents: the toast at start, and the note on
# localization's page in Launcher > Plugins
glyph_theme() {
	mkdir -p "$CARD/Themes/Reduced"
	cp "$ROOT/vendor/NextUI/workspace/all/show2/RoundedMplus1c-Bold-reduced.ttf" "$CARD/Themes/Reduced/font.ttf"
	printf 'version=1\nname=Reduced\nfont=font.ttf\n' >"$CARD/Themes/Reduced/theme.txt"
}

begin
on localization theme-plus
cfg localization language=es
cfg theme-plus theme=Reduced
glyph_theme
press "wait"
shot font-toast

begin
on localization theme-plus
cfg localization language=es
cfg theme-plus theme=Reduced
glyph_theme
args --settings plugins
# past the twelve plugins of the Main menu to Background groups and Home
# screen and Game switcher's, to Language's Localization
press "down down down down down down down down down down down down a"
shot font-note
