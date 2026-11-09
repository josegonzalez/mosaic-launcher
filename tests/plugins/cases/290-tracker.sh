# shellcheck shell=sh
# shard: 1

# tracker reads the play history from a fixture game_logs.sqlite made with
# gametimectl's schema: per game totals, orphans by name, today's total,
# one read however often it asks, answers kept with the file moved away,
# and a fresh read after a game returns. Local time is set to about noon,
# so the sessions an hour ago are today's.
PLAY_ROMS="$CARD/Roms/Tracker"
mkdir -p "$(dirname "$PLAY_DB")" "$STATE"
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$WORK/game_logs.sqlite.saved"
now=$(date +%s)
hours_behind=$(( $(date -u +%H | sed 's/^0//') - 12 ))
make_play_db <<SQL || fail "making the play history fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Tetris', 'Tracker/Tetris.gb');
INSERT INTO rom(id, type, name, file_path) VALUES(2, 'ORPHAN', 'Metroid', NULL);
INSERT INTO rom(id, type, name, file_path) VALUES(3, '', 'Unplayed', 'Tracker/Unplayed.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 1000, $((now - 3 * 86400)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 600, $((now - 3600)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(2, 50, $((now - 10 * 86400)));
SQL
printf 'game1=%s\ngame2=%s\ngame3=%s\ngame4=%s\ndb=%s\n' "$PLAY_ROMS/Tetris.gb" "$PLAY_ROMS/Metroid.nes" \
	"$PLAY_ROMS/Unplayed.gb" "$CARD/Tools/desktop/Files.pak" "$PLAY_DB" >"$STATE/tracker.cfg"
printf 'tracker=on\n' >"$STATE/plugins.txt"
build_plugin tracker 'id=tracker\nname=Tracker\napi=1\n'
TZ="TST$(printf '%+d' "$hours_behind")"
export TZ
run_launcher "$WORK/tracker.log" "plugin tracker: done" || fail "the launcher exited early with tracker loaded"
log="$WORK/tracker.log"
plugin=tracker
expect "game Tetris.gb time 1600 last $((now - 3600)) count 2"
expect "game Metroid.nes time 50 last $((now - 10 * 86400)) count 1"
expect "game Unplayed.gb time 0 last 0 count 0"
expect "game Files.pak time 0 last 0 count 0"
expect "game today 600"
expect "null -1 -1 -1"
expect "kept Tetris.gb time 1600 last $((now - 3600)) count 2"
expect "kept today 600"
[ "$(grep -c "play history: read " "$log")" -eq 1 ] || fail "the play history wasn't read just once: $(grep "play history:" "$log")"
grep -qF "play history: read 3 games and 1 session since midnight from $PLAY_DB" "$log" || fail "the read wasn't logged"
# read only once tracker asked
asked=$(grep -n "plugin tracker: asking" "$log" | cut -d: -f1)
read_at=$(grep -n "play history: read " "$log" | cut -d: -f1)
if [ -z "$asked" ] || [ -z "$read_at" ] || [ "$read_at" -le "$asked" ]; then
	fail "the play history was read before tracker asked"
fi
[ -f "$PLAY_DB" ] || fail "tracker didn't put the database back"

# after a game returns: post_return gets a fresh read, and so does the
# next ask after the launcher's stop_all
touch /tmp/mosaic-launcher-launched
run_launcher "$WORK/tracker-return.log" "plugin tracker: done" || fail "the launcher exited early with tracker after a launch"
log="$WORK/tracker-return.log"
expect "post_return today 600"
expect "game Tetris.gb time 1600 last $((now - 3600)) count 2"
[ "$(grep -c "play history: read " "$log")" -eq 2 ] || fail "post_return and the next ask didn't both read: $(grep "play history:" "$log")"
rm -f /tmp/mosaic-launcher-launched

# without sqlite: unknown, logged once
MOSAIC_LAUNCHER_SQLITE="$WORK/no-such-libsqlite3"
export MOSAIC_LAUNCHER_SQLITE
run_launcher "$WORK/tracker-nolib.log" "plugin tracker: done" || fail "the launcher exited early with tracker and no sqlite"
unset MOSAIC_LAUNCHER_SQLITE
log="$WORK/tracker-nolib.log"
expect "game Tetris.gb time -1 last -1 count -1"
expect "game today -1"
[ "$(grep -c "play history: unknown, no sqlite library" "$log")" -eq 1 ] || fail "the missing library wasn't logged once"

# without the database: unknown, logged once
rm -f "$PLAY_DB"
run_launcher "$WORK/tracker-nodb.log" "plugin tracker: done" || fail "the launcher exited early with tracker and no database"
log="$WORK/tracker-nodb.log"
expect "game Tetris.gb time -1 last -1 count -1"
expect "kept today -1"
[ "$(grep -c "play history: unknown, no $PLAY_DB" "$log")" -eq 1 ] || fail "the missing database wasn't logged once"
[ -e "$PLAY_DB" ] && fail "the launcher made a database"
unset TZ
[ -f "$WORK/game_logs.sqlite.saved" ] && mv "$WORK/game_logs.sqlite.saved" "$PLAY_DB"
rm -rf "$WORK/plugins/tracker.plugin" "$STATE/tracker.cfg" "$STATE/plugins.txt"
