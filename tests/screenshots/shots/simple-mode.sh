# shellcheck shell=sh

begin
on simple-mode
shot default

begin
on simple-mode
press "menu"
shot quick-menu

# the first screen with the kid tag Puzzle: only what has a Puzzle game
begin
on simple-mode tags
cfg simple-mode kid=Puzzle
printf '%s\t%s\n' \
	Puzzle "/Roms/Game Boy (GB)/Libbet and the Magic Floor.gb" \
	Puzzle "/Roms/Nintendo Entertainment System (FC)/Concentration Room.nes" >"$STATE/tags.txt"
shot kid

# Y on a game in kiosk mode, with a PIN set
begin
on simple-mode tags
cfg simple-mode kiosk=on
printf '4826\n' >"$STATE/pin.txt"
press "a y"
shot kiosk
