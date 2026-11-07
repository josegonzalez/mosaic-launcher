#!/bin/sh
#
# Show what changed in upstream's launcher between two NextUI tags, so those
# changes can be ported into launcher/ by hand before bumping the submodule.
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
