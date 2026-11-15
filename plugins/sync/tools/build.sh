#!/bin/sh
# Puts the programs the sync plugin runs in place for devices, in a
# toolchain image (run by `make sync-tools-device` in the tg5040 one, as
# CI's plugins job does):
#
#   plugins/sync/tools/build.sh <output dir>
#
# leaving them in <output dir>/ship with their licences, for
# plugins/makefile to put beside the plugin's plugin.so:
#
#   rclone     rclone's own static linux-arm64 build, unchanged
#   syncthing  Syncthing's own static linux-arm64 build, unchanged
#
# Every download is checked against tools/SHA256SUMS, and the build stops
# on a mismatch. The output is stamped with this script's and
# SHA256SUMS's hash, so a rerun with nothing changed is a no-op.
set -eu

OUT=${1:?usage: build.sh <output dir>}
HERE=$(cd "$(dirname "$0")" && pwd)

RCLONE_VERSION=1.75.2
SYNCTHING_VERSION=2.1.7

RCLONE=rclone-v$RCLONE_VERSION-linux-arm64.zip
SYNCTHING=syncthing-linux-arm64-v$SYNCTHING_VERSION.tar.gz

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
STAMP="$OUT/.built-$(cat "$0" "$HERE/SHA256SUMS" | sha256sum | cut -c1-16)"
if [ -f "$STAMP" ]; then
	echo "sync tools already built in $OUT"
	exit 0
fi
rm -f "$OUT"/.built-*

DL="$OUT/downloads"
mkdir -p "$DL"

# fetch <url> <file>: downloads into $DL unless it's there, then checks it
fetch() {
	if [ ! -f "$DL/$2" ]; then
		if command -v wget >/dev/null; then
			wget -q -O "$DL/$2.part" "$1"
		else
			curl -fsSL -o "$DL/$2.part" "$1"
		fi
		mv "$DL/$2.part" "$DL/$2"
	fi
	grep -F "  $2" "$HERE/SHA256SUMS" >"$DL/$2.sha256" || {
		echo "$2 has no checksum in $HERE/SHA256SUMS" >&2
		exit 1
	}
	if ! (cd "$DL" && sha256sum -c "$2.sha256" >/dev/null); then
		echo "$2 doesn't match its checksum in $HERE/SHA256SUMS" >&2
		rm -f "$DL/$2"
		exit 1
	fi
}

fetch "https://github.com/rclone/rclone/releases/download/v$RCLONE_VERSION/$RCLONE" "$RCLONE"
fetch "https://github.com/syncthing/syncthing/releases/download/v$SYNCTHING_VERSION/$SYNCTHING" "$SYNCTHING"

rm -rf "$OUT/src" "$OUT/ship"
mkdir -p "$OUT/src" "$OUT/ship"

unzip -q -o "$DL/$RCLONE" -d "$OUT/src"
cp "$OUT/src/rclone-v$RCLONE_VERSION-linux-arm64/rclone" "$OUT/ship/rclone"

tar -xzf "$DL/$SYNCTHING" -C "$OUT/src"
cp "$OUT/src/syncthing-linux-arm64-v$SYNCTHING_VERSION/syncthing" "$OUT/ship/syncthing"

chmod 755 "$OUT/ship/rclone" "$OUT/ship/syncthing"
cp "$HERE/licenses/"* "$OUT/ship/"
rm -rf "$OUT/src"
touch "$STAMP"
ls -l "$OUT/ship"
