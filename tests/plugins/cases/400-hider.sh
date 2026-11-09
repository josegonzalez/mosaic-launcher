# shellcheck shell=sh
# shard: 3

# hider hides Apple's and Cherry's names, which have art, in its own
# folder: the built-in list leaves those rows blank, the selected Apple
# with no pill on the push into the folder, and still draws Banana's name;
# decorators are told which are hidden. hide_name is asked once an entry.
HIDE_DIR="$CARD/Hider"
mkdir -p "$HIDE_DIR" "$STATE"
cp "$ROOT/tests/plugins/drawer/art.png" "$HIDE_DIR/art.png"
printf 'hider=on\n' >"$STATE/plugins.txt"
build_plugin hider 'id=hider\nname=Hider\napi=1\nslots=transition\n'
run_launcher "$WORK/hider.log" "plugin hider: done" || fail "the launcher exited early with hider loaded"
log="$WORK/hider.log"
plugin=hider
expect "open folder 1"
expect "push to hider row 1 pill 0"
expect "row Apple selected 1 hidden 1 named 0 ink 0"
expect "row Banana selected 0 hidden 0 named 1 ink 1"
expect "row Cherry selected 0 hidden 1 named 0 ink 0"
grep -qE "plugin hider: done, asked 3$" "$log" || fail "hider wasn't asked once an entry: $(grep "plugin hider: done" "$log")"
grep -qE "plugin hider: row (Apple|Cherry) .* ink 1$" "$log" && fail "a hidden name was drawn: $(grep "plugin hider: row" "$log")"

# with no art to stand in for it, the selected Apple keeps its name and pill
printf 'art=none\n' >"$STATE/hider.cfg"
run_launcher "$WORK/hider-noart.log" "plugin hider: done" || fail "the launcher exited early with hider's art off"
log="$WORK/hider-noart.log"
expect "push to hider row 1 pill 1"
expect "row Apple selected 1 hidden 1 named 1 ink 1"
expect "row Cherry selected 0 hidden 1 named 0 ink 0"

# with Show game art off, every name shows; the card's settings are put back
rm -f "$STATE/hider.cfg"
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
printf 'gameart=0\n' >"$SETTINGS"
SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/hider-gameart.log" "plugin hider: done" \
	|| fail "the launcher exited early with Show game art off"
log="$WORK/hider-gameart.log"
expect "push to hider row 1 pill 1"
expect "row Apple selected 1 hidden 1 named 1 ink 1"
expect "row Cherry selected 0 hidden 1 named 1 ink 1"
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -f "$WORK/minuisettings.txt.saved"

# hiding nothing draws the list as stock does
printf 'hide=none\n' >"$STATE/hider.cfg"
run_launcher "$WORK/hider-none.log" "plugin hider: done" || fail "the launcher exited early with hider hiding nothing"
log="$WORK/hider-none.log"
expect "push to hider row 1 pill 1"
expect "row Apple selected 1 hidden 0 named 1 ink 1"
expect "row Banana selected 0 hidden 0 named 1 ink 1"
expect "row Cherry selected 0 hidden 0 named 1 ink 1"

# a plugin layout gets the answers in its view, and decorators in the
# cells it passes to decorate_cell
rm -f "$STATE/hider.cfg"
printf 'id=hider\nname=Hider\napi=1\nslots=layout\n' >"$WORK/plugins/hider.plugin/plugin.txt"
run_launcher "$WORK/hider-layout.log" "plugin hider: done" || fail "the launcher exited early with hider's layout"
log="$WORK/hider-layout.log"
expect "view names_hidden 101"
grep -qE "plugin hider: row Apple selected 1 hidden 1 named 0 " "$log" || fail "Apple's cell isn't hidden: $(grep "plugin hider: row" "$log")"
grep -qE "plugin hider: row Banana selected 0 hidden 0 named 0 " "$log" || fail "Banana's cell is hidden: $(grep "plugin hider: row" "$log")"
printf 'hide=none\n' >"$STATE/hider.cfg"
run_launcher "$WORK/hider-layout-none.log" "plugin hider: done" || fail "the launcher exited early with hider's layout hiding nothing"
log="$WORK/hider-layout-none.log"
expect "view names_hidden none"
rm -rf "$WORK/plugins/hider.plugin" "$HIDE_DIR" "$STATE/hider.cfg" "$STATE/plugins.txt"
