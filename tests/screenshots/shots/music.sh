# shellcheck shell=sh

# stops a music daemon an earlier shot left playing, by the pid it wrote
nomusic() {
	[ -f /tmp/mosaic-music.pid ] && kill "$(cat /tmp/mosaic-music.pid)" 2>/dev/null
	sleep 1
	rm -f /tmp/mosaic-music.pid /tmp/mosaic-music.sock
}

# two made-up tones in Music, as the sample card has no music
tones() {
	mkdir -p "$CARD/Music"
	for hz in 440 880; do
		python3 -I -c '
import math, struct, sys, wave
hz = int(sys.argv[2])
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(22050)
    w.writeframes(b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * hz * i / 22050))) for i in range(22050 * 30)))
' "$CARD/Music/Tone $hz Hz.wav" "$hz"
	done
}

nomusic
begin
on music
tones
press "menu wait left a"
shot default

nomusic
begin
on music
tones
press "menu wait left a a"
shot playing

# the player from its game list button, and Y there locks the launcher:
# the lock screen shows what plays, with X to play or pause
nomusic
begin
on music
tones
cfg music button=Y
printf 'lock_chord=select+start\n' >"$STATE/settings.txt"
press "y a wait y wait wait"
shot lock
nomusic
