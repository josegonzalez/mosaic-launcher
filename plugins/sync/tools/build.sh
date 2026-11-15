#!/bin/sh
# Puts the programs the sync plugin runs in place for devices, in a
# toolchain image (run by `make sync-tools-device` in the tg5040 one, the
# oldest sysroot, so they run on every device):
#
#   plugins/sync/tools/build.sh <output dir>
#
# leaving them in <output dir>/ship with their licences, for
# plugins/makefile to put beside the plugin's plugin.so:
#
#   rclone     rclone's own static linux-arm64 build, unchanged
#   syncthing  Syncthing's own static linux-arm64 build, unchanged
#   rsync      built from rsync's release tarball, with its own popt and
#              zlib and no optional libraries
#   dbclient   Dropbear's SSH client, built from its release tarball, for
#              rsync to reach another device over SSH, with dropbearkey
#              and dropbearconvert to make its key or convert an OpenSSH
#              one, as dbclient reads only Dropbear's own
#
# rsync and Dropbear have no official static builds, so they're built
# from their release sources against the oldest sysroot, like the plugins
# (a static glibc build can't look up host names on a device whose glibc
# differs). Every download is checked against tools/SHA256SUMS, and the
# build stops on a mismatch. The output is stamped with this script's and
# SHA256SUMS's hash, so a rebuild with nothing changed is a no-op.
set -eu

: "${CROSS_COMPILE:?CROSS_COMPILE is needed: run this in a toolchain image}"
OUT=${1:?usage: build.sh <output dir>}
HERE=$(cd "$(dirname "$0")" && pwd)

RCLONE_VERSION=1.75.2
SYNCTHING_VERSION=2.1.7
RSYNC_VERSION=3.5.2
DROPBEAR_VERSION=2026.94

RCLONE=rclone-v$RCLONE_VERSION-linux-arm64.zip
SYNCTHING=syncthing-linux-arm64-v$SYNCTHING_VERSION.tar.gz
RSYNC=rsync-$RSYNC_VERSION.tar.gz
DROPBEAR=dropbear-$DROPBEAR_VERSION.tar.bz2

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
fetch "https://download.samba.org/pub/rsync/src/$RSYNC" "$RSYNC"
fetch "https://matt.ucc.asn.au/dropbear/releases/$DROPBEAR" "$DROPBEAR"

rm -rf "$OUT/src" "$OUT/ship"
mkdir -p "$OUT/src" "$OUT/ship"

unzip -q -o "$DL/$RCLONE" -d "$OUT/src"
cp "$OUT/src/rclone-v$RCLONE_VERSION-linux-arm64/rclone" "$OUT/ship/rclone"

tar -xzf "$DL/$SYNCTHING" -C "$OUT/src"
cp "$OUT/src/syncthing-linux-arm64-v$SYNCTHING_VERSION/syncthing" "$OUT/ship/syncthing"

tar -xzf "$DL/$RSYNC" -C "$OUT/src"
(
	cd "$OUT/src/rsync-$RSYNC_VERSION"
	./configure --host=aarch64-linux-gnu CC="${CROSS_COMPILE}gcc" CFLAGS="-O2" \
		--with-included-popt --with-included-zlib --disable-openssl --disable-xxhash --disable-zstd \
		--disable-lz4 --disable-idn --disable-md2man --disable-acl-support --disable-xattr-support --disable-iconv \
		>"$OUT/rsync-configure.log"
	make -j"$(nproc)" rsync >"$OUT/rsync-make.log"
	"${CROSS_COMPILE}strip" -o "$OUT/ship/rsync" rsync
)

# the toolchain image has no bzip2, so Python unpacks Dropbear's tarball
python3 -I -c 'import sys, tarfile; tarfile.open(sys.argv[1], "r:bz2").extractall(sys.argv[2])' \
	"$DL/$DROPBEAR" "$OUT/src"
(
	cd "$OUT/src/dropbear-$DROPBEAR_VERSION"
	./configure --host=aarch64-linux-gnu CC="${CROSS_COMPILE}gcc" CFLAGS="-O2" \
		--disable-zlib --disable-syslog --disable-lastlog --disable-utmp --disable-utmpx --disable-wtmp \
		--disable-wtmpx --disable-pututline --disable-pututxline >"$OUT/dropbear-configure.log"
	make -j"$(nproc)" PROGRAMS="dbclient dropbearkey dropbearconvert" >"$OUT/dropbear-make.log"
	for program in dbclient dropbearkey dropbearconvert; do
		"${CROSS_COMPILE}strip" -o "$OUT/ship/$program" "$program"
	done
)

chmod 755 "$OUT/ship/rclone" "$OUT/ship/syncthing" "$OUT/ship/rsync" "$OUT/ship/dbclient" "$OUT/ship/dropbearkey" \
	"$OUT/ship/dropbearconvert"
cp "$HERE/licenses/"* "$OUT/ship/"
rm -rf "$OUT/src"
touch "$STAMP"
ls -l "$OUT/ship"
