# shellcheck shell=sh
# shard: 3

# speaker plays audio through the host's audio calls with SDL's dummy
# driver: refusals before it opens, a queue from its own thread that falls
# as it plays and holds while paused, the queue's cap, closing, the
# main-thread-only calls refused from its thread, a reopen in another
# format, a thread queueing while the device opens and closes, and the
# launcher closing the device itself before a sleep (once what's queued
# plays out), before a launch and at quit; SDL's audio is started only
# while the device is open (until a sleep, after which stock starts it
# itself). hearer is refused the device meanwhile. A case with no sleep
# checks the launch and quit again, for SDL2's dummy driver
SPEAK_DIR="$CARD/Speaker"
SPEAK_PAK="$SPEAK_DIR/Spoken.pak"
mkdir -p "$SPEAK_PAK" "$STATE"
: >"$SPEAK_PAK/launch.sh"
[ -f /tmp/next ] && cp /tmp/next "$WORK/speaker-next.saved"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/speaker-last.saved"
build_plugin speaker 'id=speaker\nname=Speaker\napi=1\n'
build_plugin hearer 'id=hearer\nname=Hearer\napi=1\n'
printf 'speaker=on\nhearer=on\n' >"$STATE/plugins.txt"
printf 'pak=%s\n' "$SPEAK_PAK" >"$STATE/speaker.cfg"
SDL_AUDIODRIVER=dummy run_launcher "$WORK/speaker.log" && fail "the launcher didn't quit to launch speaker's pak"
log="$WORK/speaker.log"
plugin=speaker
expect "sdl audio before open 0"
expect "before open: queue -1, queued -1, pause -1, close -1"
expect "bad rate -1, bad channels -1, unknown id -1, max ms 1000"
expect "open 0"
expect "open again 0, queued 0, empty queue 0, sdl audio 1"
expect "thread queued 37 of 37 chunks, null -1, over -1, queued 1"
expect "queued fell 1"
expect "pause 0"
expect "paused held 1"
expect "unpause 0"
expect "playing again 1"
expect "close 0, again -1, queued after -1, queue after -1, sdl audio 0"
expect "off main open -1, pause -1, close -1, queued -1"
expect "open 0, other format 0"
expect "race cycles 1, calls 1, ok 1, refused 1"
expect "queued for sleep 0"
expect "before_sleep queued 1"
expect "after_resume queued -1"
# SDL2's own dummy driver opens one device at a time (sdl2-compat's opens
# any number), and stock opens its own on waking, so there speaker's reopen
# is refused and the launch case below checks the closes instead
one_device=0
grep -qF "plugins: speaker can't open audio: Audio device already open" "$log" && one_device=1
if [ "$one_device" -eq 1 ]; then
	expect "open after sleep -1"
else
	expect "open after sleep 0"
	expect "open after launch 0"
fi
expect "open pak 1"
expect "after launch queued -1"
expect "quit queued -1"
plugin=hearer
expect "open while taken -1, queue -1, queued -1, close -1"
grep -qF "plugins: speaker opened audio at 48000 Hz with 2 channels (dummy)" "$log" || fail "speaker's open wasn't logged"
grep -qF "plugins: hearer can't open audio, speaker has it" "$log" || fail "hearer's refusal wasn't logged"
grep -qF "plugins: speaker can't open audio at 1000 Hz with 2 channels" "$log" || fail "the bad rate wasn't logged"
[ "$(grep -cF "plugins: speaker queued more than 1000 ms of audio" "$log")" -eq 1 ] || fail "the queue's cap wasn't logged once an open"
grep -qF "plugins: speaker called audio_open off the main thread" "$log" || fail "the open off the main thread wasn't logged"
grep -qF "plugins: speaker reopened audio at 22050 Hz with 1 channel (dummy)" "$log" || fail "the reopen wasn't logged"
grep -qF "plugins: speaker closed audio" "$log" || fail "speaker's close wasn't logged"
drained=$(sed -n "s/.*plugins: closed speaker's audio before sleep, after \([0-9]*\) ms for it to play out.*/\1/p" "$log")
if [ -z "$drained" ]; then
	fail "the launcher didn't close speaker's audio before sleep"
elif [ "$drained" -lt 300 ] || [ "$drained" -gt 1100 ]; then
	fail "the launcher waited $drained ms for speaker's audio to play out, not most of 0.6 s"
fi
# speaker_closes: the launcher closed speaker's audio before the launch in
# $log, and at quit before speaker's quit
speaker_closes() {
	launch_line=$(grep -nF "plugins: closed speaker's audio before a launch" "$log" | cut -d: -f1)
	cmd_line=$(grep -nF "cmd: '" "$log" | cut -d: -f1)
	if [ -z "$launch_line" ] || [ -z "$cmd_line" ] || [ "$launch_line" -gt "$cmd_line" ]; then
		fail "the launcher didn't close speaker's audio before the launch"
	fi
	quit_line=$(grep -nF "plugin speaker: quit" "$log" | cut -d: -f1)
	closed_line=$(grep -nF "plugins: closed speaker's audio at quit" "$log" | cut -d: -f1)
	if [ -z "$quit_line" ] || [ -z "$closed_line" ] || [ "$closed_line" -gt "$quit_line" ]; then
		fail "the launcher didn't close speaker's audio at quit, before its quit"
	fi
}
[ "$one_device" -eq 1 ] || speaker_closes
# (Linux's killall says so when stock stops an audiomon that isn't running)
grep -i "audio" "$WORK/hello.log" | grep -qvF "audiomon.elf: no process found" && fail "audio was touched with no plugin asking"

# with no sleep first, speaker opens, launches a pak, which closes its
# device, opens it again, and the launcher's quit closes that
printf 'speaker=on\n' >"$STATE/plugins.txt"
printf 'mode=launch\npak=%s\n' "$SPEAK_PAK" >"$STATE/speaker.cfg"
SDL_AUDIODRIVER=dummy run_launcher "$WORK/speaker-launch.log" && fail "the launcher didn't quit to launch speaker's pak"
log="$WORK/speaker-launch.log"
plugin=speaker
expect "open 0, open pak 1, after launch queued -1"
expect "open after launch 0"
expect "quit queued -1"
speaker_closes

# a plugin that declines to start has its device closed, and so does
# powering off; /tmp/next and /tmp/last.txt are put back afterwards
printf 'speaker=on\n' >"$STATE/plugins.txt"
printf 'mode=decline\n' >"$STATE/speaker.cfg"
SDL_AUDIODRIVER=dummy run_launcher "$WORK/speaker-decline.log" "plugins: speaker declined to start" \
	|| fail "the launcher exited early after speaker declined"
log="$WORK/speaker-decline.log"
plugin=speaker
expect "open in init 0, declining"
grep -qF "plugins: closed speaker's audio as it declined to start" "$log" || fail "a declined plugin's audio stayed open"
printf 'mode=poweroff\n' >"$STATE/speaker.cfg"
SDL_AUDIODRIVER=dummy run_launcher "$WORK/speaker-poweroff.log" && fail "power_off didn't quit the launcher with speaker"
log="$WORK/speaker-poweroff.log"
expect "open 0, powering off"
grep -qF "plugins: closed speaker's audio before powering off" "$log" || fail "the launcher didn't close speaker's audio before powering off"
rm -rf "$WORK/plugins/speaker.plugin" "$WORK/plugins/hearer.plugin" "$SPEAK_DIR" "$STATE/speaker.cfg" "$STATE/hearer.cfg" \
	"$STATE/plugins.txt"
if [ -f "$WORK/speaker-next.saved" ]; then cp "$WORK/speaker-next.saved" /tmp/next; else rm -f /tmp/next; fi
if [ -f "$WORK/speaker-last.saved" ]; then cp "$WORK/speaker-last.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
rm -f "$WORK/speaker-next.saved" "$WORK/speaker-last.saved"
