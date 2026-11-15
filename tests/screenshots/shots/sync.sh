# shellcheck shell=sh

# stops a Syncthing an earlier shot left running, by the pid it wrote
nosyncthing() {
	[ -f /tmp/mosaic-sync-syncthing.pid ] && kill "$(cat /tmp/mosaic-sync-syncthing.pid)" 2>/dev/null
	sleep 1
	rm -f /tmp/mosaic-sync-syncthing.pid
}

# rclone to gdrive:Mosaic, an alias for a folder here, synced as the
# launcher starts
begin
on sync
cfg sync backend=rclone remote=gdrive:Mosaic
mkdir -p "$STATE/sync" "$WORK/tmp/cloud"
printf '[gdrive]\ntype = alias\nremote = %s\n' "$WORK/tmp/cloud" >"$STATE/sync/rclone.conf"
press "wait wait wait menu wait left a"
shot rclone

# Syncthing, started with the launcher, before the other device is set
nosyncthing
begin
on sync
cfg sync backend=syncthing
press "menu wait left a"
after 6000
shot syncthing
nosyncthing
