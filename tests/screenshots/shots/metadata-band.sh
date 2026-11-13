# shellcheck shell=sh

# Thwaite, in Nintendo Entertainment System
begin
on metadata-band
press "down down down down a down"
shot default

begin
on metadata-band
cfg metadata-band "fields=genre,year"
press "down down down down a down"
shot fields

begin
on metadata-band layout-fancy
slot layout layout-fancy
press "down down down down a down"
shot with-layout-fancy

begin
on metadata-band layout-carousel
slot layout layout-carousel
press "right right right right a right"
shot with-layout-carousel
