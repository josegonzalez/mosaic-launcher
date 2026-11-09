# shellcheck shell=sh
# shard: 3

# the plugin template builds and loads as it ships
template="$WORK/plugins/example.plugin"
mkdir -p "$template"
cp "$ROOT/template/plugin.txt" "$template/"
(cd "$ROOT/template" && make -s desktop CC="$CC" >/dev/null) || fail "building the template"
cp -R "$ROOT/template/lib" "$template/"
rm -rf "$ROOT/template/lib"
run_launcher "$WORK/template.log" "plugins: loaded example" || fail "the launcher exited early with the template plugin"
grep -q "plugins: loaded example" "$WORK/template.log" || fail "the template plugin didn't load"
rm -rf "$template"
