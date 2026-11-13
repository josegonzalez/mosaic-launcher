# shellcheck shell=sh

# 15 seconds idle, then a few more
idle() { press "$(printf 'wait %.0s' $(seq 60))"; }

begin
on screensaver
cfg screensaver idle=15
idle
shot default

begin
on screensaver
cfg screensaver idle=15 clock=24h
idle
shot clock
