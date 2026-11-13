# shellcheck shell=sh

begin
on theme-plus
press "menu wait left a"
shot default

for theme in nord dracula solarized-dark pocket-green high-contrast; do
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

begin
on theme-plus
cfg theme-plus ui_size=compact
press "down down"
shot ui-size-compact
