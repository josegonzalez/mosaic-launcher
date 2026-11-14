# shellcheck shell=sh

begin
on gamification
press "menu wait left a"
shot default

begin
on gamification
press "menu wait left"
shot tile

begin
on playtime-limit gamification
cfg playtime-limit limit=45
cfg gamification daily_minutes=60
press "menu wait left a"
shot with-playtime-limit

# with romm set up: the RomM quest, done today, scrolled down to it
begin
on romm gamification
cfg romm url=http://127.0.0.1:9
mkdir -p "$STATE/gamification"
printf 'day=%s 0 0 0 0 0 1\n' "$(python3 -I -c 'import time; t = time.localtime(); print((int(time.mktime((t.tm_year, t.tm_mon, t.tm_mday, 12, 0, 0, 0, 0, -1))) + t.tm_gmtoff) // 86400)')" \
	>"$STATE/gamification/state.txt"
press "menu wait left a down down down"
shot with-romm
