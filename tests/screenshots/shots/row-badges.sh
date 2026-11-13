# shellcheck shell=sh

# tiers in the Game Boy Color games' .info files
tiers() {
	echo "tier=4" >>"$CARD/Roms/Game Boy Color (GBC)/.media/µCity.info"
	echo "tier=2" >>"$CARD/Roms/Game Boy Color (GBC)/.media/Tobu Tobu Girl Deluxe.info"
	echo "tier=5" >>"$CARD/Roms/Game Boy (GB)/.media/Tobu Tobu Girl.info"
}

# µCity, in the Homebrew collection
begin
on row-badges
cfg row-badges unplayed=on
press "down a down a down down down"
shot unplayed

begin
on row-badges
cfg row-badges tiers=on
tiers
press "down a down a down down down"
shot tiers

begin
on row-badges layout-grid
slot layout layout-grid
cfg row-badges unplayed=on tiers=on
tiers
press "right a right a right right right"
shot with-layout-grid

# µCity and Libbet are favorites
begin
on favorites row-badges
cfg row-badges favorites=on
press "down a down a down down down"
shot favorites

# Libbet and Concentration Room tagged Puzzle
begin
on tags row-badges
cfg row-badges tagged=on
printf '%s\t%s\n' \
	Puzzle "/Roms/Game Boy (GB)/Libbet and the Magic Floor.gb" \
	Puzzle "/Roms/Nintendo Entertainment System (FC)/Concentration Room.nes" >"$STATE/tags.txt"
press "down a down a"
shot tagged
