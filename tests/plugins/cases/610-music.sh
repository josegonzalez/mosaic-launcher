# shellcheck shell=sh
# shard: 2

# the bundled music plugin and its daemon, with SDL's dummy audio driver:
# the player opens from its button and lists the music folder (folders
# first, no dot files or other files), A opens a folder and plays a track,
# which starts the daemon; X pauses and plays on, R1 goes to the next
# track; B goes back up, and Y locks the launcher's lock and closes the
# player, the lock screen takes A, and the lock's chord unlocks, back on
# the list, where the now playing pill shows. The daemon
# plays on after the launcher stops, pauses for a pak's launch and plays on
# when the launcher comes back, and quits before a power-off, saving the
# queue and the track's place; the next start shows that paused, on the
# pill and the player, and X plays on from the place. The card's
# Music folder, recents and the launch's /tmp files are put back, and any
# daemon left is stopped by its pid
MU_DIR="$CARD/Music"
MU_ALBUM="$MU_DIR/Album"
MU_PAK="$CARD/Music Test/Tool.pak"
MU_LOG="$CARD/.userdata/desktop/logs/music.txt"
MU_WORK="$WORK/music"
MU_SOCK=/tmp/mosaic-music.sock
MU_PIDFILE=/tmp/mosaic-music.pid
MU_RESUME="$STATE/music/resume.txt"

# mu_stop: stops a daemon left running, by the pid it wrote
mu_stop() {
	if [ -f "$MU_PIDFILE" ]; then
		mu_pid=$(cat "$MU_PIDFILE")
		kill "$mu_pid" 2>/dev/null
		i=0
		while kill -0 "$mu_pid" 2>/dev/null && [ "$i" -lt 30 ]; do
			sleep 0.1
			i=$((i + 1))
		done
	fi
	rm -f "$MU_SOCK" "$MU_PIDFILE"
}

# mu_send <line>: the daemon's answer to a command
mu_send() {
	python3 -I -c '
import socket, sys
s = socket.socket(socket.AF_UNIX)
s.settimeout(3)
s.connect(sys.argv[1])
s.sendall((sys.argv[2] + "\n").encode())
print(s.makefile().readline().rstrip())' "$MU_SOCK" "$1" 2>/dev/null
}

mu_stop
rm -rf "$MU_WORK" "$STATE/music" /tmp/mosaic-music-art
mkdir -p "$MU_WORK" "$STATE" "$MU_PAK"
[ -e "$MU_DIR" ] && mv "$MU_DIR" "$MU_WORK/Music.saved"
[ -f "$RECENTS" ] && cp "$RECENTS" "$MU_WORK/recent.txt.saved"
for f in next last.txt; do
	[ -f "/tmp/$f" ] && cp "/tmp/$f" "$MU_WORK/$f.saved"
done
: >"$MU_PAK/launch.sh"
mkdir -p "$MU_ALBUM"
python3 -I -c '
import math, struct, sys, wave
for path, seconds in zip(sys.argv[1::2], sys.argv[2::2]):
    w = wave.open(path, "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(8000)
    w.writeframes(b"".join(struct.pack("<h", int(6000 * math.sin(i * 0.1))) for i in range(8000 * int(seconds))))
    w.close()
' "$MU_ALBUM/01 One.wav" 40 "$MU_ALBUM/02 Two.wav" 40 "$MU_DIR/Loose.wav" 2 || fail "making the test tracks"
cp "$MU_DIR/Loose.wav" "$MU_ALBUM/._01 One.wav"
: >"$MU_ALBUM/notes.txt"
# the folder's cover, an 8 by 8 PNG
python3 -I -c '
import struct, sys, zlib
def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
rows = b"".join(b"\0" + b"\xc0\x40\x40" * 8 for _ in range(8))
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 8, 8, 8, 2, 0, 0, 0)) \
    + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
' "$MU_ALBUM/cover.png" || fail "making the cover"
MU_TRACKS=2
MU_TAGGED=""
if command -v ffmpeg >/dev/null; then
	ffmpeg -v error -f lavfi -i sine=frequency=440:duration=2 "$MU_ALBUM/03 Three.flac" || fail "making the FLAC track"
	# tagged, with its own cover embedded
	ffmpeg -v error -f lavfi -i sine=frequency=440:duration=20 -i "$MU_ALBUM/cover.png" -map 0 -map 1 -c:v copy \
		-id3v2_version 3 -metadata title="Tenth Song" -metadata artist="Test Artist" -metadata album="Test Album" \
		-metadata:s:v comment="Cover (front)" "$MU_ALBUM/10 Ten.mp3" || fail "making the MP3 track"
	MU_TRACKS=4
	MU_TAGGED=1
fi
rm -f "$MU_LOG"

PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
make -s -C "$ROOT/plugins" music ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building music"
printf 'music=on\n' >"$STATE/plugins.txt"
printf 'button=Y\noverlay=10\n' >"$STATE/music.cfg"
[ -f "$STATE/settings.txt" ] && cp "$STATE/settings.txt" "$MU_WORK/settings.txt.saved"
printf 'lock_chord=select+start\n' >"$STATE/settings.txt"
SDL_AUDIODRIVER=dummy
export SDL_AUDIODRIVER
plugin=music

# the player, the daemon starting, pause, next, the lock screen and the pill
log="$WORK/music.log"
PRESSER_KEYS="y a a wait wait x wait x r1 wait b y a select+start/1500 wait wait wait wait wait"
PRESSER_LOG="$MU_WORK/player.texts" SMOKE_PRELOAD="$PRESSER"
export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
run_launcher "$log" "presser: done" || fail "the launcher exited early with music"
unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
expect "music from $MU_DIR, daemon $WORK/plugins/music.plugin/lib/$ARCH/musicd, games pause, pill 10, shuffle off, repeat off"
expect "the daemon isn't running"
expect "showing $MU_DIR: 1 of 2, Album/"
expect "showing $MU_ALBUM: 1 of $MU_TRACKS, 01 One.wav"
expect "started the daemon, pid "
expect "playing $MU_ALBUM/01 One.wav"
expect "now: 01 One, playing of 0:40"
expect "now: 01 One, paused of 0:40"
expect "now: 02 Two, playing of 0:40"
expect "locked"
grep -qF "lock: unlocked by the chord" "$log" || fail "the lock's chord didn't unlock"
expect "pill over gamelist: Playing: 02 Two"
[ "$(grep -c "plugin music: playing " "$log")" = 1 ] || fail "the lock screen's A reached the player: $(grep "plugin music: playing " "$log")"
[ "$(grep -c "plugin music: showing $MU_DIR: 1 of 2, Album/" "$log")" = 2 ] \
	|| fail "B didn't go back up to the music folder with Album selected"
grep -qF "Hold SELECT+START to unlock" "$MU_WORK/player.texts" || fail "the lock screen didn't show"
# untagged tracks keep their file names, with the folder's cover
expect "tags of $MU_ALBUM/01 One.wav: title \"\", artist \"\", art $MU_ALBUM/cover.png"
expect "showing the art $MU_ALBUM/cover.png"
grep -qxF "01 One" "$MU_WORK/player.texts" || fail "the untagged track's file name didn't show"
if [ -n "$MU_TAGGED" ]; then
	# the tagged track's title in the list, in file name order
	expect "read the tags of $MU_TRACKS tracks in $MU_ALBUM, 1 with titles"
	grep -qxF "Tenth Song" "$MU_WORK/player.texts" || fail "the tagged track's title didn't show in the list"
fi
grep -qF "musicd: playing $MU_ALBUM/01 One.wav (8000 Hz, 1 channels, 0:40), 1 of $MU_TRACKS" "$MU_LOG" \
	|| fail "the daemon didn't play the first track: $(cat "$MU_LOG")"
grep -qF "musicd: paused at" "$MU_LOG" || fail "the daemon didn't pause"
grep -qF "musicd: playing on at" "$MU_LOG" || fail "the daemon didn't play on"
grep -qF "musicd: playing $MU_ALBUM/02 Two.wav" "$MU_LOG" || fail "the daemon didn't go to the next track"

# it plays on with the launcher gone
mu_pid=$(cat "$MU_PIDFILE" 2>/dev/null)
kill -0 "$mu_pid" 2>/dev/null || fail "the daemon didn't outlive the launcher"
mu_send status | grep -q "state=playing.*track=$MU_ALBUM/02 Two.wav$" || fail "the daemon isn't playing on: $(mu_send status)"

# a pak's launch pauses it, holding it for the launcher's return
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
printf 'steps=pak:%s\n' "$MU_PAK" >"$STATE/screener.cfg"
printf 'music=on\nscreener=on\n' >"$STATE/plugins.txt"
log="$WORK/music-launch.log"
run_launcher "$log" && fail "the launcher didn't quit to launch the pak"
expect "the daemon is running"
expect "now: 02 Two, playing of 0:40"
expect "paused for the launch"
mu_send status | grep -q "state=paused.*held=1" || fail "the launch didn't hold the music: $(mu_send status)"
grep -qF "musicd: closed the audio device: held for a game" "$MU_LOG" || fail "the hold kept the audio device"

# and it plays on once the launcher is back
rm -f "$STATE/screener.cfg"
printf 'music=on\n' >"$STATE/plugins.txt"
log="$WORK/music-return.log"
run_launcher "$log" "plugin music: playing on after the launch" || fail "the launcher exited early after the launch"
expect "now: 02 Two, paused of 0:40, held"
mu_send status | grep -q "state=playing.*held=0" || fail "the music didn't play on after the launch: $(mu_send status)"
rm -f /tmp/mosaic-launcher-launched

# a power-off stops it
build_plugin powerer 'id=powerer\nname=Powerer\napi=1\n'
printf 'power=off\n' >"$STATE/powerer.cfg"
printf 'music=on\npowerer=on\n' >"$STATE/plugins.txt"
log="$WORK/music-poweroff.log"
run_launcher "$log" && fail "power_off didn't quit the launcher with music"
expect "stopped the daemon for the power-off"
i=0
while kill -0 "$mu_pid" 2>/dev/null && [ "$i" -lt 30 ]; do
	sleep 0.1
	i=$((i + 1))
done
kill -0 "$mu_pid" 2>/dev/null && fail "the daemon didn't quit for the power-off"
[ -e "$MU_SOCK" ] && fail "the daemon left its socket"
[ -e "$MU_PIDFILE" ] && fail "the daemon left its pid file"
grep -qF "musicd: quit" "$MU_LOG" || fail "the daemon didn't log its quit"

# it saved the queue and the track's place first
[ -f "$MU_RESUME" ] || fail "the power-off didn't save the place: $(cat "$MU_LOG")"
grep -qxF "track=$MU_ALBUM/02 Two.wav" "$MU_RESUME" || fail "the resume file isn't on the track playing: $(cat "$MU_RESUME")"
grep -qxF "count=$MU_TRACKS" "$MU_RESUME" || fail "the resume file doesn't have the queue: $(cat "$MU_RESUME")"
grep -qF "musicd: saved the place, " "$MU_LOG" || fail "the daemon didn't log the save"
mu_saved=$(sed -n 's/^pos=//p' "$MU_RESUME")
[ "${mu_saved:-0}" -gt 0 ] || fail "the saved place is the track's start: $(cat "$MU_RESUME")"
# at 20 seconds, so playing on from it is told apart from the track's start
sed 's/^pos=.*/pos=20000/' "$MU_RESUME" >"$MU_WORK/resume.txt" && mv "$MU_WORK/resume.txt" "$MU_RESUME"

# the next start shows it paused on the pill and the player without
# starting the daemon, and X plays on from the place
rm -rf "$WORK/plugins/powerer.plugin" "$WORK/plugins/screener.plugin" "$STATE/powerer.cfg"
printf 'music=on\n' >"$STATE/plugins.txt"
log="$WORK/music-resume.log"
PRESSER_KEYS="wait wait y wait x wait wait"
PRESSER_LOG="$MU_WORK/resume.texts" SMOKE_PRELOAD="$PRESSER"
export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
run_launcher "$log" "presser: done" || fail "the launcher exited early resuming music"
unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
expect "the daemon isn't running"
expect "resuming $MU_ALBUM/02 Two.wav at 20000 ms, 2 of $MU_TRACKS (0 gone), paused"
expect "now: 02 Two, paused of 0:40"
expect "pill over gamelist: Paused: 02 Two"
expect "playing on from $MU_ALBUM/02 Two.wav at 20000 ms"
mu_pill=$(grep -n "plugin music: pill over gamelist: Paused: 02 Two" "$log" | head -n 1 | cut -d: -f1)
mu_started=$(grep -n "plugin music: started the daemon" "$log" | head -n 1 | cut -d: -f1)
if [ -z "$mu_started" ] || [ -z "$mu_pill" ] || [ "$mu_pill" -gt "$mu_started" ]; then
	fail "the daemon started before X"
fi
grep -qF "Paused  0:20 / 0:40" "$MU_WORK/resume.texts" || fail "the player didn't show the place paused"
grep -qF "musicd: playing on from $MU_ALBUM/02 Two.wav at 20000 ms, 2 of $MU_TRACKS" "$MU_LOG" \
	|| fail "the daemon didn't play on from the place: $(cat "$MU_LOG")"
mu_status=$(mu_send status)
printf '%s\n' "$mu_status" | grep -q "state=playing.*track=$MU_ALBUM/02 Two.wav$" || fail "it isn't playing on: $mu_status"
mu_pos=$(printf '%s\n' "$mu_status" | tr '\t' '\n' | sed -n 's/^pos=//p')
if [ "${mu_pos:-0}" -lt 20000 ] || [ "$mu_pos" -ge 40000 ]; then
	fail "it didn't play on from the place: $mu_status"
fi

# the tagged track: its title and artist on the player and the pill, and
# its embedded cover
if [ -n "$MU_TAGGED" ]; then
	log="$WORK/music-tags.log"
	# B goes up from the album to the music folder, then closes the player
	PRESSER_KEYS="y down down a wait wait b b wait wait"
	PRESSER_LOG="$MU_WORK/tags.texts" SMOKE_PRELOAD="$PRESSER"
	export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	run_launcher "$log" "presser: done" || fail "the launcher exited early with the tagged track"
	unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	expect "playing $MU_ALBUM/10 Ten.mp3"
	expect "tags of $MU_ALBUM/10 Ten.mp3: title \"Tenth Song\", artist \"Test Artist\", art /tmp/mosaic-music-art/art-"
	grep "plugin music: tags of $MU_ALBUM/10 Ten.mp3: " "$log" | grep -qF "(embedded)" || fail "the tagged track's art wasn't its own"
	expect "showing the art /tmp/mosaic-music-art/art-"
	grep -qxF "Tenth Song" "$MU_WORK/tags.texts" || fail "the player didn't show the tagged title"
	grep -qxF "Test Artist" "$MU_WORK/tags.texts" || fail "the player didn't show the artist"
	# the pill's whole text, which a small screen cuts to fit, and what it drew
	expect "pill over gamelist: Playing: Tenth Song - Test Artist"
	grep -qF "Playing: Tenth Song - " "$MU_WORK/tags.texts" || fail "the pill didn't draw the title and artist"
fi

mu_stop
unset SDL_AUDIODRIVER
rm -rf "$MU_DIR" "$CARD/Music Test"
[ -e "$MU_WORK/Music.saved" ] && mv "$MU_WORK/Music.saved" "$MU_DIR"
if [ -f "$MU_WORK/recent.txt.saved" ]; then cp "$MU_WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
for f in next last.txt; do
	if [ -f "$MU_WORK/$f.saved" ]; then cp "$MU_WORK/$f.saved" "/tmp/$f"; else rm -f "/tmp/$f"; fi
done
rm -f /tmp/mosaic-launcher-launched "$MU_LOG"
if [ -f "$MU_WORK/settings.txt.saved" ]; then cp "$MU_WORK/settings.txt.saved" "$STATE/settings.txt"; else rm -f "$STATE/settings.txt"; fi
rm -rf /tmp/mosaic-music-art
rm -rf "$MU_WORK" "$PRESSER" "$WORK/plugins/music.plugin" "$WORK/plugins/screener.plugin" "$WORK/plugins/powerer.plugin" \
	"$STATE/music.cfg" "$STATE/powerer.cfg" "$STATE/plugins.txt" "$STATE/music"
case_log=""
