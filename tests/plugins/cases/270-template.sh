# shellcheck shell=sh
# shard: 3

# the plugin template builds and loads as it ships, starting its example
# helper from beside plugin.so, which answers a ping, logs to the plugin's
# log file and quits once the launcher stops; and with helper/ deleted, it
# builds and loads without one. Both are built from copies of template/,
# and their paks hold the helper only when it was built
template="$WORK/plugins/example.plugin"
TP_LOG="$CARD/.userdata/desktop/logs/example.txt"

# tp_copy <dir>: a fresh copy of template/ in dir, without anything built
tp_copy() {
	rm -rf "$1"
	mkdir -p "$1"
	cp -R "$ROOT/template/." "$1/"
	rm -rf "${1:?}/lib" "${1:?}/build"
}

# tp_install <dir>: the template built in dir as the example plugin
tp_install() {
	rm -rf "$template"
	mkdir -p "$template"
	cp "$1/plugin.txt" "$template/"
	cp -R "$1/lib" "$template/"
}

# tp_pak <dir>: dir's pak, with the host's compiler standing in for the
# device's, and the files in it in dir/pak.txt
tp_pak() {
	(cd "$1" && make -s build CC="$CC" >/dev/null && make -s pak >/dev/null) || fail "packaging the template in $1"
	unzip -Z1 "$1/build/Example.pak.zip" >"$1/pak.txt"
}

tp_copy "$WORK/template"
(cd "$WORK/template" && make -s desktop CC="$CC" >/dev/null) || fail "building the template"
[ -x "$WORK/template/lib/$ARCH/example-helper" ] || fail "the template didn't build its helper"
tp_install "$WORK/template"
rm -f "$TP_LOG"
log="$WORK/template.log"
plugin=example
run_launcher "$log" "plugins: loaded example" "plugin example: helper " || fail "the launcher exited early with the template plugin"
grep -q "plugins: loaded example" "$log" || fail "the template plugin didn't load"
tp_pid=$(sed -n 's/.*plugin example: helper \([0-9]*\) answered pong.*/\1/p' "$log" | tail -n 1)
if [ -z "$tp_pid" ]; then
	fail "the template's helper didn't answer: $(grep -F "plugin example: " "$log")"
else
	grep -qF "example-helper: started, pid $tp_pid" "$TP_LOG" || fail "the helper didn't log to $TP_LOG"
	i=0
	while kill -0 "$tp_pid" 2>/dev/null && [ "$i" -lt 30 ]; do
		sleep 0.1
		i=$((i + 1))
	done
	if kill -0 "$tp_pid" 2>/dev/null; then
		fail "the template's helper ran on after the launcher stopped"
		kill "$tp_pid"
	fi
fi

tp_copy "$WORK/template-nohelper"
rm -rf "$WORK/template-nohelper/helper"
(cd "$WORK/template-nohelper" && make -s desktop CC="$CC" >/dev/null) || fail "building the template without its helper"
if [ -e "$WORK/template-nohelper/lib/$ARCH/example-helper" ]; then fail "the template built a helper without helper/"; fi
tp_install "$WORK/template-nohelper"
log="$WORK/template-nohelper.log"
run_launcher "$log" "plugins: loaded example" "plugin example: no helper to start" ||
	fail "the launcher exited early with the template plugin without its helper"
grep -q "plugins: loaded example" "$log" || fail "the template plugin didn't load without its helper"
expect "no helper to start"

if command -v zip >/dev/null && command -v unzip >/dev/null; then
	tp_pak "$WORK/template"
	grep -qx "lib/aarch64/example-helper" "$WORK/template/pak.txt" || fail "the template's pak has no helper"
	tp_pak "$WORK/template-nohelper"
	if grep -q "example-helper" "$WORK/template-nohelper/pak.txt"; then fail "the pak without helper/ has a helper"; fi
else
	echo "skipping the template's paks: no zip or unzip"
fi
rm -rf "$template" "$WORK/template" "$WORK/template-nohelper" "$TP_LOG"
