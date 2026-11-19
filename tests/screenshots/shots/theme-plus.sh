# shellcheck shell=sh

begin
on theme-plus
press "menu wait left a"
shot default

for theme in nord dracula solarized-dark pocket-green nocturne ember mint high-contrast; do
	begin
	on theme-plus
	cfg theme-plus "theme=$theme"
	press "down down"
	shot "theme-$theme"
done

begin
on theme-plus
cfg theme-plus text_size=larger
press "down down"
shot text-size-larger

# the Theme button's hint beside POWER SLEEP doesn't fit its half at this size
begin
on theme-plus
cfg theme-plus text_size=larger button=Y
press "down down"
shot hints-wrapped

begin
on theme-plus
cfg theme-plus ui_size=compact
press "down down"
shot ui-size-compact

# a card theme, Dusk, with a wallpaper and its own Game Boy wallpaper
tp_dusk() {
	mkdir -p "$CARD/Themes/Dusk/wallpapers"
	printf 'version=1\nname=Dusk\ncolor1=0xF2B66D\ncolor2=0x3B2A4A\ncolor3=0x22182B\ncolor4=0xF5E9DA\ncolor5=0x22182B\ncolor6=0xE0C9B0\ncolor7=0x22182B\nwallpaper=wall.png\n' \
		>"$CARD/Themes/Dusk/theme.txt"
	"$MAGICK" -size 640x480 gradient:'#22182B-#7A4A6B' "$CARD/Themes/Dusk/wall.png"
	"$MAGICK" -size 640x480 radial-gradient:'#F2B66D-#3B2A4A' "$CARD/Themes/Dusk/wallpapers/GB.png"
}

# the picker on Dusk, the first card theme after Stock
begin
on theme-plus
tp_dusk
press "menu wait left a down"
shot picker-preview

begin
on theme-plus
tp_dusk
cfg theme-plus theme=Dusk
press "down down a"
shot system-wallpaper

# a pack of round icons, one colour a system
begin
on theme-plus
mkdir -p "$CARD/Icons/Round"
for icon in GB:#8BAC0F GBC:#C0306B FC:#E04040; do
	"$MAGICK" -size 240x240 xc:none -fill "${icon#*:}" -draw 'circle 120,120 120,12' -fill '#FFFFFF' -draw 'circle 120,120 120,70' \
		"$CARD/Icons/Round/${icon%%:*}.png"
done
cfg theme-plus icon_pack=Round
press "down down"
shot icon-pack
