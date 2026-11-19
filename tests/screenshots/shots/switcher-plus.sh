# shellcheck shell=sh

begin
on switcher-plus
press "select"
shot default

begin
on switcher-plus
cfg switcher-plus style=strip
press "select"
shot style-strip

begin
on switcher-plus
cfg switcher-plus style=list
press "select"
shot style-list

begin
on switcher-plus playtime-captions
cfg switcher-plus captions=on
press "select"
shot captions-with-playtime-captions

# each game's thumbnail copied in as its box, in nx-redux's .media/boxart2d
boxes() {
	for dir in "$CARD"/Roms/*/.media; do
		mkdir -p "$dir/boxart2d"
		for art in "$dir"/*.png; do cp "$art" "$dir/boxart2d/"; done
	done
}

begin
on art-boxart switcher-plus
cfg switcher-plus style=boxart
boxes
press "select"
shot style-boxart

# Libbet's long name first among more recents, so names take two lines
begin
on switcher-plus
cfg switcher-plus style=list
printf '%s\n' "/Roms/Game Boy (GB)/Libbet and the Magic Floor.gb" "/Roms/Game Boy Color (GBC)/Tobu Tobu Girl Deluxe.gbc" \
	"/Roms/Game Boy (GB)/Tobu Tobu Girl.gb" "/Roms/Game Boy Color (GBC)/µCity.gbc" \
	"/Roms/Nintendo Entertainment System (FC)/Thwaite.nes" >"$CARD/.userdata/shared/.minui/recent.txt"
press "select"
shot long-names
