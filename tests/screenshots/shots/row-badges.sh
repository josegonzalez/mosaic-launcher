# shellcheck shell=sh

# tiers in the Game Boy Color games' .info files
tiers() {
	echo "tier=4" >>"$CARD/Roms/Game Boy Color (GBC)/.media/µCity.info"
	echo "tier=2" >>"$CARD/Roms/Game Boy Color (GBC)/.media/Tobu Tobu Girl Deluxe.info"
	echo "tier=5" >>"$CARD/Roms/Game Boy (GB)/.media/Tobu Tobu Girl.info"
}

# µCity, in the Homebrew collection
begin
on row-badges
cfg row-badges unplayed=on
press "down a down a down down down"
shot unplayed

begin
on row-badges
cfg row-badges tiers=on
tiers
press "down a down a down down down"
shot tiers

begin
on row-badges layout-grid
slot layout layout-grid
cfg row-badges unplayed=on tiers=on
tiers
press "right a right a right right right"
shot with-layout-grid

# µCity and Libbet are favorites
begin
on favorites row-badges
cfg row-badges favorites=on
press "down a down a down down down"
shot favorites

# Libbet and Concentration Room tagged Puzzle
begin
on tags row-badges
cfg row-badges tagged=on
printf '%s\t%s\n' \
	Puzzle "/Roms/Game Boy (GB)/Libbet and the Magic Floor.gb" \
	Puzzle "/Roms/Nintendo Entertainment System (FC)/Concentration Room.nes" >"$STATE/tags.txt"
press "down a down a"
shot tagged

# Libbet and Thwaite have manuals, from tests/screenshots/manuals
begin
on manuals row-badges
cfg row-badges manual=on
mkdir -p "$CARD/Roms/Nintendo Entertainment System (FC)/.manuals" "$CARD/Roms/Game Boy (GB)/.manuals"
cp -R "$ROOT/tests/screenshots/manuals/Thwaite" "$CARD/Roms/Nintendo Entertainment System (FC)/.manuals/"
cp "$ROOT/tests/screenshots/manuals/Libbet and the Magic Floor.txt" "$CARD/Roms/Game Boy (GB)/.manuals/"
press "down a down a"
shot with-manuals

# Libbet Beaten and Completed, µCity Broken and Backlog, and Tobu Tobu Girl
# with both of its achievements unlocked, on Tobu Tobu Girl Deluxe so the
# others aren't under the selection, in minarch's .ra cache format:
# [u32 length][json][sha256 of json]
ra_frame() {
	len=$(printf '%s' "$1" | wc -c | tr -d ' ')
	# shellcheck disable=SC2059 # the format is the length's four bytes
	{
		printf "$(printf '\\%03o\\%03o\\%03o\\%03o' $((len & 255)) $((len >> 8 & 255)) $((len >> 16 & 255)) $((len >> 24 & 255)))"
		printf '%s' "$1"
		printf '%s' "$1" | openssl dgst -sha256 -binary
	} >"$2"
}
begin
on tags row-badges
cfg row-badges completed=on beaten=on broken=on backlog=on perfect=on
printf '%s\t%s\n' \
	beaten "/Roms/Game Boy (GB)/Libbet and the Magic Floor.gb" \
	completed "/Roms/Game Boy (GB)/Libbet and the Magic Floor.gb" \
	broken "/Roms/Game Boy Color (GBC)/µCity.gbc" \
	backlog "/Roms/Game Boy Color (GBC)/µCity.gbc" >"$STATE/status.txt"
tobu="$CARD/Roms/Game Boy (GB)/Tobu Tobu Girl.gb"
printf 'tobu tobu girl screenshot cart %.0s' 1 2 3 4 5 6 7 8 >"$tobu"
if command -v md5sum >/dev/null; then hash=$(md5sum "$tobu" | cut -d' ' -f1); else hash=$(md5 -q "$tobu"); fi
mkdir -p "$CARD/.userdata/shared/.ra/offline/cache"
ra_frame '{"Success":true,"GameId":1,"Sets":[{"Type":"core","Achievements":[{"ID":1,"Flags":3,"Points":5},{"ID":2,"Flags":3,"Points":10}]}]}' \
	"$CARD/.userdata/shared/.ra/offline/cache/achievementsets_$hash.bin"
ra_frame '{"Success":true,"Unlocks":[{"ID":1,"When":1},{"ID":2,"When":2}],"ServerNow":3}' \
	"$CARD/.userdata/shared/.ra/offline/cache/startsession_$hash.bin"
press "down a down a down down"
shot status
