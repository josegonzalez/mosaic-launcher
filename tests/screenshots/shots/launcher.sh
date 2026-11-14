# shellcheck shell=sh

# the lock screen, locked with SELECT and START held for a second and a half
begin
printf 'lock_chord=select+start\n' >"$STATE/settings.txt"
press "select+start/1500 wait"
shot lock

# the Launcher page, on Ambient screens, with the lock's chord set
begin
printf 'lock_chord=select+start\n' >"$STATE/settings.txt"
args --settings
press "down down down down"
shot settings
