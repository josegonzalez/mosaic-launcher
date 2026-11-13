# shellcheck shell=sh

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

begin
on music
tones
press "menu wait left a"
shot default

begin
on music
tones
press "menu wait left a a"
shot playing
