# shellcheck shell=sh
# shard: 3

# the bundled activity-stats, over a fixture game_logs.sqlite made with
# gametimectl's schema: screener runs its action, steps from the week's
# chart to the month's, through the games list, most played first, to the
# orphan, then to the launches list, most sessions first, and backs out.
# Then A on a game whose file is on the card opens it, which quits the
# launcher to launch it (the card's recents and the launch's /tmp files
# are put back afterwards), and with no sqlite every tab says there's no
# history. Local time is set to about noon, so midnight is about 12 hours
# ago, and a session over midnight is split between the two days.
AS_ROMS="$CARD/Roms/Activity"
mkdir -p "$AS_ROMS" "$(dirname "$PLAY_DB")" "$STATE"
: >"$AS_ROMS/Tetris.gb"
: >"$AS_ROMS/Zelda.gba"
: >"$AS_ROMS/Puzzle.gb"
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$WORK/game_logs.sqlite.saved"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/recent.txt.saved"
for f in next last.txt resume_slot.txt; do
	[ -f "/tmp/$f" ] && cp "/tmp/$f" "$WORK/$f.saved"
done
now=$(date +%s)
day=86400
hours_behind=$(( now / 3600 % 24 - 12 ))
# whole hours from UTC, so the minutes and seconds past noon are UTC's
midnight=$(( now - 43200 - now / 60 % 60 * 60 - now % 60 ))
make_play_db <<SQL || fail "making the activity stats fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Tetris', 'Activity/Tetris.gb');
INSERT INTO rom(id, type, name, file_path) VALUES(2, 'ORPHAN', 'Metroid', NULL);
INSERT INTO rom(id, type, name, file_path) VALUES(3, '', 'Zelda', 'Activity/Zelda.gba');
INSERT INTO rom(id, type, name, file_path) VALUES(4, '', 'Puzzle', 'Activity/Puzzle.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 600, $((now - 3600)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 1000, $((now - day)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(2, 50, $((now - 3 * day)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(3, 7200, $((now - 2 * day)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(3, 900, $((midnight - 600)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(4, 60, $((now - 4 * day)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(4, 60, $((now - 4 * day + 100)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(4, 60, $((now - 4 * day + 200)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(4, 60, $((now - 4 * day + 300)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(4, 60, $((now - 4 * day + 400)));
SQL
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" activity-stats >/dev/null || fail "building activity-stats"
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
TZ="TST$(printf '%+d' "$hours_behind")"
export TZ
plugin=activity-stats
dot=$(printf '\302\267')

printf 'steps=action:activity-stats.open,right,right,down,down,down,right,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/activity-stats.log" "plugin screener: done" || fail "the launcher exited early with activity-stats"
log="$WORK/activity-stats.log"
expect "history: 4 games, 10 sessions, 10050 seconds"
expect "showing 7 days: days 900 1600 7200 50 300 0 0, axis 7200, 7 days: 2h 47m $dot 23m a day"
expect "showing 30 days: days 900 1600 7200 50 300 0 0 "
expect "showing Games: 0 of 4: Zelda, 2h 15m $dot 2 sessions, can open"
expect "showing Games: 1 of 4: Tetris, 26m $dot 2 sessions, can open"
expect "showing Games: 2 of 4: Puzzle, 5m $dot 5 sessions, can open"
expect "showing Games: 3 of 4: Metroid, <1m $dot 1 session, can't open"
expect "showing Launches: 0 of 4: Puzzle, 5 sessions $dot 5m, can open"
[ "$(seen)" = "gamelist activity-stats.main gamelist " ] || fail "activity-stats didn't open and close: $(seen)"
grep -qF "plugins: activity-stats's screen activity-stats.main's" "$log" && fail "activity-stats's screen was turned off"
grep -qF "plugin activity-stats: couldn't" "$log" && fail "activity-stats couldn't set up: $(grep -F "plugin activity-stats: couldn't" "$log")"

# A on a game on the card opens it
printf 'steps=action:activity-stats.open,right,right,down,a\n' >"$STATE/screener.cfg"
run_launcher "$WORK/activity-stats-open.log" && fail "the launcher didn't quit to launch the game from activity-stats"
log="$WORK/activity-stats-open.log"
expect "open $AS_ROMS/Tetris.gb 1"
rm -f /tmp/mosaic-launcher-launched

# no sqlite: no history
MOSAIC_LAUNCHER_SQLITE="$WORK/no-such-libsqlite3"
export MOSAIC_LAUNCHER_SQLITE
printf 'steps=action:activity-stats.open,right,right,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/activity-stats-nodb.log" "plugin screener: done" || fail "the launcher exited early with activity-stats and no sqlite"
unset MOSAIC_LAUNCHER_SQLITE
log="$WORK/activity-stats-nodb.log"
expect "no play history"
expect "showing 7 days: no play history"
expect "showing Games: no play history"

unset TZ
rm -f "$PLAY_DB"
[ -f "$WORK/game_logs.sqlite.saved" ] && mv "$WORK/game_logs.sqlite.saved" "$PLAY_DB"
if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
for f in next last.txt resume_slot.txt; do
	if [ -f "$WORK/$f.saved" ]; then mv "$WORK/$f.saved" "/tmp/$f"; else rm -f "/tmp/$f"; fi
done
rm -rf "$AS_ROMS" "$WORK/recent.txt.saved" "$WORK/plugins/activity-stats.plugin" "$WORK/plugins/screener.plugin" \
	"$STATE/screener.cfg"
case_log=""
