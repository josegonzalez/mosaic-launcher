# shellcheck shell=sh

# Thwaite's art moved to an Imgs folder, which stock doesn't read
imgs() {
	mkdir -p "$CARD/Roms/Nintendo Entertainment System (FC)/Imgs"
	mv "$CARD/Roms/Nintendo Entertainment System (FC)/.media/Thwaite.png" "$CARD/Roms/Nintendo Entertainment System (FC)/Imgs/"
}

begin
on art-boxart
imgs
press "down down down down a down"
shot default

begin
on art-boxart
cfg art-boxart hide=off
imgs
press "down down down down a down down"
shot hide-off

# a folder's cover.png, under the grid layout, with and without its name
covers() {
	mkdir -p "$CARD/Roms/Game Boy (GB)/Tobu Tobu Girl series"
	mv "$CARD/Roms/Game Boy (GB)/Tobu Tobu Girl.gb" "$CARD/Roms/Game Boy (GB)/Tobu Tobu Girl series/"
	cp "$CARD/Roms/Game Boy (GB)/.media/Tobu Tobu Girl.png" "$CARD/Roms/Game Boy (GB)/Tobu Tobu Girl series/cover.png"
}

begin
on art-boxart layout-grid
slot layout layout-grid
covers
press "right right a"
shot cover-with-layout-grid

begin
on art-boxart layout-grid
slot layout layout-grid
cfg art-boxart cover_name=on
covers
press "right right a"
shot cover-name-with-layout-grid
