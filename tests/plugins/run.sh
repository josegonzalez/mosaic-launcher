#!/bin/sh
#
# Loads test plugins into the desktop build and checks what happened:
# a plugin that loads and uses the host table, one with the wrong API
# version, and one that crashes during startup.
#
#   tests/plugins/run.sh

set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$ROOT/build/tests/plugins"
CARD=/var/tmp/nextui/sdcard
STATE="$CARD/.userdata/desktop/launcher"
ARCH="desktop-$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
CC="${CC:-cc}"
failures=0

fail() {
	echo "FAIL: $*" >&2
	failures=$((failures + 1))
}

# build_plugin <name> <manifest text>
build_plugin() {
	dir="$WORK/plugins/$1.plugin"
	mkdir -p "$dir/lib/$ARCH"
	printf '%b' "$2" >"$dir/plugin.txt"
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -I"$ROOT/sdk" \
		"$ROOT/tests/plugins/$1/$1.c" -o "$dir/lib/$ARCH/plugin.so" || fail "building $1"
}

run_launcher() {
	NEXTUI_LAUNCHER_PLUGIN_PATH="$WORK/plugins" "$ROOT/scripts/desktop-smoke.sh" "$1" 3 >/dev/null 2>&1
}

rm -rf "$WORK" "$STATE"
build_plugin hello 'id=hello\nname=Hello\napi=1\n'

# loads, logs through the host, and saves its config
run_launcher "$WORK/hello.log" || fail "the launcher exited early with hello loaded"
grep -q "plugins: loaded hello" "$WORK/hello.log" || fail "hello didn't load"
grep -q "plugin hello: hello from desktop" "$WORK/hello.log" || fail "hello's log line is missing"
grep -q "^started=yes$" "$STATE/hello.cfg" 2>/dev/null || fail "hello's cfg_set didn't save"
[ ! -f "$STATE/loading" ] || fail "the loading marker outlived the first frame"

# reads its config back on the next start
printf 'greeting=hi\nstarted=yes\n' >"$STATE/hello.cfg"
run_launcher "$WORK/hello-cfg.log" || fail "the launcher exited early on the second start"
grep -q "plugin hello: hi from desktop" "$WORK/hello-cfg.log" || fail "hello didn't read its config"

# turned off in plugins.txt
echo "hello=off" >"$STATE/plugins.txt"
run_launcher "$WORK/off.log" || fail "the launcher exited early with hello off"
grep -q "plugins: not loading hello: turned off" "$WORK/off.log" || fail "hello loaded while turned off"
rm "$STATE/plugins.txt"

# wrong API version is skipped, and the launcher still starts
printf 'id=hello\nname=Hello\napi=99\n' >"$WORK/plugins/hello.plugin/plugin.txt"
run_launcher "$WORK/api.log" || fail "the launcher exited early with a mismatched plugin"
grep -q "plugins: hello targets API 99" "$WORK/api.log" || fail "the API mismatch wasn't reported"
grep -q "plugins: loaded hello" "$WORK/api.log" && fail "a mismatched plugin loaded"
rm -rf "$WORK/plugins/hello.plugin"

# the bundled plugins load and start cleanly
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building the bundled plugins"
run_launcher "$WORK/bundled.log" || fail "the launcher exited early with the bundled plugins"
grep -q "plugins: loaded favorites" "$WORK/bundled.log" || fail "favorites didn't load"
grep -q "couldn't bind Y" "$WORK/bundled.log" && fail "favorites couldn't bind Y"
rm -rf "$WORK/plugins"/*.plugin

# the plugin template builds and loads as it ships
template="$WORK/plugins/example.plugin"
mkdir -p "$template"
cp "$ROOT/template/plugin.txt" "$template/"
(cd "$ROOT/template" && make -s desktop CC="$CC" >/dev/null) || fail "building the template"
cp -R "$ROOT/template/lib" "$template/"
rm -rf "$ROOT/template/lib"
run_launcher "$WORK/template.log" || fail "the launcher exited early with the template plugin"
grep -q "plugins: loaded example" "$WORK/template.log" || fail "the template plugin didn't load"
rm -rf "$template"

# /tmp/stay_awake holds off auto-sleep
touch /tmp/stay_awake
run_launcher "$WORK/awake.log" || fail "the launcher exited early with /tmp/stay_awake"
rm -f /tmp/stay_awake
grep -q "awake: holding off auto-sleep" "$WORK/awake.log" || fail "/tmp/stay_awake didn't hold off auto-sleep"
grep -q "awake: holding off auto-sleep" "$WORK/hello.log" && fail "auto-sleep was held off without /tmp/stay_awake"

# a crash while starting pauses plugins on the next start
build_plugin crasher 'id=crasher\nname=Crasher\napi=1\n'
run_launcher "$WORK/crash.log" && fail "the crashing plugin didn't crash the launcher"
[ -f "$STATE/loading" ] || fail "no loading marker after the crash"
run_launcher "$WORK/after-crash.log" || fail "the launcher crashed again instead of pausing plugins"
grep -q "pausing them" "$WORK/after-crash.log" || fail "plugins weren't paused after the crash"
[ -f "$STATE/plugins_paused" ] || fail "no plugins_paused marker"
run_launcher "$WORK/paused.log" || fail "the launcher crashed while plugins were paused"
grep -q "paused after a crash" "$WORK/paused.log" || fail "plugins didn't stay paused"

rm -rf "$STATE"
if [ "$failures" -gt 0 ]; then
	echo "$failures plugin check(s) failed" >&2
	exit 1
fi
echo "ok plugins"
