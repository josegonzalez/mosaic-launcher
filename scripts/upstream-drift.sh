#!/bin/sh
#
# Show what changed in upstream's launcher between two NextUI tags, so those
# changes can be ported into launcher/ by hand before bumping the submodule,
# and whether the launcher's patch to stock's api.c (launcher/patches/
# api.c.patch, which makes stock's UI sizes runtime values for the theme
# slot) still applies to the new tag, with upstream's changes to api.c and
# defines.h that it has to be ported over.
#
#   ./scripts/upstream-drift.sh            pinned tag -> latest tag
#   ./scripts/upstream-drift.sh v6.15.0 v6.16.0

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/vendor/NextUI"

git fetch --quiet --tags origin

from="${1:-$(git describe --tags --exact-match HEAD 2>/dev/null || git rev-parse HEAD)}"
to="${2:-$(git describe --tags "$(git rev-list --tags --max-count=1)")}"

if [ "$from" = "$to" ]; then
	echo "already at $to" >&2
	exit 0
fi

echo "launcher changes $from..$to" >&2
git log --oneline "$from..$to" -- workspace/all/nextui
git diff --stat "$from" "$to" -- workspace/all/nextui workspace/all/common
git diff "$from" "$to" -- workspace/all/nextui

PATCH="$ROOT/launcher/patches/api.c.patch"
echo "launcher patch to api.c (launcher/patches/api.c.patch) on $to" >&2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
git show "$to:workspace/all/common/api.c" >"$work/api.c"
if patch -s --dry-run -o "$work/out.c" "$work/api.c" "$PATCH" >"$work/patch.log" 2>&1; then
	echo "applies cleanly" >&2
else
	echo "DOES NOT APPLY; port it before bumping:" >&2
	cat "$work/patch.log" >&2
fi
git diff "$from" "$to" -- workspace/all/common/api.c workspace/all/common/defines.h
