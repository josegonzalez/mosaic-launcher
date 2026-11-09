# shellcheck shell=sh
# shard: 1

# the root slot owner replaces the root, and the open, recents and reload
# calls work; the card's own recents are put back afterwards
ROM="$CARD/Roms/.rooter-test/Game.gb"
mkdir -p "$(dirname "$RECENTS")" "$(dirname "$ROM")" "$STATE"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/recent.txt.saved"
: >"$ROM"
echo "/Roms/.rooter-test/Game.gb" >"$RECENTS"
echo "rom=$ROM" >"$STATE/rooter.cfg"
build_plugin rooter 'id=rooter\nname=Rooter\napi=1\nslots=root\n'
run_launcher "$WORK/rooter.log" "plugin rooter: selected after reload" || fail "the launcher exited early with rooter loaded"
plugin=rooter
log="$WORK/rooter.log"
expect "open from init 0"
expect "root_replace called"
expect "selected $CARD/Rooter"
expect "recents 1, first $ROM"
expect "past the end (null)"
expect "pre_launch $ROM"
expect "open rom 0"
expect "open missing 0"
expect "open folder 1, now in $CARD/Rooter"
expect "removed 0, recents 0, bad index -1"
expect "cleared, recents 0"
expect "reload asked, listed 1"
expect "reloaded, in $CARD/Rooter, listed 2"
expect "selected after reload $ROM"
[ -s "$RECENTS" ] && fail "recents_clear didn't save an empty list"

# a plugin that doesn't claim the root slot isn't asked for the root
printf 'id=rooter\nname=Rooter\napi=1\n' >"$WORK/plugins/rooter.plugin/plugin.txt"
run_launcher "$WORK/rooter-unslotted.log" "plugins: rooter has root_replace but no slots=root" || fail "the launcher exited early with rooter unslotted"
grep -q "plugins: rooter has root_replace but no slots=root" "$WORK/rooter-unslotted.log" || fail "the missing root slot wasn't reported"
grep -q "plugin rooter: root_replace called" "$WORK/rooter-unslotted.log" && fail "root_replace ran without the root slot"

rm -rf "$WORK/plugins/rooter.plugin" "$(dirname "$ROM")" "$STATE/rooter.cfg"
if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
