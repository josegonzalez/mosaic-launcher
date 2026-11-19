# shellcheck shell=sh

begin
on screenshot-viewer
press "menu wait left a"
shot default

begin
on screenshot-viewer
press "menu wait left a a"
shot full

begin
on screenshot-viewer
press "menu wait left"
shot tile

# Y on Tobu Tobu Girl Deluxe in Game Boy Color, then right: its
# screenshot, the second of its media after its .media thumbnail
begin
on screenshot-viewer
press "down down down a y right"
shot media
