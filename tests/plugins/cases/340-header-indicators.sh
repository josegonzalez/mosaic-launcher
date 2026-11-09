# shellcheck shell=sh
# shard: 3

# the bundled header-indicators shows free storage and a profile badge in a
# pill in the theme's accent with hint-coloured text, a gap left of the
# status pill; spotter, a widget after it in load order, reads the header
# back. The SSH dot stays off on desktop, the FPS shows once a second has
# passed, and with nothing to show there's no widget.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" header-indicators >/dev/null || fail "building header-indicators"
build_plugin spotter 'id=spotter\nname=Spotter\napi=1\n'
mkdir -p "$STATE"
printf 'header-indicators=on\nspotter=on\n' >"$STATE/plugins.txt"
printf 'profile=Kid\nssh=on\n' >"$STATE/header-indicators.cfg"
run_launcher "$WORK/header-indicators.log" "plugin header-indicators: widget at " "plugin spotter: runs " \
	|| fail "the launcher exited early with header-indicators loaded"
log="$WORK/header-indicators.log"
plugin=header-indicators
expect "unsupported on desktop: the SSH dot stays off"
grep -qE "plugin header-indicators: widget at [0-9]+,15 [0-9]+x90: profile storage$" "$log" \
	|| fail "header-indicators' widget is wrong: $(grep "plugin header-indicators: widget" "$log")"
hi_x=$(sed -n 's/.*plugin header-indicators: widget at \([0-9]*\),.*/\1/p' "$log" | tail -n 1)
hi_w=$(sed -n 's/.*plugin header-indicators: widget at [0-9]*,15 \([0-9]*\)x90:.*/\1/p' "$log" | tail -n 1)
# runs <first start> <first end> <second start>, the edges blended by up to 2 pixels
runs=$(grep "plugin spotter: runs " "$log" | tail -n 1 | sed 's/.*: runs //; s/,.*//; s/-/ /g')
# shellcheck disable=SC2086
set -- $runs
if [ -z "$hi_x" ] || [ -z "$hi_w" ] || [ "$#" -lt 4 ]; then
	fail "spotter didn't see header-indicators' widget and the status pill: $(grep "plugin spotter: runs" "$log" | tail -n 1)"
elif [ $(($1 - hi_x)) -gt 2 ] || [ $((hi_x - $1)) -gt 2 ] || [ $(($2 - hi_x - hi_w)) -gt 2 ] || [ $((hi_x + hi_w - $2)) -gt 2 ]; then
	fail "header-indicators didn't draw at $hi_x, $hi_w wide: spotter saw $runs"
elif [ "$3" -lt $((hi_x + hi_w + 10)) ]; then
	fail "header-indicators' widget isn't a gap left of the status pill: $runs"
fi
grep "plugin spotter: runs " "$log" | tail -n 1 | grep -qF ", accent 1 hint 1" \
	|| fail "header-indicators' pill isn't in the theme's colours: $(grep "plugin spotter: runs" "$log" | tail -n 1)"
grep -qF "plugins: header-indicators'" "$log" && fail "header-indicators' widget was turned off"

printf 'storage=off\nfps=on\n' >"$STATE/header-indicators.cfg"
run_launcher "$WORK/header-indicators-fps.log" "plugin header-indicators: fps first " "plugin header-indicators: widget at " \
	|| fail "the launcher exited early with header-indicators showing the FPS"
log="$WORK/header-indicators-fps.log"
fps=$(sed -n 's/.*plugin header-indicators: fps first \([0-9]*\)$/\1/p' "$log")
if [ -z "$fps" ] || [ "$fps" -lt 10 ] || [ "$fps" -gt 250 ]; then
	fail "header-indicators' FPS is wrong: $fps"
fi
grep -qE "plugin header-indicators: widget at [0-9]+,15 [0-9]+x90: fps$" "$log" \
	|| fail "header-indicators didn't show only the FPS: $(grep "plugin header-indicators: widget" "$log")"
grep -qF "SSH dot" "$log" && fail "header-indicators logged the SSH dot with it off"

printf 'storage=off\n' >"$STATE/header-indicators.cfg"
run_launcher "$WORK/header-indicators-off.log" "plugin spotter: runs " || fail "the launcher exited early with header-indicators showing nothing"
grep -qF "plugin header-indicators: widget at" "$WORK/header-indicators-off.log" && fail "header-indicators drew a widget with nothing to show"
rm -rf "$WORK/plugins/header-indicators.plugin" "$WORK/plugins/spotter.plugin" "$STATE/header-indicators.cfg" "$STATE/plugins.txt"
