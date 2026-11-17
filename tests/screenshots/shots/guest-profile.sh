# shellcheck shell=sh

# the Guest tile on the quick menu, before Clear guest data
begin
on guest-profile
press "menu wait left left"
shot default

# guest mode on, from the tile: the mark, and the guest's own Recently Played
begin
on guest-profile
mkdir -p "$CARD/.userdata/shared/launcher/profiles/guest"
printf '%s\n' "/Roms/Nintendo Entertainment System (FC)/Concentration Room.nes" \
	>"$CARD/.userdata/shared/launcher/profiles/guest/recent.txt"
press "menu wait left left a b"
shot guest

# Clear guest data asks first
begin
on guest-profile
press "menu wait left a"
shot clear
