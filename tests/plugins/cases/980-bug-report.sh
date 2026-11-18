# shellcheck shell=sh
# shard: 3

# the bundled bug-report, seeker running its action: the zip in Reports
# has the README, info, the game list summary, the settings and the logs,
# with the card's games counted and no file names, seeker's secret
# setting left out and its value, the token, the URL's password, the IP
# and MAC addresses and the Wi-Fi name taken out of the logs. With
# keep_network on, the IP and MAC addresses and the Wi-Fi name stay, and
# the secrets still go. Ten older reports are there to begin with, so
# each report removes the oldest. A start after a crash pauses plugins
# and makes one itself, saying it came from the crash guard. Reports,
# the logs and settings it made are removed afterwards.
BR_REPORTS="$CARD/Reports"
BR_LOGS="$CARD/.userdata/desktop/logs"
BR_ROMS="$CARD/Roms/Bug Report (BRP)"
BR_SAVED="$WORK/bug-report-saved"
rm -rf "$BR_REPORTS" "$BR_ROMS" "$BR_SAVED"
mkdir -p "$BR_REPORTS" "$BR_ROMS" "$BR_LOGS/seeker" "$BR_SAVED" "$STATE"
for game in "Alpha Quest" "Bravo Racer"; do : >"$BR_ROMS/$game.gb"; done
[ -f "$BR_LOGS/nextui.txt" ] && mv "$BR_LOGS/nextui.txt" "$BR_SAVED/nextui.txt"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" bug-report >/dev/null || fail "building bug-report"
build_plugin seeker 'id=seeker\nname=Seeker\napi=1\nsetting.password=text|Password|||secret\nsetting.mode=choice|Mode|a|a,b\n'

# bug-report and seeker on, and every other plugin built so far off
: >"$STATE/plugins.txt"
for dir in "$WORK"/plugins/*.plugin; do
	id=$(basename "$dir" .plugin)
	case "$id" in
	bug-report | seeker) printf '%s=on\n' "$id" >>"$STATE/plugins.txt" ;;
	*) printf '%s=off\n' "$id" >>"$STATE/plugins.txt" ;;
	esac
done

# private details where a report finds them
cat >"$BR_LOGS/nextui.txt" <<'EOF'
[INFO] wifi: connected to 192.168.1.207 on wlan0, gateway fe80::1c2b:3cff:fe4d:5e6f%wlan0
[INFO] plugin system-info: values: Battery: 80%; Wi-Fi: Home Net 5G, 192.168.1.207, -48 dBm
[INFO] plugin romm: GET http://admin:s3cr3tpw@romm.example.com/api/roms?token=abc123token&page=2
[INFO] plugins: bt connected AA:BB:CC:DD:EE:FF
[INFO] seeker typed hunter2hunter2
EOF
printf 'INFO: request X-API-Key: AbCdEf123GhIj from 10.0.0.5\n' >"$BR_LOGS/seeker.txt"
printf 'ran at 172.16.4.20\n' >"$BR_LOGS/seeker/run.txt"
for i in 0 1 2 3 4 5 6 7 8 9; do : >"$BR_REPORTS/mosaic-2000-01-01-00000$i.zip"; done
: >"$BR_REPORTS/notes.txt"

# br_unzip <zip> <out>: its file list in <out>.list and every file's text in <out>
br_unzip() {
	python3 -I - "$1" "$2" <<'EOF'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
bad = z.testzip()
if bad: sys.exit("bad crc: " + bad)
with open(sys.argv[2] + ".list", "w") as names, open(sys.argv[2], "wb") as text:
    for name in z.namelist():
        names.write(name.split("/", 1)[1] + "\n")
        text.write(z.read(name))
EOF
}

# br_has <file> <name>: the zip's file list has it
br_has() {
	grep -qxF "$2" "$1.list" || fail "the report has no $2"
}

# br_newest: the newest report, by name
br_newest() {
	find "$BR_REPORTS" -name 'mosaic-2*.zip' ! -name 'mosaic-2000-*' | sort | tail -n 1
}

# from the action, with network details taken out
printf 'password=hunter2hunter2\nmode=b\nsteps=%s\n' "wait,wait,action:bug-report.make,wait,wait" >"$STATE/seeker.cfg"
log="$WORK/bug-report.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early making a bug report"
plugin=bug-report
expect "ready"
expect "wait screen drawn"
expect "saved Reports/mosaic-"
grep -qF "plugins: bug-report asked for a bug report" "$log" || fail "the launcher didn't log the ask"
grep -qF "bug report: removed the oldest report, Reports/mosaic-2000-01-01-000000.zip, keeping the newest 10" "$log" \
	|| fail "the launcher didn't warn about removing the oldest report"
[ -e "$BR_REPORTS/mosaic-2000-01-01-000000.zip" ] && fail "the oldest report wasn't removed"
[ -e "$BR_REPORTS/notes.txt" ] || fail "a file that isn't a report was removed"
[ "$(find "$BR_REPORTS" -name 'mosaic-*.zip' | wc -l | tr -d ' ')" = 10 ] || fail "Reports doesn't hold 10 reports"
zip=$(br_newest)
[ -n "$zip" ] || fail "no report in $BR_REPORTS"
grep -qF "plugin bug-report: saved Reports/$(basename "$zip")" "$log" || fail "bug-report didn't name the file it saved"
out="$WORK/bug-report.txt"
br_unzip "$zip" "$out" || fail "the report isn't a zip python can read"
for name in README.txt info.txt games.txt settings/plugins.txt settings/seeker.cfg logs/nextui.txt logs/seeker.txt \
	logs/seeker/run.txt; do
	br_has "$out" "$name"
done
grep -qF "Bug Report (BRP): 2 games" "$out" || fail "games.txt doesn't count the card's games"
grep -qF "Alpha Quest" "$out" && fail "the report has a game's file name"
grep -qF "mode=b" "$out" || fail "seeker's setting isn't in the report"
grep -qF "left out, protected or secret in plugin.txt: password" "$out" || fail "the report doesn't say what it left out"
grep -qF "bug-report, own pak, version not given, api 1, Bug report: on, loaded" "$out" || fail "info.txt doesn't list bug-report"
grep -qF "network details: taken out" "$out" || fail "info.txt doesn't say network details were taken out"
for secret in hunter2hunter2 abc123token s3cr3tpw AbCdEf123GhIj 192.168.1.207 10.0.0.5 172.16.4.20 fe80::1c2b \
	AA:BB:CC:DD:EE:FF "Home Net 5G"; do
	grep -qF "$secret" "$out" && fail "the report has $secret"
done
grep -qF "connected to <ip> on wlan0, gateway <ip>" "$out" || fail "the addresses weren't marked as taken out"
grep -qF "Wi-Fi: <wifi>, <ip>" "$out" || fail "the Wi-Fi name wasn't marked as taken out"
grep -qF "http://<credentials>@romm.example.com/api/roms?token=<redacted>&page=2" "$out" || fail "the URL's secrets weren't taken out"
grep -qF "bt connected <mac>" "$out" || fail "the MAC address wasn't taken out"

# keep_network keeps the addresses and the Wi-Fi name, not the secrets
printf 'keep_network=on\n' >"$STATE/bug-report.cfg"
log="$WORK/bug-report-network.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early making a bug report with network details"
plugin=bug-report
expect "ready, keeping network details"
expect "saved Reports/mosaic-"
zip=$(br_newest)
out="$WORK/bug-report-network.txt"
br_unzip "$zip" "$out" || fail "the report with network details isn't a zip python can read"
for kept in "connected to 192.168.1.207 on wlan0" "10.0.0.5" "AA:BB:CC:DD:EE:FF" "Wi-Fi: Home Net 5G" "network details: kept"; do
	grep -qF "$kept" "$out" || fail "the report with network details doesn't have $kept"
done
for secret in hunter2hunter2 abc123token s3cr3tpw AbCdEf123GhIj; do
	grep -qF "$secret" "$out" && fail "the report with network details has $secret"
done

# a crash while starting pauses plugins, and the launcher makes one itself
rm -f "$STATE/bug-report.cfg"
printf 'steps=wait\n' >"$STATE/seeker.cfg"
before=$(br_newest)
clear_guard
: >"$STATE/loading"
log="$WORK/bug-report-crash.log"
start_launcher "$log" "pausing them" "bug report: saved Reports/mosaic-" || fail "the launcher exited making a crash report"
grep -qF "plugins: bug-report provides report.crash, so the launcher makes a bug report" "$log" \
	|| fail "the launcher didn't say why it made a crash report"
[ -f "$STATE/plugins_paused" ] || fail "plugins weren't paused"
zip=$(br_newest)
if [ -z "$zip" ] || [ "$zip" = "$before" ]; then
	fail "no report after the crash"
fi
out="$WORK/bug-report-crash.txt"
br_unzip "$zip" "$out" || fail "the crash report isn't a zip python can read"
grep -qF "by the crash guard, which paused plugins after a crash" "$out" || fail "the crash report doesn't say where it came from"
grep -qF "bug-report, own pak, version not given, api 1, Bug report: on, paused after a crash" "$out" \
	|| fail "the crash report doesn't say plugins are paused"
grep -qF "crash guard: plugins_paused" "$out" || fail "the crash report doesn't show the crash guard"
grep -qF "192.168.1.207" "$out" && fail "the crash report has an IP address"
# paused again: no second report
count=$(find "$BR_REPORTS" -name 'mosaic-2*.zip' | wc -l | tr -d ' ')
start_launcher "$WORK/bug-report-paused.log" "paused after a crash" || fail "the launcher exited with plugins paused"
grep -qF "bug report:" "$WORK/bug-report-paused.log" && fail "a start with plugins already paused made another report"
[ "$(find "$BR_REPORTS" -name 'mosaic-2*.zip' | wc -l | tr -d ' ')" = "$count" ] || fail "the reports changed with plugins paused"
clear_guard

rm -rf "$BR_REPORTS" "$BR_ROMS" "$BR_LOGS/seeker" "$WORK/plugins/bug-report.plugin" "$WORK/plugins/seeker.plugin"
rm -f "$BR_LOGS/nextui.txt" "$BR_LOGS/seeker.txt" "$STATE/seeker.cfg" "$STATE/bug-report.cfg" "$STATE/plugins.txt"
[ -f "$BR_SAVED/nextui.txt" ] && mv "$BR_SAVED/nextui.txt" "$BR_LOGS/nextui.txt"
rm -rf "$BR_SAVED"
case_log=""
