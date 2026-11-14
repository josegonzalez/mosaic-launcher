# shellcheck shell=sh

# stops a music daemon an earlier shot left playing, by the pid it wrote
nomusic() {
	[ -f /tmp/mosaic-music.pid ] && kill "$(cat /tmp/mosaic-music.pid)" 2>/dev/null
	sleep 1
	rm -f /tmp/mosaic-music.pid /tmp/mosaic-music.sock
}

# µCity tagged, and music's resume file paused on a made-up tone, as the
# sample card has no music
together() {
	printf '%s\t%s\n' "City builder" "/Roms/Game Boy Color (GBC)/µCity.gbc" >"$STATE/tags.txt"
	mkdir -p "$CARD/Music" "$STATE/music"
	python3 -I -c '
import math, struct, sys, wave
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(22050)
    w.writeframes(b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * 440 * i / 22050))) for i in range(22050 * 30)))
' "$CARD/Music/Tone 440 Hz.wav"
	printf 'music-resume 1\npos=12000\nlen=30000\nindex=0\nshuffle=0\nrepeat=off\ncount=1\norder=0\ntrack=%s\npath=%s\nend\n' \
		"$CARD/Music/Tone 440 Hz.wav" "$CARD/Music/Tone 440 Hz.wav" >"$STATE/music/resume.txt"
}

# µCity, a favorite, in the Homebrew collection, in second-screen's
# window, with the plugins its panel shows
nomusic
begin
on second-screen metadata-band playtime-captions favorites tags music
together
window
press "down a down a down down down"
after 3000
shot default

nomusic
begin
on second-screen metadata-band playtime-captions favorites tags music
together
cfg second-screen details=off music=off
window
press "down a down a down down down"
after 3000
shot toggles

nomusic
begin
on second-screen metadata-band playtime-captions favorites tags music
together
cfg second-screen size=480x640
window
press "down a down a down down down"
after 3000
shot tall
nomusic
