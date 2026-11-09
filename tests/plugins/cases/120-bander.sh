# shellcheck shell=sh
# shard: 1

# bander decorates the built-in list's thumbnail: it's called once the
# selected game's art is in, with the art's rect as the list places it and
# the column under it, its drawing lands there and stops at the art, and
# select_index and nested decorating refuse. A game with no art, an empty
# folder and art still loading for another game get no call. As the
# transition slot's owner it reads the push's old frame, where the layers
# are drawn together: its band shows over the background, and the
# thumbnail over the screen, beside it
BAND_DIR="$CARD/Bander"
mkdir -p "$BAND_DIR/.media" "$STATE"
cp "$ROOT/tests/plugins/peeker/art.png" "$BAND_DIR/.media/Apple.png"
cp "$ROOT/tests/plugins/drawer/art.png" "$BAND_DIR/.media/Cherry.png"
build_plugin bander 'id=bander\nname=Bander\napi=1\nslots=transition\n'
printf 'bander=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/bander.log" "plugin bander: done" || fail "the launcher exited early with bander loaded"
log="$WORK/bander.log"
plugin=bander
expect "open folder 1"
expect "decorate art outside a layout -1"
expect "art Apple rect 519,154 460x460 below 519,614 460x49, band 0 FF00FF, clipped 1 (fill 0), select -1, nested -1 -1"
expect "art Cherry rect 519,269 460x230 below 519,499 460x164, band 0 FF00FF, clipped 1 (fill 0), select -1, nested -1 -1"
expect "open inner 1"
expect "push band FF00FF art FF0000"
expect "done: Apple 1, Banana 0, Cherry 1, inner 0"
grep -qF "plugins: bander's" "$log" && fail "bander's hooks were turned off"
grep -qF "decorate_art" "$WORK/hello.log" && fail "a plugin without art decorators logged them"

# an error turns that plugin's decorate_art off until the launcher restarts
printf 'fail=art\n' >"$STATE/bander.cfg"
run_launcher "$WORK/bander-fail.log" "plugin bander: done" || fail "the launcher exited early with bander failing"
log="$WORK/bander-fail.log"
grep -qF "plugins: bander's decorate_art returned -1, leaving its art decorations out until the launcher restarts" "$log" \
	|| fail "bander's failing decorate_art wasn't logged"
[ "$(grep -cF "plugins: bander's" "$log")" -eq 1 ] || fail "bander's failing decorate_art was logged more than once"
expect "done: Apple 0, Banana 0, Cherry 0, inner 0"
rm -f "$STATE/bander.cfg"

# with Show game art off no thumbnail shows, so nothing is decorated; the
# card's settings are put back
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
printf 'gameart=0\n' >"$SETTINGS"
SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/bander-gameart.log" "plugin bander: done" \
	|| fail "the launcher exited early with Show game art off"
log="$WORK/bander-gameart.log"
expect "done: Apple 0, Banana 0, Cherry 0, inner 0"
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -f "$WORK/minuisettings.txt.saved"

# art decorators are chained in load order: the first draws first
build_plugin captioner 'id=captioner\nname=Captioner\napi=1\n'
printf 'bander=on\ncaptioner=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/bander-chain.log" "plugin bander: done" "plugin captioner: art after" || fail "the launcher exited early with captioner after bander"
log="$WORK/bander-chain.log"
plugin=captioner
expect "art after bander 1"
printf 'captioner=on\nbander=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/bander-chain-first.log" "plugin bander: done" "plugin captioner: art after" || fail "the launcher exited early with captioner first"
log="$WORK/bander-chain-first.log"
expect "art after bander 0"
rm -rf "$WORK/plugins/captioner.plugin"

# under a plugin layout, the layout passes its art to decorate_art, which
# runs the decorators clipped to the area under it, as wide as the art
build_plugin gridder 'id=gridder\nname=Gridder\napi=1\nslots=layout\n'
printf 'gridder=on\nbander=on\n' >"$STATE/plugins.txt"
printf 'art=1\n' >"$STATE/gridder.cfg"
printf 'open=0\nfolder=Gridder\n' >"$STATE/bander.cfg"
run_launcher "$WORK/bander-grid.log" "plugin gridder: renders" "plugin bander: done" || fail "the launcher exited early with bander under gridder"
log="$WORK/bander-grid.log"
plugin=gridder
expect "render art 0"
expect "render art bad index -1 -1, no rect -1, outside -1, not the screen -1, at the bottom 0"
expect "after render art -1"
plugin=bander
expect "art Apple rect 4,4 248x86 below 4,90 248x573, band 0 FF00FF, clipped 1 (fill 0), select -1, nested -1 -1"
expect "done: Apple 1, Banana 0, Cherry 0"
grep -qF "plugins: bander's" "$log" && fail "bander's hooks were turned off under gridder"
grep -qF "plugins: gridder's" "$log" && fail "gridder's layout was turned off with bander"
rm -rf "$WORK/plugins/gridder.plugin" "$WORK/plugins/bander.plugin" "$BAND_DIR" "$STATE/gridder.cfg" "$STATE/bander.cfg" "$STATE/plugins.txt"
