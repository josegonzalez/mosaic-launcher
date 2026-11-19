# shellcheck shell=sh

# game-actions' Rename file, with tags (on L2) and the play history: the
# launcher starts on Alpha (the last path), where presser presses Y, which
# opens the keyboard on "Alpha", types the q it starts on, and confirms.
# Alpha.gb becomes Alphaq.gb with its art and .info, save, state, resume
# slot and preview, and its recent, tag, status, collection entry and play
# time follow, while Bravo's are left. Then a rename of Bravo whose save
# would land on a file that's there is refused, naming it, and nothing
# changes. The card's recents, play history and /tmp/last.txt are put
# back afterwards.
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" game-actions tags >/dev/null || fail "building game-actions"
RC_ROMS="$CARD/Roms/Rename Carry (RNC)"
RC_REL="/Roms/Rename Carry (RNC)"
RC_EMU="$CARD/Emus/desktop/RNC.pak"
RC_SAVES="$CARD/Saves/RNC"
RC_STATES="$CARD/.userdata/shared/RNC-core"
RC_SLOTS="$CARD/.userdata/shared/.minui/RNC"
RC_COLLECTION="$CARD/Collections/Rename Carry.txt"
RC_WORK="$WORK/rename-carry"
rm -rf "$RC_ROMS" "$RC_SAVES" "$RC_STATES" "$RC_SLOTS" "$RC_WORK"
mkdir -p "$RC_ROMS/.media" "$RC_EMU" "$RC_SAVES" "$RC_STATES" "$RC_SLOTS" "$RC_WORK" "$(dirname "$RECENTS")" \
	"$(dirname "$RC_COLLECTION")" "$STATE"
for saved in "$RECENTS" "$PLAY_DB" /tmp/last.txt; do
	[ -f "$saved" ] && cp "$saved" "$RC_WORK/$(basename "$saved").saved"
done
: >"$RC_EMU/launch.sh"
printf 'alpha rom' >"$RC_ROMS/Alpha.gb"
printf 'bravo rom' >"$RC_ROMS/Bravo.gb"
cp "$ROOT/tests/plugins/drawer/art.png" "$RC_ROMS/.media/Alpha.png"
printf 'year=1989\n' >"$RC_ROMS/.media/Alpha.info"
printf 'alpha save' >"$RC_SAVES/Alpha.gb.sav"
printf 'bravo save' >"$RC_SAVES/Bravo.gb.sav"
printf 'theirs' >"$RC_SAVES/Bravoq.gb.sav"
printf 'alpha state' >"$RC_STATES/Alpha.gb.st0"
printf '0' >"$RC_SLOTS/Alpha.gb.txt"
printf 'preview' >"$RC_SLOTS/Alpha.gb.0.bmp"
printf '%s/Alpha.gb\tAlpha\n%s/Bravo.gb\tBravo\n' "$RC_REL" "$RC_REL" >"$RECENTS"
printf '%s/Bravo.gb\n%s/Alpha.gb\n' "$RC_REL" "$RC_REL" >"$RC_COLLECTION"
printf 'Puzzle\nPuzzle\t%s/Alpha.gb\nPuzzle\t%s/Bravo.gb\n' "$RC_REL" "$RC_REL" >"$STATE/tags.txt"
printf 'completed\ncompleted\t%s/Alpha.gb\n' "$RC_REL" >"$STATE/status.txt"
printf 'button=L2\n' >"$STATE/tags.cfg"
printf 'rename=off\ndelete=off\ncollections=off\n' >"$STATE/game-actions.cfg"
printf 'game-actions=on\ntags=on\n' >"$STATE/plugins.txt"
now=$(date +%s)
make_play_db <<SQL || fail "making the play history fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Alpha', 'Rename Carry (RNC)/Alpha.gb');
INSERT INTO rom(id, type, name, file_path) VALUES(2, '', 'Bravo', 'Rename Carry (RNC)/Bravo.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 600, $((now - 3600)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(2, 300, $((now - 7200)));
SQL
rc_rows() {
	python3 -I -c 'import sqlite3, sys; print(",".join("%s:%s:%s" % r for r in sqlite3.connect(sys.argv[1]).execute(
		"SELECT rom.file_path, rom.name, COUNT(pa.rom_id) FROM rom LEFT JOIN play_activity pa ON pa.rom_id = rom.id GROUP BY rom.id ORDER BY rom.id")))' "$PLAY_DB"
}

# Alpha, renamed with everything it has
printf '%s' "$RC_ROMS/Alpha.gb" >/tmp/last.txt
PRESSER_KEYS="y a start a" SMOKE_PRELOAD="$PRESSER"
export PRESSER_KEYS SMOKE_PRELOAD
log="$WORK/rename-carry.log"
run_launcher "$log" "presser: done" "plugin game-actions: renamed" || fail "the launcher exited early renaming Alpha"
unset PRESSER_KEYS SMOKE_PRELOAD
plugin=game-actions
expect "moving $RC_REL/Alpha.gb to $RC_REL/Alphaq.gb"
expect "moving /.userdata/shared/RNC-core/Alpha.gb.st0 to /.userdata/shared/RNC-core/Alphaq.gb.st0"
expect "moved the play time of $RC_REL/Alpha.gb (1 paths with sessions)"
expect "recent $RC_REL/Alpha.gb is now $RC_REL/Alphaq.gb"
expect "moved it in the Rename Carry collection"
expect "renamed $RC_REL/Alpha.gb to $RC_REL/Alphaq.gb"
grep -qF "plugins: game-actions moved the play history of 1 path: 1" "$log" || fail "the launcher didn't log the play history move"
for file in "$RC_ROMS/Alphaq.gb" "$RC_ROMS/.media/Alphaq.png" "$RC_ROMS/.media/Alphaq.info" "$RC_SAVES/Alphaq.gb.sav" \
	"$RC_STATES/Alphaq.gb.st0" "$RC_SLOTS/Alphaq.gb.txt" "$RC_SLOTS/Alphaq.gb.0.bmp"; do
	[ -f "$file" ] || fail "$(basename "$file") isn't there after the rename"
done
for file in "$RC_ROMS/Alpha.gb" "$RC_ROMS/.media/Alpha.png" "$RC_ROMS/.media/Alpha.info" "$RC_SAVES/Alpha.gb.sav" \
	"$RC_STATES/Alpha.gb.st0" "$RC_SLOTS/Alpha.gb.txt" "$RC_SLOTS/Alpha.gb.0.bmp"; do
	[ -e "$file" ] && fail "$(basename "$file") is still there after the rename"
done
[ "$(cat "$RC_SAVES/Alphaq.gb.sav")" = "alpha save" ] || fail "Alpha's save isn't the one that moved"
[ "$(cat "$RC_SAVES/Bravo.gb.sav")" = "bravo save" ] || fail "Bravo's save changed"
[ "$(cut -f1 "$RECENTS" | tr '\n' ',')" = "$RC_REL/Alphaq.gb,$RC_REL/Bravo.gb," ] || fail "recents weren't moved: $(cat "$RECENTS")"
[ "$(cat "$RC_COLLECTION")" = "$(printf '%s/Bravo.gb\n%s/Alphaq.gb' "$RC_REL" "$RC_REL")" ] \
	|| fail "the collection wasn't moved: $(cat "$RC_COLLECTION")"
grep -qF "$RC_REL/Alphaq.gb" "$STATE/tags.txt" || fail "the tag wasn't moved: $(cat "$STATE/tags.txt")"
grep -qF "$RC_REL/Alpha.gb" "$STATE/tags.txt" && fail "the old path is still tagged: $(cat "$STATE/tags.txt")"
grep -qF "$RC_REL/Bravo.gb" "$STATE/tags.txt" || fail "Bravo's tag went: $(cat "$STATE/tags.txt")"
grep -qF "$RC_REL/Alphaq.gb" "$STATE/status.txt" || fail "the status wasn't moved: $(cat "$STATE/status.txt")"
rows=$(rc_rows)
[ "$rows" = "Rename Carry (RNC)/Alphaq.gb:Alphaq:1,Rename Carry (RNC)/Bravo.gb:Bravo:1" ] || fail "the play history isn't at the new path: $rows"

# Bravo, whose save would land on Bravoq.gb.sav: refused, nothing changes
printf '%s' "$RC_ROMS/Bravo.gb" >/tmp/last.txt
PRESSER_KEYS="y a start" SMOKE_PRELOAD="$PRESSER"
export PRESSER_KEYS SMOKE_PRELOAD
log="$WORK/rename-carry-clash.log"
run_launcher "$log" "presser: done" "plugin game-actions: not renaming" || fail "the launcher exited early renaming Bravo"
unset PRESSER_KEYS SMOKE_PRELOAD
expect "not renaming $RC_REL/Bravo.gb: /Saves/RNC/Bravoq.gb.sav is there already"
grep -qF "plugin game-actions: moving" "$log" && fail "a refused rename moved something"
if ! { [ -f "$RC_ROMS/Bravo.gb" ] && [ ! -e "$RC_ROMS/Bravoq.gb" ]; }; then
	fail "the refused rename moved Bravo"
fi
if ! { [ "$(cat "$RC_SAVES/Bravo.gb.sav")" = "bravo save" ] && [ "$(cat "$RC_SAVES/Bravoq.gb.sav")" = "theirs" ]; }; then
	fail "the refused rename changed a save"
fi
[ "$(rc_rows)" = "$rows" ] || fail "the refused rename changed the play history: $(rc_rows)"
grep -qF "$RC_REL/Bravo.gb" "$RC_COLLECTION" || fail "the refused rename changed the collection"

for saved in "$RECENTS" "$PLAY_DB" /tmp/last.txt; do
	if [ -f "$RC_WORK/$(basename "$saved").saved" ]; then cp "$RC_WORK/$(basename "$saved").saved" "$saved"; else rm -f "$saved"; fi
done
rm -rf "$RC_ROMS" "$RC_EMU" "$RC_SAVES" "$RC_STATES" "$RC_SLOTS" "$RC_WORK" "$RC_COLLECTION" "$PRESSER" \
	"$WORK/plugins/game-actions.plugin" "$WORK/plugins/tags.plugin" "$STATE/tags.txt" "$STATE/status.txt" "$STATE/tags.cfg" \
	"$STATE/game-actions.cfg" "$STATE/plugins.txt" "$STATE/game-actions-names.txt"
case_log=""
