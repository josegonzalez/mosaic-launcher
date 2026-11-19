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

# the Storage page, with the sample games at sizes like real ones' (sparse
# files, as their roms are empty) and some saves, states and a BIOS
begin
on system-info
si_size() { mkdir -p "$(dirname "$1")" && dd if=/dev/zero of="$1" bs=1 count=0 seek="$2" 2>/dev/null; }
si_size "$CARD/Roms/Game Boy (GB)/Libbet and the Magic Floor.gb" 262144
si_size "$CARD/Roms/Game Boy (GB)/Tobu Tobu Girl.gb" 524288
si_size "$CARD/Roms/Game Boy Color (GBC)/Tobu Tobu Girl Deluxe.gbc" 1048576
si_size "$CARD/Roms/Game Boy Color (GBC)/µCity.gbc" 1048576
si_size "$CARD/Roms/Nintendo Entertainment System (FC)/Concentration Room.nes" 40976
si_size "$CARD/Roms/Nintendo Entertainment System (FC)/Thwaite.nes" 40976
si_size "$CARD/Saves/GB/Tobu Tobu Girl.sav" 8192
si_size "$CARD/Saves/GBC/µCity.sav" 32768
si_size "$CARD/.userdata/shared/GBC-gambatte/µCity.st0" 98304
si_size "$CARD/.userdata/shared/GB-gambatte/Tobu Tobu Girl.st0" 98304
si_size "$CARD/Bios/GBA/gba_bios.bin" 16384
press "menu wait left a y"
after 1500
shot storage

# the input tester, with A held and a stand-in left stick pushed up and to
# the right, after RIGHT, B and Y
begin
on system-info
MOSAIC_LAUNCHER_STICK_FAKE="21000,-14000"
export MOSAIC_LAUNCHER_STICK_FAKE
press "menu wait left a x right b y a/3000"
after 900
shot input
unset MOSAIC_LAUNCHER_STICK_FAKE
