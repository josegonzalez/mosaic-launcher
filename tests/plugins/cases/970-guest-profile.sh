# shellcheck shell=sh

# the bundled guest-profile, with favorites and tags, seeker running its
# actions: switching to the guest parks the owner's recents, play history
# and Collections untouched in the launcher's profiles folder, and the
# guest favourites and launches a game into data of their own, tags
# reading the guest's tags.txt. Guest mode stays on as the launcher comes
# back from the game. Switching back puts the owner's data back as it
# was and keeps the guest's. With guest-profile off and the guest's data
# in place, the launcher puts the owner's back as it starts. Clear guest
# data asks, then removes the guest's data, leaving the owner's. The
# card's recents, play history, Collections, /tmp/next and /tmp/last.txt
# are put back afterwards.
GP_ROMS="$CARD/Roms/Guest Profile (GPR)"
GP_EMU="$CARD/Emus/desktop/GPR.pak"
GP_SHARED="$CARD/.userdata/shared"
GP_PARKS="$GP_SHARED/launcher/profiles"
GP_LIVE="$GP_SHARED/launcher/profile.txt"
GP_COLLECTIONS="$CARD/Collections"
GP_SAVED="$WORK/guest-profile-saved"
rm -rf "$GP_ROMS" "$GP_SAVED" "$GP_PARKS" "$GP_LIVE"
mkdir -p "$GP_ROMS" "$GP_EMU" "$GP_SAVED" "$STATE" "$(dirname "$RECENTS")"
: >"$GP_EMU/launch.sh"
for game in Alpha Bravo; do : >"$GP_ROMS/$game.gb"; done
[ -f /tmp/next ] && cp /tmp/next "$GP_SAVED/next"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$GP_SAVED/last.txt"
[ -f "$RECENTS" ] && mv "$RECENTS" "$GP_SAVED/recent.txt"
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$GP_SAVED/game_logs.sqlite"
[ -d "$GP_COLLECTIONS" ] && mv "$GP_COLLECTIONS" "$GP_SAVED/Collections"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" guest-profile favorites tags >/dev/null \
	|| fail "building guest-profile, favorites and tags"
build_plugin seeker 'id=seeker\nname=Seeker\napi=1\n'

# the plugins named on, and every other plugin built so far off
gp_plugins() {
	: >"$STATE/plugins.txt"
	for dir in "$WORK"/plugins/*.plugin; do
		id=$(basename "$dir" .plugin)
		case " $* " in
		*" $id "*) printf '%s=on\n' "$id" >>"$STATE/plugins.txt" ;;
		*) printf '%s=off\n' "$id" >>"$STATE/plugins.txt" ;;
		esac
	done
}

# the owner's data
printf '%s\n' "/Roms/Guest Profile (GPR)/Bravo.gb" >"$RECENTS"
make_play_db <<SQL || fail "making the owner's play history"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Bravo', 'Guest Profile (GPR)/Bravo.gb');
INSERT INTO play_activity(rom_id, play_time) VALUES(1, 900);
SQL
mkdir -p "$GP_COLLECTIONS"
printf '%s\n' "/Roms/Guest Profile (GPR)/Bravo.gb" >"$GP_COLLECTIONS/Favorites.txt"
printf 'Owner\t%s\n' "/Roms/Guest Profile (GPR)/Bravo.gb" >"$STATE/tags.txt"
owner_db=$(md5_of "$PLAY_DB")

# to the guest, who favourites Alpha and launches it
gp_plugins guest-profile favorites tags seeker
printf '%s' "$GP_ROMS/Alpha.gb" >/tmp/last.txt
printf 'steps=%s\n' "action:guest-profile.toggle,wait,action:favorites.toggle,wait,a" >"$STATE/seeker.cfg"
log="$WORK/guest-profile.log"
run_launcher "$log" && fail "the launcher didn't quit to launch a game as the guest"
plugin=guest-profile
expect "the owner's profile is live"
expect "guest on"
plugin=favorites
expect "profile changed: 0 favorites"
expect "added /Roms/Guest Profile (GPR)/Alpha.gb"
plugin=tags
expect "profile changed: 0 tags, 1 announced"
grep -qF "profiles: guest-profile switched from the owner's to guest" "$log" || fail "the launcher didn't log the switch"
# the quick menu is built again once tags read the guest's tags, so the owner's tag's tile goes at once
tiles=$(sed -n "/profiles: guest-profile switched/,\$p" "$log" | grep -F "quick menu: top row" | head -n 1)
[ "$tiles" = "[INFO] quick menu: top row Games,Tools,Guest,Clear guest" ] || fail "the quick menu after the switch was \"$tiles\""
[ "$(cat "$GP_LIVE" 2>/dev/null)" = guest ] || fail "the guest isn't live in $GP_LIVE"
# the owner's, parked untouched
[ "$(cat "$GP_PARKS/owner/recent.txt" 2>/dev/null)" = "/Roms/Guest Profile (GPR)/Bravo.gb" ] || fail "the owner's recents changed"
[ "$(md5_of "$GP_PARKS/owner/game_logs.sqlite" 2>/dev/null)" = "$owner_db" ] || fail "the owner's play history changed"
[ "$(cat "$GP_PARKS/owner/Collections/Favorites.txt" 2>/dev/null)" = "/Roms/Guest Profile (GPR)/Bravo.gb" ] \
	|| fail "the owner's favorites changed"
grep -qF Owner "$STATE/tags.txt" || fail "the owner's tags changed"
# the guest's own
[ "$(head -n 1 "$RECENTS" 2>/dev/null | cut -f1)" = "/Roms/Guest Profile (GPR)/Alpha.gb" ] || fail "the guest's launch isn't in its recents"
grep -qF Bravo "$RECENTS" && fail "the guest's recents have the owner's"
[ "$(cat "$GP_COLLECTIONS/Favorites.txt" 2>/dev/null)" = "/Roms/Guest Profile (GPR)/Alpha.gb" ] || fail "the guest's favorite wasn't saved in its own"
[ -e "$PLAY_DB" ] && fail "the guest has the owner's play history"

# back from the game: still the guest
touch /tmp/mosaic-launcher-launched
printf 'steps=%s\n' "wait" >"$STATE/seeker.cfg"
log="$WORK/guest-profile-return.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early back from the guest's game"
rm -f /tmp/mosaic-launcher-launched
plugin=guest-profile
expect "the guest's profile is live"
expect "mark "
[ "$(cat "$GP_LIVE" 2>/dev/null)" = guest ] || fail "the guest isn't live back from the game"

# back to the owner: the owner's as they were, the guest's kept
printf 'steps=%s\n' "action:guest-profile.toggle,wait" >"$STATE/seeker.cfg"
log="$WORK/guest-profile-owner.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early switching back to the owner"
plugin=guest-profile
expect "guest off"
plugin=favorites
expect "profile changed: 1 favorites"
[ -e "$GP_LIVE" ] && fail "the launcher still names a live profile in $GP_LIVE"
[ "$(cat "$RECENTS" 2>/dev/null)" = "/Roms/Guest Profile (GPR)/Bravo.gb" ] || fail "the owner's recents aren't back"
[ "$(md5_of "$PLAY_DB" 2>/dev/null)" = "$owner_db" ] || fail "the owner's play history isn't back as it was"
[ "$(cat "$GP_COLLECTIONS/Favorites.txt" 2>/dev/null)" = "/Roms/Guest Profile (GPR)/Bravo.gb" ] || fail "the owner's favorites aren't back"
grep -qF Alpha "$GP_PARKS/guest/recent.txt" 2>/dev/null || fail "the guest's recents weren't kept"
grep -qF Alpha "$GP_PARKS/guest/Collections/Favorites.txt" 2>/dev/null || fail "the guest's favorites weren't kept"
[ -e "$GP_PARKS/owner/recent.txt" ] && fail "the owner's recents are still parked"

# the guest live with guest-profile off: the owner's back as it starts
printf 'steps=%s\n' "action:guest-profile.toggle,wait" >"$STATE/seeker.cfg"
run_launcher "$WORK/guest-profile-again.log" "plugin seeker: done" || fail "the launcher exited early switching to the guest again"
[ "$(cat "$GP_LIVE" 2>/dev/null)" = guest ] || fail "the guest isn't live again"
grep -qF Alpha "$RECENTS" || fail "the guest's recents didn't come back"
gp_plugins favorites tags seeker
printf 'steps=%s\n' "wait" >"$STATE/seeker.cfg"
log="$WORK/guest-profile-off.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early with guest-profile off"
grep -qF "profiles: the launcher, as no plugin provides profile.guest, switched from guest to the owner's" "$log" \
	|| fail "the launcher didn't put the owner's data back with guest-profile off"
[ -e "$GP_LIVE" ] && fail "the guest is still live with guest-profile off"
[ "$(cat "$RECENTS" 2>/dev/null)" = "/Roms/Guest Profile (GPR)/Bravo.gb" ] || fail "the owner's recents aren't back with guest-profile off"

# Clear guest data asks, then removes the guest's, leaving the owner's
mkdir -p "$STATE/profiles/guest"
printf 'Guest\t%s\n' "/Roms/Guest Profile (GPR)/Alpha.gb" >"$STATE/profiles/guest/tags.txt"
gp_plugins guest-profile favorites tags seeker
printf 'steps=%s\n' "action:guest-profile.clear,wait,a,wait" >"$STATE/seeker.cfg"
log="$WORK/guest-profile-clear.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early clearing the guest's data"
plugin=guest-profile
expect "cleared the guest's data"
grep -qF "plugins: guest-profile cleared profile guest" "$log" || fail "the launcher didn't log clearing the guest"
[ -e "$GP_PARKS/guest" ] && fail "the guest's parked data is still there"
[ -e "$STATE/profiles/guest" ] && fail "the guest's tags are still there"
[ "$(cat "$RECENTS" 2>/dev/null)" = "/Roms/Guest Profile (GPR)/Bravo.gb" ] || fail "clearing the guest changed the owner's recents"
[ "$(md5_of "$PLAY_DB" 2>/dev/null)" = "$owner_db" ] || fail "clearing the guest changed the owner's play history"
[ "$(cat "$GP_COLLECTIONS/Favorites.txt" 2>/dev/null)" = "/Roms/Guest Profile (GPR)/Bravo.gb" ] || fail "clearing the guest changed the owner's favorites"
grep -qF Owner "$STATE/tags.txt" || fail "clearing the guest changed the owner's tags"

rm -rf "$GP_ROMS" "$GP_EMU" "$GP_PARKS" "$GP_LIVE" "$GP_COLLECTIONS" "$STATE/profiles"
rmdir "$CARD/Emus/desktop" 2>/dev/null
rm -f "$RECENTS" "$PLAY_DB"
[ -f "$GP_SAVED/recent.txt" ] && mv "$GP_SAVED/recent.txt" "$RECENTS"
[ -f "$GP_SAVED/game_logs.sqlite" ] && mv "$GP_SAVED/game_logs.sqlite" "$PLAY_DB"
[ -d "$GP_SAVED/Collections" ] && mv "$GP_SAVED/Collections" "$GP_COLLECTIONS"
if [ -f "$GP_SAVED/next" ]; then mv "$GP_SAVED/next" /tmp/next; else rm -f /tmp/next; fi
if [ -f "$GP_SAVED/last.txt" ]; then mv "$GP_SAVED/last.txt" /tmp/last.txt; else rm -f /tmp/last.txt; fi
rm -rf "$GP_SAVED" "$WORK/plugins/guest-profile.plugin" "$WORK/plugins/favorites.plugin" "$WORK/plugins/tags.plugin" \
	"$WORK/plugins/seeker.plugin" "$STATE/seeker.cfg" "$STATE/tags.txt" "$STATE/plugins.txt"
case_log=""
