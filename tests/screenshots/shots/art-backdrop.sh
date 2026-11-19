# shellcheck shell=sh

# µCity, in Game Boy Color
begin
on art-backdrop
press "down down down a down"
shot default

begin
on art-backdrop
cfg art-backdrop style=full
press "down down down a down"
shot style-full

begin
on art-backdrop
cfg art-backdrop height=half
press "down down down a down"
shot height-half

begin
on art-backdrop
cfg art-backdrop dim=0
press "down down down a down"
shot dim-0

begin
on art-backdrop
cfg art-backdrop fill=full
press "down down down a down"
shot fill-full

begin
on layout-fancy art-backdrop
cfg art-backdrop fill=full
cfg layout-fancy position=left
press "down down down a down"
shot with-layout-fancy
