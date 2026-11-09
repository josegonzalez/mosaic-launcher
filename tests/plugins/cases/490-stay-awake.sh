# shellcheck shell=sh
# shard: 2

# /tmp/stay_awake holds off auto-sleep
touch /tmp/stay_awake
run_launcher "$WORK/awake.log" "awake: holding off auto-sleep" || fail "the launcher exited early with /tmp/stay_awake"
rm -f /tmp/stay_awake
grep -q "awake: holding off auto-sleep" "$WORK/awake.log" || fail "/tmp/stay_awake didn't hold off auto-sleep"
grep -q "awake: holding off auto-sleep" "$WORK/hello.log" && fail "auto-sleep was held off without /tmp/stay_awake"

# so does /tmp/stay_alive
touch /tmp/stay_alive
run_launcher "$WORK/alive.log" "awake: holding off auto-sleep" || fail "the launcher exited early with /tmp/stay_alive"
rm -f /tmp/stay_alive
grep -q "awake: holding off auto-sleep" "$WORK/alive.log" || fail "/tmp/stay_alive didn't hold off auto-sleep"
