# shellcheck shell=sh
# shard: 1

# launch-animation (built in whole by glider, which reads its frames back)
# owns the launch slot: launching a pak from the root, the selection pill
# glides from its row to the middle as the rest dims to black, then fades,
# and the last frame is black; under gridder the selected cell glides, at
# the duration and ease settings. Each launch quits the launcher; the
# card's recents, /tmp/next and /tmp/last.txt are put back
GLIDE_PAK="$WORK/glider/Glide.pak"
mkdir -p "$GLIDE_PAK"
: >"$GLIDE_PAK/launch.sh"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/recent.txt.saved"
[ -f /tmp/next ] && cp /tmp/next "$WORK/next.saved"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/last.txt.saved"
build_plugin glider "$(cat "$ROOT/plugins/launch-animation/plugin.txt")\n"
build_plugin gridder 'id=gridder\nname=Gridder\napi=1\nslots=layout\n'
plugin=glider
for mode in list grid; do
	case "$mode" in
	list)
		printf 'launch-animation=on\ngridder=off\n' >"$STATE/plugins.txt"
		printf 'glider_pak=%s\n' "$GLIDE_PAK" >"$STATE/launch-animation.cfg"
		what=pill; want_duration=350; want_ease=2
		;;
	grid)
		printf 'launch-animation=on\ngridder=on\n' >"$STATE/plugins.txt"
		printf 'glider_pak=%s\nduration=200\nease=in-out\n' "$GLIDE_PAK" >"$STATE/launch-animation.cfg"
		what=cell; want_duration=200; want_ease=3
		;;
	esac
	run_launcher "$WORK/launch-animation-$mode.log" && fail "the launcher didn't quit to launch glider's pak"
	log="$WORK/launch-animation-$mode.log"
	grep -qF "plugins: launch-animation picks the launch style" "$log" || fail "launch-animation didn't take the launch slot"
	expect "duration $want_duration ease $want_ease"
	expect "open pak 1"
	grep -qE "plugin launch-animation: gliding the $what at [0-9]+,[0-9]+ [1-9][0-9]*x[1-9][0-9]* to the middle over $want_duration ms$" "$log" \
		|| fail "launch-animation didn't glide the $what: $(grep "plugin launch-animation: " "$log")"
	# the pill or cell is on something the frame beside it isn't, and a probe was found
	begin=$(grep "plugin glider: begin " "$log")
	echo "$begin" | grep -qE "style 2 gliding 1 from .* on ([0-9A-F]{6}) beside ([0-9A-F]{6}) probe [0-9]+,[0-9]+$" \
		|| fail "glider's begin is wrong: $begin"
	if [ "$mode" = list ]; then
		on=$(echo "$begin" | sed 's/.* on \([0-9A-F]*\) beside.*/\1/')
		beside=$(echo "$begin" | sed 's/.* beside \([0-9A-F]*\) probe.*/\1/')
		[ "$on" != "$beside" ] || fail "glider's pill isn't on the pill: $begin"
	fi
	# every frame up to the glide's end had the pill where the model puts it,
	# the rest dimmed by the model's amount, and the last was black (the
	# ease can reach 1 a frame early, so the last summary counts)
	last=$(grep "plugin glider: last " "$log" | tail -1)
	echo "$last" | grep -qE "^.*last frames ([0-9]+) pill ([3-9]|[1-9][0-9]+) of ([0-9]+) dim ([0-9]+) of ([0-9]+) black 1$" \
		|| fail "launch-animation's frames are wrong: ${last:-no last frame}"
	# shellcheck disable=SC2046
	set -- $(echo "$last" | sed 's/.* pill \([0-9]*\) of \([0-9]*\) dim \([0-9]*\) of \([0-9]*\) black.*/\1 \2 \3 \4/')
	{ [ "${1:-0}" -eq "${2:-1}" ] && [ "${3:-0}" -eq "${4:-1}" ] && [ "${4:-0}" -gt 0 ]; } \
		|| fail "launch-animation's frames don't match the model: $last"
	grep -qF "using stock's transitions until the launcher restarts" "$log" && fail "launch-animation's launch fell back to stock's"
done
if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
if [ -f "$WORK/next.saved" ]; then cp "$WORK/next.saved" /tmp/next; else rm -f /tmp/next; fi
if [ -f "$WORK/last.txt.saved" ]; then cp "$WORK/last.txt.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
rm -f "$WORK/recent.txt.saved" "$WORK/next.saved" "$WORK/last.txt.saved"
rm -rf "$WORK/plugins/glider.plugin" "$WORK/plugins/gridder.plugin" "$WORK/glider" "$STATE/launch-animation.cfg" "$STATE/plugins.txt"
