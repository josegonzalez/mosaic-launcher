# shellcheck shell=sh

# the systems numbered in their folders' names
numbered() {
	mv "$CARD/Roms/Nintendo Entertainment System (FC)" "$CARD/Roms/1) Nintendo Entertainment System (FC)"
	mv "$CARD/Roms/Game Boy (GB)" "$CARD/Roms/2) Game Boy (GB)"
	mv "$CARD/Roms/Game Boy Color (GBC)" "$CARD/Roms/10) Game Boy Color (GBC)"
}

begin
on numeric-prefix
numbered
press "down down"
shot default
