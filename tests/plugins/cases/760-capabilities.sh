# shellcheck shell=sh
# shard: 3

# capabilities: asker names hello and decliner in optional=, so although
# plugins.txt lists it first it loads after them, and before adder. From
# init, plugin_loaded is 1 for hello, which loaded before it, and 0 for
# decliner, which is on but declined, for asker itself and for adder,
# which loads later; has_capability is 1 for hello's provides=. On its
# first tick adder and asker count too, plugin_info has each plugin's
# description, and a MosaicPluginInfo from before description was added
# keeps it untouched.
build_plugin asker 'id=asker\nname=Asker\napi=1\noptional=hello, decliner\ndescription=Asks around\n'
build_plugin hello 'id=hello\nname=Hello\napi=1\nprovides=test.greets\ndescription=Says hello\n'
build_plugin decliner 'id=decliner\nname=Decliner\napi=1\nprovides=test.nothing\n'
build_plugin adder 'id=adder\nname=Adder\napi=1\n'
mkdir -p "$STATE"
printf 'asker=on\nhello=on\ndecliner=on\nadder=on\n' >"$STATE/plugins.txt"

log="$WORK/capabilities.log"
run_launcher "$log" "plugin asker: old info" || fail "the launcher exited early (capabilities)"
plugin=asker
expect "init hello 1 decliner 0 asker 0 adder 0 missing 0 cap 1 none 0 empty 0"
expect "tick hello 1 decliner 0 asker 1 adder 1 missing 0 cap 1 none 0 empty 0"
expect "info hello [Says hello]"
expect "info asker [Asks around]"
expect "info adder []"
expect "old info kept"
grep -qF "info decliner" "$log" && fail "plugin_info listed decliner, which declined"
order=$(grep -oE "plugins: loaded (hello|asker|adder)" "$log" | sed 's/plugins: loaded //' | tr '\n' ' ')
[ "$order" = "hello asker adder " ] || fail "asker didn't load after hello and before adder: $order"

rm -rf "$WORK/plugins/asker.plugin" "$WORK/plugins/hello.plugin" "$WORK/plugins/decliner.plugin" \
	"$WORK/plugins/adder.plugin" "$STATE/hello.cfg" "$STATE/plugins.txt"
case_log=""
