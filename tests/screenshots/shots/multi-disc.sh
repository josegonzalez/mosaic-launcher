# shellcheck shell=sh

# Tobu Tobu Girl split into two discs, shown as one game with Disc 1's art
md_discs() {
	gb="$CARD/Roms/Game Boy (GB)"
	mv "$gb/Tobu Tobu Girl.gb" "$gb/Tobu Tobu Girl (Disc 1).gb"
	: >"$gb/Tobu Tobu Girl (Disc 2).gb"
	mv "$gb/.media/Tobu Tobu Girl.png" "$gb/.media/Tobu Tobu Girl (Disc 1).png"
}

# Game Boy, with the two discs as one row
begin
on multi-disc
md_discs
press "down down a down"
shot default

# Y on it: Combine discs asks first
begin
on multi-disc
md_discs
press "down down a down y"
shot combine
