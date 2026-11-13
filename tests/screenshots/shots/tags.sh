# shellcheck shell=sh

tagged() {
	printf '%s\t%s\n' \
		Puzzle "/Roms/Game Boy (GB)/Libbet and the Magic Floor.gb" \
		Puzzle "/Roms/Nintendo Entertainment System (FC)/Concentration Room.nes" \
		"Two players" "/Roms/Nintendo Entertainment System (FC)/Thwaite.nes" >"$STATE/tags.txt"
}

# Thwaite's checklist
begin
on tags
tagged
press "down down down down a down y"
shot default

begin
on tags
tagged
press "down down down down down"
shot folder

begin
on tags
tagged
press "menu wait left"
shot tiles
