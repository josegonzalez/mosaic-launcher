# shellcheck shell=sh

# the bundled activity-stats' All time tab, Most Played folder, export and
# reset, with presser, over a fixture game_logs.sqlite in gametimectl's
# schema, in UTC. Y on the game list opens the screen (its button setting),
# and two steps right show All time: the totals, the longest session,
# play by hour with the most active hour and the time per system. Y there
# writes the CSV and HTML to Reports, a row per session, with a game name
# that has a comma, quotes, < and & escaped. The Most Played folder on the
# first screen lists the games still on the card, most played first. X on
# the Games tab asks before resetting Zelda, then deletes its sessions and
# nothing else from the database; with simple mode's kiosk and no PIN the
# reset is refused and the database left alone. Recents, Reports and the
# settings it made are put back or removed afterwards
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" activity-stats simple-mode >/dev/null \
	|| fail "building activity-stats and simple-mode"
AM_ROMS="$CARD/Roms/More (GB)"
AM_REPORTS="$CARD/Reports"
AM_PIN="$STATE/pin.txt"
mkdir -p "$AM_ROMS" "$(dirname "$PLAY_DB")" "$STATE"
: >"$AM_ROMS/Zelda.gba"
: >"$AM_ROMS/Tetris, DX.gb"
: >"$AM_ROMS/Puzzle.gb"
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$WORK/activity-more-db.saved"
[ -f "$RECENTS" ] && mv "$RECENTS" "$WORK/activity-more-recent.saved"
[ -f "$AM_PIN" ] && mv "$AM_PIN" "$WORK/activity-more-pin.saved"
AM_HAD_REPORTS=""
[ -d "$AM_REPORTS" ] && AM_HAD_REPORTS=1
# two days back at midnight UTC
now=$(date +%s)
day=$(( now - now % 86400 - 2 * 86400 ))
am_db() {
	make_play_db <<SQL || fail "making the activity fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Zelda', 'More (GB)/Zelda.gba');
INSERT INTO rom(id, type, name, file_path) VALUES(2, '', 'Tetris, "DX" <&>', 'More (GB)/Tetris, DX.gb');
INSERT INTO rom(id, type, name, file_path) VALUES(3, 'ORPHAN', 'Metroid', NULL);
INSERT INTO rom(id, type, name, file_path) VALUES(4, '', 'Puzzle', 'More (GB)/Puzzle.gb');
INSERT INTO rom(id, type, name, file_path) VALUES(5, '', 'Gone', 'More (GB)/Gone.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 7200, $((day + 20 * 3600 + 1800)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 600, $((day + 9 * 3600)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(2, 900, $((day + 9 * 3600 + 900)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(3, 50, $((day + 21 * 3600)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(4, 300, $((day + 21 * 3600 + 600)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(5, 60, $((day + 3 * 3600)));
SQL
}
# rows <table>: how many rows the fixture's table has
rows() {
	if command -v sqlite3 >/dev/null; then
		sqlite3 "$PLAY_DB" "SELECT COUNT(*) FROM $1;"
	else
		python3 -I -c 'import sqlite3, sys; print(sqlite3.connect(sys.argv[1]).execute("SELECT COUNT(*) FROM " + sys.argv[2]).fetchone()[0])' \
			"$PLAY_DB" "$1"
	fi
}
# the plugins it names on, and every other plugin built so far off
am_plugins() {
	: >"$STATE/plugins.txt"
	for dir in "$WORK"/plugins/*.plugin; do
		id=$(basename "$dir" .plugin)
		case " $* " in
		*" $id "*) printf '%s=on\n' "$id" >>"$STATE/plugins.txt" ;;
		*) printf '%s=off\n' "$id" >>"$STATE/plugins.txt" ;;
		esac
	done
}
# am_run <name> <keys>: presses keys from the first screen; what was drawn
# goes to $WORK/<name>.texts
am_run() {
	texts="$WORK/$1.texts"
	: >"$texts"
	PRESSER_KEYS="$2" PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER"
	export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	run_launcher "$WORK/$1.log" "presser: done" || fail "the launcher exited early in $1"
	unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	log="$WORK/$1.log"
}
shown() {
	grep -qF -- "$1" "$texts" || fail "\"$1\" wasn't drawn"
}
TZ=UTC0
export TZ
plugin=activity-stats
dot=$(printf '\302\267')
am_db
am_plugins activity-stats
printf 'button=Y\nfolder=on\ntile=off\n' >"$STATE/activity-stats.cfg"

# All time, then Y exports it
am_run activity-all "y right right y"
expect "showing All time: 2h 31m $dot 6 sessions $dot 5 games, Longest session: 2h (Zelda), Most active hour: 21:00, hours 0 0 0 60 0 0 0 0 0 1500 0 0 0 0 0 0 0 0 0 0 1800 3950 1800 0, systems More 2h 31m, Other <1m"
shown "Most active hour: 21:00"
shown "Longest session: 2h (Zelda)"
expect "exported $AM_REPORTS/activity-"
csv=$(find "$AM_REPORTS" -name 'activity-*.csv' | head -n 1)
html="${csv%.csv}.html"
if [ -z "$csv" ] || [ ! -f "$csv" ]; then fail "no activity CSV in Reports"; fi
[ -f "$html" ] || fail "no activity HTML beside $csv"
shown "Saved Reports/$(basename "$csv") and .html"
[ "$(wc -l <"$csv" | tr -d ' ')" = 7 ] || fail "the CSV hasn't a header and a row per session: $(cat "$csv")"
grep -qF 'started,start_unix,seconds,minutes,game,system,path' "$csv" || fail "the CSV has no header"
grep -qF ",900,15,\"Tetris, \"\"DX\"\" <&>\",More,\"$AM_ROMS/Tetris, DX.gb\"" "$csv" || fail "the CSV didn't escape Tetris: $(cat "$csv")"
grep -qF ",50,1,Metroid,Other," "$csv" || fail "the CSV hasn't the orphan's session"
grep -qF '<td>Tetris, &quot;DX&quot; &lt;&amp;&gt;</td>' "$html" || fail "the HTML didn't escape Tetris"
grep -qF '<tr><td>21:00</td><td>1h 5m</td></tr>' "$html" || fail "the HTML hasn't the hour table"
grep -qF '<tr><td>More</td><td>2h 31m</td></tr>' "$html" || fail "the HTML hasn't the system table"
ls "$AM_REPORTS"/*.tmp >/dev/null 2>/dev/null && fail "the export left a temporary file"
rm -f "$csv" "$html"

# the Most Played folder: after the system, with the games still on the card
am_run activity-folder "down a"
shown "Most Played"
expect "most played: 3 games"
shown "Zelda"
shown "Puzzle"
grep -qF "Gone" "$texts" && fail "the Most Played folder lists a game no longer on the card"

# X on the Games tab asks, then resets Zelda alone
am_run activity-reset "y right right right x a"
shown "Reset the play time of Zelda?"
shown "undone."
expect "reset $AM_ROMS/Zelda.gba: 2"
grep -qF "play history: forgot More (GB)/Zelda.gba (2 sessions)" "$log" || fail "the launcher didn't log the reset"
[ "$(rows play_activity)" = 4 ] || fail "the reset didn't leave the 4 other sessions: $(rows play_activity)"
[ "$(rows rom)" = 5 ] || fail "the reset changed the rom table"
shown "Play time reset"

# kiosk with no PIN: the reset is refused, and nothing deleted
am_db
am_plugins activity-stats simple-mode
printf 'on=on\nkiosk=on\n' >"$STATE/simple-mode.cfg"
am_run activity-kiosk "y right right right x"
grep -qF "plugins: activity-stats.reset was refused by a plugin's action_gate" "$log" || fail "the reset wasn't refused in kiosk mode"
[ "$(rows play_activity)" = 6 ] || fail "kiosk mode let the reset delete sessions: $(rows play_activity)"

unset TZ
rm -f "$PLAY_DB" "$AM_PIN"
[ -f "$WORK/activity-more-db.saved" ] && mv "$WORK/activity-more-db.saved" "$PLAY_DB"
[ -f "$WORK/activity-more-recent.saved" ] && mv "$WORK/activity-more-recent.saved" "$RECENTS"
[ -f "$WORK/activity-more-pin.saved" ] && mv "$WORK/activity-more-pin.saved" "$AM_PIN"
[ -z "$AM_HAD_REPORTS" ] && rm -rf "$AM_REPORTS"
rm -rf "$AM_ROMS" "$PRESSER" "$WORK/plugins/activity-stats.plugin" "$WORK/plugins/simple-mode.plugin" \
	"$STATE/activity-stats.cfg" "$STATE/simple-mode.cfg" "$STATE/plugins.txt"
case_log=""
