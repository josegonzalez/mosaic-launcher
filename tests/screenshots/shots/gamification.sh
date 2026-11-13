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
