#!/bin/sh
#
# Assemble build/Launcher.pak and zip it. The zip holds the contents of the
# pak directory, not the directory itself, so it unzips into
# Tools/<platform>/Launcher.pak.
#
#   ./scripts/package.sh <version> <platform>...
#
# Each platform needs build/<platform>/nextui.elf from `make build-<platform>`.

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [ "$#" -lt 2 ]; then
	echo "usage: $0 <version> <platform>..." >&2
	exit 2
fi
version="$1"
shift

OUT="$ROOT/build/Launcher.pak"
ZIP="$ROOT/build/Launcher.pak.zip"
rm -rf "$OUT" "$ZIP"
mkdir -p "$OUT/bin" "$OUT/licenses"

cp "$ROOT/pak/launch.sh" "$OUT/launch.sh"
for script in common.sh manage.sh boot.sh wrapper.sh hook-stub.sh; do
	cp "$ROOT/pak/bin/$script" "$OUT/bin/$script"
done
chmod +x "$OUT/launch.sh" "$OUT"/bin/*.sh

for platform in "$@"; do
	elf="$ROOT/build/$platform/nextui.elf"
	if [ ! -f "$elf" ]; then
		echo "missing $elf; run: make build-$platform" >&2
		exit 1
	fi
	mkdir -p "$OUT/bin/$platform"
	cp "$elf" "$OUT/bin/$platform/nextui.elf"
	chmod +x "$OUT/bin/$platform"/*
done

# Bundled plugins, from `make plugins-device`. They ship turned off.
if [ -d "$ROOT/build/plugins" ]; then
	for plugin in "$ROOT"/build/plugins/*.plugin; do
		[ -f "$plugin/lib/aarch64/plugin.so" ] || continue
		mkdir -p "$OUT/plugins"
		cp -R "$plugin" "$OUT/plugins/"
	done
fi

cp "$ROOT/LICENSE" "$OUT/licenses/mosaic-launcher.txt"
cp "$ROOT/sdk/LICENSE" "$OUT/licenses/mosaic-launcher-sdk.txt"

# The launcher only runs on the NextUI build it was compiled against.
(cd "$ROOT/vendor/NextUI" && git rev-parse HEAD) >"$OUT/supported-versions.txt"
echo "$version" >"$OUT/VERSION"

(cd "$OUT" && zip -qr "$ZIP" .)
echo "wrote $ZIP" >&2
