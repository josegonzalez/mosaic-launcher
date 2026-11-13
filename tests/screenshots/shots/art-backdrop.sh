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
