# shellcheck shell=sh

# a clip made from Tobu Tobu Girl Deluxe's other screen, playing in place of
# its title screen thumbnail, in Game Boy Color
begin
on video-previews
mkdir -p "$CARD/Roms/Game Boy Color (GBC)/.videos"
ffmpeg -loglevel error -loop 1 -i "$CARD/Screenshots/Tobu Tobu Girl Deluxe.2026-10-04-18-22-05.png" -t 4 \
	-vf scale=320:288:flags=neighbor -c:v libx264 -pix_fmt yuv420p \
	"$CARD/Roms/Game Boy Color (GBC)/.videos/Tobu Tobu Girl Deluxe.mp4" || fail "video-previews: making its clip needs ffmpeg"
press "down down down a $(printf 'wait %.0s' $(seq 12))"
shot default
