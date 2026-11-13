# shellcheck shell=sh

begin
on favorites focus-mode
shot default

# the Focus on list, from its tile
begin
on favorites tags focus-mode
printf '%s\t%s\n' Puzzle "/Roms/Game Boy (GB)/Libbet and the Magic Floor.gb" >"$STATE/tags.txt"
press "menu wait left a"
shot focus-on

# focused on the Puzzle tag
begin
on tags focus-mode
cfg focus-mode set=tags:Puzzle
printf '%s\t%s\n' \
	Puzzle "/Roms/Game Boy (GB)/Libbet and the Magic Floor.gb" \
	Puzzle "/Roms/Nintendo Entertainment System (FC)/Concentration Room.nes" >"$STATE/tags.txt"
shot with-tags
