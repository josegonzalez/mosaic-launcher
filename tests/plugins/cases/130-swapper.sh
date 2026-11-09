# shellcheck shell=sh
# shard: 2

# swapper shows surfaces of its own in place of the built-in list's
# thumbnail with art_replace: placed as a thumbnail of their size, with its
# rounded corners, and art decorators under them. New frames of the same
# surface redraw only the thumbnail. It goes when the selection moves or
# the surface is freed, the first plugin's keeps the place over taker's,
# and bad calls are refused. As the transition slot's owner it reads each
# push's old frame, where the layers are drawn together
SWAP_DIR="$CARD/Swapper"
mkdir -p "$SWAP_DIR/.media" "$SWAP_DIR/Next/.media" "$SWAP_DIR/Next/Next/.media" "$STATE"
cp "$ROOT/tests/plugins/drawer/art.png" "$SWAP_DIR/.media/Apple.png"
cp "$ROOT/tests/plugins/drawer/art.png" "$SWAP_DIR/Next/.media/Cherry.png"
cp "$ROOT/tests/plugins/drawer/art.png" "$SWAP_DIR/Next/Next/.media/Dog.png"
build_plugin hello 'id=hello\nname=Hello\napi=1\n'
build_plugin swapper 'id=swapper\nname=Swapper\napi=1\nslots=transition\n'
build_plugin taker 'id=taker\nname=Taker\napi=1\n'
printf 'hello=on\nswapper=on\ntaker=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/swapper.log" "plugin swapper: done" || fail "the launcher exited early with swapper loaded"
log="$WORK/swapper.log"
plugin=swapper
expect "init -1"
expect "art Apple rect 519,269 460x230"
expect "in decorate_art -1"
expect "null id -1, not loaded -1, unknown -1, other's id -1, art cache -1, clear none 0"
expect "show 0"
expect "art Apple rect 519,231 460x306"
expect "frames 30 failed 0, list redraws 0"
expect "push 1 Apple left 0000FF corner "
grep -qF "plugin swapper: push 1 Apple left 0000FF corner 0000FF" "$log" && fail "the replacement's corners weren't rounded"
expect "show Banana 0"
expect "art Banana rect 519,231 460x306"
expect "select Cherry 0"
expect "art Cherry rect 519,269 460x230"
expect "push 2 Cherry left FF0000"
expect "show small 0"
expect "art Dog rect 519,231 460x306"
expect "free small 0"
expect "while taker's shows -1"
expect "art Dog rect 519,154 460x460"
expect "push 3 Dog left FF0000"
expect "empty -1"
plugin=taker
expect "refused -1"
expect "shown 0"
expect "cleared 0"
grep -qF "plugins: swapper's" "$log" && fail "swapper's hooks were turned off"
grep -F "plugin swapper: push" "$log"

# nothing shows with Show game art off; under a plugin layout it's the
# layout's to draw, which gridder does with swap=1 from art_replacement
printf 'once=1\n' >"$STATE/swapper.cfg"
printf 'swapper=on\n' >"$STATE/plugins.txt"
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
printf 'gameart=0\n' >"$SETTINGS"
SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/swapper-gameart.log" "plugin swapper: done" \
	|| fail "the launcher exited early with Show game art off"
log="$WORK/swapper-gameart.log"
plugin=swapper
expect "once -1"
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -f "$WORK/minuisettings.txt.saved"
build_plugin gridder 'id=gridder\nname=Gridder\napi=1\nslots=layout\n'
printf 'gridder=on\nswapper=on\n' >"$STATE/plugins.txt"
printf 'swap=1\n' >"$STATE/gridder.cfg"
run_launcher "$WORK/swapper-grid.log" "plugin gridder: renders" "plugin swapper: done" "plugin gridder: render replacement Apple 1" \
	|| fail "the launcher exited early with swapper under gridder"
log="$WORK/swapper-grid.log"
expect "once 0"
plugin=gridder
expect "after render replacement -1"
expect "render replacement Apple 1 lit 00FF00, other 0, bad index -1 -1, no out -1"
grep -qF "plugins: gridder's" "$log" && fail "gridder's layout was turned off with swapper"
rm -rf "$WORK/plugins/gridder.plugin" "$WORK/plugins/swapper.plugin" "$WORK/plugins/taker.plugin" "$WORK/plugins/hello.plugin" "$SWAP_DIR" \
	"$STATE/swapper.cfg" "$STATE/taker.cfg" "$STATE/hello.cfg" "$STATE/gridder.cfg" "$STATE/plugins.txt"
