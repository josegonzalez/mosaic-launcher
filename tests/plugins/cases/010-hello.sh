# shellcheck shell=sh
# shard: all
# its logs are what later cases check against, so every shard runs it

build_plugin hello 'id=hello\nname=Hello\napi=1\n'

# loads, logs through the host, and saves its config
run_launcher "$WORK/hello.log" "plugin hello: hello from desktop" || fail "the launcher exited early with hello loaded"
grep -q "plugins: loaded hello" "$WORK/hello.log" || fail "hello didn't load"
grep -q "plugin hello: hello from desktop" "$WORK/hello.log" || fail "hello's log line is missing"
grep -q "^started=yes$" "$STATE/hello.cfg" 2>/dev/null || fail "hello's cfg_set didn't save"
[ ! -f "$STATE/loading" ] || fail "the loading marker outlived the first frame"

# reads its config back on the next start
printf 'greeting=hi\nstarted=yes\n' >"$STATE/hello.cfg"
run_launcher "$WORK/hello-cfg.log" "plugin hello: hi from desktop" || fail "the launcher exited early on the second start"
grep -q "plugin hello: hi from desktop" "$WORK/hello-cfg.log" || fail "hello didn't read its config"

# turned off in plugins.txt
echo "hello=off" >"$STATE/plugins.txt"
run_launcher "$WORK/off.log" "plugins: not loading hello: turned off" || fail "the launcher exited early with hello off"
grep -q "plugins: not loading hello: turned off" "$WORK/off.log" || fail "hello loaded while turned off"
rm "$STATE/plugins.txt"

# wrong API version is skipped, and the launcher still starts
printf 'id=hello\nname=Hello\napi=99\n' >"$WORK/plugins/hello.plugin/plugin.txt"
run_launcher "$WORK/api.log" "plugins: hello targets API 99" || fail "the launcher exited early with a mismatched plugin"
grep -q "plugins: hello targets API 99" "$WORK/api.log" || fail "the API mismatch wasn't reported"
grep -q "plugins: loaded hello" "$WORK/api.log" && fail "a mismatched plugin loaded"
rm -rf "$WORK/plugins/hello.plugin"
