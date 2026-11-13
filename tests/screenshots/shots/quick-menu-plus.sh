# shellcheck shell=sh

begin
on quick-menu-plus
press "menu wait left"
shot default

begin
on quick-menu-plus
cfg quick-menu-plus "labels=Recents=Played, Games=Library"
press "menu"
shot labels

begin
on quick-menu-plus
cfg quick-menu-plus "hide=Collections"
press "menu"
shot hide
