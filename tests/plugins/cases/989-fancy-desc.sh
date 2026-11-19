# shellcheck shell=sh

# layout-fancy's Art position and Description settings, with glancer
# reading the panel down a column on each selected row. Left: the panel on
# the left, the rows on the right, and Beta's green thumbnail in the panel
# a quarter of the way across. Top: the panel across the top half, the
# thumbnail in its middle. Description on, on the right: Alpha's long
# .info description, over the 255 bytes other keys keep, is drawn whole
# under its art, and it scrolls only once the
# selection has stayed put two seconds, so not while glancer moves on
# after a second and a half, and it does when glancer waits longer.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" layout-fancy >/dev/null || fail "building layout-fancy"
build_plugin glancer 'id=glancer\nname=Glancer\napi=1\n'
GLANCE_DIR="$CARD/Roms/Glancer (GLANCE)"
mkdir -p "$GLANCE_DIR/.media" "$STATE"
cp "$ROOT/tests/plugins/peeker/art.png" "$GLANCE_DIR/.media/Alpha.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$GLANCE_DIR/.media/Beta.png"
printf 'description=%s\n' "Alpha is a game about the first letter, which sets out across a long road of words to find \
the rest of the alphabet, meeting vowels, consonants and the odd accent on the way, and learning that every line \
has to end somewhere before the next one can begin. It keeps going past the 255 bytes other keys are cut to, as a \
description may, through a second paragraph's worth of words about the road, the letters met along it, and the \
long way round to the end of the alphabet." >"$GLANCE_DIR/.media/Alpha.info"
printf 'description=Beta comes second.\n' >"$GLANCE_DIR/.media/Beta.info"
printf 'layout-fancy=on\nglancer=on\nslot.layout=layout-fancy\n' >"$STATE/plugins.txt"
plugin=glancer

printf 'position=left\n' >"$STATE/layout-fancy.cfg"
printf 'column=25\n' >"$STATE/glancer.cfg"
log="$WORK/fancy-desc-left.log"
run_launcher "$log" "plugin glancer: done" || fail "the launcher exited early with layout-fancy's art on the left"
grep -qF "plugin layout-fancy: art left, description off" "$log" || fail "layout-fancy's position wasn't read"
grep -qF "plugin layout-fancy: rows 443 wide (" "$log" || fail "layout-fancy's rows are wrong on the left: $(grep "plugin layout-fancy: rows" "$log")"
grep -qE "plugin layout-fancy: rows 443 wide \([0-9]+ at the top\), panel 45,[0-9]+ 460x" "$log" \
	|| fail "layout-fancy's panel isn't on the left: $(grep "plugin layout-fancy: rows" "$log")"
beta=$(grep "plugin glancer: panel Beta" "$log" | tail -n 1)
echo "$beta" | grep -qE "panel Beta runs 00FF00:[0-9]+-[0-9]+$" || fail "Beta's thumbnail isn't in the panel on the left: $beta"
grep -qF "plugin layout-fancy: description" "$log" && fail "layout-fancy drew a description with it off"

printf 'position=top\n' >"$STATE/layout-fancy.cfg"
printf 'column=50\n' >"$STATE/glancer.cfg"
log="$WORK/fancy-desc-top.log"
run_launcher "$log" "plugin glancer: done" || fail "the launcher exited early with layout-fancy's art at the top"
grep -qF "plugin layout-fancy: art top, description off" "$log" || fail "layout-fancy's position wasn't read"
grep -qE "plugin layout-fancy: rows 949 wide \(949 at the top\), panel [0-9]+,30 [0-9]+x271$" "$log" \
	|| fail "layout-fancy's panel isn't across the top: $(grep "plugin layout-fancy: rows" "$log")"
beta=$(grep "plugin glancer: panel Beta" "$log" | tail -n 1)
echo "$beta" | grep -qE "panel Beta runs 00FF00:[0-9]+-[0-9]+$" || fail "Beta's thumbnail isn't in the panel at the top: $beta"

printf 'description=on\n' >"$STATE/layout-fancy.cfg"
rm -f "$STATE/glancer.cfg"
log="$WORK/fancy-desc-on.log"
run_launcher "$log" "plugin glancer: done" || fail "the launcher exited early with layout-fancy's description on"
grep -qF "plugin layout-fancy: art right, description on" "$log" || fail "layout-fancy's description setting wasn't read"
grep -qE "plugin layout-fancy: description [0-9]+ lines?, [0-9]+ tall in 460x[0-9]+ at 519," "$log" \
	|| fail "layout-fancy didn't draw Alpha's description under its art: $(grep "plugin layout-fancy: description" "$log")"
# all of its 500 or so bytes, which take more than 15 lines; cut to 255 they'd take 11
grep -qE "plugin layout-fancy: description (1[6-9]|[2-9][0-9]) lines, " "$log" \
	|| fail "Alpha's description was cut: $(grep "plugin layout-fancy: description" "$log")"
alpha=$(grep "plugin glancer: panel Alpha" "$log" | tail -n 1)
echo "$alpha" | grep -qE "panel Alpha runs 00FF00:[0-9]+-[0-9]+$" || fail "Alpha's thumbnail isn't in the panel with its description: $alpha"
grep -qF "plugin layout-fancy: description scrolling" "$log" && fail "Alpha's description scrolled before the selection settled two seconds"

printf 'step_ms=4000\n' >"$STATE/glancer.cfg"
log="$WORK/fancy-desc-scroll.log"
run_launcher "$log" "plugin layout-fancy: description scrolling" \
	|| fail "the launcher exited early waiting on layout-fancy's description"
grep -qF "plugin layout-fancy: description scrolling" "$log" || fail "Alpha's description didn't scroll once the selection settled"
grep -qF "plugins: layout-fancy's" "$log" && fail "layout-fancy's layout was turned off"
rm -rf "$WORK/plugins/layout-fancy.plugin" "$WORK/plugins/glancer.plugin" "$GLANCE_DIR" \
	"$STATE/layout-fancy.cfg" "$STATE/glancer.cfg" "$STATE/plugins.txt"
