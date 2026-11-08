#!/bin/sh
# Builds the trimmed ffmpeg video-previews loads on devices, in a toolchain
# image (run by `make ffmpeg-device` in the tg5040 one, the oldest sysroot,
# so it runs on every device):
#
#   plugins/video-previews/ffmpeg/build.sh <output dir>
#
# leaving the headers the plugin is built against in <output dir>/prefix/include
# and, in <output dir>/ship, the three libraries it loads, named by their
# sonames (FAT cards can't hold symlinks), with ffmpeg's licence. The plugin never links them: it
# loads them with dlopen from its own folder the first time a clip plays.
#
# It's ffmpeg's own source from a pinned, checksummed tarball, LGPL (no
# --enable-gpl and no encoders), with only what preview clips need: the
# H.264, AAC and MP3 decoders, the MP4/MOV demuxer and their parsers, and
# the bitstream filters H.264 in MP4 needs. --disable-autodetect keeps it
# from picking up other libraries from the sysroot. There's no swscale or
# swresample: the plugin converts pictures and sound itself. The output is
# stamped with this script's hash, so a rebuild with nothing changed is a
# no-op, and changing the options rebuilds it.
set -eu

: "${CROSS_COMPILE:?CROSS_COMPILE is needed: run this in a toolchain image}"
OUT=${1:?usage: build.sh <output dir>}

VERSION=6.1.2
SHA256=3b624649725ecdc565c903ca6643d41f33bd49239922e45c9b1442c63dca4e38
URL="https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz"

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
STAMP="$OUT/.built-$VERSION-$(sha256sum "$0" | cut -c1-16)"
if [ -f "$STAMP" ]; then
	echo "ffmpeg $VERSION already built in $OUT"
	exit 0
fi
rm -f "$OUT"/.built-*

TARBALL="$OUT/ffmpeg-$VERSION.tar.xz"
if [ ! -f "$TARBALL" ]; then
	if command -v wget >/dev/null; then
		wget -q -O "$TARBALL.part" "$URL"
	else
		curl -fsSL -o "$TARBALL.part" "$URL"
	fi
	mv "$TARBALL.part" "$TARBALL"
fi
if ! echo "$SHA256  $TARBALL" | sha256sum -c - >/dev/null; then
	echo "ffmpeg-$VERSION.tar.xz doesn't match its checksum" >&2
	rm -f "$TARBALL"
	exit 1
fi

rm -rf "$OUT/src" "$OUT/prefix" "$OUT/ship"
mkdir -p "$OUT/src"
tar -xJf "$TARBALL" -C "$OUT/src"
cd "$OUT/src/ffmpeg-$VERSION"

./configure \
	--prefix="$OUT/prefix" \
	--enable-cross-compile \
	--cross-prefix="$CROSS_COMPILE" \
	--arch=aarch64 \
	--target-os=linux \
	--enable-shared \
	--disable-static \
	--enable-pic \
	--disable-everything \
	--enable-decoder=h264 \
	--enable-decoder=aac \
	--enable-decoder=mp3 \
	--enable-demuxer=mov \
	--enable-parser=h264 \
	--enable-parser=aac \
	--enable-parser=mpegaudio \
	--enable-bsf=extract_extradata \
	--enable-bsf=h264_mp4toannexb \
	--enable-bsf=null \
	--enable-protocol=file \
	--disable-programs \
	--disable-doc \
	--disable-autodetect \
	--disable-avdevice \
	--disable-avfilter \
	--disable-swscale \
	--disable-swresample \
	--disable-postproc \
	--disable-network \
	--disable-hwaccels \
	--disable-debug \
	--extra-cflags="-O2 -mcpu=cortex-a53" >"$OUT/configure.log"
grep -q "^License: LGPL" "$OUT/configure.log" || {
	echo "ffmpeg's configure didn't make an LGPL build" >&2
	exit 1
}
make -j"$(nproc)" >"$OUT/make.log"
make install >/dev/null

mkdir -p "$OUT/ship"
for lib in avutil avcodec avformat; do
	for file in "$OUT/prefix/lib/lib$lib.so."*.*.*; do :; done
	soname=$("${CROSS_COMPILE}readelf" -d "$file" | sed -n 's/.*SONAME.*\[\(.*\)\].*/\1/p')
	cp "$file" "$OUT/ship/$soname"
	"${CROSS_COMPILE}strip" --strip-unneeded "$OUT/ship/$soname"
done
cp COPYING.LGPLv2.1 "$OUT/ship/ffmpeg-COPYING.LGPLv2.1"
cd "$OUT"
rm -rf "$OUT/src"
touch "$STAMP"
ls -l "$OUT/ship"
