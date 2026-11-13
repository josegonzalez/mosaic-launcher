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
