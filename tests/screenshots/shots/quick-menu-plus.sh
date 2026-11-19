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

begin
on quick-menu-plus
cfg quick-menu-plus top_row=off
press "menu"
shot top-row

begin
on quick-menu-plus
cfg quick-menu-plus bottom_row=off
press "menu"
shot bottom-row

begin
on quick-menu-plus
cfg quick-menu-plus tile_labels=off
press "menu"
shot tile-labels

begin
on quick-menu-plus
cfg quick-menu-plus toggles= awake_tile=off stock_tile=off restart_tile=on reset_tile=on
press "menu wait left left"
shot restart

begin
on quick-menu-plus
cfg quick-menu-plus toggles= awake_tile=off stock_tile=off restart_tile=on reset_tile=on
press "menu wait left a"
shot reset
