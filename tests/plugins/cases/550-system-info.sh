# shellcheck shell=sh
# shard: 3

# the bundled system-info: screener runs its action, presses A (the
# graph's own page where it doesn't fit under the rows) and backs out. Desktop
# has no battery, so that row is unavailable, as is Wi-Fi; the version
# comes from the card's version.txt and the graph from a battery-logger
# CSV, then a second run without the CSV says there's no history, and a
# third with a fake Wi-Fi shows the network
SI_VERSION="$CARD/.system/version.txt"
SI_CSV="$STATE/battery-logger.csv"
[ -e "$SI_VERSION" ] && mv "$SI_VERSION" "$WORK/version.saved"
[ -e "$SI_CSV" ] && mv "$SI_CSV" "$WORK/battery-logger.saved"
mkdir -p "$CARD/.system" "$STATE"
printf 'NextUI-20261013-0\n39a06de\n' >"$SI_VERSION"
printf '%s\n' 'date,game,path,seconds,start_percent,end_percent,drain_percent,percent_per_hour,charged' \
	'2026-11-01 10:00:00,Tetris,Roms/GB/Tetris.gb,1800,100,85,15,30.0,no' \
	'2026-11-02 10:00:00,"Mario, Dr.",Roms/NES/Dr. Mario.nes,1800,80,60,20,40.0,no' >"$SI_CSV"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" system-info >/dev/null || fail "building system-info"
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
printf 'steps=action:system-info.open,a,b,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/system-info.log" "plugin screener: done" || fail "the launcher exited early with system-info"
log="$WORK/system-info.log"
plugin=system-info
expect "opened, 2 games of battery history"
expect "values: Battery: unavailable; Wi-Fi: unavailable; IP address: "
grep -qF "; Platform: desktop; Version: NextUI-20261013-0 (39a06de)" "$log" || fail "system-info didn't show the platform and version"
grep -qE "plugin system-info: values: .*; Storage: [0-9.]+ [KMGTP]B free of [0-9.]+ [KMGTP]B;" "$log" \
	|| fail "system-info didn't show the card's storage"
grep -qE "plugin system-info: graph of 2 games over [0-9]+ columns, from (99|100)% to (60|61)%" "$log" \
	|| fail "system-info didn't draw the battery graph"
expect "closed"
[ "$(seen)" = "gamelist system-info.main gamelist " ] || fail "system-info didn't open and close: $(seen)"
grep -qF "plugins: system-info's screen system-info.main's" "$log" && fail "system-info's screen was turned off"

rm -f "$SI_CSV"
run_launcher "$WORK/system-info-empty.log" "plugin screener: done" || fail "the launcher exited early with system-info"
log="$WORK/system-info-empty.log"
expect "opened, 0 games of battery history"
expect "no battery history"

# with a file standing in for a device's Wi-Fi: checking until the
# launcher's first look, then the network, while the screen stays open
SI_WIFI="$WORK/system-info-wifi.fake"
printf 'enabled=1\nconnected=1\nssid=Home Net\nip=192.168.1.50\nrssi=-65\nfreq=5180\nlink=433\ndelay_ms=100\n' >"$SI_WIFI"
printf 'steps=action:system-info.open,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,b\n' >"$STATE/screener.cfg"
export MOSAIC_LAUNCHER_WIFI_FAKE="$SI_WIFI"
run_launcher "$WORK/system-info-wifi.log" "plugin screener: done" || fail "the launcher exited early with system-info and a fake Wi-Fi"
unset MOSAIC_LAUNCHER_WIFI_FAKE
log="$WORK/system-info-wifi.log"
expect "values: Battery: unavailable; Wi-Fi: checking; "
expect "values: Battery: unavailable; Wi-Fi: Home Net, 192.168.1.50, -65 dBm; "
rm -f "$SI_WIFI"

rm -f "$SI_VERSION"
[ -e "$WORK/version.saved" ] && mv "$WORK/version.saved" "$SI_VERSION"
[ -e "$WORK/battery-logger.saved" ] && mv "$WORK/battery-logger.saved" "$SI_CSV"
rm -rf "$WORK/plugins/system-info.plugin" "$WORK/plugins/screener.plugin" "$STATE/screener.cfg"
case_log=""
