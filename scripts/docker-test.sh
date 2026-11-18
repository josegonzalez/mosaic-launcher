#!/bin/sh
#
# Runs the tests in CI's image (docker/ci/Dockerfile), on any host with
# Docker: by default `make test-all`, everything CI checks, or the make
# targets given. The image is built here (tagged mosaic-launcher-ci) and
# runs privileged, for the plugin test workers' namespaces and gdb, with an
# init that reaps orphans, as the tests check that what they stopped is gone.
#
# The checkout is copied into the container (without build/ and tmp/), so
# the Linux build never mixes with the host's. Afterwards the logs and
# timings are copied back to build/docker/: build/docker/tests/plugins has
# the plugin cases' logs, cases.tsv and starts.tsv.
#
#   scripts/docker-test.sh
#   scripts/docker-test.sh test-plugins
#   JOBS=8 SHARD=2/3 scripts/docker-test.sh test-plugins
#
# TEST_JOBS (for test-all), JOBS, SHARD, CASES and PLUGIN_TEST_SECONDS are
# passed through.

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE=mosaic-launcher-ci
OUT="$ROOT/build/docker"
RUNTIME="${CONTAINER_RUNTIME:-$(command -v docker || command -v podman)}"
[ -n "$RUNTIME" ] || { echo "docker or podman is required" >&2; exit 1; }

"$RUNTIME" build -q -t "$IMAGE" "$ROOT/docker/ci" >/dev/null || exit 1
mkdir -p "$OUT"

[ $# -gt 0 ] || set -- test-all
# shellcheck disable=SC2016 # the container's shell expands them
"$RUNTIME" run --rm --privileged --init \
	-v "$ROOT":/src:ro -v "$OUT":/out \
	-e TEST_JOBS -e JOBS -e SHARD -e CASES -e PLUGIN_TEST_SECONDS -e SMOKE_STACKS=1 \
	"$IMAGE" sh -c '
		rsync -a --exclude /build --exclude /tmp /src/ /work/
		cd /work || exit 1
		xvfb-run -a make "$@"
		status=$?
		for dir in build/tests build/profiles; do
			[ -d "$dir" ] && mkdir -p "/out/${dir#build/}" && cp -R "$dir/." "/out/${dir#build/}/"
		done
		cp build/*.log /out/ 2>/dev/null
		exit $status
	' docker-test "$@"
status=$?
[ -s "$OUT/tests/plugins/cases.tsv" ] && echo "plugin case timings in $OUT/tests/plugins (scripts/test-times.sh summary $OUT/tests/plugins)"
exit $status
