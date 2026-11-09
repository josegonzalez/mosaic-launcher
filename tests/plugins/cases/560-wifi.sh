# shellcheck shell=sh
# shard: 1

# wifier finds no Wi-Fi on desktop, and nothing looks for it
printf 'wifier=on\n' >"$STATE/plugins.txt"
build_plugin wifier 'id=wifier\nname=Wifier\napi=1\n'
log="$WORK/wifier-desktop.log"
plugin=wifier
run_launcher "$log" "plugin wifier: tick:" || fail "the launcher exited early with wifier on desktop"
expect "init: supported 0, enabled -1, connection -1 \"\" \"\" rssi 0 bars 0 freq 0 link 0"
expect "init ask under 50 ms 1, null -1"
expect "tick: supported 0, enabled -1, connection -1 \"\" \"\" rssi 0 bars 0 freq 0 link 0"
grep -q "wifi: worker" "$log" && fail "the Wi-Fi worker started on desktop"
grep -q "wifi: worker" "$WORK/hello.log" && fail "the Wi-Fi worker started without a plugin asking"

# with a file standing in for a device: unknown until the launcher's first
# look, which takes 300 ms on its thread without holding up the asks; then
# the connection, the fields a smaller MosaicWifi has room for, off at once,
# and on again with no network
WIFI_FAKE="$WORK/wifi.fake"
printf 'enabled=1\nconnected=1\nssid=Home Net\nip=192.168.1.50\nrssi=-65\nfreq=5180\nlink=433\ndelay_ms=300\n' >"$WIFI_FAKE"
export MOSAIC_LAUNCHER_WIFI_FAKE="$WIFI_FAKE"
log="$WORK/wifier.log"
run_launcher "$log" "plugin wifier: done" || fail "the launcher exited early with wifier and a fake Wi-Fi"
unset MOSAIC_LAUNCHER_WIFI_FAKE
expect "init: supported 1, enabled 1, connection -1 \"\" \"\" rssi 0 bars 0 freq 0 link 0"
expect "init ask under 50 ms 1, null -1"
expect "tick: supported 1, enabled 1, connection 1 \"Home Net\" \"192.168.1.50\" rssi -65 bars 2 freq 5180 link 433"
expect "slowest ask under 50 ms 1"
expect "small 1 \"Home Net\" ip untouched 1"
expect "off: supported 1, enabled 0, connection 0 \"\" \"\" rssi 0 bars 0 freq 0 link 0"
expect "joined nothing: supported 1, enabled 1, connection 0 \"\" \"\" rssi 0 bars 0 freq 0 link 0"
[ "$(grep -c "plugins: wifi: worker started" "$log")" -eq 1 ] || fail "the Wi-Fi worker didn't start just once"
grep -q "plugins: wifi: worker stopped" "$log" || fail "the Wi-Fi worker wasn't stopped at quit"
rm -rf "$WORK/plugins/wifier.plugin" "$STATE/plugins.txt" "$WIFI_FAKE"
