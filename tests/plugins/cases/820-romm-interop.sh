# shellcheck shell=sh
# shard: 2

# romm with save-state-manager, gamification and home-dashboard, against
# tests/plugins/romm-server, the stand-in RomM. A download from the RomM
# folder publishes romm.download_done, which completes gamification's
# RomM quest, offered only while romm has a server; home-dashboard's Stats
# tab lists it under Recently downloaded. With Upload states after
# playing on, the game's changed state goes up when it returns, and
# save-state-manager marks it, as romm.synced says, until it changes; its
# Restore from server cell runs romm.flashback on the game. Without romm,
# there's no mark, no cell, no quest and no section. The card's recents
# and the launches' /tmp files are put back afterwards.
RI_STATE="$STATE/romm"
RI_ROMS="$CARD/Roms/Game Boy (GB)"
RI_ONE="$RI_ROMS/Mosaic Test One (World).gb"
RI_CORE="$CARD/.userdata/shared/GB-gambatte"
RI_STATE_FILE="$RI_CORE/Mosaic Test One (World).gb.st0"
RI_GM_STATE="$STATE/gamification"
RI_WORK="$WORK/romm-interop"
PRESSER="$WORK/presser.so"
rm -rf "$RI_STATE" "$RI_ONE" "$RI_GM_STATE" "$RI_WORK" /tmp/mosaic-gamification.launch
mkdir -p "$RI_WORK" "$RI_ROMS" "$STATE"
[ -d "$RI_CORE" ] || ri_made_core=1
mkdir -p "$RI_CORE"
[ -f "$RECENTS" ] && cp "$RECENTS" "$RI_WORK/recent.txt.saved"
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$RI_WORK/play.sqlite.saved"
mkdir -p "$(dirname "$PLAY_DB")"
# an empty play history, so home-dashboard's Stats tab has its rows
make_play_db <<SQL || fail "making the romm interop play history"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
SQL
for f in next last.txt resume_slot.txt; do
	[ -f "/tmp/$f" ] && cp "/tmp/$f" "$RI_WORK/$f.saved"
done
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" romm save-state-manager gamification home-dashboard \
	>/dev/null || fail "building the romm interop plugins"
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
all_off='romm=off\nsave-state-manager=off\ngamification=off\nhome-dashboard=off\nscreener=off\n'

# ri_plugins <id=on...>: plugins.txt with only those on, as a plugin it
# doesn't name loads
ri_plugins() {
	printf '%b' "$all_off" >"$STATE/plugins.txt"
	for on in "$@"; do printf '%s\n' "$on" >>"$STATE/plugins.txt"; done
}

# ri_run <name> <keys> <last path> [line...]: the launcher with presser
# pressing keys, starting at the last path
ri_run() {
	name=$1
	keys=$2
	printf '%s' "$3" >/tmp/last.txt
	shift 3
	log="$WORK/romm-interop-$name.log"
	texts="$RI_WORK/$name.texts"
	PRESSER_KEYS="$keys" PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER"
	export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	run_launcher "$log" "$@"
	ri_status=$?
	unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	return "$ri_status"
}

python3 -I "$ROOT/tests/plugins/romm-server/server.py" --fixtures "$ROOT/tests/c/fixtures/romm" \
	--port-file "$RI_WORK/port" --log "$RI_WORK/requests.log" &
RI_PID=$!
i=0
while [ ! -s "$RI_WORK/port" ] && [ "$i" -lt 50 ]; do
	sleep 0.1
	i=$((i + 1))
done
RI_PORT=$(cat "$RI_WORK/port" 2>/dev/null)
[ -n "$RI_PORT" ] || fail "the stand-in RomM didn't start"
# the Flashback button off, so Y is save-state-manager's alone
printf 'url=http://127.0.0.1:%s\nuser=admin\npassword=Pw-820\nbutton=none\n' "$RI_PORT" >"$STATE/romm.cfg"

# the RomM folder's platforms and Game Boy's roms, then a download that
# completes gamification's RomM quest as it goes on to launch the game
ri_plugins romm=on gamification=on
ri_run platforms "wait" "$CARD/RomM/Game Boy/x" "plugin romm: platforms: 4, 4 with a system" \
	|| fail "the launcher exited early fetching platforms"
ri_run roms "wait" "$CARD/RomM/Game Boy/x" "plugin romm: roms for gb: 2" || fail "the launcher exited early fetching roms"
ri_run download "a wait wait wait wait wait wait" "$CARD/RomM/Game Boy/x" \
	&& fail "the launcher didn't quit to launch the downloaded game"
rm -f /tmp/mosaic-launcher-launched
plugin=romm
expect "downloaded $RI_ONE: 32768 bytes"
grep -qF "	$RI_ONE	Mosaic Test One" "$RI_STATE/downloads.txt" 2>/dev/null || fail "romm didn't log the download"
plugin=gamification
expect "download $RI_ONE"
expect "quest done: download (Download a game from RomM)"
grep -q "^done=download " "$RI_GM_STATE/state.txt" 2>/dev/null || fail "gamification didn't keep the RomM quest done"

# back from the game with a state: not uploaded with Upload states after
# playing off (the default), and uploaded once it's on
printf state >"$RI_STATE_FILE"
touch -t 203001010900 "$RI_STATE_FILE"
ri_plugins romm=on
ri_run return "wait wait wait wait" "$RI_ONE" "presser: done" || fail "the launcher exited early back from the game"
grep -qF "UPLOAD-STATE" "$RI_WORK/requests.log" && fail "romm uploaded a state with Upload states after playing off"
printf '%s' "$RI_ONE" >"$RI_STATE/pending.txt"
printf 'sync_states=on\n' >>"$STATE/romm.cfg"
ri_run return-states "wait wait wait wait" "$RI_ONE" "plugin romm: uploaded state Mosaic Test One (World).gb.st0 for rom 2" \
	|| fail "the launcher exited early uploading the state"
grep -qF "POST /api/states?rom_id=2&emulator=gambatte" "$RI_WORK/requests.log" || fail "the state upload's query isn't RomM's"
grep -qF "UPLOAD-STATE gambatte Mosaic Test One (World).gb.st0 state" "$RI_WORK/requests.log" \
	|| fail "the stand-in didn't get the state"
grep -qF "up	" "$RI_STATE/synced.txt" 2>/dev/null || fail "romm didn't note the uploaded state"
[ -f "$RI_STATE/pending.txt" ] && fail "the state upload left the pending file"

# the quests: offered with romm, and not without it
printf 'steps=action:gamification.open,b\n' >"$STATE/screener.cfg"
ri_plugins romm=on gamification=on screener=on
ri_run quests "wait" "$RI_ONE" "plugin screener: done" || fail "the launcher exited early with the quests"
grep -qF ", revisit 0/1, download 1/1 done, week_days " "$log" || fail "gamification didn't show the RomM quest done"
ri_plugins gamification=on screener=on
ri_run quests-off "wait" "$RI_ONE" "plugin screener: done" || fail "the launcher exited early with the quests and no romm"
grep -qF ", revisit 0/1, week_days " "$log" || fail "gamification offered the RomM quest without romm"
rm -f "$STATE/screener.cfg"

# home-dashboard's Stats tab: the download under Recently downloaded
ri_plugins romm=on home-dashboard=on
ri_run home "r1" "$RI_ONE" "presser: done" || fail "the launcher exited early with home-dashboard"
plugin=home-dashboard
expect "history on, downloads 1"
grep -qxF "RECENTLY DOWNLOADED" "$texts" || fail "home-dashboard didn't draw the Recently downloaded heading"
grep -qxF "Mosaic Test One" "$texts" || fail "home-dashboard didn't list the download"
ri_plugins home-dashboard=on
ri_run home-off "r1" "$RI_ONE" "presser: done" || fail "the launcher exited early with home-dashboard and no romm"
expect "history on, downloads 0"
grep -qxF "RECENTLY DOWNLOADED" "$texts" && fail "home-dashboard drew Recently downloaded without romm"

# save-state-manager: the state romm uploaded, marked, then Restore from
# server opens romm's Flashback for the game
printf 'steps=action:save-state-manager.game,right,a\n' >"$STATE/screener.cfg"
ri_plugins romm=on save-state-manager=on screener=on
ri_run states "wait wait wait wait wait wait wait wait" "$RI_ONE" "plugin screener: done" "plugin romm: flashback for rom 2: 3 saves, 1 states" \
	|| fail "the launcher exited early with save-state-manager and romm"
plugin=save-state-manager
expect "Mosaic Test One: 1 save states, restore from server"
grep -qE "plugin save-state-manager: slots 0 of 2: Mosaic Test One, Slot 1, .*, synced$" "$log" \
	|| fail "save-state-manager didn't mark the synced state"
expect "slots 1 of 2: Mosaic Test One, restore from server"
plugin=romm
expect "flashback for rom 2, Mosaic Test One (World).gb"
# changed since: no mark
touch -t 203001011000 "$RI_STATE_FILE"
printf 'steps=action:save-state-manager.game\n' >"$STATE/screener.cfg"
ri_run states-changed "wait" "$RI_ONE" "plugin screener: done" || fail "the launcher exited early with a changed state"
grep -qE "plugin save-state-manager: slots 0 of 2: Mosaic Test One, Slot 1, .*columns$" "$log" \
	|| fail "save-state-manager marked a state changed since romm synced it"
# without romm: no mark and no cell
ri_plugins save-state-manager=on screener=on
ri_run states-off "wait" "$RI_ONE" "plugin screener: done" || fail "the launcher exited early with save-state-manager alone"
plugin=save-state-manager
expect "Mosaic Test One: 1 save states"
grep -qF "restore from server" "$log" && fail "save-state-manager offered Restore from server without romm"
grep -qE "plugin save-state-manager: slots 0 of 1: Mosaic Test One, Slot 1, .*columns$" "$log" \
	|| fail "save-state-manager marked a state without romm"

kill "$RI_PID"
wait "$RI_PID" 2>/dev/null
for f in "$WORK"/romm-interop-*.log; do
	grep -qF "Pw-820" "$f" && fail "romm logged the password in $(basename "$f")"
done
if [ -f "$RI_WORK/recent.txt.saved" ]; then cp "$RI_WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
rm -f "$PLAY_DB"
[ -f "$RI_WORK/play.sqlite.saved" ] && mv "$RI_WORK/play.sqlite.saved" "$PLAY_DB"
for f in next last.txt resume_slot.txt; do
	if [ -f "$RI_WORK/$f.saved" ]; then cp "$RI_WORK/$f.saved" "/tmp/$f"; else rm -f "/tmp/$f"; fi
done
rm -f "$RI_STATE_FILE" "$RI_ONE" /tmp/mosaic-launcher-launched /tmp/mosaic-gamification.launch "$STATE/status.d/romm.status"
[ -n "${ri_made_core:-}" ] && rmdir "$RI_CORE" 2>/dev/null
rm -rf "$RI_STATE" "$RI_GM_STATE" "$RI_WORK" "$PRESSER" "$WORK/plugins/romm.plugin" "$WORK/plugins/save-state-manager.plugin" \
	"$WORK/plugins/gamification.plugin" "$WORK/plugins/home-dashboard.plugin" "$WORK/plugins/screener.plugin" \
	"$STATE/romm.cfg" "$STATE/screener.cfg" "$STATE/plugins.txt"
case_log=""
