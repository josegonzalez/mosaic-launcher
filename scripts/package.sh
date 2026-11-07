#!/bin/sh
#
# Assemble build/Launcher.pak and zip it the way the Pak Store expects: the
# zip holds the contents of the pak directory, not the directory itself.
#
#   ./scripts/package.sh <version> <platform>...
#
# Each platform needs build/<platform>/nextui.elf from `make build-<platform>`.
# With RELEASE=1, the version must match pak.json.

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOCK="$ROOT/pak/minui.lock"
# shellcheck source=scripts/minui-common.sh
. "$ROOT/scripts/minui-common.sh"

if [ "$#" -lt 2 ]; then
	echo "usage: $0 <version> <platform>..." >&2
	exit 2
fi
version="$1"
shift

if [ "${RELEASE:-}" = "1" ]; then
	pak_version="$(sed -n 's/^ *"version": *"\(.*\)",*$/\1/p' "$ROOT/pak.json")"
	if [ "$version" != "$pak_version" ]; then
		echo "version $version does not match pak.json ($pak_version)" >&2
		exit 1
	fi
fi

OUT="$ROOT/build/Launcher.pak"
CACHE="$ROOT/build/cache/minui"
ZIP="$ROOT/build/Launcher.pak.zip"
rm -rf "$OUT" "$ZIP"
mkdir -p "$OUT/bin" "$OUT/licenses" "$CACHE"

# Download one pinned file into the cache, or reuse it, and refuse it unless
# it matches the lock.
fetch_pinned() {
	fp_tool="$1" fp_platform="$2" fp_out="$3"
	fp_want="$(lock_hash "$fp_tool" "$fp_platform")"
	if [ -z "$fp_want" ]; then
		echo "no pin for $fp_tool $fp_platform in $LOCK" >&2
		exit 1
	fi
	fp_cached="$CACHE/$fp_tool-$fp_platform-$fp_want"
	if [ ! -f "$fp_cached" ]; then
		fp_version="$(tool_version "$fp_tool" "$(lock_value presenter_version)" "$(lock_value list_version)")"
		if [ "$fp_platform" = "LICENSE" ]; then
			fetch_license "$fp_tool" "$fp_version" "$fp_cached.part"
		else
			fetch_asset "$fp_tool" "$fp_version" "$fp_tool-$(lock_flavour "$fp_tool" "$fp_platform")" "$fp_cached.part"
		fi
		fp_got="$(sha256 "$fp_cached.part")"
		if [ "$fp_got" != "$fp_want" ]; then
			rm -f "$fp_cached.part"
			echo "$fp_tool $fp_platform: sha256 $fp_got does not match the pinned $fp_want" >&2
			exit 1
		fi
		mv "$fp_cached.part" "$fp_cached"
	fi
	cp "$fp_cached" "$fp_out"
}

cp "$ROOT/pak/launch.sh" "$OUT/launch.sh"
for script in common.sh plugins.sh boot.sh wrapper.sh hook-stub.sh; do
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
	for tool in minui-presenter minui-list; do
		fetch_pinned "$tool" "$platform" "$OUT/bin/$platform/$tool"
	done
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

cp "$ROOT/LICENSE" "$OUT/licenses/nextui-launcher.txt"
cp "$ROOT/sdk/LICENSE" "$OUT/licenses/nextui-launcher-sdk.txt"
for tool in minui-presenter minui-list; do
	fetch_pinned "$tool" LICENSE "$OUT/licenses/$tool.txt"
done

# The launcher only runs on the NextUI build it was compiled against.
(cd "$ROOT/vendor/NextUI" && git rev-parse HEAD) >"$OUT/supported-versions.txt"
echo "$version" >"$OUT/VERSION"

(cd "$OUT" && zip -qr "$ZIP" .)
echo "wrote $ZIP" >&2
