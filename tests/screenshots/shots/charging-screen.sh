# shellcheck shell=sh

# stops a music daemon an earlier shot left playing, by the pid it wrote
nomusic() {
	[ -f /tmp/mosaic-music.pid ] && kill "$(cat /tmp/mosaic-music.pid)" 2>/dev/null
	sleep 1
	rm -f /tmp/mosaic-music.pid /tmp/mosaic-music.sock
}

# the test battery at 73% and charging, as desktop has none, and 15
# seconds idle, then a few more
charging() {
	printf '73 1\n' >"$WORK/charging-screen.fake"
	export MOSAIC_CHARGING_SCREEN_FAKE="$WORK/charging-screen.fake"
}
idle() { press "$* $(printf 'wait %.0s' $(seq 60))"; }

begin
on charging-screen
cfg charging-screen idle=15
charging
idle
shot default
unset MOSAIC_CHARGING_SCREEN_FAKE

# with music playing, from the player on its game list button, what it
# plays at the bottom
nomusic
begin
on charging-screen music
cfg charging-screen idle=15
charging
mkdir -p "$CARD/Music"
python3 -I -c '
import math, struct, sys, wave
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(22050)
    w.writeframes(b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * 440 * i / 22050))) for i in range(22050 * 60)))
' "$CARD/Music/Tone 440 Hz.wav"
cfg music button=Y
idle "y a b"
shot with-music
unset MOSAIC_CHARGING_SCREEN_FAKE
nomusic
