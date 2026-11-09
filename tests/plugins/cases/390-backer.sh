# shellcheck shell=sh
# shard: 3

# backer owns the background slot: its background_render gets a lent
# screen-sized surface over stock's background, which it draws over, with
# NULL in an empty folder, and both pushes start from frames with its
# background on them, the new one this frame's
BACK_BG="$WORK/back.png"
cp "$ROOT/tests/plugins/drawer/art.png" "$BACK_BG"
printf 'bg=%s\n' "$BACK_BG" >"$STATE/backer.cfg"
printf 'backer=on\n' >"$STATE/plugins.txt"
build_plugin backer 'id=backer\nname=Backer\napi=1\nslots=background,transition\n'
run_launcher "$WORK/backer.log" "plugin backer: done at" || fail "the launcher exited early with backer loaded"
log="$WORK/backer.log"
plugin=backer
grep -qF "plugins: backer draws the background" "$log" || fail "backer didn't take the background slot"
grep -qE "plugin backer: render size ([0-9]+)x([0-9]+) of \1x\2, select -1, free -1$" "$log" \
	|| fail "backer's lent surface is wrong: $(grep "plugin backer: render size" "$log")"
# stock's background, Empty's from art_path, red on the left, under it once it has loaded
expect "render Empty under FF0000FF"
# the first push's old frame is from the startup fade, so only its new one
grep -qE "plugin backer: push to backer old [0-9A-F]{8} new 00FF00FF" "$log" || fail "the first push's new frame has no background from backer"
expect "push to empty old 00FF00FF new FFFF00FF left FF0000FF"
expect "render (none) under FF0000FF"
expect "done at $CARD/Backer/Empty"
grep -qF "stock's background until" "$log" && fail "backer's background was turned off"

# an error turns it off for the run: stock's background, blue on the right, in the new frame
printf 'bg=%s\nfail=empty\n' "$BACK_BG" >"$STATE/backer.cfg"
run_launcher "$WORK/backer-fail.log" "plugin backer: done at" || fail "the launcher exited early with backer's error"
log="$WORK/backer-fail.log"
grep -qF "plugins: backer's background_render returned -1, using stock's background until the launcher restarts" "$log" \
	|| fail "backer's error wasn't logged"
[ "$(grep -c "plugin backer: render failing" "$log")" -eq 1 ] || fail "backer was asked again after its error"
expect "push to empty old 00FF00FF new 0000FFFF"

# without the slot it's never called
rm -f "$STATE/backer.cfg"
printf 'id=backer\nname=Backer\napi=1\nslots=transition\n' >"$WORK/plugins/backer.plugin/plugin.txt"
run_launcher "$WORK/backer-unslotted.log" "plugin backer: done at" || fail "the launcher exited early with backer unslotted"
log="$WORK/backer-unslotted.log"
grep -qF "plugins: backer has background_render but no slots=background, so it won't be called" "$log" || fail "the missing background slot wasn't reported"
grep -qF "plugin backer: render" "$log" && fail "backer drew without the slot"
rm -rf "$WORK/plugins/backer.plugin" "$BACK_BG" "$STATE/plugins.txt"
grep -qF "draws the background" "$WORK/hello.log" && fail "a background was drawn with no plugin taking the slot"
