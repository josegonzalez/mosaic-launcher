# shellcheck shell=sh

begin
on random-game
press "down down down down down"
shot default

begin
on random-game
cfg random-game tile=on
press "menu wait left"
shot tile

begin
on random-game
cfg random-game from_tile=on
press "menu wait left"
shot from-tile

# the Pick from list, with favorites and a tag to pick from
begin
on favorites tags random-game
cfg random-game from_tile=on
printf '%s\t%s\n' Puzzle "/Roms/Game Boy (GB)/Libbet and the Magic Floor.gb" >"$STATE/tags.txt"
press "menu wait left a"
shot pick-from
