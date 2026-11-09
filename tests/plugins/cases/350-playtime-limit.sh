# shellcheck shell=sh
# shard: 3

# the bundled playtime-limit refuses a game once today's play, from a
# fixture game_logs.sqlite with 40 minutes played an hour ago, reaches the
# daily limit, offering the code; allows it under the limit, counting
# today's grant, and when the history can't be read; and shows the time
# left as a header widget, which spotter reads back. opener launches the
# game; a launch that goes ahead quits the launcher, and the card's
# recents and the launch's /tmp files are put back afterwards.
LIMIT_ROM="$CARD/Roms/.limit-test/Tetris.gb"
mkdir -p "$(dirname "$LIMIT_ROM")" "$(dirname "$PLAY_DB")" "$STATE"
: >"$LIMIT_ROM"
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$WORK/game_logs.sqlite.saved"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/recent.txt.saved"
for f in next last.txt resume_slot.txt; do
	[ -f "/tmp/$f" ] && cp "/tmp/$f" "$WORK/$f.saved"
done
now=$(date +%s)
hours_behind=$(( $(date -u +%H | sed 's/^0//') - 12 ))
TZ="TST$(printf '%+d' "$hours_behind")"
export TZ
make_play_db <<SQL || fail "making the play time limit fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Tetris', '.limit-test/Tetris.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 2400, $((now - 3600)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 5000, $((now - 3 * 86400)));
SQL
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" playtime-limit >/dev/null || fail "building playtime-limit"
build_plugin opener 'id=opener\nname=Opener\napi=1\n'
printf 'playtime-limit=on\nopener=on\n' >"$STATE/plugins.txt"
printf 'rom=%s\n' "$LIMIT_ROM" >"$STATE/opener.cfg"
plugin=playtime-limit

# over the limit: refused, with the code offered, and the code never logged
printf 'limit=30\ncode=Sekr1t\n' >"$STATE/playtime-limit.cfg"
run_launcher "$WORK/limit-over.log" "plugin opener: done" || fail "the launcher exited early with a refused launch"
log="$WORK/limit-over.log"
expect "daily limit 30 minutes, warning at 10, 15 more behind the code"
expect "refused Tetris.gb: 2400 seconds played today, the limit is 30 minutes; offering extra time behind the code"
grep -qF "plugin opener: open rom 0" "$log" || fail "the refused launch went ahead"
grep -qF "plugins: launch of $LIMIT_ROM cancelled by a plugin" "$log" || fail "the refused launch wasn't cancelled"
grep -qF "Sekr1t" "$log" && fail "playtime-limit logged the code"

# under the limit, with today's grant counted: allowed
printf 'limit=30\ncode=Sekr1t\ngrant_day=%s\ngrant_minutes=15\n' "$(date +%Y-%m-%d)" >"$STATE/playtime-limit.cfg"
run_launcher "$WORK/limit-grant.log" && fail "the launcher didn't quit to launch the game with today's grant"
log="$WORK/limit-grant.log"
expect "allowed Tetris.gb: 5 of 45 minutes left today"
rm -f /tmp/mosaic-launcher-launched

# under the limit: allowed
printf 'limit=60\n' >"$STATE/playtime-limit.cfg"
run_launcher "$WORK/limit-under.log" && fail "the launcher didn't quit to launch the game under the limit"
log="$WORK/limit-under.log"
expect "allowed Tetris.gb: 20 of 60 minutes left today"
grep -qF "plugin opener: open rom 1" "$log" || fail "the launch under the limit didn't go ahead"
rm -f /tmp/mosaic-launcher-launched

# the time left as a header widget, in the theme's colours, then the
# warning colour once it's at or under the threshold
rm -rf "$STATE/opener.cfg" "$WORK/plugins/opener.plugin"
build_plugin spotter 'id=spotter\nname=Spotter\napi=1\n'
printf 'playtime-limit=on\nspotter=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/limit-widget.log" "plugin playtime-limit: widget at " "plugin spotter: runs " \
	|| fail "the launcher exited early with playtime-limit's widget"
log="$WORK/limit-widget.log"
grep -qE "plugin playtime-limit: widget at [0-9]+,15 [0-9]+x90: 20m left$" "$log" \
	|| fail "playtime-limit's widget is wrong: $(grep "plugin playtime-limit: widget" "$log")"
pl_x=$(sed -n 's/.*plugin playtime-limit: widget at \([0-9]*\),.*/\1/p' "$log" | tail -n 1)
pl_w=$(sed -n 's/.*plugin playtime-limit: widget at [0-9]*,15 \([0-9]*\)x90:.*/\1/p' "$log" | tail -n 1)
runs=$(grep "plugin spotter: runs " "$log" | tail -n 1 | sed 's/.*: runs //; s/,.*//; s/-/ /g')
# shellcheck disable=SC2086
set -- $runs
if [ -z "$pl_x" ] || [ -z "$pl_w" ] || [ "$#" -lt 4 ]; then
	fail "spotter didn't see playtime-limit's widget and the status pill: $(grep "plugin spotter: runs" "$log" | tail -n 1)"
elif [ $(($1 - pl_x)) -gt 2 ] || [ $((pl_x - $1)) -gt 2 ] || [ $(($2 - pl_x - pl_w)) -gt 2 ] || [ $((pl_x + pl_w - $2)) -gt 2 ]; then
	fail "playtime-limit didn't draw at $pl_x, $pl_w wide: spotter saw $runs"
fi
grep "plugin spotter: runs " "$log" | tail -n 1 | grep -qF ", accent 1 hint 1" \
	|| fail "playtime-limit's pill isn't in the theme's colours: $(grep "plugin spotter: runs" "$log" | tail -n 1)"

printf 'limit=45\n' >"$STATE/playtime-limit.cfg"
run_launcher "$WORK/limit-warn.log" "plugin playtime-limit: widget at " "plugin spotter: runs " \
	|| fail "the launcher exited early with playtime-limit's warning"
log="$WORK/limit-warn.log"
grep -qE "plugin playtime-limit: widget at [0-9]+,15 [0-9]+x90: 5m left, warning$" "$log" \
	|| fail "playtime-limit's warning is wrong: $(grep "plugin playtime-limit: widget" "$log")"
grep "plugin spotter: runs " "$log" | tail -n 1 | grep -qF ", accent 0 " \
	|| fail "playtime-limit's warning is in the theme's colours: $(grep "plugin spotter: runs" "$log" | tail -n 1)"

# off by default: nothing shows
rm -f "$STATE/playtime-limit.cfg"
run_launcher "$WORK/limit-off.log" "plugin playtime-limit: no daily limit set" "plugin spotter: runs " \
	|| fail "the launcher exited early with playtime-limit off by default"
grep -qF "plugin playtime-limit: widget at" "$WORK/limit-off.log" && fail "playtime-limit drew a widget with no limit"
rm -rf "$WORK/plugins/spotter.plugin"

# with no history: allowed, saying why once
rm -f "$PLAY_DB"
build_plugin opener 'id=opener\nname=Opener\napi=1\n'
printf 'playtime-limit=on\nopener=on\n' >"$STATE/plugins.txt"
printf 'rom=%s\n' "$LIMIT_ROM" >"$STATE/opener.cfg"
printf 'limit=30\n' >"$STATE/playtime-limit.cfg"
run_launcher "$WORK/limit-unknown.log" && fail "the launcher didn't quit to launch the game with no history"
log="$WORK/limit-unknown.log"
[ "$(grep -c "plugin playtime-limit: the play history can't be read" "$log")" -eq 1 ] \
	|| fail "playtime-limit didn't say once why it allowed the launch"
grep -qF "plugin opener: open rom 1" "$log" || fail "the launch with no history didn't go ahead"
rm -f /tmp/mosaic-launcher-launched

unset TZ
[ -f "$WORK/game_logs.sqlite.saved" ] && mv "$WORK/game_logs.sqlite.saved" "$PLAY_DB"
if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
for f in next last.txt resume_slot.txt; do
	if [ -f "$WORK/$f.saved" ]; then mv "$WORK/$f.saved" "/tmp/$f"; else rm -f "/tmp/$f"; fi
done
rm -f "$WORK/recent.txt.saved"
rm -rf "$WORK/plugins/playtime-limit.plugin" "$WORK/plugins/opener.plugin" "$(dirname "$LIMIT_ROM")" \
	"$STATE/playtime-limit.cfg" "$STATE/opener.cfg" "$STATE/plugins.txt"
