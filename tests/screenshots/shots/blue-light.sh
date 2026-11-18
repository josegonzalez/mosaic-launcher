# shellcheck shell=sh

# the Warm tint tile on the quick menu, off
begin
on blue-light
press "menu wait left"
shot default

# on, with the default amber at 50%: the game list
begin
on blue-light
cfg blue-light on=on
press "wait"
shot on

# and the quick menu, with the tile on
begin
on blue-light
cfg blue-light on=on
press "menu wait left"
shot quick

# orange at 50%
begin
on blue-light
cfg blue-light on=on colour=orange
press "wait"
shot orange

# red at 30%
begin
on blue-light
cfg blue-light on=on colour=red strength=30
press "wait"
shot red

# on Y, which the game list's hint shows
begin
on blue-light
cfg blue-light button=Y
press "wait"
shot button
