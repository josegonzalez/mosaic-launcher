# shellcheck shell=sh
# shard: 2

# the ambient plugins together, with music's daemon and SDL's dummy audio
# driver. Music adds what plays to the launcher's lock screen and X plays
# or pauses it there, and its Y locks the launcher. On the screensaver,
# what music plays shows and X plays or pauses it without closing, while
# with music off X closes it as any button does. The screensaver's videos
# source has video-previews play a game's clip (its test frames, so it
# runs without ffmpeg), pausing music, and music plays on once the next
# image has no clip; with video-previews off it shows the games' art. The
# user's order of ambient screens decides between charging-screen and the
# screensaver. The card's Music folder, recents and the daemon are put back
AM_WORK="$WORK/ambient-interop"
AM_MUSIC="$CARD/Music"
AM_ROMS="$CARD/Roms/Ambient Interop (AMI)"
AM_SAVER="$CARD/Ambient Interop Saver"
AM_SOCK=/tmp/mosaic-music.sock
AM_PIDFILE=/tmp/mosaic-music.pid

# am_stop: stops a music daemon left running, by the pid it wrote
am_stop() {
	if [ -f "$AM_PIDFILE" ]; then
		am_pid=$(cat "$AM_PIDFILE")
		kill "$am_pid" 2>/dev/null
		i=0
		while kill -0 "$am_pid" 2>/dev/null && [ "$i" -lt 30 ]; do
			sleep 0.1
			i=$((i + 1))
		done
	fi
	rm -f "$AM_SOCK" "$AM_PIDFILE"
}

# am_status: the daemon's status line
am_status() {
	python3 -I -c '
import socket, sys
s = socket.socket(socket.AF_UNIX)
s.settimeout(3)
s.connect(sys.argv[1])
s.sendall(b"status\n")
print(s.makefile().readline().rstrip())' "$AM_SOCK" 2>/dev/null
}

# am_run <name> <keys>: the launcher with presser pressing keys, its log
# in $log
am_run() {
	log="$AM_WORK/$1.log"
	PRESSER_KEYS="$2" PRESSER_LOG="$AM_WORK/$1.texts" SMOKE_PRELOAD="$PRESSER"
	export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	run_launcher "$log" "presser: done" || fail "the launcher exited early in $1"
	unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
}

# am_before <first> <second>: $log has first before second
am_before() {
	first=$(grep -nF "$1" "$log" | head -n 1 | cut -d: -f1)
	second=$(grep -nF "$2" "$log" | head -n 1 | cut -d: -f1)
	if [ -z "$first" ] || [ -z "$second" ] || [ "$first" -gt "$second" ]; then
		fail "\"$1\" didn't come before \"$2\""
	fi
}

am_stop
rm -rf "$AM_WORK" "$STATE/music" /tmp/mosaic-music-art
mkdir -p "$AM_WORK" "$STATE" "$AM_SAVER" "$AM_ROMS/.media" "$AM_ROMS/.videos" "$(dirname "$RECENTS")"
[ -e "$AM_MUSIC" ] && mv "$AM_MUSIC" "$AM_WORK/Music.saved"
[ -f "$RECENTS" ] && cp "$RECENTS" "$AM_WORK/recent.txt.saved"
[ -f "$STATE/settings.txt" ] && cp "$STATE/settings.txt" "$AM_WORK/settings.txt.saved"
mkdir -p "$AM_MUSIC"
python3 -I -c '
import math, struct, sys, wave
w = wave.open(sys.argv[1], "wb")
w.setnchannels(1)
w.setsampwidth(2)
w.setframerate(8000)
w.writeframes(b"".join(struct.pack("<h", int(6000 * math.sin(i * 0.1))) for i in range(8000 * 60)))
w.close()
' "$AM_MUSIC/One.wav" || fail "making the test track"
cp "$ROOT/tests/plugins/peeker/art.png" "$AM_SAVER/a.png"
# Alpha has a clip (any file: video-previews' test frames don't read it),
# Bravo none
for game in Alpha Bravo; do
	: >"$AM_ROMS/$game.gb"
	cp "$ROOT/tests/plugins/peeker/art.png" "$AM_ROMS/.media/$game.png"
done
: >"$AM_ROMS/.videos/Alpha.mp4"
printf '/Roms/Ambient Interop (AMI)/Alpha.gb\n/Roms/Ambient Interop (AMI)/Bravo.gb\n' >"$RECENTS"

PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
for id in music screensaver video-previews charging-screen; do
	make -s -C "$ROOT/plugins" "$id" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building $id"
done
SDL_AUDIODRIVER=dummy
export SDL_AUDIODRIVER
printf 'button=Y\noverlay=off\n' >"$STATE/music.cfg"
printf 'lock_chord=select+start\n' >"$STATE/settings.txt"

# music on the lock screen: Y opens the player, A plays One, Y there locks
# and closes the player, the lock screen shows what plays, X pauses it
# there, and the chord unlocks
printf 'screensaver=off\nvideo-previews=off\ncharging-screen=off\n' >"$STATE/plugins.txt"
am_run lock "y a wait wait y wait wait x wait wait select+start/1500 wait"
plugin=music
expect "playing $AM_MUSIC/One.wav"
expect "locked"
grep -qF "lock: locked by music" "$log" || fail "music's Y didn't lock the launcher"
grep -qF "plugins: music put music.toggle on the lock screen's button" "$log" || fail "music didn't put X on the lock screen"
grep -F "lock: lock screen " "$log" | grep -qF "| One | Playing " \
	|| fail "the lock screen didn't show what music plays: $(grep "lock: lock screen" "$log" | head -n 1)"
grep -qF "lock: running music.toggle on the lock screen" "$log" || fail "X on the lock screen didn't run music.toggle"
expect "now: One, paused of 1:00"
grep -F "lock: lock screen " "$log" | grep -qF "| One | Paused " || fail "the lock screen didn't show the pause"
grep -qF "lock: unlocked by the chord" "$log" || fail "the chord didn't unlock"
grep -qiF "Play or pause music" "$AM_WORK/lock.texts" || fail "the lock screen didn't show X's hint"
am_status | grep -q "state=paused" || fail "X on the lock screen didn't pause the music: $(am_status)"

# the screensaver: what music plays, X plays it on and stays, A closes it
printf 'idle=1\nsource=folder\nfolder=Ambient Interop Saver\n' >"$STATE/screensaver.cfg"
printf 'screensaver=on\nvideo-previews=off\ncharging-screen=off\n' >"$STATE/plugins.txt"
am_run saver "wait wait wait wait wait wait x wait wait wait a wait"
plugin=screensaver
expect "opened after 1s idle: 1 image from folder"
expect "music: Paused: One"
expect "X: music playing"
expect "music: Playing: One"
expect "closed by a button"
am_before "plugin screensaver: X: music playing" "plugin screensaver: closed by a button"
[ "$(grep -c "plugin screensaver: opened after" "$log")" = 1 ] || fail "X closed the screensaver"
am_status | grep -q "state=playing" || fail "X on the screensaver didn't play the music on: $(am_status)"

# the videos source: video-previews plays Alpha's clip, pausing music,
# and music plays on with Bravo's art, which has none
printf 'idle=1\nsource=videos\ninterval=1\n' >"$STATE/screensaver.cfg"
printf 'screensaver=on\nvideo-previews=on\ncharging-screen=off\n' >"$STATE/plugins.txt"
MOSAIC_VIDEO_PREVIEWS_FAKE=1
export MOSAIC_VIDEO_PREVIEWS_FAKE
am_run videos "wait wait wait wait wait wait wait wait wait wait wait wait a wait"
unset MOSAIC_VIDEO_PREVIEWS_FAKE
plugin=screensaver
expect "opened after 1s idle: 2 images from videos"
expect "playing the clip of Alpha.gb"
expect "no clip for this image"
plugin=video-previews
expect "playing Alpha.mp4 for the screensaver as test frames"
expect "paused music for the clip"
expect "music plays on: playing"
am_before "plugin video-previews: paused music for the clip" "plugin screensaver: no clip for this image"
am_before "plugin screensaver: no clip for this image" "plugin video-previews: music plays on: playing"
grep -qF "plugin music: pausing for another plugin" "$log" || fail "music didn't pause for the clip"
grep -qF "plugin music: playing on for another plugin" "$log" || fail "music didn't play on after the clip"
am_status | grep -q "state=playing" || fail "the music isn't playing after the clip: $(am_status)"

# music off: no line, and X closes the screensaver; video-previews off:
# the videos source shows the games' art
am_stop
printf 'screensaver=on\nvideo-previews=off\ncharging-screen=off\nmusic=off\n' >"$STATE/plugins.txt"
am_run alone "wait wait wait wait wait wait x wait"
plugin=screensaver
expect "opened after 1s idle: 2 images from videos"
expect "closed by a button"
grep -qF "plugin screensaver: X: music" "$log" && fail "X didn't close the screensaver with music off"
grep -qF "plugin screensaver: music: Playing" "$log" && fail "the screensaver showed music with music off"
grep -qF "plugin screensaver: playing the clip" "$log" && fail "the screensaver played a clip with video-previews off"

# the user's order: by default charging-screen comes first, so with the
# test battery charging it takes over from the screensaver once its own
# idle time has passed; with the screensaver first, it never opens
CS_FAKE="$AM_WORK/charging.fake"
printf '73 1\n' >"$CS_FAKE"
printf 'idle=2\n' >"$STATE/charging-screen.cfg"
printf 'idle=1\nsource=folder\nfolder=Ambient Interop Saver\n' >"$STATE/screensaver.cfg"
printf 'screensaver=on\nvideo-previews=off\ncharging-screen=on\nmusic=off\n' >"$STATE/plugins.txt"
MOSAIC_CHARGING_SCREEN_FAKE="$CS_FAKE"
export MOSAIC_CHARGING_SCREEN_FAKE
log="$AM_WORK/order-default.log"
run_launcher "$log" "plugin charging-screen: opened after" || fail "the launcher exited early with the default order"
grep -qF "plugins: ambient screens in order: lock, charging-screen.main, screensaver.main" "$log" \
	|| fail "the ambient screens aren't in the default order: $(grep "ambient screens in order" "$log")"
am_before "plugins: opened ambient screen screensaver.main" "plugins: opened ambient screen charging-screen.main"
printf 'lock_chord=select+start\nambient_order=lock,screensaver.main,charging-screen.main\n' >"$STATE/settings.txt"
log="$AM_WORK/order-saver.log"
WAIT_SAVED=$WAIT
WAIT=4
run_launcher "$log"
WAIT=$WAIT_SAVED
unset MOSAIC_CHARGING_SCREEN_FAKE
grep -qF "plugins: ambient screens in order: lock, screensaver.main, charging-screen.main" "$log" \
	|| fail "the ambient screens aren't in the user's order: $(grep "ambient screens in order" "$log")"
grep -qF "plugins: opened ambient screen screensaver.main" "$log" || fail "the screensaver didn't open first in the user's order"
grep -qF "plugins: opened ambient screen charging-screen.main" "$log" \
	&& fail "charging-screen took over from the screensaver the user put first"

am_stop
unset SDL_AUDIODRIVER
rm -rf "$AM_MUSIC" "$AM_ROMS" "$AM_SAVER" /tmp/mosaic-music-art
[ -e "$AM_WORK/Music.saved" ] && mv "$AM_WORK/Music.saved" "$AM_MUSIC"
if [ -f "$AM_WORK/recent.txt.saved" ]; then cp "$AM_WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
if [ -f "$AM_WORK/settings.txt.saved" ]; then cp "$AM_WORK/settings.txt.saved" "$STATE/settings.txt"; else rm -f "$STATE/settings.txt"; fi
rm -rf "$AM_WORK" "$PRESSER" "$STATE/music" "$WORK/plugins/music.plugin" "$WORK/plugins/screensaver.plugin" \
	"$WORK/plugins/video-previews.plugin" "$WORK/plugins/charging-screen.plugin" "$STATE/music.cfg" \
	"$STATE/screensaver.cfg" "$STATE/charging-screen.cfg" "$STATE/plugins.txt" "$CARD/.userdata/desktop/logs/music.txt"
case_log=""
