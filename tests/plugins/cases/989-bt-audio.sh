# shellcheck shell=sh

# the bundled header-indicators shows a headset, last in its pill, while
# audio_sink says the sound goes to Bluetooth. Desktop has none, so
# MOSAIC_LAUNCHER_AUDIO_SINK_FAKE stands audiomon's .asoundrc in for it:
# Bluetooth from the start, Bluetooth once the launcher is running, a USB
# DAC, the setting off, and no stand-in at all
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" header-indicators >/dev/null || fail "building header-indicators"
mkdir -p "$STATE"
printf 'header-indicators=on\n' >"$STATE/plugins.txt"
BA_FAKE="$WORK/bt-audio.asoundrc"
BA_BLUETOOTH='defaults.bluealsa.device "AA:BB:CC:DD:EE:FF"

pcm.!default {
    type plug
    slave.pcm {
        type bluealsa
        device "AA:BB:CC:DD:EE:FF"
        profile "a2dp"
    }
}
ctl.!default {
    type bluealsa
}
'
MOSAIC_LAUNCHER_AUDIO_SINK_FAKE="$BA_FAKE"
export MOSAIC_LAUNCHER_AUDIO_SINK_FAKE

# Bluetooth from the start, on its own
printf '%s' "$BA_BLUETOOTH" >"$BA_FAKE"
printf 'storage=off\n' >"$STATE/header-indicators.cfg"
log="$WORK/bt-audio.log"
run_launcher "$log" "plugins: audio sink is bluetooth" "plugin header-indicators: widget at " \
	|| fail "the launcher exited early with a Bluetooth sink"
grep -qE "plugin header-indicators: widget at [0-9]+,15 [0-9]+x90: audio$" "$log" \
	|| fail "header-indicators didn't show only the headset: $(grep "plugin header-indicators: widget" "$log")"

# Bluetooth once the launcher is running: the headset joins free storage,
# after it
printf '' >"$BA_FAKE"
printf 'ssh=on\n' >"$STATE/header-indicators.cfg"
log="$WORK/bt-audio-live.log"
rm -f "$log"
(
	i=0
	while [ "$i" -lt 600 ] && ! grep -qF "plugin header-indicators: widget at " "$log" 2>/dev/null; do
		sleep 0.1
		i=$((i + 1))
	done
	printf '%s' "$BA_BLUETOOTH" >"$BA_FAKE.tmp"
	mv "$BA_FAKE.tmp" "$BA_FAKE"
) &
ba_writer=$!
run_launcher "$log" "plugins: audio sink is bluetooth" ": storage audio" \
	|| fail "the launcher exited early with the sink turning Bluetooth"
wait "$ba_writer"
if ! { grep -qE "plugin header-indicators: widget at [0-9]+,15 [0-9]+x90: storage$" "$log" \
	&& grep -qE "plugin header-indicators: widget at [0-9]+,15 [0-9]+x90: storage audio$" "$log"; }; then
	fail "header-indicators didn't add the headset after free storage: $(grep "plugin header-indicators: widget" "$log")"
fi

# a USB DAC isn't Bluetooth
printf 'pcm.!default {\n    type hw\n    card 1\n}\n' >"$BA_FAKE"
log="$WORK/bt-audio-usb.log"
run_launcher "$log" "plugins: audio sink is usb" "plugin header-indicators: widget at " \
	|| fail "the launcher exited early with a USB sink"
grep "plugin header-indicators: widget" "$log" | grep -qF "audio" \
	&& fail "header-indicators showed the headset for a USB DAC: $(grep "plugin header-indicators: widget" "$log")"

# the setting off
printf '%s' "$BA_BLUETOOTH" >"$BA_FAKE"
printf 'bt_audio=off\n' >"$STATE/header-indicators.cfg"
log="$WORK/bt-audio-off.log"
run_launcher "$log" "plugins: audio sink is bluetooth" "plugin header-indicators: widget at " \
	|| fail "the launcher exited early with the headset off"
grep "plugin header-indicators: widget" "$log" | grep -qF "audio" \
	&& fail "header-indicators showed the headset with it off: $(grep "plugin header-indicators: widget" "$log")"

# no stand-in: desktop has no sink
unset MOSAIC_LAUNCHER_AUDIO_SINK_FAKE
rm -f "$STATE/header-indicators.cfg"
log="$WORK/bt-audio-none.log"
run_launcher "$log" "plugin header-indicators: widget at " || fail "the launcher exited early with no sink"
grep -qF "plugins: audio sink is" "$log" && fail "desktop had an audio sink: $(grep "plugins: audio sink is" "$log")"
grep "plugin header-indicators: widget" "$log" | grep -qF "audio" \
	&& fail "header-indicators showed the headset with no sink: $(grep "plugin header-indicators: widget" "$log")"
rm -rf "$WORK/plugins/header-indicators.plugin" "$STATE/header-indicators.cfg" "$STATE/plugins.txt" "$BA_FAKE"
