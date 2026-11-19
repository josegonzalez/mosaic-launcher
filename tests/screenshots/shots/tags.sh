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

# Libbet's Status menu, Beaten and Completed, with the Status button on Y
# beside the Tags button
begin
on tags
cfg tags status_button=Y
printf '%s\t%s\n' \
	beaten "/Roms/Game Boy (GB)/Libbet and the Magic Floor.gb" \
	completed "/Roms/Game Boy (GB)/Libbet and the Magic Floor.gb" >"$STATE/status.txt"
press "down a down a y down a"
shot status
