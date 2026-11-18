# shellcheck shell=sh

# the bundled sort-modes, with jump-letter's strip for the sections,
# multi-disc, numeric-prefix and guest-profile, seeker pressing the keys,
# over a fixture system: a Puzzle folder, Alpha to Echo with a play
# history, .info years and publishers and modified times in the future
# (so they're newer than the files' change times), "1) Zulu" that
# numeric-prefix shows as Zulu, and Golf's two discs, one game whose .m3u
# isn't on the card. Starting in most played, the action, Y and the Sort
# tile's list step through each mode, over two starts, each order with the folder first,
# the games without a value last by name and the selection kept on
# Alpha, with jump-letter's sections and L1 and R1 following the mode and
# the header showing it. The mode the tile picked, last played, is saved,
# and the next start, back from a game, keeps it and sorts the play
# history again, as it does when the guest's profile is put in place and
# taken away. The
# card's recents, play history, Collections, /tmp/next and /tmp/last.txt
# are put back afterwards.
SO_ROMS="$CARD/Roms/Sort Modes (SRT)"
SO_EMU="$CARD/Emus/desktop/SRT.pak"
SO_SAVED="$WORK/sort-modes-saved"
rm -rf "$SO_ROMS" "$SO_SAVED" "$STATE/profiles" "$CARD/.userdata/shared/launcher/profiles" "$CARD/.userdata/shared/launcher/profile.txt"
mkdir -p "$SO_ROMS/.media" "$SO_ROMS/Puzzle" "$SO_EMU" "$SO_SAVED" "$STATE" "$(dirname "$PLAY_DB")" "$(dirname "$RECENTS")"
: >"$SO_EMU/launch.sh"
for saved in /tmp/next /tmp/last.txt "$RECENTS" "$PLAY_DB"; do
	[ -f "$saved" ] && mv "$saved" "$SO_SAVED/$(basename "$saved")"
done
[ -d "$CARD/Collections" ] && mv "$CARD/Collections" "$SO_SAVED/Collections"
: >"$SO_ROMS/Puzzle/Foxtrot.gb"
# recently added: Charlie, Golf, Echo, then Alpha and Bravo together, Delta and Zulu
so_game() { : >"$SO_ROMS/$1.gb" && touch -t "$2" "$SO_ROMS/$1.gb"; }
so_game Alpha 202901010000
so_game Bravo 202901010000
so_game Charlie 203101010000
so_game Delta 202801010000
so_game Echo 203001010000
so_game "1) Zulu" 202701010000
so_game "Golf (Disc 1)" 203006010000
so_game "Golf (Disc 2)" 203006010000
printf 'year=1991\npublisher=Nintendo\n' >"$SO_ROMS/.media/Alpha.info"
printf 'year=1989\npublisher=capcom\n' >"$SO_ROMS/.media/Bravo.info"
printf 'year=1991-04-01\npublisher=Capcom\n' >"$SO_ROMS/.media/Delta.info"
printf 'year=1995\n' >"$SO_ROMS/.media/Echo.info"
printf 'year=1993\npublisher=Namco\n' >"$SO_ROMS/.media/Golf (Disc 1).info"
now=$(date +%s)
make_play_db <<SQL || fail "making the sort modes fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Alpha', 'Sort Modes (SRT)/Alpha.gb');
INSERT INTO rom(id, type, name, file_path) VALUES(2, '', 'Bravo', 'Sort Modes (SRT)/Bravo.gb');
INSERT INTO rom(id, type, name, file_path) VALUES(3, '', 'Delta', 'Sort Modes (SRT)/Delta.gb');
INSERT INTO rom(id, type, name, file_path) VALUES(4, '', 'Golf (Disc 1)', 'Sort Modes (SRT)/Golf (Disc 1).gb');
INSERT INTO rom(id, type, name, file_path) VALUES(5, '', '1) Zulu', 'Sort Modes (SRT)/1) Zulu.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 7200, $((now - 3 * 86400)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(2, 43200, $((now - 60)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(3, 1800, $((now - 20 * 86400)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(4, 18000, $((now - 2 * 86400)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(5, 600, $((now - 10 * 86400)));
SQL
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" sort-modes jump-letter multi-disc numeric-prefix guest-profile >/dev/null \
	|| fail "building sort-modes and the plugins it works with"
build_plugin seeker 'id=seeker\nname=Seeker\napi=1\n'

# so_plugins <id...>: those on, and every other plugin built so far off
so_plugins() {
	: >"$STATE/plugins.txt"
	for dir in "$WORK"/plugins/*.plugin; do
		id=$(basename "$dir" .plugin)
		case " $* " in
		*" $id "*) printf '%s=on\n' "$id" >>"$STATE/plugins.txt" ;;
		*) printf '%s=off\n' "$id" >>"$STATE/plugins.txt" ;;
		esac
	done
}

# so_order <names> <selected>: seeker listed the folder in that order with Alpha selected
so_order() {
	plugin=seeker
	expect "list $SO_ROMS: $1 selected $2"
}

# so_strip <sections>: jump-letter's strip had these sections, lit on Alpha's
so_strip() {
	plugin=jump-letter
	expect "strip $SO_ROMS: $1"
}

# most played, then last played from the action, year from Y, and L1
# and R1 by years
so_plugins sort-modes jump-letter multi-disc numeric-prefix seeker
printf 'mode=most played\nbutton=Y\n' >"$STATE/sort-modes.cfg"
printf 'button=R2\n' >"$STATE/multi-disc.cfg"
printf '%s' "$SO_ROMS/Alpha.gb" >/tmp/last.txt
strip="action:jump-letter.strip,b"
printf 'steps=%s\n' "wait,wait,$strip,action:sort-modes.next,$strip,y,$strip,r1,l1" >"$STATE/seeker.cfg"
log="$WORK/sort-modes.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early with sort-modes"
plugin=sort-modes
expect "mode most played"
expect "sort-modes.next on Y"
expect "header: Most played"
expect "mode last played"
expect "header: Last played"
expect "mode year"
expect "header: Year"
expect "sorted $SO_ROMS by year: 8 entries, 7 games, in "
so_order "Puzzle, Bravo, Golf, Alpha, Delta, Zulu, Charlie, Echo" 3
so_strip "P, 10h+, 1h+, Under 1h, Not played at 1h+"
so_order "Puzzle, Bravo, Golf, Alpha, Zulu, Delta, Charlie, Echo" 3
so_strip "P, Today, Last 7 days, Last 30 days, Not played at Last 7 days"
so_order "Puzzle, Bravo, Alpha, Delta, Golf, Echo, Charlie, Zulu" 2
so_strip "P, 1989, 1991, 1993, 1995, No year at 1991"
# R1 to the next year's first game, past Delta's 1991, and L1 back
so_order "Puzzle, Bravo, Alpha, Delta, Golf, Echo, Charlie, Zulu" 4
grep -qxF "mode=year" "$STATE/sort-modes.cfg" || fail "sort-modes didn't save the mode Y picked"

# started again in year, as saved: publisher and recently added from the
# action, then the Sort tile's list picking last played
printf 'steps=%s\n' "wait,action:sort-modes.next,$strip,action:sort-modes.next,$strip,action:gamelist.quick_menu,left,a,up,up,up,a,wait" \
	>"$STATE/seeker.cfg"
log="$WORK/sort-modes-tile.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early starting sort-modes in year"
plugin=sort-modes
expect "mode year"
expect "mode publisher"
expect "mode recently added"
expect "header: Recently added"
expect "mode last played"
so_order "Puzzle, Bravo, Alpha, Delta, Golf, Echo, Charlie, Zulu" 2
so_order "Puzzle, Bravo, Delta, Golf, Alpha, Charlie, Echo, Zulu" 4
so_strip "P, capcom, Namco, Nintendo, No publisher at Nintendo"
so_order "Puzzle, Charlie, Golf, Echo, Alpha, Bravo, Delta, Zulu" 4
so_strip "P, Today at Today"
so_order "Puzzle, Bravo, Golf, Alpha, Zulu, Delta, Charlie, Echo" 3
grep -qxF "mode=last played" "$STATE/sort-modes.cfg" || fail "sort-modes didn't save the mode the tile picked"

# the next start, back from a game: still last played, sorted again from
# the play history; then name from the tile's list, with letters and
# numeric-prefix's Zulu first; then most played, and the guest's profile,
# with no play history, and back
so_plugins sort-modes jump-letter multi-disc numeric-prefix guest-profile seeker
printf 'path=%s\ntype=rom\n' "$SO_ROMS/Alpha.gb" >/tmp/mosaic-launcher-launched
printf '%s' "$SO_ROMS/Alpha.gb" >/tmp/last.txt
steps="wait,action:sort-modes.pick,up,up,a,$strip,action:sort-modes.pick,down,a"
steps="$steps,action:guest-profile.toggle,wait,action:guest-profile.toggle,wait"
printf 'steps=%s\n' "$steps" >"$STATE/seeker.cfg"
log="$WORK/sort-modes-again.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early starting sort-modes again"
rm -f /tmp/mosaic-launcher-launched
plugin=sort-modes
expect "mode last played"
expect "launcher.return: sorting again"
expect "mode name"
expect "mode most played"
expect "launcher.profile_changed: sorting again"
so_order "Puzzle, Bravo, Golf, Alpha, Zulu, Delta, Charlie, Echo" 3
so_order "Zulu, Alpha, Bravo, Charlie, Delta, Echo, Golf, Puzzle" 1
so_strip "Z, A, B, C, D, E, G, P at A"
# the guest has no play history: the games by name
so_order "Puzzle, Alpha, Bravo, Charlie, Delta, Echo, Golf, Zulu" 1
[ "$(grep -c "plugin seeker: list $SO_ROMS: Puzzle, Bravo, Golf, Alpha, Delta, Zulu, Charlie, Echo selected 3" "$log")" -ge 2 ] \
	|| fail "sort-modes didn't sort the owner's play history again after the guest's"

rm -rf "$SO_ROMS" "$SO_EMU" "$STATE/profiles" "$CARD/.userdata/shared/launcher/profiles" "$CARD/.userdata/shared/launcher/profile.txt"
rm -rf "$CARD/Collections"
rmdir "$CARD/Emus/desktop" 2>/dev/null
rm -f /tmp/next /tmp/last.txt "$RECENTS" "$PLAY_DB"
for saved in next last.txt; do
	[ -f "$SO_SAVED/$saved" ] && mv "$SO_SAVED/$saved" "/tmp/$saved"
done
[ -f "$SO_SAVED/recent.txt" ] && mv "$SO_SAVED/recent.txt" "$RECENTS"
[ -f "$SO_SAVED/game_logs.sqlite" ] && mv "$SO_SAVED/game_logs.sqlite" "$PLAY_DB"
[ -d "$SO_SAVED/Collections" ] && mv "$SO_SAVED/Collections" "$CARD/Collections"
rm -rf "$SO_SAVED" "$WORK/plugins/sort-modes.plugin" "$WORK/plugins/jump-letter.plugin" "$WORK/plugins/multi-disc.plugin" \
	"$WORK/plugins/numeric-prefix.plugin" "$WORK/plugins/guest-profile.plugin" "$WORK/plugins/seeker.plugin" \
	"$STATE/sort-modes.cfg" "$STATE/multi-disc.cfg" "$STATE/seeker.cfg" "$STATE/plugins.txt" "$STATE/jump-letter.cfg" \
	"$STATE/guest-profile.cfg" "$STATE/numeric-prefix.cfg"
case_log=""
