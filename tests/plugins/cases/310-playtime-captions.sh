# shellcheck shell=sh
# shard: 1

# playtime-captions puts a caption at the right of played games' rows, which
# subtitler, after it in load order, reads back off the screen: Tetris was
# played 2h 15m over 2 sessions 3 days ago, Q 2h 5m an hour ago,
# Unplayed never, and the long name leaves no room. Local time is about
# noon, as for tracker.
CAPTION_DIR="$CARD/Roms/Play Captions (PCT)"
LONG_NAME="The Legend of a Game Whose Name Is Far Too Long for Any Row on the Screen"
mkdir -p "$CAPTION_DIR/Extras" "$CAPTION_DIR/.media" "$(dirname "$PLAY_DB")" "$STATE"
for game in Tetris Q Unplayed "$LONG_NAME"; do : >"$CAPTION_DIR/$game.gb"; done
: >"$CAPTION_DIR/Extras/Bonus.gb"
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$WORK/game_logs.sqlite.saved"
now=$(date +%s)
hours_behind=$(( $(date -u +%H | sed 's/^0//') - 12 ))
make_play_db <<SQL || fail "making the play captions fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Tetris', 'Play Captions (PCT)/Tetris.gb');
INSERT INTO rom(id, type, name, file_path) VALUES(2, '', 'Long', 'Play Captions (PCT)/$LONG_NAME.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 7500, $((now - 10 * 86400)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 600, $((now - 3 * 86400)));
INSERT INTO rom(id, type, name, file_path) VALUES(3, '', 'Q', 'Play Captions (PCT)/Q.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(2, 600, $((now - 3600)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(3, 7500, $((now - 3600)));
SQL
TZ="TST$(printf '%+d' "$hours_behind")"
export TZ
make -s -C "$ROOT/plugins" playtime-captions ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building playtime-captions"
build_plugin subtitler 'id=subtitler\nname=Subtitler\napi=1\n'
printf 'folder=Roms/Play Captions (PCT)\nTetris=2h 15m \302\267 3 days ago\n' >"$STATE/subtitler.cfg"
printf 'playtime-captions=on\nsubtitler=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/playtime-captions.log" "plugin subtitler: row Unplayed" "plugin subtitler: row Tetris" || fail "the launcher exited early with playtime-captions loaded"
log="$WORK/playtime-captions.log"
plugin=playtime-captions
expect "time on, last on, sessions off, format relative, never off"
plugin=subtitler
expect "open folder 1"
expect "row Tetris caption at right 1, clear of name 1, as wide as \"2h 15m · 3 days ago\" 1"
expect "row Unplayed caption none"
expect "row Extras caption none"
expect "row $LONG_NAME caption none"
grep -qF "plugins: playtime-captions's" "$log" && fail "playtime-captions' decorations were turned off"

# every part in the compact format, and "Never played"
printf 'sessions=on\nformat=compact\nnever=on\n' >"$STATE/playtime-captions.cfg"
printf 'folder=Roms/Play Captions (PCT)\nTetris=2h \302\267 3d \302\267 2x\nUnplayed=Never played\n' >"$STATE/subtitler.cfg"
run_launcher "$WORK/playtime-captions-set.log" "plugin subtitler: row Unplayed" "plugin subtitler: row Tetris" || fail "the launcher exited early with playtime-captions' settings"
log="$WORK/playtime-captions-set.log"
plugin=playtime-captions
expect "time on, last on, sessions on, format compact, never on"
plugin=subtitler
expect "row Tetris caption at right 1, clear of name 1, as wide as \"2h · 3d · 2x\" 1"
expect "row Unplayed caption at right 1, clear of name 1, as wide as \"Never played\" 1"
expect "row Extras caption none"

# with row-badges' dots after the names, both show, apart (row-badges
# counts the play history too, so only Unplayed gets its unplayed dot)
printf 'tier=3\n' >"$CAPTION_DIR/.media/Tetris.info"
printf 'unplayed=on\ntiers=on\n' >"$STATE/row-badges.cfg"
printf 'folder=Roms/Play Captions (PCT)\n' >"$STATE/dotter.cfg"
printf 'folder=Roms/Play Captions (PCT)\nTetris=2h 15m \302\267 3 days ago\n' >"$STATE/subtitler.cfg"
rm -f "$STATE/playtime-captions.cfg"
make -s -C "$ROOT/plugins" row-badges ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building row-badges"
build_plugin dotter 'id=dotter\nname=Dotter\napi=1\n'
printf 'row-badges=on\nplaytime-captions=on\ndotter=on\nsubtitler=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/playtime-captions-badges.log" "plugin subtitler: row Unplayed" "plugin subtitler: row Tetris" "plugin dotter: row Unplayed" || fail "the launcher exited early with playtime-captions and row-badges"
log="$WORK/playtime-captions-badges.log"
plugin=dotter
expect "row Tetris unplayed 0 tiers 3"
expect "row Unplayed unplayed 1 tiers 0"
plugin=subtitler
expect "row Tetris caption at right 1, clear of name 1, as wide as \"2h 15m · 3 days ago\" 1"
grep -qF "plugins: playtime-captions's" "$log" && fail "playtime-captions' decorations were turned off with row-badges"
grep -qF "plugins: row-badges's" "$log" && fail "row-badges' decorations were turned off with playtime-captions"
rm -rf "$WORK/plugins/dotter.plugin" "$WORK/plugins/row-badges.plugin" "$STATE/dotter.cfg" "$STATE/row-badges.cfg"

# under the bundled layout-grid, on the cells' name line, after the
# centred name: Q's short name leaves room for "2h", and Tetris' for
# nothing
make -s -C "$ROOT/plugins" layout-grid ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building layout-grid"
printf 'folder=Roms/Play Captions (PCT)\nQ=2h\n' >"$STATE/subtitler.cfg"
printf 'layout-grid=on\nplaytime-captions=on\nsubtitler=on\nslot.layout=layout-grid\n' >"$STATE/plugins.txt"
run_launcher "$WORK/playtime-captions-grid.log" "plugin subtitler: row Unplayed" "plugin subtitler: row Q" || fail "the launcher exited early with playtime-captions under layout-grid"
log="$WORK/playtime-captions-grid.log"
plugin=subtitler
expect "row Q caption at right 1, clear of name 1, as wide as \"2h\" 1"
expect "row Tetris caption none"
expect "row Unplayed caption none"
grep -qF "plugins: playtime-captions's" "$log" && fail "playtime-captions' decorations were turned off under layout-grid"
grep -qF "plugins: layout-grid's" "$log" && fail "layout-grid was turned off with playtime-captions"
rm -rf "$WORK/plugins/layout-grid.plugin"

# the achievements part: Tetris has a cached set of 3 with 1 unlocked.
# PCT is a tag minarch doesn't map, so the launcher takes rcheevos' guess
# that the card has a set for, as minarch takes the one the server knows.
printf 'Tetris cart %.0s' 1 2 3 4 >"$CAPTION_DIR/Tetris.gb"
TETRIS_HASH=$(md5_of "$CAPTION_DIR/Tetris.gb")
[ -d "$RA_DIR" ] && mv "$RA_DIR" "$WORK/ra.saved"
mkdir -p "$RA_DIR/offline/cache"
frame_json '{"Success":true,"GameId":7,"Sets":[{"Achievements":[{"ID":1,"Flags":3,"Points":1},{"ID":2,"Flags":3,"Points":1},{"ID":3,"Flags":3,"Points":1}]}]}' \
	"$RA_DIR/offline/cache/achievementsets_$TETRIS_HASH.bin"
frame_json '{"Success":true,"Unlocks":[{"ID":3,"When":1}]}' "$RA_DIR/offline/cache/startsession_$TETRIS_HASH.bin"
printf 'achievements=on\n' >"$STATE/playtime-captions.cfg"
printf 'folder=Roms/Play Captions (PCT)\nTetris=2h 15m \302\267 3 days ago \302\267 1/3 achievements\n' >"$STATE/subtitler.cfg"
printf 'playtime-captions=on\nsubtitler=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/playtime-captions-achievements.log" "plugin subtitler: row Unplayed" \
	"plugin subtitler: row Tetris caption at right 1, clear of name 1, as wide as \"2h 15m · 3 days ago · 1/3 achievements\" 1" ||
	fail "the launcher exited early with playtime-captions' achievements"
log="$WORK/playtime-captions-achievements.log"
plugin=playtime-captions
expect "time on, last on, sessions off, format relative, never off, achievements on"
plugin=subtitler
expect "row Tetris caption at right 1, clear of name 1, as wide as \"2h 15m · 3 days ago · 1/3 achievements\" 1"
expect "row Unplayed caption none"
grep -qF "retroachievements: Roms/Play Captions (PCT)/Tetris.gb is $TETRIS_HASH, guessed" "$log" || fail "Tetris.gb's guessed hash wasn't logged"
grep -qF "plugins: playtime-captions's" "$log" && fail "playtime-captions' decorations were turned off with achievements"
rm -rf "$RA_DIR" "$RA_HASHES"
# the folder the launcher made for its hash cache, if nothing else is in it
if [ -d "$(dirname "$RA_HASHES")" ]; then rmdir "$(dirname "$RA_HASHES")" 2>/dev/null; fi
[ -d "$WORK/ra.saved" ] && mv "$WORK/ra.saved" "$RA_DIR"
rm -f "$STATE/playtime-captions.cfg"

# with the history unknown, nothing, even with never on
printf 'never=on\n' >"$STATE/playtime-captions.cfg"
printf 'playtime-captions=on\nsubtitler=on\n' >"$STATE/plugins.txt"
MOSAIC_LAUNCHER_SQLITE="$WORK/no-such-libsqlite3"
export MOSAIC_LAUNCHER_SQLITE
run_launcher "$WORK/playtime-captions-unknown.log" "plugin subtitler: row Unplayed" "plugin subtitler: row Tetris" || fail "the launcher exited early with playtime-captions and no sqlite"
unset MOSAIC_LAUNCHER_SQLITE
log="$WORK/playtime-captions-unknown.log"
plugin=subtitler
expect "row Tetris caption none"
expect "row Unplayed caption none"
unset TZ
rm -f "$PLAY_DB"
[ -f "$WORK/game_logs.sqlite.saved" ] && mv "$WORK/game_logs.sqlite.saved" "$PLAY_DB"
rm -rf "$WORK/plugins/subtitler.plugin" "$WORK/plugins/playtime-captions.plugin" "$CAPTION_DIR" \
	"$STATE/subtitler.cfg" "$STATE/playtime-captions.cfg" "$STATE/plugins.txt"
