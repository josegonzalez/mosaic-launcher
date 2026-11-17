# shellcheck shell=sh

# an arcade folder of made-up sets, with tests/screenshots/arcade-names.dat
an_card() {
	mkdir -p "$CARD/Roms/Arcade (FBN)" "$CARD/Emus/desktop/FBN.pak" "$CARD/DATs"
	: >"$CARD/Emus/desktop/FBN.pak/launch.sh"
	cp "$ROOT/tests/screenshots/arcade-names.dat" "$CARD/DATs/"
	for set in mybios mysound boltbr boltbru boltbrj cometc duner3k kittycan; do
		: >"$CARD/Roms/Arcade (FBN)/$set.zip"
	done
}

# Arcade, with clean titles, the BIOS and sound chip hidden, and the
# clones' regions
begin
on arcade-names
an_card
press "down down a"
shot default

# the DAT's full names, with a short one selected, as a long one scrolls
begin
on arcade-names
cfg arcade-names names=Full
an_card
press "down down a down down down"
shot full

# clones hidden while their parent is there
begin
on arcade-names
cfg arcade-names clones=on
an_card
press "down down a"
shot clones
