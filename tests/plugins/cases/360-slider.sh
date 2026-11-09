# shellcheck shell=sh
# shard: 3

# rect_at <kind> <old|new> [log]: slider's line for where the selection is
# in that frame of the first transition of kind in log ($log by default), as
# "x y w h pill_x pill_y pill_w pill_h on beside cell", or another with
# RECT_NTH (a sed line address, such as 2 or $)
rect_at() {
	grep "plugin slider: begin $1 $2 row " "${3:-$log}" | sed -n "${RECT_NTH:-1}p" \
		| sed 's/.* row //; s/ pill / /; s/ on / /; s/ beside / /; s/ cell / /; s/[,x]/ /g'
}

# check_pill <what> <rect_at line>: the row is known and the pill is on it,
# from its left and as tall, and what's under the pill isn't what's beside it
check_pill() {
	# shellcheck disable=SC2086
	set -- "$1" $2
	what=$1
	shift
	if [ "$#" -ne 11 ]; then
		fail "slider didn't log where the selection is in $what"
	elif [ "$3" -le 0 ] || [ "$4" -le 0 ] || [ "$7" -le 0 ] || [ "$7" -gt "$3" ]; then
		fail "the selection in $what isn't on a row: $*"
	elif [ "$5" -ne "$1" ] || [ "$6" -ne "$2" ] || [ "$8" -ne "$4" ]; then
		fail "the pill in $what isn't on its row: $*"
	elif [ "$9" = "-" ] || [ "$9" = "${10}" ]; then
		fail "the pill isn't where $what says: $*"
	fi
}

# slider owns the transition slot and draws both pushes into its folders,
# opened with open_path from tick, with frames from progress 0 to exactly 1
# on a lent screen that starts black, the old and new frames read-only and
# taken back afterwards, and blit_alpha fading by half. Each says where the
# selection is in both frames, and the pill is there: Before on the first
# push's new frame, and Inner, the row under it, on the second's old one.
build_plugin slider 'id=slider\nname=Slider\napi=1\nslots=transition\n'
printf 'slider=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/slider.log" "plugin slider: done at" || fail "the launcher exited early with slider loaded"
log="$WORK/slider.log"
plugin=slider
grep -qF "plugins: slider picks the transition styles" "$log" || fail "slider didn't take the transition slot"
[ "$(grep -c "plugin slider: begin push stock" "$log")" -eq 2 ] || fail "slider wasn't asked about both pushes"
grep -qE "plugin slider: begin push stock [0-9]+ duration [0-9]+ ease 2, size 1, old [1-9][0-9]*x[1-9][0-9]* new [1-9][0-9]*x[1-9][0-9]*, max 1000$" "$log" \
	|| fail "slider's transition is wrong: $(grep "plugin slider: begin push stock" "$log" | head -1)"
expect "begin push read-only: fill -1, blit onto -1, free -1, select -1"
[ "$(grep -c "plugin slider: frame first push progress 0.000 duration 200, screen 1024x768 000000FF, select -1" "$log")" -eq 2 ] \
	|| fail "slider's first frames are wrong: $(grep "plugin slider: frame first" "$log" | head -1)"
grep -qE "plugin slider: frame alpha 0 [78][0-9A-F]0000FF, none 0 000000FF, bad -1 -1, new [0-9A-F]{6}FF$" "$log" \
	|| fail "blit_alpha didn't blend by half: $(grep "plugin slider: frame alpha" "$log" | head -1)"
[ "$(grep -cE "plugin slider: frame last push progress 1.000 frames [0-9]+ backwards 0 took [0-9]+ ms" "$log")" -eq 2 ] \
	|| fail "slider's transitions didn't end at progress 1: $(grep "plugin slider: frame last" "$log")"
expect "after: old - new -"
expect "done at $CARD/Slider/Inner"
grep -qF "transitions until the launcher restarts" "$log" && fail "slider's transitions were turned off"
expect "select inner 0"
check_pill "the first push's old frame" "$(rect_at push old)"
check_pill "the first push's new frame" "$(rect_at push new)"
check_pill "the second push's old frame" "$(RECT_NTH=2 rect_at push old)"
check_pill "the second push's new frame" "$(RECT_NTH=2 rect_at push new)"
# shellcheck disable=SC2046
set -- $(rect_at push new) $(RECT_NTH=2 rect_at push old)
if [ "$#" -ne 22 ] || [ "${13}" -ne $(($2 + $4)) ] || [ "${12}" -ne "$1" ]; then
	fail "Inner isn't the row under Before: $(grep "plugin slider: begin push .* row " "$log")"
fi

# under a plugin layout, the row is the last cell gridder passed to
# decorate_cell for the selected entry, the lit one, and there's no pill;
# Inner is the cell beside Before. Where the stock pill would rest on each
# push's new frame (the first entry, 40,60 inside its left end) is the lit
# cell, not the pill.
build_plugin gridder 'id=gridder\nname=Gridder\napi=1\nslots=layout\n'
printf 'slider=on\ngridder=on\n' >"$STATE/plugins.txt"
echo "probe=40,60" >"$STATE/slider.cfg"
run_launcher "$WORK/slider-layout.log" "plugin slider: done at" || fail "the launcher exited early with slider and gridder loaded"
log="$WORK/slider-layout.log"
expect "done at $CARD/Slider/Inner"
grep "plugin slider: begin push \(old\|new\) row " "$log" | grep -v " pill 0,0 0x0 on - beside - cell FF0000FF$" \
	&& fail "a push's selection under gridder isn't its lit cell"
[ "$(grep -c "plugin slider: begin push \(old\|new\) row [0-9]*,[0-9]* [1-9][0-9]*x[1-9][0-9]* " "$log")" -ge 4 ] \
	|| fail "slider wasn't told where gridder's cells are: $(grep "plugin slider: begin push .* row " "$log")"
# shellcheck disable=SC2046
set -- $(RECT_NTH='$' rect_at push new) $(RECT_NTH='$' rect_at push old)
if [ "$#" -ne 22 ] || [ "${12}" -le "$1" ] || [ "${13}" -ne "$2" ]; then
	fail "Inner isn't the cell beside Before under gridder: $(grep "plugin slider: begin push .* row " "$log")"
fi
[ "$(grep -c "plugin slider: begin push probe 40,60 old [0-9A-F]*FF new " "$log")" -ge 2 ] \
	|| fail "slider didn't read the stock pill's spot under gridder: $(grep "plugin slider: begin push probe" "$log")"
grep "plugin slider: begin push probe " "$log" | grep -v " new FF0000FF$" \
	&& fail "the stock pill was drawn over gridder's layout on a push's new frame"
rm -rf "$WORK/plugins/gridder.plugin" "$STATE/gridder.cfg" "$STATE/slider.cfg"
printf 'slider=on\n' >"$STATE/plugins.txt"

# off: no frames, and the folders still open
echo "style=off" >"$STATE/slider.cfg"
run_launcher "$WORK/slider-off.log" "plugin slider: done at" || fail "the launcher exited early with slider's transitions off"
log="$WORK/slider-off.log"
[ "$(grep -c "plugin slider: begin push stock" "$log")" -eq 2 ] || fail "slider wasn't asked about both pushes with transitions off"
grep -qF "plugin slider: frame" "$log" && fail "slider drew frames with its transitions off"
expect "done at $CARD/Slider/Inner"

# stock's own slide, with no frames from slider
echo "style=stock" >"$STATE/slider.cfg"
run_launcher "$WORK/slider-stock.log" "plugin slider: done at" || fail "the launcher exited early with slider choosing stock's"
log="$WORK/slider-stock.log"
grep -qF "plugin slider: frame" "$log" && fail "slider drew frames after choosing stock's"
expect "done at $CARD/Slider/Inner"

# a duration past the cap runs for the cap, logged once
printf 'duration=5000\nease=3\n' >"$STATE/slider.cfg"
run_launcher "$WORK/slider-cap.log" "plugin slider: done at" || fail "the launcher exited early with slider's long transition"
log="$WORK/slider-cap.log"
[ "$(grep -cF "plugins: slider asked for a 5000 ms transition, running it for 1000 ms" "$log")" -eq 1 ] || fail "the capped transition wasn't logged once"
expect "frame first push progress 0.000 duration 1000"
took=$(sed -n 's/.*plugin slider: frame last push progress 1.000 frames [0-9]* backwards 0 took \([0-9]*\) ms.*/\1/p' "$log" | head -1)
if [ -z "$took" ]; then
	fail "slider's capped transition didn't end"
elif [ "$took" -lt 900 ] || [ "$took" -gt 2500 ]; then
	fail "slider's capped transition took $took ms"
fi

# errors turn slider's transitions off for the run: the second push is stock's
for mode in bad frame noframe; do
	case "$mode" in
	frame) printf 'fail=frame\n' >"$STATE/slider.cfg" ;;
	*) printf 'style=%s\n' "$mode" >"$STATE/slider.cfg" ;;
	esac
	run_launcher "$WORK/slider-$mode.log" "plugin slider: done at" || fail "the launcher exited early with slider's $mode error"
	log="$WORK/slider-$mode.log"
	case "$mode" in
	bad) want="plugins: slider's transition_begin returned 7, using stock's transitions until the launcher restarts" ;;
	frame) want="plugins: slider's transition_frame returned -1, using stock's transitions until the launcher restarts" ;;
	noframe) want="plugins: slider chose a custom transition but has no transition_frame, using stock's transitions until the launcher restarts" ;;
	esac
	grep -qF "$want" "$log" || fail "slider's $mode error wasn't logged"
	[ "$(grep -c "plugin slider: begin push stock" "$log")" -eq 1 ] || fail "slider was asked again after its $mode error"
	expect "done at $CARD/Slider/Inner"
done
rm -f "$STATE/slider.cfg"

# with stock's Menu transitions off there are none: slider isn't asked about
# the pushes, and the folders still open; the card's settings are put back
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
printf 'menutransitions=0\n' >"$SETTINGS"
SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/slider-setting.log" "plugin slider: done at" \
	|| fail "the launcher exited early with Menu transitions off"
log="$WORK/slider-setting.log"
grep -qF "plugin slider: begin" "$log" && fail "slider was asked with Menu transitions off"
expect "done at $CARD/Slider/Inner"
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi

# without a slot it's never asked
printf 'id=slider\nname=Slider\napi=1\n' >"$WORK/plugins/slider.plugin/plugin.txt"
run_launcher "$WORK/slider-unslotted.log" "plugin slider: done at" || fail "the launcher exited early with slider unslotted"
log="$WORK/slider-unslotted.log"
grep -qF "plugins: slider has transitions but no slots=transition or slots=launch, so they won't be called" "$log" || fail "the missing transition slot wasn't reported"
grep -qF "plugin slider: begin" "$log" && fail "slider was asked without a slot"

# with only the launch slot, the pushes are stock's and the launch of a pak
# is slider's, from the last frame into black; the launch quits the
# launcher, and the card's recents, /tmp/next and /tmp/last.txt are put back
SLIDE_PAK="$WORK/slider/Slid.pak"
mkdir -p "$SLIDE_PAK"
: >"$SLIDE_PAK/launch.sh"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/recent.txt.saved"
[ -f /tmp/next ] && cp /tmp/next "$WORK/next.saved"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/last.txt.saved"
printf 'id=slider\nname=Slider\napi=1\nslots=launch\n' >"$WORK/plugins/slider.plugin/plugin.txt"
echo "pak=$SLIDE_PAK" >"$STATE/slider.cfg"
run_launcher "$WORK/slider-launch.log" && fail "the launcher didn't quit to launch slider's pak"
log="$WORK/slider-launch.log"
grep -qF "plugins: slider picks the launch style" "$log" || fail "slider didn't take the launch slot"
grep -qF "plugin slider: begin push" "$log" && fail "slider was asked about a push with only the launch slot"
expect "open pak 1"
grep -qE "plugin slider: begin launch stock 0 duration 0 ease 2, size 1, old [1-9][0-9]*x[1-9][0-9]* new [1-9][0-9]*x[1-9][0-9]*, max 1000$" "$log" \
	|| fail "slider's launch transition is wrong: $(grep "plugin slider: begin launch" "$log")"
grep -qE "plugin slider: frame alpha 0 [78][0-9A-F]0000FF, none 0 000000FF, bad -1 -1, new 000000FF$" "$log" \
	|| fail "the launch's new frame isn't black: $(grep "plugin slider: frame alpha" "$log")"
grep -qE "plugin slider: frame last launch progress 1.000 frames [0-9]+ backwards 0" "$log" || fail "slider's launch transition didn't end"
# the launch starts from Inner's list with Game selected, where the
# transition slot's second push left it; the black frame it goes into has
# none
check_pill "the launch's old frame" "$(rect_at launch old)"
[ "$(rect_at launch old | cut -d' ' -f1-8)" = "$(RECT_NTH=2 rect_at push new "$WORK/slider.log" | cut -d' ' -f1-8)" ] \
	|| fail "the launch's selection isn't Game's row: $(rect_at launch old)"
expect "begin launch new row 0,0 0x0 pill 0,0 0x0 on - beside -"
if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
if [ -f "$WORK/next.saved" ]; then cp "$WORK/next.saved" /tmp/next; else rm -f /tmp/next; fi
if [ -f "$WORK/last.txt.saved" ]; then cp "$WORK/last.txt.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
rm -f "$WORK/recent.txt.saved" "$WORK/next.saved" "$WORK/last.txt.saved"
rm -rf "$WORK/plugins/slider.plugin" "$WORK/slider" "$STATE/slider.cfg" "$STATE/plugins.txt"
grep -q "plugin slider:\|transition styles\|launch style" "$WORK/hello.log" && fail "transitions were asked about with no plugin taking them"
