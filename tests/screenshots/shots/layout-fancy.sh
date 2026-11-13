# shellcheck shell=sh

# the Homebrew collection
begin
on layout-fancy
slot layout layout-fancy
press "down a down a"
shot default

# Tobu Tobu Girl, which has a save state
begin
on layout-fancy
slot layout layout-fancy
press "down a down a down"
shot state-preview

begin
on layout-fancy
slot layout layout-fancy
cfg layout-fancy prefer_preview=off
press "down a down a down"
shot prefer-preview-off
