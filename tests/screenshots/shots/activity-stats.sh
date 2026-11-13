# shellcheck shell=sh

# the 7 days tab, opened from its quick menu tile
begin
on activity-stats
press "menu wait left a"
shot default

begin
on activity-stats
press "menu wait left a r1 r1"
shot games

begin
on activity-stats
press "menu wait left"
shot tile
