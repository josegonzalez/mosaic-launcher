# shellcheck shell=sh

# a device's sample values in place of this computer's: Wi-Fi from the
# launcher's desktop stand-in, the address, storage, memory and uptime from
# tests/screenshots/device.c, and version.txt as stock's build of the pinned
# NextUI writes it
begin
on system-info
printf 'enabled=1\nconnected=1\nssid=Home Net\nip=192.168.1.50\nrssi=-65\nfreq=5180\nlink=433\n' >"$WORK/tmp/wifi.fake"
release=$(cd "$ROOT/vendor/NextUI" && TZ=GMT git log -1 --format=%cd --date=format-local:%Y%m%d)
hash=$(cd "$ROOT/vendor/NextUI" && git rev-parse --short HEAD)
printf 'NextUI-%s-0\n%s\n' "$release" "$hash" >"$CARD/.system/version.txt"
MOSAIC_LAUNCHER_WIFI_FAKE="$WORK/tmp/wifi.fake" DEVICE_IP="wlan0 192.168.1.50" \
	DEVICE_STORAGE="13207024435 63887638528" DEVICE_UPTIME="9240.00 0.00" \
	DEVICE_MEMINFO="$(printf 'MemTotal:        1015808 kB\nMemFree:          412160 kB\nMemAvailable:     634880 kB\n')"
export MOSAIC_LAUNCHER_WIFI_FAKE DEVICE_IP DEVICE_STORAGE DEVICE_UPTIME DEVICE_MEMINFO
press "menu wait left a"
shot default
unset MOSAIC_LAUNCHER_WIFI_FAKE DEVICE_IP DEVICE_STORAGE DEVICE_UPTIME DEVICE_MEMINFO

begin
on system-info
press "menu wait left"
shot tile
