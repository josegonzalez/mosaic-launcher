# shellcheck shell=sh

# power-plus' battery from its voltage: on desktop, with no battery, it
# shows nothing, even with the readout on, until MOSAIC_POWER_PLUS_FAKE_VOLTS
# names a test voltage. Then its header widget shows it in volts, or as a
# percentage along the curve, and with guard_volts on the low battery
# guard goes by that percentage, not stock's
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" power-plus >/dev/null || fail "building power-plus"
mkdir -p "$STATE"
printf 'power-plus=on\n' >"$STATE/plugins.txt"
plugin=power-plus
BV_FAKE="$WORK/battery-volts.fake"

printf 'volts=volts\n' >"$STATE/power-plus.cfg"
log="$WORK/battery-volts-none.log"
run_launcher "$log" "plugin power-plus: can't read the battery's voltage, so the voltage readout is off" \
	|| fail "the launcher exited early with power-plus' readout on and no test voltage"
grep -qF "plugin power-plus: widget at" "$log" && fail "power-plus drew a widget on desktop without a test voltage"

printf '3850000\n' >"$BV_FAKE"
export MOSAIC_POWER_PLUS_FAKE_VOLTS="$BV_FAKE"
log="$WORK/battery-volts.log"
run_launcher "$log" "plugin power-plus: widget at " || fail "the launcher exited early with power-plus showing volts"
expect "voltage from $BV_FAKE, curve axp-default"
expect "voltage 3.85 V, 57%"
grep -qE "plugin power-plus: widget at [0-9]+,[0-9]+ [0-9]+x[0-9]+: 3\.85 V$" "$log" \
	|| fail "power-plus' widget didn't show the volts: $(grep "plugin power-plus: widget" "$log")"
grep -qF "plugins: power-plus'" "$log" && fail "power-plus' widget was turned off"

printf 'volts=percent\n' >"$STATE/power-plus.cfg"
log="$WORK/battery-volts-percent.log"
run_launcher "$log" "plugin power-plus: widget at " || fail "the launcher exited early with power-plus showing a percentage"
grep -qE "plugin power-plus: widget at [0-9]+,[0-9]+ [0-9]+x[0-9]+: V 57%$" "$log" \
	|| fail "power-plus' widget didn't show the percentage: $(grep "plugin power-plus: widget" "$log")"

# 3.64 V is 10% along the curve: the guard warns after 3 readings 5 seconds apart
printf '3640\n' >"$BV_FAKE"
printf 'guard_volts=on\nwarn_at=10\n' >"$STATE/power-plus.cfg"
log="$WORK/battery-volts-guard.log"
# 10 seconds of readings, so this start gets longer
bv_wait=$WAIT
WAIT=$((WAIT + 10))
run_launcher "$log" "plugin power-plus: Battery at 10%, charge soon" \
	|| fail "the launcher exited early with power-plus' guard going by the voltage"
WAIT=$bv_wait
expect "Battery at 10%, charge soon"
grep -qF "plugin power-plus: widget at" "$log" && fail "power-plus drew a widget with the readout off"
unset MOSAIC_POWER_PLUS_FAKE_VOLTS

rm -rf "$WORK/plugins/power-plus.plugin" "$STATE/power-plus.cfg" "$STATE/plugins.txt" "$BV_FAKE"
case_log=""
