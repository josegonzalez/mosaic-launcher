# shellcheck shell=sh

# Concentration Room hidden
hide() { echo "$CARD/Roms/Nintendo Entertainment System (FC)/Concentration Room.nes" >"$STATE/hidden.txt"; }

begin
on hidden
hide
press "down down down down down"
shot default

begin
on hidden
hide
press "down down down down down a"
shot folder
