# shellcheck shell=sh

# a test voltage of 3.85 V, as desktop has no battery
printf '3850\n' >"$WORK/power-plus.fake"
export MOSAIC_POWER_PLUS_FAKE_VOLTS="$WORK/power-plus.fake"

begin
on power-plus
cfg power-plus volts=volts
shot volts

begin
on power-plus
cfg power-plus volts=percent
shot percent

unset MOSAIC_POWER_PLUS_FAKE_VOLTS
