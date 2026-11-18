# shellcheck shell=sh
# shard: 1

# smart-collections' rules, with row-badges for games.played, over a
# fixture play history: Bravo played an hour ago for 12 hours and
# Charlie 20 days ago, with .info genres and years. The built-in rule
# collections and a rules.txt line come first in the Smart Collections
# folder, before the decades and genres, and its bad line is logged by
# number. Y dismisses a rule collection; with tags on, Save as tag puts a
# new tag named as the collection shows on its games. Alpha is launched
# from Never played, and with its session in the play history the next
# start has it played. The card's recents, play history, /tmp/next and
# /tmp/last.txt are put back afterwards.
SR_ROMS="$CARD/Roms/Smart Rules (SMR)"
SR_EMU="$CARD/Emus/desktop/SMR.pak"
SR_FOLDER="$CARD/Smart Collections"
SR_RULES="$STATE/smart-collections/rules.txt"
SR_SAVED="$WORK/smart-rules-saved"
rm -rf "$SR_ROMS" "$SR_SAVED" "$STATE/smart-collections"
mkdir -p "$SR_ROMS/.media" "$SR_EMU" "$SR_SAVED" "$STATE/smart-collections" "$(dirname "$PLAY_DB")" "$(dirname "$RECENTS")"
: >"$SR_EMU/launch.sh"
for game in Alpha Bravo Charlie Delta; do : >"$SR_ROMS/$game.gb"; done
printf 'genre=Puzzle\nyear=1991\n' >"$SR_ROMS/.media/Alpha.info"
printf 'genre=Puzzle\nyear=1994-02-01\n' >"$SR_ROMS/.media/Bravo.info"
printf 'genre=Shooter\nyear=1987\n' >"$SR_ROMS/.media/Charlie.info"
for saved in /tmp/next /tmp/last.txt "$RECENTS" "$PLAY_DB"; do
	[ -f "$saved" ] && mv "$saved" "$SR_SAVED/$(basename "$saved")"
done
now=$(date +%s)
# sr_play_db [extra SQL]: the fixture play history, and any more sessions
sr_play_db() {
	make_play_db <<SQL || fail "making the smart rules fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Bravo', 'Smart Rules (SMR)/Bravo.gb');
INSERT INTO rom(id, type, name, file_path) VALUES(2, '', 'Charlie', 'Smart Rules (SMR)/Charlie.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 43200, $((now - 3600)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(2, 3600, $((now - 20 * 86400)));
$1
SQL
}
sr_play_db ""
printf 'SMR unplayed puzzles = genre:puzzle unplayed system:smr\nBroken = played:soon\n' >"$SR_RULES"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" smart-collections row-badges tags >/dev/null \
	|| fail "building smart-collections, row-badges and tags"
build_plugin seeker 'id=seeker\nname=Seeker\napi=1\n'

# sr_plugins <id...>: those on, and every other plugin built so far off
sr_plugins() {
	: >"$STATE/plugins.txt"
	for dir in "$WORK"/plugins/*.plugin; do
		id=$(basename "$dir" .plugin)
		case " $* " in
		*" $id "*) printf '%s=on\n' "$id" >>"$STATE/plugins.txt" ;;
		*) printf '%s=off\n' "$id" >>"$STATE/plugins.txt" ;;
		esac
	done
}

# sr_run <name> <steps>: the launcher on the root, which has Smart Rules
# then Smart Collections once the card index is read, seeker opening the
# folder and pressing steps
sr_run() {
	printf 'builtin=off\nmin=1\n' >"$STATE/smart-collections.cfg"
	rm -f /tmp/last.txt
	printf 'steps=wait,wait,down,a%s\n' "$2" >"$STATE/seeker.cfg"
	log="$WORK/$1.log"
	plugin=smart-collections
}

# the eight rows that fit, of nine
all="Played in the last 7 days, Over 10 hours, Added in the last 30 days, SMR unplayed puzzles, 1980s, 1990s, Puzzle"

# the collections, and Y dismissing Never played
sr_plugins smart-collections row-badges seeker
sr_run smart-rules ",y,a"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early with smart-collections"
expect "$SR_RULES line 2 left out, played:soon isn't a filter: Broken = played:soon"
expect "1 rules from $SR_RULES"
expect "rule Never played (unplayed): 2 games"
expect "rule Recently played (played:7): 1 games"
expect "rule Long played (hours:10): 1 games"
expect "rule Recently added (added:30): 4 games"
expect "rule SMR unplayed puzzles (genre:puzzle unplayed system:smr): 1 games"
expect "matched 9 rules, 4 of them decades and genres, against 4 games in "
expect "dismissed Never played"
plugin=seeker
expect "list $SR_FOLDER: Never played, $all selected 0"
expect "list $SR_FOLDER: $all, Shooter selected 0"
grep -qxF "dismissed=Never played" "$STATE/smart-collections.cfg" || fail "smart-collections didn't keep Never played dismissed"

# with tags: Y on Played in the last 7 days, the second, opens Dismiss and Save as tag,
# and the keyboard starts from the name it shows
sr_plugins smart-collections row-badges tags seeker
rm -f "$STATE/tags.txt"
sr_run smart-rules-tag ",down,y,down,a,start"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early with smart-collections and tags"
expect "saved Recently played as the tag Played in the last 7 days, 1 games"
grep -qxF "Played in the last 7 days	/Roms/Smart Rules (SMR)/Bravo.gb" "$STATE/tags.txt" 2>/dev/null \
	|| fail "tags didn't put the collection's tag on Bravo: $(cat "$STATE/tags.txt" 2>/dev/null)"

# Alpha launched from Never played
sr_plugins smart-collections row-badges seeker
sr_run smart-rules-launch ",a,a"
run_launcher "$log" && fail "the launcher didn't quit to launch Alpha from Never played"
plugin=seeker
expect "list $SR_FOLDER/Never played: Alpha, Delta selected 0"
rm -f /tmp/mosaic-launcher-launched

# back from it, with its session in the play history
sr_play_db "INSERT INTO rom(id, type, name, file_path) VALUES(3, '', 'Alpha', 'Smart Rules (SMR)/Alpha.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(3, 1800, $((now - 60)));"
sr_run smart-rules-return ""
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early back from Alpha"
expect "rule Never played (unplayed): 1 games"
expect "rule Recently played (played:7): 2 games"
expect "rule SMR unplayed puzzles (genre:puzzle unplayed system:smr): 0 games"

rm -rf "$SR_ROMS" "$SR_EMU" "$STATE/smart-collections" /tmp/next /tmp/last.txt "$RECENTS" "$PLAY_DB"
rmdir "$CARD/Emus/desktop" 2>/dev/null
for saved in /tmp/next /tmp/last.txt "$RECENTS" "$PLAY_DB"; do
	[ -f "$SR_SAVED/$(basename "$saved")" ] && mv "$SR_SAVED/$(basename "$saved")" "$saved"
done
rm -rf "$SR_SAVED" "$WORK/plugins/smart-collections.plugin" "$WORK/plugins/row-badges.plugin" "$WORK/plugins/tags.plugin" \
	"$WORK/plugins/seeker.plugin" "$STATE/smart-collections.cfg" "$STATE/seeker.cfg" "$STATE/tags.txt" "$STATE/tags.cfg" \
	"$STATE/row-badges.cfg" "$STATE/plugins.txt" /tmp/mosaic-launcher-launched
case_log=""
