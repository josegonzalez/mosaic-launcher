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

# Homebrew between the consoles, with its icon
begin
on collections-plus
cfg collections-plus "promote=*" "order=Game Boy, Homebrew, Game Boy Color"
mkdir -p "$CARD/Collections/.icons"
cp "$CARD/Roms/Game Boy (GB)/.media/Libbet and the Magic Floor.png" "$CARD/Collections/.icons/Homebrew.png"
press "down down down"
shot rows
