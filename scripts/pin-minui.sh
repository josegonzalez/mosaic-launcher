#!/bin/sh
#
# Pin the minui-presenter and minui-list builds this pak bundles.
#
# Writes pak/minui.lock: the versions to fetch, and the SHA-256 of every file
# fetched, so packaging can verify what it downloads instead of trusting
# whatever a release tag points at on the day.
#
# Upgrade:  ./scripts/pin-minui.sh 0.14.0 0.16.0
# Re-pin:   ./scripts/pin-minui.sh
#
# The diff is the review: two version numbers and a list of hashes.

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOCK="$ROOT/pak/minui.lock"
# shellcheck source=scripts/minui-common.sh
. "$ROOT/scripts/minui-common.sh"

presenter_version="${1:-$(lock_value presenter_version)}"
list_version="${2:-$(lock_value list_version)}"

if [ -z "$presenter_version" ] || [ -z "$list_version" ]; then
	echo "usage: $0 <minui-presenter version> <minui-list version>" >&2
	echo "(no lock to read current versions from)" >&2
	exit 2
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo "pinning minui-presenter $presenter_version and minui-list $list_version" >&2

{
	echo "# The SHA-256 of every minui-presenter and minui-list file this pak"
	echo "# bundles, and the versions they come from."
	echo "#"
	echo "# Written by ./scripts/pin-minui.sh, so do not edit it by hand."
	echo "# Packaging fetches exactly these files and refuses any that do not"
	echo "# match. Both tools are MIT, and their LICENSE files ship in the pak."
	echo "presenter_version $presenter_version"
	echo "list_version $list_version"

	for tool in minui-presenter minui-list; do
		version="$(tool_version "$tool" "$presenter_version" "$list_version")"
		fetch_license "$tool" "$version" "$tmp/LICENSE"
		echo "sha256 $tool LICENSE LICENSE $(sha256 "$tmp/LICENSE")"
		for target in $TARGETS; do
			platform="${target%%:*}"
			flavour="${target#*:}"
			echo "  fetching $tool-$flavour" >&2
			fetch_asset "$tool" "$version" "$tool-$flavour" "$tmp/asset"
			echo "sha256 $tool $platform $flavour $(sha256 "$tmp/asset")"
		done
	done
} >"$LOCK.new"

mv "$LOCK.new" "$LOCK"
echo "wrote $LOCK" >&2
