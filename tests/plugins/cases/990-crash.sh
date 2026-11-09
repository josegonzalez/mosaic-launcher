# shellcheck shell=sh
# shard: 3
# leaves crasher in and plugins paused, so it runs last

# a crash while starting pauses plugins on the next start
build_plugin crasher 'id=crasher\nname=Crasher\napi=1\n'
run_launcher "$WORK/crash.log" && fail "the crashing plugin didn't crash the launcher"
[ -f "$STATE/loading" ] || fail "no loading marker after the crash"
start_launcher "$WORK/after-crash.log" "pausing them" || fail "the launcher crashed again instead of pausing plugins"
grep -q "pausing them" "$WORK/after-crash.log" || fail "plugins weren't paused after the crash"
[ -f "$STATE/plugins_paused" ] || fail "no plugins_paused marker"
start_launcher "$WORK/paused.log" "paused after a crash" || fail "the launcher crashed while plugins were paused"
grep -q "paused after a crash" "$WORK/paused.log" || fail "plugins didn't stay paused"
