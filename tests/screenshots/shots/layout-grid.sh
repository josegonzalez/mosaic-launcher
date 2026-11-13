# shellcheck shell=sh

# the Homebrew collection
begin
on layout-grid
slot layout layout-grid
press "right a right a"
shot default

begin
on layout-grid
slot layout layout-grid
cfg layout-grid columns=4
press "right a right a"
shot columns-4
