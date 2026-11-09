#!/bin/bash
# Diagnostic for #233: N first starts, each cold (caches dropped, no Mesa
# cache, a new Xvfb), timing when hasRecents is logged and when the loading
# marker goes, with every thread's stack for a start not ready after WAIT s.
if [ "${1:-}" = "--one" ]; then
	ONE="$2"
else
	ONE=""
	N="${1:-150}"
	WAIT="${2:-8}"
	OUT="${3:-build/diag}"
fi
COLD="${COLD:-1}"
ROOT="$(pwd)"
WORK=/tmp/w
CARD=/var/tmp/nextui/sdcard
STATE="$CARD/.userdata/desktop/launcher"
ARCH="desktop-$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
mkdir -p "$OUT" "$WORK/plugins/hello.plugin/lib/$ARCH"
printf 'id=hello\nname=Hello\napi=1\n' >"$WORK/plugins/hello.plugin/plugin.txt"
cc -shared -fPIC -std=gnu99 -I"$ROOT/sdk" "$ROOT/tests/plugins/hello/hello.c" -o "$WORK/plugins/hello.plugin/lib/$ARCH/plugin.so" || exit 1
export LD_LIBRARY_PATH=/var/tmp/nextui/lib
ms() { echo $(( $(date +%s%N) / 1000000 )); }

one() {
	i=$1
	bash "$ROOT/vendor/NextUI/workspace/desktop/prepare_fake_sd_root.sh" >/dev/null
	rm -f "$STATE/loading" "$STATE/plugins_paused" "$STATE/crashes" "$STATE/fallback"
	start=$(ms)
	MOSAIC_LAUNCHER_PLUGIN_PATH="$WORK/plugins" "$ROOT/build/desktop/nextui.elf" >"$WORK/run.log" 2>&1 &
	pid=$!
	recents=-1
	ready=-1
	while kill -0 "$pid" 2>/dev/null; do
		now=$(ms)
		[ "$recents" -lt 0 ] && grep -q hasRecents "$WORK/run.log" && recents=$((now - start))
		if [ "$recents" -ge 0 ] && [ ! -e "$STATE/loading" ]; then
			ready=$((now - start))
			break
		fi
		[ $((now - start)) -gt $((WAIT * 1000)) ] && break
		sleep 0.02
	done
	echo "start $i: hasRecents ${recents}ms, ready ${ready}ms"
	if [ "$ready" -lt 0 ] && kill -0 "$pid" 2>/dev/null; then
		echo "start $i: STALLED"
		cp "$WORK/run.log" "$OUT/stall-$i.log"
		sudo gdb -p "$pid" -batch -ex "set pagination off" -ex "thread apply all bt" >"$OUT/stall-$i.bt" 2>&1
		grep -A30 "Thread 1 " "$OUT/stall-$i.bt" | head -40
		kill "$pid"
		sleep 6
		kill -0 "$pid" 2>/dev/null && kill -KILL "$pid"
		echo "start $i: after the stall, marker there: $([ -e "$STATE/loading" ] && echo yes || echo no)"
	else
		kill "$pid" 2>/dev/null
	fi
	wait "$pid" 2>/dev/null
}

if [ -n "$ONE" ]; then
	one "$ONE"
	exit 0
fi

for i in $(seq 1 "$N"); do
	if [ "$COLD" = 1 ]; then
		sync
		echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null
		rm -rf "$HOME/.cache/mesa_shader_cache" "$HOME/.cache/mesa_shader_cache_db"
		WAIT="$WAIT" OUT="$OUT" xvfb-run -a bash "$0" --one "$i"
	else
		one "$i"
	fi
done
