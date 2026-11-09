# shellcheck shell=sh
# shard: 2

# the bundled gamification, over a fixture game_logs.sqlite with 40 minutes
# of Tetris today and 10 minutes on each of the two days before: screener
# runs its action and backs out, and the screen shows a 3 day streak and
# the daily play quest done. Then opener launches a game the history has
# never seen, which quits the launcher, and the next start counts its
# return, completing the never played quest, and keeps both in its state
# file. The card's recents and the launch's /tmp files are put back
# afterwards. Local time is set to about noon, so the sessions an hour and
# whole days ago fall on today and the days before.
GM_ROMS="$CARD/Roms/Quests"
GM_STATE="$STATE/gamification"
GM_LAUNCH=/tmp/mosaic-gamification.launch
mkdir -p "$GM_ROMS" "$(dirname "$PLAY_DB")" "$STATE"
: >"$GM_ROMS/Tetris.gb"
: >"$GM_ROMS/Zelda.gb"
rm -rf "$GM_STATE" "$GM_LAUNCH"
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$WORK/game_logs.sqlite.saved"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/recent.txt.saved"
for f in next last.txt resume_slot.txt; do
	[ -f "/tmp/$f" ] && cp "/tmp/$f" "$WORK/$f.saved"
done
now=$(date +%s)
day=86400
hours_behind=$(( now / 3600 % 24 - 12 ))
make_play_db <<SQL || fail "making the gamification fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Tetris', 'Quests/Tetris.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 2400, $((now - 3600)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 600, $((now - day)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 600, $((now - 2 * day)));
SQL
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" gamification >/dev/null || fail "building gamification"
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
build_plugin opener 'id=opener\nname=Opener\napi=1\n'
TZ="TST$(printf '%+d' "$hours_behind")"
export TZ
plugin=gamification
dot=$(printf '\302\267')

# the screen
printf 'gamification=on\nscreener=on\nopener=off\n' >"$STATE/plugins.txt"
printf 'steps=action:gamification.open,down,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/gamification.log" "plugin screener: done" || fail "the launcher exited early with gamification"
log="$WORK/gamification.log"
expect "quests: play 30 minutes, 3 games, 5 days and 5 hours a week; streak at 5 minutes"
expect "quest done: play (Play for 30 minutes today)"
expect "showing 3 day streak, Best 3 days $dot 1 quest done; play 40/30 done, games 0/3, fresh 0/1, revisit 0/1, week_days "
[ "$(seen)" = "gamelist gamification.main gamelist " ] || fail "gamification didn't open and close: $(seen)"
grep -qF "plugins: gamification's screen gamification.main's" "$log" && fail "gamification's screen was turned off"
grep -qxF "best=3" "$GM_STATE/state.txt" 2>/dev/null || fail "gamification didn't keep the best streak"

# a launch of a game the history hasn't seen quits the launcher
printf 'gamification=on\nscreener=off\nopener=on\n' >"$STATE/plugins.txt"
printf 'rom=%s\n' "$GM_ROMS/Zelda.gb" >"$STATE/opener.cfg"
run_launcher "$WORK/gamification-launch.log" && fail "the launcher didn't quit to launch the game with gamification"
log="$WORK/gamification-launch.log"
expect "launch $GM_ROMS/Zelda.gb: fresh 1, revisit 0"
[ -f "$GM_LAUNCH" ] || fail "gamification didn't keep the launch"

# and the next start counts its return
printf 'gamification=on\nscreener=off\nopener=off\n' >"$STATE/plugins.txt"
run_launcher "$WORK/gamification-return.log" "plugin gamification: toast: " \
	|| fail "the launcher exited early back from a game with gamification"
log="$WORK/gamification-return.log"
grep -qE "plugin gamification: return $GM_ROMS/Zelda.gb: [0-9]+ seconds$" "$log" || fail "gamification didn't count the return"
expect "quest done: fresh (Start a game you've never played)"
expect "toast: Quest done: Start a game you've never played"
[ -f "$GM_LAUNCH" ] && fail "gamification kept the launch past its return"
grep -qxF "completed=2" "$GM_STATE/state.txt" 2>/dev/null || fail "gamification didn't count the quests done: $(cat "$GM_STATE/state.txt" 2>/dev/null)"
rm -f /tmp/mosaic-launcher-launched

unset TZ
rm -f "$PLAY_DB"
[ -f "$WORK/game_logs.sqlite.saved" ] && mv "$WORK/game_logs.sqlite.saved" "$PLAY_DB"
if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
for f in next last.txt resume_slot.txt; do
	if [ -f "$WORK/$f.saved" ]; then mv "$WORK/$f.saved" "/tmp/$f"; else rm -f "/tmp/$f"; fi
done
rm -rf "$GM_ROMS" "$GM_STATE" "$GM_LAUNCH" "$WORK/recent.txt.saved" "$WORK/plugins/gamification.plugin" \
	"$WORK/plugins/screener.plugin" "$WORK/plugins/opener.plugin" "$STATE/screener.cfg" "$STATE/opener.cfg" \
	"$STATE/plugins.txt"
case_log=""
