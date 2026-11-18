# shellcheck shell=sh

# second-screen's panel from the other bundled plugins: clipper opens a
# folder and moves from Apple (art, a .info sidecar, a favorite tagged
# Puzzle, with play history) to Banana (none of them). With metadata-band,
# playtime-captions, favorites, tags and music on (music showing its
# resume file's track paused), Apple's frame has metadata-band's three
# fields, the caption, both marks and what music plays. With metadata-band
# off its fields don't show and the rest still do, and with second-screen's
# toggles off none of them show.
SI_DIR="$CARD/Roms/Second Screen Interop (SSI)"
SI_MUSIC="$CARD/Music"
SI_FAV="$CARD/Collections/Favorites.txt"
SI_WORK="$WORK/second-screen-interop"
SI_RESUME="$STATE/music/resume.txt"
SI_LOG="$CARD/.userdata/desktop/logs/second-screen.txt"

# si_stop: stops a music daemon left running, by the pid it wrote
si_stop() {
	if [ -f /tmp/mosaic-music.pid ]; then
		si_pid=$(cat /tmp/mosaic-music.pid)
		kill "$si_pid" 2>/dev/null
		i=0
		while kill -0 "$si_pid" 2>/dev/null && [ "$i" -lt 30 ]; do
			sleep 0.1
			i=$((i + 1))
		done
	fi
	rm -f /tmp/mosaic-music.sock /tmp/mosaic-music.pid
}

# si_run <name>: the launcher until second-screen drew Banana's frame, its
# log in $log
si_run() {
	log="$WORK/second-screen-interop-$1.log"
	rm -f "$SI_LOG"
	run_launcher "$log" "plugin clipper: done" "Banana: no art, 1 title line, Game, 0 info lines, read in " \
		|| fail "the launcher exited early ($1)"
	plugin=second-screen
}

# si_frame <name> <what> <panel>: second-screen drew name's frame as what
# (eg. "art 8x8 at 24,98 284x284, 1 title line, Game, 3 info lines") and
# with panel (eg. marks "Favorite", caption "", music "")
si_frame() {
	n=$(grep -F "plugin second-screen: frame " "$log" | grep -F ": $1: $2, read in " |
		sed -n 's/.*plugin second-screen: frame \([0-9]*\): .*/\1/p' | tail -n 1)
	if [ -z "$n" ]; then
		fail "second-screen drew no frame for $1 as \"$2\": $(grep -F "plugin second-screen: frame " "$log")"
		return
	fi
	grep -qF "plugin second-screen: frame $n panel: $3" "$log" \
		|| fail "$1's frame $n didn't have $3: $(grep -F "plugin second-screen: frame $n panel: " "$log")"
}

si_stop
rm -rf "$SI_WORK" "$STATE/music"
mkdir -p "$SI_WORK" "$SI_DIR/.media" "$STATE/music" "$CARD/Collections"
[ -e "$SI_MUSIC" ] && mv "$SI_MUSIC" "$SI_WORK/Music.saved"
[ -f "$SI_FAV" ] && cp "$SI_FAV" "$SI_WORK/Favorites.txt.saved"
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$SI_WORK/game_logs.sqlite.saved"
mkdir -p "$SI_MUSIC"
: >"$SI_DIR/Apple.gb"
: >"$SI_DIR/Banana.gb"
cp "$ROOT/tests/plugins/peeker/art.png" "$SI_DIR/.media/Apple.png"
printf 'year=1994-11-21\npublisher=Nintendo\ngenre=Platformer\n' >"$SI_DIR/.media/Apple.info"
printf '/Roms/Second Screen Interop (SSI)/Apple.gb\n' >"$SI_FAV"
printf 'Puzzle\t/Roms/Second Screen Interop (SSI)/Apple.gb\n' >"$STATE/tags.txt"
now=$(date +%s)
make_play_db <<SQL || fail "making the play history"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Apple', 'Second Screen Interop (SSI)/Apple.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 7500, $((now - 3 * 86400)));
SQL
# a second of silence, and music's resume file paused on it, so music.now
# has a line without playing anything
python3 -I -c '
import sys, wave
w = wave.open(sys.argv[1], "wb")
w.setnchannels(1)
w.setsampwidth(2)
w.setframerate(8000)
w.writeframes(b"\0\0" * 8000)
w.close()
' "$SI_MUSIC/One.wav" || fail "making the test track"
printf 'music-resume 1\npos=0\nlen=1000\nindex=0\nshuffle=0\nrepeat=off\ncount=1\norder=0\ntrack=%s\npath=%s\nend\n' \
	"$SI_MUSIC/One.wav" "$SI_MUSIC/One.wav" >"$SI_RESUME"
for id in second-screen metadata-band playtime-captions favorites tags music; do
	make -s -C "$ROOT/plugins" "$id" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building $id"
done
build_plugin clipper 'id=clipper\nname=Clipper\napi=1\n'
printf 'folder=Roms/Second Screen Interop (SSI)\nstep=2500\n' >"$STATE/clipper.cfg"
SDL_AUDIODRIVER=dummy
export SDL_AUDIODRIVER

APPLE="art 8x8 at 24,98 284x284, 1 title line, Game"
BANANA="no art, 1 title line, Game, 0 info lines"
MARKS="marks \"Favorite $(printf '\302\267') Tags: Puzzle\""

# every owner on
printf 'second-screen=on\nclipper=on\nmetadata-band=on\nplaytime-captions=on\nfavorites=on\ntags=on\nmusic=on\n' \
	>"$STATE/plugins.txt"
si_run all
expect "details on, caption on, marks on, music on"
si_frame Apple "$APPLE, 3 info lines" "$MARKS, caption \""
grep -F "plugin second-screen: frame " "$log" | grep -qE 'panel: marks "[^"]+", caption "[^"]+", music "Paused: One"' \
	|| fail "Apple's frame had no caption or music: $(grep -F "panel: " "$log" | head -n 3)"
si_frame Banana "$BANANA" "marks \"\", caption \"\", music \"Paused: One\""

# metadata-band off: no fields, and the rest as before
printf 'second-screen=on\nclipper=on\nmetadata-band=off\nplaytime-captions=on\nfavorites=on\ntags=on\nmusic=on\n' \
	>"$STATE/plugins.txt"
si_run no-band
si_frame Apple "$APPLE, 0 info lines" "$MARKS, caption \""
grep -F "panel: $MARKS" "$log" | grep -qF 'music "Paused: One"' || fail "music didn't show with metadata-band off"

# every owner on and second-screen's toggles off: none of it
printf 'second-screen=on\nclipper=on\nmetadata-band=on\nplaytime-captions=on\nfavorites=on\ntags=on\nmusic=on\n' \
	>"$STATE/plugins.txt"
printf 'details=off\ncaption=off\nmarks=off\nmusic=off\n' >"$STATE/second-screen.cfg"
si_run toggles-off
expect "details off, caption off, marks off, music off"
si_frame Apple "$APPLE, 0 info lines" "marks \"\", caption \"\", music \"\""

si_stop
rm -rf "$SI_DIR" "$SI_MUSIC" "$STATE/music"
[ -e "$SI_WORK/Music.saved" ] && mv "$SI_WORK/Music.saved" "$SI_MUSIC"
if [ -f "$SI_WORK/Favorites.txt.saved" ]; then mv "$SI_WORK/Favorites.txt.saved" "$SI_FAV"; else rm -f "$SI_FAV"; fi
rm -f "$PLAY_DB"
[ -f "$SI_WORK/game_logs.sqlite.saved" ] && mv "$SI_WORK/game_logs.sqlite.saved" "$PLAY_DB"
unset SDL_AUDIODRIVER
for id in second-screen metadata-band playtime-captions favorites tags music clipper; do
	rm -rf "$WORK/plugins/$id.plugin"
done
rm -rf "$SI_WORK" "$SI_LOG"
rm -f "$STATE/clipper.cfg" "$STATE/second-screen.cfg" "$STATE/tags.txt" "$STATE/plugins.txt" "$STATE/music.cfg" \
	"$STATE/favorites.cfg" "$STATE/tags.cfg"
case_log=""
