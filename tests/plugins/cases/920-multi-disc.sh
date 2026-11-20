# shellcheck shell=sh

# the bundled multi-disc, with favorites, tags, search, random-game, seeker
# and looker: a test system's loose discs show as one row per game at its
# .m3u's path, with the first disc's art, and the discs hidden. The card
# index has the game before its .m3u is written, under Disc 1's name and
# year, and is_filtered hides its discs. The first launch of Game, from
# search, writes its .m3u and copies Disc 1's save and state to the .m3u's
# name, and moves Disc 1's recent and favorite to it; Other's own .m3u and
# its save under the .m3u's name are left as they are. A second launch
# copies nothing and moves nothing, and random-game picks Third, never
# launched, which writes its .m3u. Then Combine discs refuses Other, whose
# folder is there, and moves Game's discs into Game/ with its .m3u, saves
# still found under the same name, and recents, favorites, tags and play
# time at the new paths; afterwards the plugin leaves the folder to stock.
# The card's recents, favorites, play history and /tmp/last.txt are put
# back afterwards.
MD_ROMS="$CARD/Roms/Multi Disc Test (MDT)"
MD_REL="/Roms/Multi Disc Test (MDT)"
MD_EMU="$CARD/Emus/desktop/MDT.pak"
MD_SAVES="$CARD/Saves/MDT"
MD_STATES="$CARD/.userdata/shared/MDT-core"
MD_FAVORITES="$CARD/Collections/Favorites.txt"
MD_WORK="$WORK/multi-disc"
rm -rf "$MD_ROMS" "$MD_SAVES" "$MD_STATES" "$MD_WORK" "$STATE/multi-disc.txt"
mkdir -p "$MD_ROMS/.media" "$MD_EMU" "$MD_SAVES" "$MD_STATES" "$MD_WORK" "$(dirname "$RECENTS")" "$(dirname "$MD_FAVORITES")" "$STATE"
for saved in "$RECENTS" "$MD_FAVORITES" "$PLAY_DB" "$STATE/tags.txt" /tmp/last.txt; do
	[ -f "$saved" ] && cp "$saved" "$MD_WORK/$(basename "$saved").saved"
done
: >"$MD_EMU/launch.sh"
for name in "Game (Disc 1).chd" "Game (Disc 2).chd" "Other (Disc 1).chd" "Other (Disc 2).chd" "Third (Disc 1).chd" \
	"Third (Disc 2).chd" "Zelda.chd"; do
	: >"$MD_ROMS/$name"
done
cp "$ROOT/tests/plugins/drawer/art.png" "$MD_ROMS/.media/Game (Disc 1).png"
printf 'Game (Disc 1).chd\tQzest Deluxe\n' >"$MD_ROMS/map.txt"
printf 'year=1999\n' >"$MD_ROMS/.media/Game (Disc 1).info"
printf 'Other (Disc 2).chd\nOther (Disc 1).chd\n' >"$MD_ROMS/Other.m3u"
printf 'disc one' >"$MD_SAVES/Game (Disc 1).chd.sav"
printf 'state one' >"$MD_STATES/Game (Disc 1).chd.st0"
printf 'old' >"$MD_SAVES/Other (Disc 1).chd.sav"
printf 'mine' >"$MD_SAVES/Other.m3u.sav"
printf '%s/Game (Disc 1).chd\tGame\n%s/Zelda.chd\tZelda\n' "$MD_REL" "$MD_REL" >"$RECENTS"
printf '%s/Game (Disc 1).chd\n' "$MD_REL" >"$MD_FAVORITES"
printf 'Done\nDone\t%s/Game.m3u\nPick\nPick\t%s/Third.m3u\n' "$MD_REL" "$MD_REL" >"$STATE/tags.txt"
now=$(date +%s)
make_play_db <<SQL || fail "making the play history fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Game', 'Multi Disc Test (MDT)/Game.m3u');
INSERT INTO rom(id, type, name, file_path) VALUES(2, '', 'Game (Disc 2)', 'Multi Disc Test (MDT)/Game (Disc 2).chd');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 600, $((now - 3600)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(2, 300, $((now - 7200)));
SQL

make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" multi-disc favorites tags search random-game >/dev/null || fail "building multi-disc"
build_plugin seeker 'id=seeker\nname=Seeker\napi=1\n'
build_plugin looker 'id=looker\nname=Looker\napi=1\n'

# every other plugin built so far off, as a plugin plugins.txt doesn't name loads
md_plugins() {
	: >"$STATE/plugins.txt"
	for dir in "$WORK"/plugins/*.plugin; do
		id=$(basename "$dir" .plugin)
		case " $* " in
		*" $id "*) printf '%s=on\n' "$id" >>"$STATE/plugins.txt" ;;
		*) printf '%s=off\n' "$id" >>"$STATE/plugins.txt" ;;
		esac
	done
}
md_rows() {
	python3 -I -c 'import sqlite3, sys; print(",".join("%s:%s" % r for r in sqlite3.connect(sys.argv[1]).execute(
		"SELECT rom.file_path, COUNT(pa.rom_id) FROM rom LEFT JOIN play_activity pa ON pa.rom_id = rom.id GROUP BY rom.id ORDER BY rom.id")))' "$PLAY_DB"
}

# the rows, their art, the index, and the first launch of Game, from
# search: qz finds Disc 1's name, and only the game, not its discs
md_plugins multi-disc favorites search seeker looker
printf 'folder=%s\nindex=Game.m3u,Game (Disc 1).chd,Game (Disc 2).chd,Zelda.chd\n' "$MD_ROMS" >"$STATE/looker.cfg"
printf '%s' "$MD_ROMS/Zelda.chd" >/tmp/last.txt
rm -f "$STATE/search.cfg"
printf 'cancel_launches=on\nsteps=%s\n' "action:search.open,a,down,down,a,start,a,x,x" >"$STATE/seeker.cfg"
log="$WORK/multi-disc-first.log"
run_launcher "$log" "plugin seeker: done" "plugin looker: done" || fail "the launcher exited early with multi-disc"
plugin=looker
expect "entries Game.m3u,Other.m3u,Third.m3u,Zelda.chd"
expect 'art Game.m3u thumbnail 1 ".media/Game (Disc 1).png"'
expect 'art Other.m3u thumbnail 0 ".media/Other.png"'
expect 'art Zelda.chd thumbnail 0 ".media/Zelda.png"'
expect 'index Game.m3u 1 "Qzest Deluxe" 1999 added 1 filtered 0'
expect 'index Game (Disc 1).chd 1 "Qzest Deluxe" 1999 added 0 filtered 1'
expect 'index Game (Disc 2).chd 1 "Game"  added 0 filtered 1'
expect 'index Zelda.chd 1 "Zelda"  added 0 filtered 0'
plugin=seeker
expect "list $CARD/Search: Qzest Deluxe selected 0"
expect "pre_launch $MD_ROMS/Game.m3u"
plugin=multi-disc
expect "wrote $MD_REL/Game.m3u with 2 discs"
expect "copied /Saves/MDT/Game (Disc 1).chd.sav to Game.m3u.sav"
expect "copied /.userdata/shared/MDT-core/Game (Disc 1).chd.st0 to Game.m3u.st0"
expect "carried Game (Disc 1).chd over to Game.m3u, 2 save and state files copied"
expect "recent $MD_REL/Game (Disc 1).chd is now $MD_REL/Game.m3u"
[ "$(cat "$MD_ROMS/Game.m3u" 2>/dev/null)" = "$(printf 'Game (Disc 1).chd\nGame (Disc 2).chd')" ] || fail "Game.m3u doesn't list the discs in order"
[ "$(cat "$MD_SAVES/Game.m3u.sav" 2>/dev/null)" = "disc one" ] || fail "Disc 1's save wasn't copied to the .m3u's name"
[ "$(cat "$MD_STATES/Game.m3u.st0" 2>/dev/null)" = "state one" ] || fail "Disc 1's state wasn't copied to the .m3u's name"
[ -f "$MD_SAVES/Game (Disc 1).chd.sav" ] || fail "Disc 1's own save went"
[ "$(head -n 1 "$RECENTS" | cut -f1)" = "$MD_REL/Game.m3u" ] || fail "Disc 1's recent wasn't moved to the .m3u: $(head -n 1 "$RECENTS")"
[ "$(cat "$MD_FAVORITES")" = "$MD_REL/Game.m3u" ] || fail "Disc 1's favorite wasn't moved to the .m3u: $(cat "$MD_FAVORITES")"
grep -qxF "$MD_REL/Game.m3u" "$STATE/multi-disc.txt" 2>/dev/null || fail "Game wasn't marked done"

# Other, from the list: its own .m3u, launched as it is
printf '%s' "$MD_ROMS/Other.m3u" >/tmp/last.txt
printf 'cancel_launches=on\nsteps=%s\n' "a,x,x" >"$STATE/seeker.cfg"
md_plugins multi-disc favorites seeker
log="$WORK/multi-disc-other.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early launching Other"
plugin=seeker
expect "pre_launch $MD_ROMS/Other.m3u"
plugin=multi-disc
expect "carried Other (Disc 1).chd over to Other.m3u, 0 save and state files copied"
grep -qF "wrote $MD_REL/Other.m3u" "$log" && fail "multi-disc wrote over Other's own .m3u"
[ "$(cat "$MD_ROMS/Other.m3u")" = "$(printf 'Other (Disc 2).chd\nOther (Disc 1).chd')" ] || fail "Other's own .m3u changed"
[ "$(cat "$MD_SAVES/Other.m3u.sav")" = "mine" ] || fail "Other's save under its .m3u's name was overwritten"

# a second launch copies nothing and moves nothing again
rm -f "$MD_SAVES/Game.m3u.sav"
printf '%s/Game (Disc 1).chd\tGame\n' "$MD_REL" >"$RECENTS"
printf '%s/Game (Disc 1).chd\n' "$MD_REL" >"$MD_FAVORITES"
printf '%s' "$MD_ROMS/Game.m3u" >/tmp/last.txt
printf 'cancel_launches=on\nsteps=%s\n' "a,x,x" >"$STATE/seeker.cfg"
md_plugins multi-disc favorites seeker
log="$WORK/multi-disc-again.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early launching again with multi-disc"
plugin=seeker
expect "pre_launch $MD_ROMS/Game.m3u"
grep -qF "plugin multi-disc: carried" "$log" && fail "the second launch carried saves over again"
[ -e "$MD_SAVES/Game.m3u.sav" ] && fail "the second launch copied Disc 1's save again"
[ "$(cut -f1 "$RECENTS")" = "$MD_REL/Game (Disc 1).chd" ] || fail "the second launch moved Disc 1's recent again"
[ "$(cat "$MD_FAVORITES")" = "$MD_REL/Game (Disc 1).chd" ] || fail "the second launch moved Disc 1's favorite again"

# random-game, picking from the Pick tag in this system: Third, which
# isn't on the card yet, and its launch writes its .m3u
printf '%s' "$MD_ROMS/Zelda.chd" >/tmp/last.txt
printf 'from=tags:Pick\nroot=off\n' >"$STATE/random-game.cfg"
printf 'cancel_launches=on\nsteps=%s\n' "action:random-game.pick,x,x" >"$STATE/seeker.cfg"
md_plugins multi-disc tags random-game seeker
log="$WORK/multi-disc-random.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early picking a random game"
plugin=random-game
expect "picked $MD_ROMS/Third.m3u"
plugin=seeker
expect "pre_launch $MD_ROMS/Third.m3u"
plugin=multi-disc
expect "wrote $MD_REL/Third.m3u with 2 discs"
[ "$(cat "$MD_ROMS/Third.m3u" 2>/dev/null)" = "$(printf 'Third (Disc 1).chd\nThird (Disc 2).chd')" ] || fail "random-game's pick didn't write Third.m3u"

# Combine discs: refused for Other, whose folder is there, then Game
printf '%s/Game.m3u\tGame\n%s/Game (Disc 2).chd\tGame\n' "$MD_REL" "$MD_REL" >"$RECENTS"
printf '%s/Game.m3u\n' "$MD_REL" >"$MD_FAVORITES"
mkdir -p "$MD_ROMS/Other"
md_plugins multi-disc favorites tags seeker
printf '%s' "$MD_ROMS/Other.m3u" >/tmp/last.txt
printf 'steps=%s\n' "action:multi-disc.combine,x,a,x,x" >"$STATE/seeker.cfg"
log="$WORK/multi-disc-refused.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early refusing to combine"
plugin=multi-disc
expect "not combining Other: the folder is there already"
for name in "Other (Disc 1).chd" "Other (Disc 2).chd" Other.m3u; do
	[ -f "$MD_ROMS/$name" ] || fail "a refused Combine discs moved $name"
done
rmdir "$MD_ROMS/Other"

printf '%s' "$MD_ROMS/Game.m3u" >/tmp/last.txt
log="$WORK/multi-disc-combine.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early combining discs"
expect "combined 2 discs and 0 files of theirs into $MD_REL/Game"
plugin=seeker
expect "run multi-disc.combine 0"
for name in "Game (Disc 1).chd" "Game (Disc 2).chd"; do
	[ -f "$MD_ROMS/Game/$name" ] || fail "$name wasn't moved into Game/"
	[ -e "$MD_ROMS/$name" ] && fail "$name is still beside the folder"
done
[ "$(cat "$MD_ROMS/Game/Game.m3u" 2>/dev/null)" = "$(printf 'Game (Disc 1).chd\nGame (Disc 2).chd')" ] || fail "Game/Game.m3u doesn't list the discs"
[ -e "$MD_ROMS/Game.m3u" ] && fail "the loose Game.m3u is still there"
[ -f "$MD_ROMS/.media/Game.png" ] || fail "the folder didn't get Disc 1's art"
# minarch names a folder's saves by its .m3u too, so they're the same files
[ "$(cat "$MD_STATES/Game.m3u.st0" 2>/dev/null)" = "state one" ] || fail "the state under the .m3u's name went"
plugin=multi-disc
expect "recent $MD_REL/Game (Disc 2).chd is now $MD_REL/Game/Game (Disc 2).chd"
expect "recent $MD_REL/Game.m3u is now $MD_REL/Game/Game.m3u"
# and stock keeps one line for a game in a folder when recents load again
[ "$(cut -f1 "$RECENTS" | tr '\n' ',')" = "$MD_REL/Game/Game.m3u," ] || fail "recents weren't moved: $(cut -f1 "$RECENTS" | tr '\n' ',')"
[ "$(cat "$MD_FAVORITES")" = "$MD_REL/Game/Game.m3u" ] || fail "the favorite wasn't moved: $(cat "$MD_FAVORITES")"
grep -qF "Done	$MD_REL/Game/Game.m3u" "$STATE/tags.txt" || fail "the tag wasn't moved: $(cat "$STATE/tags.txt")"
rows=$(md_rows)
[ "$rows" = "Multi Disc Test (MDT)/Game/Game (Disc 1).chd:1,Multi Disc Test (MDT)/Game/Game (Disc 2).chd:1" ] || fail "play time wasn't moved: $rows"

# stock's layout now: the folder is the game, and the plugin leaves it alone
md_plugins multi-disc looker
printf 'folder=%s\n' "$MD_ROMS" >"$STATE/looker.cfg"
log="$WORK/multi-disc-after.log"
run_launcher "$log" "plugin looker: done" || fail "the launcher exited early after combining discs"
plugin=looker
expect "entries Game,Other.m3u,Third.m3u,Zelda.chd"
grep -qF "plugin looker: entries Game.m3u" "$log" && fail "the combined game still has a row of the plugin's"

rm -rf "$MD_ROMS" "$MD_EMU" "$MD_SAVES" "$MD_STATES" "$STATE/multi-disc.txt" "$STATE/tags.txt" "$PLAY_DB"
rmdir "$CARD/Emus/desktop" 2>/dev/null
for saved in "$RECENTS" "$MD_FAVORITES" "$PLAY_DB" "$STATE/tags.txt" /tmp/last.txt; do
	if [ -f "$MD_WORK/$(basename "$saved").saved" ]; then mv "$MD_WORK/$(basename "$saved").saved" "$saved"; else rm -f "$saved"; fi
done
rm -rf "$WORK/plugins/multi-disc.plugin" "$WORK/plugins/seeker.plugin" "$WORK/plugins/looker.plugin" \
	"$STATE/seeker.cfg" "$STATE/looker.cfg" "$STATE/plugins.txt" "$STATE/multi-disc.cfg" \
	"$STATE/search.cfg" "$STATE/random-game.cfg" "$WORK/plugins/search.plugin" "$WORK/plugins/random-game.plugin" \
	"$WORK/plugins/favorites.plugin" "$WORK/plugins/tags.plugin"
case_log=""
