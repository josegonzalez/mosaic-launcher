# shellcheck shell=sh

begin
on home-dashboard
shot default

begin
on favorites home-dashboard
press "r1"
shot pinned-with-favorites

begin
on home-dashboard
press "r1"
shot stats

begin
on home-dashboard
cfg home-dashboard mode=minui
shot mode-minui

begin
on playtime-captions home-dashboard
shot with-playtime-captions

begin
on playtime-limit home-dashboard
cfg playtime-limit limit=60
press "r1"
shot with-playtime-limit

begin
on playtime-limit home-dashboard
cfg playtime-limit limit=60
press "r1 down down"
shot with-playtime-limit-scrolled

# with romm set up and two games it downloaded: Recently downloaded on the
# Stats tab, scrolled down to it
begin
on romm home-dashboard
cfg romm url=http://127.0.0.1:9
mkdir -p "$STATE/romm"
now=$(date +%s)
printf '%s\t%s\tTobu Tobu Girl\n%s\t%s\tLibbet and the Magic Floor\n' "$now" "$CARD/Roms/Game Boy (GB)/Tobu Tobu Girl.gb" \
	"$((now - 3 * 86400))" "$CARD/Roms/Game Boy (GB)/Libbet and the Magic Floor.gb" >"$STATE/romm/downloads.txt"
press "r1 down down down down down down down down"
shot with-romm
