# shellcheck shell=sh

# playtime-limit's schedules: with the local clock moved to 22:00, bedtime
# from 21:00 to 07:00 refuses a game with no limit set, offering the code,
# and a bedtime unlock in the cfg lets it launch; at noon, a weekday
# limit of 30 and a weekend limit of 60 minutes, from a fixture
# game_logs.sqlite with 40 minutes played, refuse or allow it by the day.
# The budget bar draws across the top of the root list, not with its
# setting off, and on home-dashboard's home screen. opener launches the
# game; a launch that goes ahead quits the launcher, and the card's
# recents and the launch's /tmp files are put back afterwards.
PS_ROM="$CARD/Roms/.schedule-test/Tetris.gb"
mkdir -p "$(dirname "$PS_ROM")" "$(dirname "$PLAY_DB")" "$STATE"
: >"$PS_ROM"
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$WORK/schedules-game_logs.saved"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/schedules-recent.saved"
for f in next last.txt resume_slot.txt; do
	[ -f "/tmp/$f" ] && cp "/tmp/$f" "$WORK/schedules-$f.saved"
done
rm -f /tmp/last.txt

# at_hour <hour>: a TZ whose local time is that hour, this minute
at_hour() {
	behind=$(( ($(date -u +%H | sed 's/^0//') - $1 + 36) % 24 - 12 ))
	TZ="TST$(printf '%+d' "$behind")"
	export TZ
}

at_hour 12
now=$(date +%s)
make_play_db <<SQL || fail "making the play time schedules fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Tetris', '.schedule-test/Tetris.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 2400, $((now - 3600)));
SQL
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" playtime-limit >/dev/null || fail "building playtime-limit"
build_plugin opener 'id=opener\nname=Opener\napi=1\n'
printf 'playtime-limit=on\nopener=on\n' >"$STATE/plugins.txt"
printf 'rom=%s\n' "$PS_ROM" >"$STATE/opener.cfg"
plugin=playtime-limit

# bedtime, past its start: refused with no limit set, the code offered
# and never logged
at_hour 22
printf 'bedtime=21:00\nwake=07:00\ncode=Sekr1t\n' >"$STATE/playtime-limit.cfg"
run_launcher "$WORK/schedules-bedtime.log" "plugin opener: done" || fail "the launcher exited early at bedtime"
log="$WORK/schedules-bedtime.log"
expect "no daily limit set"
expect "bedtime from 21:00 to 07:00"
expect "refused Tetris.gb: bedtime from 21:00 to 07:00; offering extra time behind the code"
grep -qF "plugin opener: open rom 0" "$log" || fail "the launch at bedtime went ahead"
grep -qF "Sekr1t" "$log" && fail "playtime-limit logged the code"

# a bedtime unlock that hasn't run out: allowed
printf 'bedtime=21:00\nwake=07:00\ncode=Sekr1t\nbedtime_until=%s\n' "$(($(date +%s) + 900))" >"$STATE/playtime-limit.cfg"
run_launcher "$WORK/schedules-unlocked.log" && fail "the launcher didn't quit to launch the game with bedtime unlocked"
grep -qF "plugin opener: open rom 1" "$WORK/schedules-unlocked.log" || fail "the launch with bedtime unlocked didn't go ahead"
rm -f /tmp/mosaic-launcher-launched /tmp/last.txt

# weekday and weekend limits, at noon: 40 minutes played is over the
# weekday's 30 and under the weekend's 60
at_hour 12
printf 'limit=30\nweekend=60\n' >"$STATE/playtime-limit.cfg"
run_launcher "$WORK/schedules-day.log" "plugin opener: done"
status=$?
log="$WORK/schedules-day.log"
expect "weekday limit 30 minutes, weekend limit 60 minutes, warning at 10, 15 more behind the code (no code set)"
case "$(date +%u)" in
6 | 7)
	[ "$status" -eq 1 ] || fail "the launcher didn't quit to launch the game under the weekend limit"
	expect "allowed Tetris.gb: 20 of 60 minutes left today"
	;;
*)
	[ "$status" -eq 0 ] || fail "the launcher exited early with a refused launch on a weekday"
	expect "refused Tetris.gb: 2400 seconds played today, the limit is 30 minutes"
	;;
esac
rm -f /tmp/mosaic-launcher-launched /tmp/last.txt

# the budget bar across the top of the root list: 40 of 60 minutes
rm -rf "$STATE/opener.cfg" "$WORK/plugins/opener.plugin"
printf 'playtime-limit=on\n' >"$STATE/plugins.txt"
printf 'limit=60\n' >"$STATE/playtime-limit.cfg"
run_launcher "$WORK/schedules-bar.log" "plugin playtime-limit: bar over " || fail "the launcher exited early with the budget bar"
log="$WORK/schedules-bar.log"
expect "budget bar on"
grep -qE "plugin playtime-limit: bar over gamelist at 0,0 [0-9]+x[0-9]+: [0-9]+ px of 2400 seconds against 60 minutes$" "$log" \
	|| fail "the budget bar is wrong: $(grep "plugin playtime-limit: bar" "$log")"

# off by its setting
printf 'limit=60\nbar=off\n' >"$STATE/playtime-limit.cfg"
run_launcher "$WORK/schedules-bar-off.log" "plugin playtime-limit: budget bar off" "plugin playtime-limit: widget at " \
	|| fail "the launcher exited early with the budget bar off"
grep -qF "plugin playtime-limit: bar over" "$WORK/schedules-bar-off.log" && fail "playtime-limit drew the budget bar with it off"

# on home-dashboard's home screen, under the warning threshold
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" home-dashboard >/dev/null || fail "building home-dashboard"
rm -f "$HOME_LEFT"
printf 'playtime-limit=on\nhome-dashboard=on\n' >"$STATE/plugins.txt"
printf 'limit=45\n' >"$STATE/playtime-limit.cfg"
run_launcher "$WORK/schedules-bar-home.log" "plugin playtime-limit: bar over home-dashboard.main " \
	|| fail "the launcher exited early with the budget bar on home"
log="$WORK/schedules-bar-home.log"
grep -qE "plugin playtime-limit: bar over home-dashboard.main at 0,0 [0-9]+x[0-9]+: [0-9]+ px of 2400 seconds against 45 minutes, warning$" "$log" \
	|| fail "the budget bar on home is wrong: $(grep "plugin playtime-limit: bar" "$log")"
grep -qF "plugin playtime-limit: bar over gamelist" "$log" && fail "playtime-limit drew the budget bar on the game list under home"

unset TZ
rm -f "$HOME_LEFT"
if [ -f "$WORK/schedules-game_logs.saved" ]; then mv "$WORK/schedules-game_logs.saved" "$PLAY_DB"; else rm -f "$PLAY_DB"; fi
if [ -f "$WORK/schedules-recent.saved" ]; then mv "$WORK/schedules-recent.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
for f in next last.txt resume_slot.txt; do
	if [ -f "$WORK/schedules-$f.saved" ]; then mv "$WORK/schedules-$f.saved" "/tmp/$f"; else rm -f "/tmp/$f"; fi
done
rm -rf "$WORK/plugins/playtime-limit.plugin" "$WORK/plugins/home-dashboard.plugin" "$WORK/plugins/opener.plugin" \
	"$(dirname "$PS_ROM")" "$STATE/playtime-limit.cfg" "$STATE/opener.cfg" "$STATE/plugins.txt"
