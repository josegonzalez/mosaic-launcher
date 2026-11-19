# shellcheck shell=sh

begin
on playtime-limit
cfg playtime-limit limit=60
shot limit

begin
on playtime-limit
cfg playtime-limit limit=30
shot warn

# Thwaite, launched past the limit
begin
on playtime-limit
cfg playtime-limit limit=15
press "down down down down a down a"
shot refused

# the budget bar at the top of home-dashboard's home screen
begin
on home-dashboard playtime-limit
cfg playtime-limit limit=60
shot with-home-dashboard

# bedtime from 21:00 to 07:00, with the local clock moved to 22:00
TZ="TST$(( ($(date -u +%H | sed 's/^0//') - 22 + 36) % 24 - 12 ))"
export TZ
begin
on playtime-limit
cfg playtime-limit bedtime=21:00 wake=07:00
shot bedtime

# Thwaite, launched at bedtime, with a code set
begin
on playtime-limit
cfg playtime-limit bedtime=21:00 wake=07:00 code=1234
press "down down down down a down a"
shot bedtime-refused
unset TZ
