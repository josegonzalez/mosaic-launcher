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

begin
on layout-fancy
slot layout layout-fancy
cfg layout-fancy position=left
press "down a down a"
shot position-left

begin
on layout-fancy
slot layout layout-fancy
cfg layout-fancy position=top
press "down a down a"
shot position-top

# Tobu Tobu Girl, with a description in its .info file
begin
on layout-fancy
slot layout layout-fancy
cfg layout-fancy description=on
echo "description=An arcade platformer for the Game Boy by Tangram Games, from 2017: jump and dash as high into the sky as you can." \
	>>"$CARD/Roms/Game Boy (GB)/.media/Tobu Tobu Girl.info"
press "down a down a down"
shot description
