# shellcheck shell=sh

# µCity, in the Homebrew collection
begin
on layout-hero-rail
slot layout layout-hero-rail
press "right a right a right right right"
shot default

begin
on layout-hero-rail
slot layout layout-hero-rail
cfg layout-hero-rail cards=large
press "right a right a right right right"
shot cards-large

begin
on layout-hero-rail
slot layout layout-hero-rail
cfg layout-hero-rail tint=0
press "right a right a right right right"
shot tint-0
