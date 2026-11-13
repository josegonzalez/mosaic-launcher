# shellcheck shell=sh
# shard: 1

# play data interop, over a fixture game_logs.sqlite with 40 minutes of
# Played an hour ago and nothing of Fresh. home-dashboard's continue card
# shows playtime-captions' caption, or the play time while it's off, and
# its Stats tab playtime-limit's time left while a limit is set, which
# presser switches to. random-game, picking from favorites with Unplayed
# games only, never picks Played, which row-badges' games.played calls
# played from the history alone, and without row-badges counts only
# recents. gamification leaves out the daily play quest the limit leaves
# no time for. The card's
# recents and the launch's /tmp files are put back afterwards. Local time
# is set to about noon, so the session an hour ago falls on today.
PI_ROMS="$CARD/Roms/Played Interop (PIT)"
PI_EMU="$CARD/Emus/desktop/PIT.pak"
PI_GM_STATE="$STATE/gamification"
PI_FAV="$CARD/Collections/Favorites.txt"
PRESSER="$WORK/presser.so"
mkdir -p "$PI_ROMS" "$PI_EMU" "$(dirname "$PLAY_DB")" "$(dirname "$RECENTS")" "$(dirname "$PI_FAV")" "$STATE"
: >"$PI_EMU/launch.sh"
: >"$PI_ROMS/Played.gb"
: >"$PI_ROMS/Fresh.gb"
rm -rf "$PI_GM_STATE" /tmp/mosaic-gamification.launch
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$WORK/played-interop.sqlite.saved"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/played-interop-recent.saved"
[ -f "$PI_FAV" ] && cp "$PI_FAV" "$WORK/played-interop-favorites.saved"
for f in next last.txt resume_slot.txt; do
	[ -f "/tmp/$f" ] && cp "/tmp/$f" "$WORK/played-interop-$f.saved"
done
now=$(date +%s)
hours_behind=$(( $(date -u +%H | sed 's/^0//') - 12 ))
TZ="TST$(printf '%+d' "$hours_behind")"
export TZ
make_play_db <<SQL || fail "making the played interop fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Played', 'Played Interop (PIT)/Played.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 2400, $((now - 3600)));
SQL
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" home-dashboard playtime-captions playtime-limit \
	row-badges random-game gamification favorites >/dev/null || fail "building the played interop plugins"
build_plugin seeker 'id=seeker\nname=Seeker\napi=1\n'
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
dot=$(printf '\302\267')
all_off='home-dashboard=off\nplaytime-captions=off\nplaytime-limit=off\nrow-badges=off\nrandom-game=off\ngamification=off\nfavorites=off\nseeker=off\nscreener=off\n'

# pi_plugins <id=on...>: plugins.txt with only those on, as a plugin it
# doesn't name loads
pi_plugins() {
	printf '%b' "$all_off" >"$STATE/plugins.txt"
	for on in "$@"; do printf '%s\n' "$on" >>"$STATE/plugins.txt"; done
}

# pi_home <name> <id=on...>: home-dashboard and those plugins, presser
# going to the Stats tab and back to Home, so both draw, then to Stats
pi_home() {
	pi_plugins home-dashboard=on "$@"
	PRESSER_KEYS="r1 l1 r1" PRESSER_LOG="$WORK/$1.texts" SMOKE_PRELOAD="$PRESSER"
	export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	run_launcher "$WORK/$1.log" "presser: done" || fail "the launcher exited early with home-dashboard ($1)"
	unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	log="$WORK/$1.log"
	plugin=home-dashboard
}

# home-dashboard with playtime-captions and playtime-limit
printf '/Roms/Played Interop (PIT)/Played.gb\n' >"$RECENTS"
printf 'limit=60\n' >"$STATE/playtime-limit.cfg"
pi_home played-interop-home playtime-captions=on playtime-limit=on
expect "continue line: 40m $dot today"
expect "time left today: 20m of 1h 0m"
grep -qxF "Time left today" "$WORK/played-interop-home.texts" || fail "home-dashboard didn't draw its time left row"

# without them: the play time, and no time left row
pi_home played-interop-home-off
expect "continue line: 40m played"
expect "time left today: none"
grep -qxF "Time left today" "$WORK/played-interop-home-off.texts" && fail "home-dashboard drew a time left row without playtime-limit"

# and with no limit set
printf 'limit=off\n' >"$STATE/playtime-limit.cfg"
pi_home played-interop-home-nolimit playtime-limit=on
expect "time left today: none"
rm -f "$STATE/playtime-limit.cfg"

# random-game from favorites, unplayed only: never Played, which only the
# history has
: >"$RECENTS"
printf '/Roms/Played Interop (PIT)/Played.gb\n/Roms/Played Interop (PIT)/Fresh.gb\n' >"$PI_FAV"
printf 'unplayed_only=on\nfrom=favorites\n' >"$STATE/random-game.cfg"
printf 'steps=action:random-game.pick\n' >"$STATE/seeker.cfg"
for i in 1 2 3; do
	: >"$RECENTS"
	pi_plugins row-badges=on random-game=on favorites=on seeker=on
	log="$WORK/played-interop-random-$i.log"
	run_launcher "$log" && fail "the launcher didn't quit to launch random-game's pick ($i)"
	grep -qF "plugin random-game: picked $PI_ROMS/Fresh.gb" "$log" || fail "random-game didn't pick the unplayed game ($i)"
	rm -f /tmp/mosaic-launcher-launched
done
# without row-badges, only recents count: with Fresh in them, Played
printf '/Roms/Played Interop (PIT)/Fresh.gb\n' >"$RECENTS"
pi_plugins random-game=on favorites=on seeker=on
log="$WORK/played-interop-random-off.log"
run_launcher "$log" && fail "the launcher didn't quit to launch random-game's pick without row-badges"
plugin=random-game
expect "nobody provides games.played, so played games are the recents"
expect "picked $PI_ROMS/Played.gb"
rm -f /tmp/mosaic-launcher-launched "$STATE/random-game.cfg" "$STATE/seeker.cfg"

# gamification with 20 minutes left: the daily 90 minutes are left out
printf 'limit=60\n' >"$STATE/playtime-limit.cfg"
printf 'daily_minutes=90\n' >"$STATE/gamification.cfg"
printf 'steps=action:gamification.open,b\n' >"$STATE/screener.cfg"
pi_plugins gamification=on playtime-limit=on screener=on
log="$WORK/played-interop-quests.log"
run_launcher "$log" "plugin screener: done" || fail "the launcher exited early with gamification"
plugin=gamification
expect "left out play: it needs 50 minutes, and the limit leaves 20"
grep -qF "; play 40/90 left out, games 0/3" "$log" || fail "gamification didn't show the daily quest left out"
grep -q "^left_out=play " "$PI_GM_STATE/state.txt" 2>/dev/null || fail "gamification didn't keep the quest left out"

unset TZ
rm -f "$PLAY_DB" "$PRESSER" "$HOME_LEFT"
[ -f "$WORK/played-interop.sqlite.saved" ] && mv "$WORK/played-interop.sqlite.saved" "$PLAY_DB"
if [ -f "$WORK/played-interop-recent.saved" ]; then cp "$WORK/played-interop-recent.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
if [ -f "$WORK/played-interop-favorites.saved" ]; then mv "$WORK/played-interop-favorites.saved" "$PI_FAV"; else rm -f "$PI_FAV"; fi
for f in next last.txt resume_slot.txt; do
	if [ -f "$WORK/played-interop-$f.saved" ]; then mv "$WORK/played-interop-$f.saved" "/tmp/$f"; else rm -f "/tmp/$f"; fi
done
rm -rf "$PI_ROMS" "$PI_EMU" "$PI_GM_STATE" /tmp/mosaic-gamification.launch "$WORK/played-interop-recent.saved" \
	"$WORK/plugins/home-dashboard.plugin" "$WORK/plugins/playtime-captions.plugin" "$WORK/plugins/playtime-limit.plugin" \
	"$WORK/plugins/row-badges.plugin" "$WORK/plugins/random-game.plugin" "$WORK/plugins/gamification.plugin" \
	"$WORK/plugins/favorites.plugin" "$WORK/plugins/seeker.plugin" "$WORK/plugins/screener.plugin" "$STATE/screener.cfg" \
	"$STATE/playtime-limit.cfg" "$STATE/gamification.cfg" "$STATE/plugins.txt"
rmdir "$CARD/Emus/desktop" 2>/dev/null
case_log=""
