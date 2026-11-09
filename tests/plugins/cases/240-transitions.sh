# shellcheck shell=sh
# shard: 1

# the bundled transitions plugin draws wiper's push from Wiper's red
# background into Inner's blue one, and the pop back out when wiper
# presses B: wiper owns the transition slot and hands each transition to
# transitions, loaded without the slot, reading the screen back after each
# frame at its left, middle and right. Slide, the default, moves both
# frames left on the push and right on the pop; fade blends the whole
# screen; slide and fade blends all but the left edge; off and stock draw
# no frames.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" transitions >/dev/null || fail "building transitions"
TRANSITIONS_LIB="$WORK/plugins/transitions.plugin/lib/$ARCH/plugin.so"
sed '/^slots=/d' "$ROOT/plugins/transitions/plugin.txt" >"$WORK/plugins/transitions.plugin/plugin.txt"
build_plugin wiper 'id=wiper\nname=Wiper\napi=1\nslots=background,transition\n'
mkdir -p "$STATE"
printf 'transitions=on\nwiper=on\n' >"$STATE/plugins.txt"
printf 'lib=%s\n' "$TRANSITIONS_LIB" >"$STATE/wiper.cfg"
red=FF0000FF
blue=0000FFFF
any='[0-9A-F]{8}'
blend='[4-B][0-9A-F]00[4-B][0-9A-F]FF'
for style in default fade slide_fade off stock; do
	case "$style" in
	default) rm -f "$STATE/transitions.cfg" ;;
	# slow, so a frame comes near progress 0.5 on a slow machine too
	*) printf 'style=%s\nspeed=slow\nease=linear\n' "$style" >"$STATE/transitions.cfg" ;;
	esac
	run_launcher "$WORK/transitions-$style.log" "plugin wiper: done at" || fail "the launcher exited early with transitions' $style style"
	log="$WORK/transitions-$style.log"
	plugin=wiper
	expect "done at $CARD/Wiper"
	grep -qF "plugin wiper: done at $CARD/Wiper/Inner" "$log" && fail "B didn't back out of Inner"
	grep -qF "plugin wiper: no transitions plugin" "$log" && fail "wiper couldn't find the transitions plugin"
	grep -qF "transitions until the launcher restarts" "$log" && fail "transitions' $style style was turned off"
	expect "frames push to Inner old $red $red $red new $blue $blue $blue"
	expect "frames pop to Wiper old $blue $blue $blue new $red $red $red"
	case "$style" in
	default)
		grep -qF "plugin transitions: style slide, 250 ms, ease out" "$log" || fail "transitions' defaults are wrong: $(grep "plugin transitions:" "$log")"
		expect "begin push to Inner: style 2 duration 250 ease 2"
		expect "begin pop to Wiper: style 2 duration 250 ease 2"
		push_mid="$red $blue $blue"
		# the middle is on the seam at exactly 0.5
		pop_mid="$red $any $blue"
		;;
	off | stock)
		grep -qF "plugin transitions: style $style, 500 ms, ease linear" "$log" || fail "transitions' settings weren't used: $(grep "plugin transitions:" "$log")"
		want=1
		[ "$style" = stock ] && want=0
		for kind in "push to Inner" "pop to Wiper"; do
			grep -qE "plugin wiper: begin $kind: style $want duration [0-9]+ ease 2$" "$log" \
				|| fail "transitions' $style style didn't answer $want: $(grep "plugin wiper: begin $kind" "$log")"
		done
		grep -qF "plugin wiper: first " "$log" && fail "transitions drew frames in its $style style"
		continue
		;;
	*)
		grep -qF "plugin transitions: style $style, 500 ms, ease linear" "$log" || fail "transitions' settings weren't used: $(grep "plugin transitions:" "$log")"
		expect "begin push to Inner: style 2 duration 500 ease 0"
		expect "begin pop to Wiper: style 2 duration 500 ease 0"
		push_mid="$blend $blend $blend"
		[ "$style" = slide_fade ] && push_mid="$red $blend $blend"
		# the old blue fading out over the new red, the left edge red too
		pop_mid=$push_mid
		;;
	esac
	expect "first push to Inner progress 0.000: $red $red $red"
	expect "first pop to Wiper progress 0.000: $blue $blue $blue"
	grep -qE "plugin wiper: mid push to Inner progress 0\.[0-9]+: $push_mid$" "$log" \
		|| fail "transitions' $style push is wrong halfway: $(grep "plugin wiper: mid push to Inner" "$log")"
	grep -qE "plugin wiper: mid pop to Wiper progress 0\.[0-9]+: $pop_mid$" "$log" \
		|| fail "transitions' $style pop is wrong halfway: $(grep "plugin wiper: mid pop to Wiper" "$log")"
	grep -qE "plugin wiper: last push to Inner progress 1\.000 frames [0-9]+: $blue $blue $blue$" "$log" \
		|| fail "transitions' $style push didn't end on the new frame: $(grep "plugin wiper: last push to Inner" "$log")"
	grep -qE "plugin wiper: last pop to Wiper progress 1\.000 frames [0-9]+: $red $red $red$" "$log" \
		|| fail "transitions' $style pop didn't end on the new frame: $(grep "plugin wiper: last pop to Wiper" "$log")"
done

# owning the slot itself, it draws the push and pop with no errors
cp "$ROOT/plugins/transitions/plugin.txt" "$WORK/plugins/transitions.plugin/plugin.txt"
printf 'id=wiper\nname=Wiper\napi=1\nslots=background\n' >"$WORK/plugins/wiper.plugin/plugin.txt"
rm -f "$STATE/transitions.cfg"
run_launcher "$WORK/transitions-owner.log" "plugin wiper: done at" || fail "the launcher exited early with transitions owning the slot"
log="$WORK/transitions-owner.log"
grep -qF "plugins: transitions picks the transition styles" "$log" || fail "transitions didn't take the transition slot"
grep -qF "plugin wiper: begin" "$log" && fail "wiper was asked without the slot"
grep -qF "transitions until the launcher restarts" "$log" && fail "transitions' transitions were turned off"
expect "done at $CARD/Wiper"
rm -rf "$WORK/plugins/transitions.plugin" "$WORK/plugins/wiper.plugin" "$STATE/transitions.cfg" "$STATE/wiper.cfg" \
	"$STATE/plugins.txt"
