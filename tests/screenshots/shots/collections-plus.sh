# shellcheck shell=sh

# the Collections folder, with the Homebrew collection's art
collection_art() {
	mkdir -p "$CARD/Collections/.media"
	cp "$CARD/Roms/Nintendo Entertainment System (FC)/.media/Concentration Room.png" "$CARD/Collections/.media/Homebrew.png"
}

begin
on collections-plus
collection_art
press "down a down"
shot default

begin
on collections-plus
cfg collections-plus "promote=*"
collection_art
press "down down down down down down"
shot promote
