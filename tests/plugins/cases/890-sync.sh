# shellcheck shell=sh
# shard: 2

# sync with local stand-ins for the remote, each part run only where this
# computer has the program: rclone through an alias remote to a folder,
# rsync to a folder on this computer, and Syncthing on 127.0.0.1 with a
# made-up other device. As the launcher starts it syncs everything both
# ways; back from a game it syncs that game's files only; a file changed
# on both sides keeps the newer and the other as .conflict. With Wi-Fi off
# it says so and syncs everything at the next start. With romm set up and
# uploading states, the state romm last uploaded is left to romm. The
# card's files and /tmp's are put back afterwards.
SY_STATE="$STATE/sync"
SY_WORK="$WORK/sync"
SY_ROMS="$CARD/Roms/Game Boy (GB)"
SY_GAME="$SY_ROMS/Sync Test (World).gb"
SY_SAVES="$CARD/Saves/GB"
SY_SAVE="$SY_SAVES/Sync Test (World).sav"
SY_OTHER="$SY_SAVES/Sync Other (World).sav"
SY_CORE="$CARD/.userdata/shared/GB-gambatte"
SY_STATE_FILE="$SY_CORE/Sync Test (World).gb.st0"
SY_SHARED="$CARD/.userdata/shared"
SY_LOGS="$CARD/.userdata/desktop/logs"
rm -rf "$SY_STATE" "$SY_WORK" "$STATE/romm" "$SY_LOGS/sync.txt" "$SY_LOGS/syncthing.txt"
mkdir -p "$SY_WORK/shared.saved" "$SY_ROMS" "$STATE"
# the card's own saves and cores' folders out of the way, so a sync of
# everything sees only the case's files
[ -d "$CARD/Saves" ] && mv "$CARD/Saves" "$SY_WORK/Saves.saved"
for d in "$SY_SHARED"/*-*; do
	[ -d "$d" ] && mv "$d" "$SY_WORK/shared.saved/"
done
mkdir -p "$SY_SAVES" "$SY_CORE"
: >"$SY_GAME"
[ -f /tmp/mosaic-launcher-launched ] && cp /tmp/mosaic-launcher-launched "$SY_WORK/launched.saved"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" sync romm >/dev/null || fail "building sync and romm"
SY_LIB="$WORK/plugins/sync.plugin/lib/$ARCH"

# sy_run <name> [line...]: the launcher with only sync on, or the plugins
# in $sy_on
sy_run() {
	name=$1
	shift
	printf 'sync=on\nromm=off\n%b' "${sy_on:-}" >"$STATE/plugins.txt"
	log="$WORK/sync-$name.log"
	run_launcher "$log" "$@"
}

# sy_back: the next start is back from the test game
sy_back() {
	printf 'path=%s\ntype=rom' "$SY_GAME" >/tmp/mosaic-launcher-launched
}

# sy_files <time>: the card's test save and state, made at time
sy_files() {
	printf 'save one' >"$SY_SAVE"
	printf 'state one' >"$SY_STATE_FILE"
	touch -t "$1" "$SY_SAVE" "$SY_STATE_FILE"
}

# sy_gone <pid>: 0 once it's gone, in at most 10 seconds
sy_gone() {
	i=0
	while kill -0 "$1" 2>/dev/null && [ "$i" -lt 100 ]; do
		sleep 0.1
		i=$((i + 1))
	done
	! kill -0 "$1" 2>/dev/null
}

# sy_has <file> <text>
sy_has() {
	[ "$(cat "$1" 2>/dev/null)" = "$2" ] || fail "$(basename "$1") isn't \"$2\""
}

plugin=sync

# rclone, to a folder through an alias remote in the plugin's rclone.conf
if [ -x "$SY_LIB/rclone" ]; then
	rm -rf "$SY_STATE" "$CARD/Saves" "$SY_CORE"
	mkdir -p "$SY_SAVES" "$SY_CORE"
	CLOUD="$SY_WORK/cloud"
	mkdir -p "$CLOUD/mosaic/Saves/GB" "$SY_STATE"
	printf '[fake]\ntype = alias\nremote = %s\n' "$CLOUD" >"$SY_STATE/rclone.conf"
	printf 'backend=rclone\nremote=fake:mosaic\nrclone_pass=Pw-303-secret\n' >"$STATE/sync.cfg"
	sy_files 202601010900
	# only on the remote
	printf 'remote only' >"$CLOUD/mosaic/Saves/GB/Remote Only (World).sav"
	touch -t 202601010800 "$CLOUD/mosaic/Saves/GB/Remote Only (World).sav"
	sy_run rclone-start "plugin sync: synced everything: 2 up, 1 down, 0 conflicts, 0 failed" \
		|| fail "the launcher exited early with rclone"
	expect "rclone to fake:mosaic, saves and states"
	expect "toast: Sync: 2 sent, 1 fetched"
	sy_has "$CLOUD/mosaic/Saves/GB/Sync Test (World).sav" "save one"
	sy_has "$CLOUD/mosaic/States/GB-gambatte/Sync Test (World).gb.st0" "state one"
	sy_has "$SY_SAVES/Remote Only (World).sav" "remote only"
	# nothing to do the next time, and no toast for it
	sy_run rclone-again "plugin sync: synced everything: 0 up, 0 down, 0 conflicts, 0 failed" \
		|| fail "the launcher exited early with rclone again"
	grep -qF "toast: Sync: up to date" "$log" && fail "sync toasted at the start with nothing to do"

	# back from the game, its save changed here and, later, on the remote:
	# the remote's wins, this one kept as .conflict; another game's
	# changed save waits for the next start
	printf 'save here' >"$SY_SAVE"
	touch -t 202601021000 "$SY_SAVE"
	printf 'save there' >"$CLOUD/mosaic/Saves/GB/Sync Test (World).sav"
	touch -t 202601021100 "$CLOUD/mosaic/Saves/GB/Sync Test (World).sav"
	printf 'other' >"$SY_OTHER"
	sy_back
	sy_run rclone-conflict "plugin sync: synced: 0 up, 1 down, 1 conflicts, 0 failed" || fail "the launcher exited early with a conflict"
	expect "back from $SY_GAME"
	expect "syncing Sync Test (World)"
	expect "toast: Sync: 1 newer file fetched (1 changed on both sides, the older kept as .conflict)"
	sy_has "$SY_SAVE" "save there"
	sy_has "$SY_SAVE.conflict" "save here"
	[ -f "$CLOUD/mosaic/Saves/GB/Sync Other (World).sav" ] && fail "a game's sync sent another game's save"
	[ -f "$CLOUD/mosaic/Saves/GB/Sync Test (World).sav.conflict" ] && fail "the .conflict copy was sent"

	# this side newer: it goes up and the remote's is kept there
	printf 'save newer' >"$SY_SAVE"
	touch -t 202601031000 "$SY_SAVE"
	printf 'save older' >"$CLOUD/mosaic/Saves/GB/Sync Test (World).sav"
	touch -t 202601030900 "$CLOUD/mosaic/Saves/GB/Sync Test (World).sav"
	sy_back
	sy_run rclone-newer "plugin sync: synced: 1 up, 0 down, 1 conflicts, 0 failed" || fail "the launcher exited early sending a newer save"
	sy_has "$CLOUD/mosaic/Saves/GB/Sync Test (World).sav" "save newer"
	sy_has "$CLOUD/mosaic/Saves/GB/Sync Test (World).sav.conflict" "save older"

	# Wi-Fi off: it says so after the game, and syncs everything next time
	printf 'enabled=0\nconnected=0\n' >"$SY_WORK/wifi.fake"
	MOSAIC_LAUNCHER_WIFI_FAKE="$SY_WORK/wifi.fake"
	export MOSAIC_LAUNCHER_WIFI_FAKE
	sy_back
	sy_run rclone-offline "plugin sync: toast: Sync: Wi-Fi is off" || fail "the launcher exited early with Wi-Fi off"
	unset MOSAIC_LAUNCHER_WIFI_FAKE
	[ -f "$SY_STATE/pending.txt" ] || fail "sync didn't note a sync was owed"
	sy_back
	sy_run rclone-owed "plugin sync: synced everything: 1 up, 0 down, 0 conflicts, 0 failed" \
		|| fail "the launcher exited early syncing what was owed"
	sy_has "$CLOUD/mosaic/Saves/GB/Sync Other (World).sav" "other"
	[ -f "$SY_STATE/pending.txt" ] && fail "sync kept owing a sync after it"

	# romm with a server and Upload states after playing on: its uploaded
	# state is left to it, while the save, which it doesn't know, syncs
	mkdir -p "$STATE/romm"
	printf 'url=http://127.0.0.1:9\nuser=u\npassword=p\nsync_states=on\nbutton=none\n' >"$STATE/romm.cfg"
	printf 'state romm' >"$SY_STATE_FILE"
	touch -t 202601041000 "$SY_STATE_FILE"
	printf 'save romm' >"$SY_SAVE"
	touch -t 202601041000 "$SY_SAVE"
	size=$(wc -c <"$SY_STATE_FILE" | tr -d ' ')
	mtime=$(python3 -I -c 'import os, sys; print(int(os.stat(sys.argv[1]).st_mtime))' "$SY_STATE_FILE")
	printf 'up\t1\t%s\t%s\t%s\n' "$size" "$mtime" "$SY_STATE_FILE" >"$STATE/romm/synced.txt"
	sy_on='romm=on\n'
	sy_run romm "plugin sync: synced everything: 1 up, 0 down, 0 conflicts, 0 failed" || fail "the launcher exited early with romm"
	sy_on=''
	expect "leaving 1 files to romm"
	sy_has "$CLOUD/mosaic/Saves/GB/Sync Test (World).sav" "save romm"
	sy_has "$CLOUD/mosaic/States/GB-gambatte/Sync Test (World).gb.st0" "state one"
	# without romm it goes
	sy_run romm-off "plugin sync: synced everything: 1 up, 0 down, 0 conflicts, 0 failed" || fail "the launcher exited early without romm"
	sy_has "$CLOUD/mosaic/States/GB-gambatte/Sync Test (World).gb.st0" "state romm"
	rm -rf "$STATE/romm" "$STATE/romm.cfg"
	for f in "$WORK"/sync-*.log "$SY_LOGS/sync.txt"; do
		grep -qF "Pw-303-secret" "$f" && fail "the rclone password was logged in $(basename "$f")"
	done
else
	echo "skipping sync's rclone part: no rclone here" >&2
fi

# rsync, to a folder on this computer
if [ -x "$SY_LIB/rsync" ] && ! "$SY_LIB/rsync" --version 2>/dev/null | grep -q openrsync; then
	rm -rf "$SY_STATE" "$CARD/Saves" "$SY_CORE"
	mkdir -p "$SY_SAVES" "$SY_CORE"
	PEER="$SY_WORK/peer"
	mkdir -p "$PEER"
	printf 'backend=rsync\npath=%s\n' "$PEER" >"$STATE/sync.cfg"
	sy_files 202601010900
	sy_run rsync-start "plugin sync: synced everything: 2 up, 0 down, 0 conflicts, 0 failed" \
		|| fail "the launcher exited early with rsync"
	expect "rsync to $PEER, saves and states"
	sy_has "$PEER/Saves/GB/Sync Test (World).sav" "save one"
	sy_has "$PEER/States/GB-gambatte/Sync Test (World).gb.st0" "state one"
	# the times went across, so the next start has nothing to do
	sy_run rsync-again "plugin sync: synced everything: 0 up, 0 down, 0 conflicts, 0 failed" \
		|| fail "the launcher exited early with rsync again"
	# changed on both sides, this one newer: it goes, and the other is kept
	printf 'save newer' >"$SY_SAVE"
	touch -t 202601031000 "$SY_SAVE"
	printf 'save older' >"$PEER/Saves/GB/Sync Test (World).sav"
	touch -t 202601030900 "$PEER/Saves/GB/Sync Test (World).sav"
	sy_back
	sy_run rsync-conflict "plugin sync: synced: 1 up, 0 down, 1 conflicts, 0 failed" || fail "the launcher exited early with rsync's conflict"
	sy_has "$PEER/Saves/GB/Sync Test (World).sav" "save newer"
	sy_has "$PEER/Saves/GB/Sync Test (World).sav.conflict" "save older"
	# the other side's newer state comes back
	printf 'state there' >"$PEER/States/GB-gambatte/Sync Test (World).gb.st0"
	touch -t 202601051000 "$PEER/States/GB-gambatte/Sync Test (World).gb.st0"
	sy_run rsync-pull "plugin sync: synced everything: 0 up, 1 down, 0 conflicts, 0 failed" \
		|| fail "the launcher exited early pulling with rsync"
	sy_has "$SY_STATE_FILE" "state there"
else
	echo "skipping sync's rsync part: no rsync here, or only openrsync" >&2
fi

# Syncthing, on 127.0.0.1 with a made-up other device: it starts with the
# launcher, shares Saves and the cores' folders (and nothing else in
# .userdata/shared), rescans after a game, says the other device isn't
# connected, and stops as the launcher quits
if [ -x "$SY_LIB/syncthing" ]; then
	rm -rf "$SY_STATE" "$CARD/Saves" "$SY_CORE"
	mkdir -p "$SY_SAVES" "$SY_CORE"
	sy_files 202601010900
	mkdir -p "$SY_WORK/peer-home"
	peer=$("$SY_LIB/syncthing" generate --home="$SY_WORK/peer-home" 2>&1 | grep -oE '[A-Z2-7]{7}(-[A-Z2-7]{7}){7}' | head -n 1)
	[ -n "$peer" ] || fail "couldn't make a Syncthing device ID"
	printf 'backend=syncthing\npeer=%s\n' "$peer" >"$STATE/sync.cfg"
	for f in .stignore .stfolder; do
		[ -e "$SY_SHARED/$f" ] && fail "the card already has $SY_SHARED/$f"
	done
	wait_was=$WAIT
	WAIT=$((WAIT + 20))
	sy_back
	sy_run syncthing "plugin sync: Syncthing: the other device isn't connected" || fail "the launcher exited early with Syncthing"
	WAIT=$wait_was
	expect "Syncthing with $peer, saves and states"
	expect "Syncthing up as "
	expect "toast: Sync: the other device isn't connected; Syncthing sends your saves when it is"
	grep -qF "id=\"mosaic-saves\"" "$SY_STATE/syncthing/config.xml" 2>/dev/null || fail "Syncthing doesn't have the saves folder"
	grep -qF "id=\"mosaic-states\"" "$SY_STATE/syncthing/config.xml" 2>/dev/null || fail "Syncthing doesn't have the states folder"
	grep -qxF '!/GB-gambatte' "$SY_SHARED/.stignore" 2>/dev/null || fail "the states folder's ignores don't take the core's folder"
	grep -qxF '*' "$SY_SHARED/.stignore" 2>/dev/null || fail "the states folder's ignores don't leave out the rest"
	grep -qF "Syncthing is up as" "$SY_LOGS/sync.txt" 2>/dev/null || fail "sync's own log doesn't have Syncthing"
	grep -qF "127.0.0.1:8385" "$SY_LOGS/syncthing.txt" 2>/dev/null || fail "Syncthing's API isn't on 127.0.0.1"
	# a launcher stopped by a signal takes it with it on Linux; elsewhere
	# it's stopped here, by its pid
	st_pid=$(sed -n 's/.*started Syncthing, pid \([0-9]*\).*/\1/p' "$SY_LOGS/sync.txt" | tail -n 1)
	[ -n "$st_pid" ] || fail "sync didn't log Syncthing's pid"
	if [ "$(uname -s)" = Linux ]; then
		sy_gone "$st_pid" || fail "Syncthing kept running after the launcher was stopped"
	elif [ -n "$st_pid" ]; then
		kill "$st_pid" 2>/dev/null
		sy_gone "$st_pid"
	fi
	# and a launcher that quits, here by powering off, stops it
	build_plugin powerer 'id=powerer\nname=Powerer\napi=1\n'
	printf 'power=off\ndelay=6000\n' >"$STATE/powerer.cfg"
	sy_on='powerer=on\n'
	WAIT=$((WAIT + 20))
	sy_run syncthing-quit && fail "powerer didn't quit the launcher"
	WAIT=$wait_was
	sy_on=''
	expect "stopped Syncthing"
	st_pid=$(sed -n 's/.*started Syncthing, pid \([0-9]*\).*/\1/p' "$SY_LOGS/sync.txt" | tail -n 1)
	sy_gone "$st_pid" || fail "Syncthing kept running after the launcher quit"
	[ -f /tmp/mosaic-sync-syncthing.pid ] && fail "Syncthing's pid file was left"
	rm -rf "$WORK/plugins/powerer.plugin" "$STATE/powerer.cfg"
	rm -rf "$SY_SHARED/.stignore" "$SY_SHARED/.stfolder"
else
	echo "skipping sync's Syncthing part: no syncthing here" >&2
fi

if [ -f "$SY_WORK/launched.saved" ]; then cp "$SY_WORK/launched.saved" /tmp/mosaic-launcher-launched; else rm -f /tmp/mosaic-launcher-launched; fi
rm -rf "$SY_ROMS" "$CARD/Saves" "$SY_CORE"
[ -d "$SY_WORK/Saves.saved" ] && mv "$SY_WORK/Saves.saved" "$CARD/Saves"
for d in "$SY_WORK/shared.saved"/*; do
	[ -d "$d" ] && mv "$d" "$SY_SHARED/"
done
rm -rf "$SY_STATE" "$SY_WORK" "$WORK/plugins/sync.plugin" "$WORK/plugins/romm.plugin" "$STATE/sync.cfg" "$STATE/romm.cfg" \
	"$STATE/romm" "$STATE/plugins.txt"
[ -f "$SY_LOGS/sync.txt" ] && mv "$SY_LOGS/sync.txt" "$WORK/sync-own.txt"
[ -f "$SY_LOGS/syncthing.txt" ] && mv "$SY_LOGS/syncthing.txt" "$WORK/sync-syncthing.txt"
case_log=""
