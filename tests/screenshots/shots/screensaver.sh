# shellcheck shell=sh

# stops a music daemon an earlier shot left playing, by the pid it wrote
nomusic() {
	[ -f /tmp/mosaic-music.pid ] && kill "$(cat /tmp/mosaic-music.pid)" 2>/dev/null
	sleep 1
	rm -f /tmp/mosaic-music.pid /tmp/mosaic-music.sock
}

# 15 seconds idle, then a few more, after keys
idle() { press "$* $(printf 'wait %.0s' $(seq 60))"; }

# a made-up tone in Music, as the sample card has no music
tone() {
	mkdir -p "$CARD/Music"
	python3 -I -c '
import math, struct, sys, wave
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(22050)
    w.writeframes(b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * 440 * i / 22050))) for i in range(22050 * 60)))
' "$CARD/Music/Tone 440 Hz.wav"
}

begin
on screensaver
cfg screensaver idle=15
idle
shot default

begin
on screensaver
cfg screensaver idle=15 clock=24h
idle
shot clock

# with music playing, from the player on its game list button, what it
# plays in the bottom left corner
nomusic
begin
on screensaver music
cfg screensaver idle=15
tone
cfg music button=Y
idle "y a b"
shot with-music

# the videos source, with video-previews playing µCity's clip, made from
# Tobu Tobu Girl Deluxe's screenshot
begin
on screensaver video-previews
cfg screensaver idle=15 source=videos clock=24h
mkdir -p "$CARD/Roms/Game Boy Color (GBC)/.videos"
ffmpeg -loglevel error -loop 1 -i "$CARD/Screenshots/Tobu Tobu Girl Deluxe.2026-10-04-18-22-05.png" -t 30 \
	-vf scale=320:288:flags=neighbor -c:v libx264 -pix_fmt yuv420p \
	"$CARD/Roms/Game Boy Color (GBC)/.videos/µCity.mp4" || fail "screensaver: making its clip needs ffmpeg"
idle
shot with-video-previews
nomusic
