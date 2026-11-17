# shellcheck shell=sh

# Thwaite's two sample pages and Libbet's sample text manual, from
# tests/screenshots/manuals
manuals() {
	mkdir -p "$CARD/Roms/Nintendo Entertainment System (FC)/.manuals" "$CARD/Roms/Game Boy (GB)/.manuals"
	cp -R "$ROOT/tests/screenshots/manuals/Thwaite" "$CARD/Roms/Nintendo Entertainment System (FC)/.manuals/"
	cp "$ROOT/tests/screenshots/manuals/Libbet and the Magic Floor.txt" "$CARD/Roms/Game Boy (GB)/.manuals/"
}

# NES with Thwaite selected: Y opens its manual
begin
on manuals
manuals
press "down down down down a down"
shot default

# Y on Thwaite: its first page, fitted to the screen
begin
on manuals
manuals
press "down down down down a down y"
shot page

# A on it: twice the size, panned with the d-pad
begin
on manuals
manuals
press "down down down down a down y a down"
shot zoom

# Y on Libbet: its text manual
begin
on manuals
manuals
press "down down a y"
shot text
