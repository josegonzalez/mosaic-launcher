# shellcheck shell=sh
# shard: 1

# the bundled romm against tests/plugins/romm-server, a stand-in RomM
# serving responses recorded from RomM 5.4.1: inert without a server; the
# RomM folder's platforms, then Game Boy's roms, fetched in the background
# (the last path opens the folder); a slow download cancelled with B on the
# download screen; a download that goes on to launch the game; the save
# uploaded when the launcher comes back; Flashback restoring the newest
# save on the server after its confirm, with the old one backed up; a
# two-disc PlayStation game served as a zip, unpacked into its own folder
# with an .m3u and launched at it, its save uploaded under the .m3u's name,
# and a state named as RetroArch names one restored as this card's State
# format names it, into the state folder of the core the system's pak
# runs; nothing fetched with Wi-Fi off; and HTTPS to a self-signed server
# whose certificate is pinned, refused with the wrong pin. The password is
# never logged. The card's recents, State format setting and the launches'
# /tmp files are put back.
RM_STATE="$STATE/romm"
RM_ROMS="$CARD/Roms/Game Boy (GB)"
RM_SAVES="$CARD/Saves/GB"
RM_ONE="$RM_ROMS/Mosaic Test One (World).gb"
RM_TWO="$RM_ROMS/Mosaic Test Two (World).gb"
RM_SAVE="$RM_SAVES/Mosaic Test One (World).gb.sav"
RM_PS="$CARD/Roms/Sony PlayStation (PS)"
RM_GAME="$RM_PS/Mosaic Two Disc (USA)"
RM_M3U="$RM_GAME/Mosaic Two Disc (USA).m3u"
RM_PS_SAVE="$CARD/Saves/PS/Mosaic Two Disc (USA).m3u.sav"
RM_SHARED="$CARD/.userdata/shared"
RM_PS_STATE="$RM_SHARED/PS-pcsx_rearmed/Mosaic Two Disc (USA).state1"
RM_PASSWORD=Pw-86-secret
RM_WORK="$WORK/romm"
rm -rf "$RM_STATE" "$RM_ONE" "$RM_TWO" "$RM_SAVE" "$RM_GAME" "$RM_PS_SAVE"
mkdir -p "$RM_WORK" "$RM_ROMS" "$RM_SAVES" "$RM_PS" "$CARD/Saves/PS" "$STATE"
[ -f "$RECENTS" ] && cp "$RECENTS" "$RM_WORK/recent.txt.saved"
[ -f "$RM_SHARED/minuisettings.txt" ] && cp "$RM_SHARED/minuisettings.txt" "$RM_WORK/minuisettings.txt.saved"
for f in next last.txt resume_slot.txt; do
	[ -f "/tmp/$f" ] && cp "/tmp/$f" "$RM_WORK/$f.saved"
done
# the state folders this case makes, so only those are removed after
rm_made=""
for f in PS-pcsx_rearmed PS-swanstation; do
	[ -d "$RM_SHARED/$f" ] || rm_made="$rm_made $f"
done
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
make -s -C "$ROOT/plugins" romm ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building romm"
printf 'romm=on\n' >"$STATE/plugins.txt"
plugin=romm

# rm_server [tls cert key]: starts the stand-in, setting RM_PORT and RM_PID
rm_server() {
	rm -f "$RM_WORK/port"
	python3 -I "$ROOT/tests/plugins/romm-server/server.py" --fixtures "$ROOT/tests/c/fixtures/romm" \
		--port-file "$RM_WORK/port" --log "$RM_WORK/requests.log" "$@" &
	RM_PID=$!
	i=0
	while [ ! -s "$RM_WORK/port" ] && [ "$i" -lt 50 ]; do
		sleep 0.1
		i=$((i + 1))
	done
	RM_PORT=$(cat "$RM_WORK/port" 2>/dev/null)
	[ -n "$RM_PORT" ] || fail "the stand-in RomM didn't start"
}

# rm_run <name> [line...]: runs the launcher with presser pressing $RM_KEYS,
# starting in the RomM folder's $RM_FOLDER (Game Boy unless set)
rm_run() {
	name=$1
	shift
	log="$WORK/romm-$name.log"
	# each run starts in that folder, as a launch saves its game as the last path
	printf '%s' "$CARD/RomM/${RM_FOLDER:-Game Boy}/x" >/tmp/last.txt
	PRESSER_KEYS="$RM_KEYS" PRESSER_LOG="$RM_WORK/$name.texts" SMOKE_PRELOAD="$PRESSER"
	export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	run_launcher "$log" "$@"
	rm_status=$?
	unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	return "$rm_status"
}

# inert without a server
rm -f "$STATE/romm.cfg"
RM_KEYS="wait"
rm_run inert "presser: done" || fail "the launcher exited early with romm and no server"
expect "no server set, so it does nothing"
grep -qF "$CARD/RomM" "$log" && fail "romm added its folder without a server"

rm_server
printf 'url=http://127.0.0.1:%s\nuser=admin\npassword=%s\n' "$RM_PORT" "$RM_PASSWORD" >"$STATE/romm.cfg"

# the platforms, then Game Boy's roms, each fetched once its folder opens
rm_run platforms "plugin romm: platforms: 4, 4 with a system" "plugin romm: root: 4 platforms" \
	|| fail "the launcher exited early fetching platforms"
expect "server http://127.0.0.1:$RM_PORT, user admin, secret set"
expect "http:// sends the login in clear text; use https:// on networks you don't trust"
[ -f "$RM_STATE/platforms.tsv" ] || fail "romm didn't keep the platforms"
rm_run roms "plugin romm: roms for gb: 2" "plugin romm: gb: 2 roms, 0 on the card, into Game Boy (GB)" \
	|| fail "the launcher exited early fetching roms"
grep -qF "plugin romm: 4 platforms kept from before" "$log" || fail "romm didn't read its kept platforms"
grep -q "GET /api/roms?platform_ids=1&limit=500&offset=0" "$RM_WORK/requests.log" || fail "romm didn't ask for Game Boy's roms"
grep -q "GET /api/roms?platform_ids=1&.*&with_files=true" "$RM_WORK/requests.log" || fail "romm didn't ask for the roms' files"

# a slow download, cancelled from the download screen
RM_KEYS="down a wait wait wait wait b wait"
rm_run cancel "presser: done" || fail "the launcher exited early cancelling a download"
expect "downloading Mosaic Test Two (World).gb (rom 1, 32768 bytes) to $RM_TWO"
expect "download screen: Mosaic Test Two (World).gb, 0%"
expect "download cancelled"
grep -qF "Downloading Mosaic Test Two" "$RM_WORK/cancel.texts" || fail "the download screen didn't show"
[ -e "$RM_TWO" ] && fail "the cancelled download was kept"
grep -qF "plugins: launch of $RM_TWO cancelled by a plugin" "$log" || fail "the stub's launch wasn't held back"

# a download that goes on to launch the game
RM_KEYS="a wait wait wait wait wait wait"
rm_run download && fail "the launcher didn't quit to launch the downloaded game"
expect "downloading Mosaic Test One (World).gb (rom 2, 32768 bytes) to $RM_ONE"
expect "downloaded $RM_ONE: 32768 bytes"
expect "launching $RM_ONE (rom 2)"
grep -qF "openRom($RM_ONE" "$log" || fail "the downloaded game didn't launch"
[ "$(head -c 6 "$RM_ONE")" = "rom 2 " ] || fail "the download isn't the server's rom"
[ "$(wc -c <"$RM_ONE" | tr -d ' ')" = 32768 ] || fail "the download is the wrong size"
for f in "$RM_ROMS"/.romm-*; do [ -e "$f" ] && fail "romm left a partial download: $f"; done
grep -q "^state=done" "$STATE/status.d/romm.status" || fail "the status pill wasn't left at done"
grep -qF "2	GB	-	$RM_ONE" "$RM_STATE/games.txt" || fail "romm didn't record the downloaded game"

# back from the game: the save is uploaded, once
printf 'MY SAVE\n' >"$RM_SAVE"
RM_KEYS="wait wait wait"
rm_run return "plugin romm: uploaded Mosaic Test One (World).gb.sav for rom 2" || fail "the launcher exited early uploading the save"
rm -f /tmp/mosaic-launcher-launched
grep -qF "UPLOAD Mosaic Test One (World).gb.sav MY SAVE" "$RM_WORK/requests.log" || fail "the stand-in didn't get the save"
grep -q "POST /api/saves?rom_id=2&slot=mosaic-launcher&content_hash=[0-9a-f]\{32\}&autocleanup=true&autocleanup_limit=10" \
	"$RM_WORK/requests.log" || fail "the upload's query isn't RomM's"
[ -f "$RM_STATE/pending.txt" ] && fail "the upload left its pending file"

# Flashback on the downloaded game: the newest save, restored after the confirm
RM_KEYS="y wait wait a wait a wait wait wait"
rm_run flashback "presser: done" || fail "the launcher exited early with Flashback"
expect "flashback for rom 2, Mosaic Test One (World).gb"
expect "flashback for rom 2: 3 saves, 1 states"
grep -qE "plugin romm: flashback 0 of 4: [0-9-]+ [0-9:]+  Save  20 B" "$log" || fail "Flashback didn't list the newest save first"
grep -qF "plugin romm: restored save 3 to $RM_SAVE, backup $RM_STATE/backups/GB/Mosaic Test One (World).gb.sav." "$log" \
	|| fail "Flashback didn't restore the save with a backup"
[ "$(head -c 8 "$RM_SAVE")" = "saves 3 " ] || fail "the restored save isn't the server's"
for backup in "$RM_STATE"/backups/GB/*; do
	[ "$(cat "$backup")" = "MY SAVE" ] || fail "the backup isn't the old save"
done
grep -qF "Restore this save?" "$RM_WORK/flashback.texts" || fail "Flashback didn't ask first"

# a game of two discs, served as a zip: PlayStation's roms, then the
# download, unpacked into the game's folder with its .m3u, which launches
RM_FOLDER=PlayStation
RM_KEYS="wait"
rm_run ps-roms "plugin romm: roms for psx: 2" "plugin romm: psx: 2 roms, 0 on the card, into Sony PlayStation (PS)" \
	|| fail "the launcher exited early fetching PlayStation's roms"
RM_KEYS="down a wait wait wait wait wait wait"
rm_run ps-download && fail "the launcher didn't quit to launch the unpacked game"
expect "downloading Mosaic Two Disc (USA) (rom 3, 600186 bytes, a zip) to $RM_M3U"
expect "downloaded and unpacked $RM_M3U: 600256 bytes"
expect "launching $RM_M3U (rom 3)"
grep -qF "openRom($RM_M3U" "$log" || fail "the unpacked game didn't launch at its .m3u"
grep -qF "Mosaic Two Disc (USA) (Disc 1).cue" /tmp/next || fail "the game didn't launch at its first disc: $(cat /tmp/next)"
[ "$(cat "$RM_M3U")" = "$(printf 'Mosaic Two Disc (USA) (Disc 1).cue\nMosaic Two Disc (USA) (Disc 2).cue')" ] \
	|| fail "the .m3u doesn't list the discs: $(cat "$RM_M3U")"
for d in 1 2; do
	for f in "Mosaic Two Disc (USA) (Disc $d).cue" "Mosaic Two Disc (USA) (Disc $d).bin"; do
		[ -f "$RM_GAME/$f" ] || fail "the zip's $f wasn't unpacked"
	done
done
disc="3 Mosaic Two Disc (USA) (Disc 2).bin"
[ "$(head -c ${#disc} "$RM_GAME/Mosaic Two Disc (USA) (Disc 2).bin")" = "$disc" ] || fail "the unpacked disc isn't the server's"
[ "$(wc -c <"$RM_GAME/Mosaic Two Disc (USA) (Disc 2).bin" | tr -d ' ')" = 300000 ] || fail "the unpacked disc is the wrong size"
for f in "$RM_PS"/.romm-*; do [ -e "$f" ] && fail "romm left a partial download or unpack: $f"; done
grep -qF "3	PS	-	$RM_M3U" "$RM_STATE/games.txt" || fail "romm didn't record the game at its .m3u"

# back from it: the save minarch names after the .m3u is uploaded
printf 'PS SAVE\n' >"$RM_PS_SAVE"
RM_KEYS="wait wait wait"
rm_run ps-return "plugin romm: uploaded Mosaic Two Disc (USA).m3u.sav for rom 3" \
	|| fail "the launcher exited early uploading the game's save"
rm -f /tmp/mosaic-launcher-launched
grep -qF "UPLOAD Mosaic Two Disc (USA).m3u.sav PS SAVE" "$RM_WORK/requests.log" || fail "the stand-in didn't get the game's save"

# Flashback on it with two PlayStation state folders and RetroArch's State
# format set: a state named as RetroArch names slot 1 goes in the folder of
# the core PS.pak runs, named as that format names it
mkdir -p "$RM_SHARED/PS-pcsx_rearmed" "$RM_SHARED/PS-swanstation"
{
	grep -v '^stateFormat=' "$RM_WORK/minuisettings.txt.saved" 2>/dev/null
	echo stateFormat=3
} >"$RM_SHARED/minuisettings.txt"
RM_KEYS="down y wait wait a wait a wait wait wait"
rm_run ps-flashback "presser: done" || fail "the launcher exited early with the game's Flashback"
expect "flashback for rom 3, Mosaic Two Disc (USA).m3u"
grep -qE "plugin romm: states for PS: [0-9]+ folders, core pcsx_rearmed, state format 3" "$log" \
	|| fail "Flashback didn't find PS.pak's core or the State format"
expect "flashback for rom 3: 0 saves, 1 states"
expect "restored state 31 to $RM_PS_STATE, backup none (nothing there)"
[ "$(head -c 10 "$RM_PS_STATE")" = "states 31 " ] || fail "the restored state isn't the server's"
[ -n "$(ls -A "$RM_SHARED/PS-swanstation")" ] && fail "a state went in the other core's folder"

RM_FOLDER=""

# Wi-Fi off: nothing is fetched
WIFI_FAKE="$RM_WORK/wifi.fake"
printf 'enabled=0\n' >"$WIFI_FAKE"
export MOSAIC_LAUNCHER_WIFI_FAKE="$WIFI_FAKE"
: >"$RM_WORK/requests.log"
RM_KEYS="down a wait"
rm_run wifi-off "presser: done" || fail "the launcher exited early with Wi-Fi off"
unset MOSAIC_LAUNCHER_WIFI_FAKE
expect "network: Wi-Fi is off"
grep -qF "downloading" "$log" && fail "romm downloaded with Wi-Fi off"
[ -s "$RM_WORK/requests.log" ] && fail "romm used the network with Wi-Fi off: $(cat "$RM_WORK/requests.log")"
kill "$RM_PID"
wait "$RM_PID" 2>/dev/null

# HTTPS to a self-signed server, with its certificate pinned
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 2 -subj /CN=localhost \
	-keyout "$RM_WORK/key.pem" -out "$RM_WORK/cert.pem" >/dev/null 2>/dev/null || fail "making a test certificate"
pin=$(openssl x509 -in "$RM_WORK/cert.pem" -noout -fingerprint -sha256 | sed 's/.*=//')
rm_server --tls "$RM_WORK/cert.pem" "$RM_WORK/key.pem"
rm -rf "$RM_STATE"
printf 'url=https://127.0.0.1:%s\nuser=admin\npassword=%s\npin=%s\n' "$RM_PORT" "$RM_PASSWORD" "$pin" >"$STATE/romm.cfg"
RM_KEYS="wait"
rm_run https "plugin romm: platforms: 4, 4 with a system" || fail "the launcher exited early over HTTPS"
expect "server https://127.0.0.1:$RM_PORT, user admin, secret set, certificate pinned"
printf 'url=https://127.0.0.1:%s\nuser=admin\npassword=%s\npin=%s\n' "$RM_PORT" "$RM_PASSWORD" \
	"0000000000000000000000000000000000000000000000000000000000000000" >"$STATE/romm.cfg"
rm -rf "$RM_STATE"
rm_run wrong-pin "plugin romm: listing platforms failed: the server's certificate doesn't match the pinned fingerprint" \
	|| fail "the launcher exited early with the wrong pin"
kill "$RM_PID"
wait "$RM_PID" 2>/dev/null

for f in "$WORK"/romm-*.log; do
	grep -qF "$RM_PASSWORD" "$f" && fail "romm logged the password in $(basename "$f")"
done

if [ -f "$RM_WORK/recent.txt.saved" ]; then cp "$RM_WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
if [ -f "$RM_WORK/minuisettings.txt.saved" ]; then
	cp "$RM_WORK/minuisettings.txt.saved" "$RM_SHARED/minuisettings.txt"
else
	rm -f "$RM_SHARED/minuisettings.txt"
fi
for f in next last.txt resume_slot.txt; do
	if [ -f "$RM_WORK/$f.saved" ]; then cp "$RM_WORK/$f.saved" "/tmp/$f"; else rm -f "/tmp/$f"; fi
done
rm -f "$RM_PS_STATE"
for f in $rm_made; do rm -rf "${RM_SHARED:?}/$f"; done
rm -f /tmp/mosaic-launcher-launched "$RM_ONE" "$RM_TWO" "$RM_SAVE" "$RM_PS_SAVE" "$STATE/status.d/romm.status"
rm -rf "$RM_STATE" "$RM_WORK" "$RM_GAME" "$PRESSER" "$WORK/plugins/romm.plugin" "$STATE/romm.cfg" "$STATE/plugins.txt"
case_log=""
