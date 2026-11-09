# shellcheck shell=sh
# shard: 2

# tracker, with history=1, lists the whole play history from a fixture
# game_logs.sqlite made with gametimectl's schema: every game with a
# session, most played first, with a second row for one path and an
# unplayed game left out and an orphan by name; and each of the last days'
# totals, with a session over midnight split between them. The days are a
# read of their own, once however often it asks, and kept with the file
# moved away; a game returning reads both again. Local time is set to
# about noon, so midnight is about 12 hours ago.
PLAY_ROMS="$CARD/Roms/Tracker"
mkdir -p "$(dirname "$PLAY_DB")" "$STATE"
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$WORK/game_logs.sqlite.saved"
now=$(date +%s)
day=86400
hours_behind=$(( now / 3600 % 24 - 12 ))
# whole hours from UTC, so the minutes and seconds past noon are UTC's
midnight=$(( now - 43200 - now / 60 % 60 * 60 - now % 60 ))
make_play_db <<SQL || fail "making the play history fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Tetris', 'Tracker/Tetris.gb');
INSERT INTO rom(id, type, name, file_path) VALUES(2, 'ORPHAN', 'Metroid', NULL);
INSERT INTO rom(id, type, name, file_path) VALUES(3, '', 'Unplayed', 'Tracker/Unplayed.gb');
INSERT INTO rom(id, type, name, file_path) VALUES(4, '', 'Zelda', 'Tracker/Zelda.gba');
INSERT INTO rom(id, type, name, file_path) VALUES(5, '', 'Tetris', 'Tracker/Tetris.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 600, $((now - 3600)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 1000, $((now - day)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(2, 50, $((now - 3 * day)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(4, 7200, $((now - 2 * day)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(4, 900, $((midnight - 600)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(5, 300, $((now - 4 * day)));
SQL
printf 'game1=%s\ndb=%s\nhistory=1\n' "$PLAY_ROMS/Tetris.gb" "$PLAY_DB" >"$STATE/tracker.cfg"
printf 'tracker=on\n' >"$STATE/plugins.txt"
build_plugin tracker 'id=tracker\nname=Tracker\napi=1\n'
TZ="TST$(printf '%+d' "$hours_behind")"
export TZ
run_launcher "$WORK/history.log" "plugin tracker: done" || fail "the launcher exited early with tracker listing the history"
log="$WORK/history.log"
plugin=tracker
zelda="$PLAY_ROMS/Zelda.gba name Zelda time 8100 first $((now - 2 * day)) last $((midnight - 600)) count 2 orphan 0"
tetris="$PLAY_ROMS/Tetris.gb name Tetris time 1600 first $((now - day)) last $((now - 3600)) count 2 orphan 0"
metroid="- name Metroid time 50 first $((now - 3 * day)) last $((now - 3 * day)) count 1 orphan 1"
for label in game kept; do
	expect "$label games 3"
	expect "$label listed 0 $zelda"
	expect "$label listed 1 $tetris"
	expect "$label listed 2 $metroid"
	expect "$label day 0 900"
	expect "$label day 1 1600"
	expect "$label day 2 7200"
	expect "$label day 3 50"
	expect "$label day 4 300"
	expect "$label day -1 -1"
done
expect "game today 900"
expect "game Tetris.gb time 1600 last $((now - 3600)) count 2"
expect "edges -1 -1 -1"
expect "small 0 path Zelda.gba untouched 1"
grep -q "plugin tracker: game listed 3 " "$log" && fail "tracker listed more than three games"
[ "$(grep -c "play history: read " "$log")" -eq 1 ] || fail "the play history wasn't read just once: $(grep "play history:" "$log")"
[ "$(grep -c "play history: days from 6 sessions in $PLAY_DB" "$log")" -eq 1 ] \
	|| fail "the sessions weren't read just once for the days: $(grep "play history:" "$log")"
[ -f "$PLAY_DB" ] || fail "tracker didn't put the database back"

# after a game returns: post_return and the next ask after the launcher's
# stop_all each read both again
touch /tmp/mosaic-launcher-launched
run_launcher "$WORK/history-return.log" "plugin tracker: done" || fail "the launcher exited early with tracker after a launch"
log="$WORK/history-return.log"
expect "post_return games 3"
expect "post_return listed 0 $zelda"
expect "post_return day 1 1600"
expect "game day 4 300"
[ "$(grep -c "play history: days from 6 sessions in $PLAY_DB" "$log")" -eq 2 ] \
	|| fail "post_return and the next ask didn't both read the days: $(grep "play history:" "$log")"
rm -f /tmp/mosaic-launcher-launched

# without the database: unknown, and the days aren't tried
rm -f "$PLAY_DB"
run_launcher "$WORK/history-nodb.log" "plugin tracker: done" || fail "the launcher exited early with tracker and no database"
log="$WORK/history-nodb.log"
expect "game games -1"
expect "game day 0 -1"
expect "game day 1 -1"
expect "edges -1 -1 -1"
expect "small -1 path - untouched 1"
[ "$(grep -c "play history: unknown, no $PLAY_DB" "$log")" -eq 1 ] || fail "the missing database wasn't logged once"
grep -q "play history: days from" "$log" && fail "the days were read with no database"
[ -e "$PLAY_DB" ] && fail "the launcher made a database"
unset TZ
[ -f "$WORK/game_logs.sqlite.saved" ] && mv "$WORK/game_logs.sqlite.saved" "$PLAY_DB"
rm -rf "$WORK/plugins/tracker.plugin" "$STATE/tracker.cfg" "$STATE/plugins.txt"
