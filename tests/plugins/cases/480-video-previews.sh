# shellcheck shell=sh
# shard: 1

# the bundled video-previews plays a game's .videos clip a second (its
# delay setting) after the selection settles, in place of the thumbnail,
# with its sound through SDL's dummy driver, and stops when the selection
# moves, going round again at each clip's end: Alpha's half-second clip
# has sound, Bravo's has none, Charlie's isn't a clip and Delta has none.
# clipper walks the selection down the folder. Under a plugin layout that
# asks for it, the layout draws the clip in its own art spot.
# Built without ffmpeg's libraries, it only says it's unavailable; the
# clips are made with the ffmpeg command, and without it the cases that
# play them are skipped
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" video-previews >/dev/null || fail "building video-previews"
VP_DIR="$CARD/Roms/Video Previews (VPT)"
mkdir -p "$VP_DIR/.videos" "$STATE"
for game in Alpha Bravo Charlie Delta; do : >"$VP_DIR/$game.gb"; done
printf 'not a clip\n' >"$VP_DIR/.videos/Charlie.mp4"
build_plugin clipper 'id=clipper\nname=Clipper\napi=1\n'
printf 'folder=Roms/Video Previews (VPT)\nstep=3000\n' >"$STATE/clipper.cfg"
printf 'delay=1\n' >"$STATE/video-previews.cfg"
printf 'video-previews=on\nclipper=on\n' >"$STATE/plugins.txt"
vp_clips=0
if ! pkg-config --exists libavformat libavcodec libavutil 2>/dev/null; then
	SDL_AUDIODRIVER=dummy run_launcher "$WORK/video-previews.log" "plugin video-previews: unavailable" \
		|| fail "the launcher exited early with video-previews loaded"
	log="$WORK/video-previews.log"
	plugin=video-previews
	expect "unavailable: built without ffmpeg, so no clip plays"
elif ! command -v ffmpeg >/dev/null; then
	echo "note: skipping video-previews' clips: no ffmpeg command to make them with" >&2
elif ffmpeg -v error -y -f lavfi -i "testsrc=size=64x48:rate=30" -f lavfi -i "sine=frequency=440:sample_rate=48000" -t 0.5 \
	-c:v libx264 -pix_fmt yuv420p -c:a aac -ac 2 -shortest "$VP_DIR/.videos/Alpha.mp4" </dev/null \
	&& ffmpeg -v error -y -f lavfi -i "testsrc=size=64x48:rate=30" -t 0.5 -c:v libx264 -pix_fmt yuv420p -an \
		"$VP_DIR/.videos/Bravo.mp4" </dev/null; then
	vp_clips=1
else
	echo "note: skipping video-previews' clips: the ffmpeg command couldn't make them" >&2
fi
if [ "$vp_clips" -eq 1 ]; then
	vp_wait=$WAIT
	WAIT=$((WAIT + 10))
	SDL_AUDIODRIVER=dummy run_launcher "$WORK/video-previews.log" "plugin clipper: done" \
		|| fail "the launcher exited early with video-previews loaded"
	WAIT=$vp_wait
	log="$WORK/video-previews.log"
	plugin=video-previews
	expect "clips play 1000 ms after the selection settles, with sound"
	expect "loaded ffmpeg "
	expect "playing Alpha.mp4: 64x48, with sound"
	grep -qF "plugins: video-previews opened audio at 48000 Hz with 2 channels (dummy)" "$log" || fail "video-previews didn't open audio"
	expect "Bravo.mp4 has no sound"
	expect "playing Bravo.mp4: 64x48, silent"
	expect "can't open Charlie.mp4"
	for game in Alpha Bravo; do
		frames=$(sed -n "s|.*plugin video-previews: stopped $VP_DIR/$game.gb after showing \([0-9]*\) frames.*|\1|p" "$log")
		if [ -z "$frames" ]; then
			fail "video-previews didn't stop $game's clip when the selection moved"
		elif [ "$frames" -lt 5 ]; then
			fail "video-previews showed only $frames frames of $game's clip"
		fi
		passes=$(sed -n "s|.*plugin video-previews: stopped $game.mp4 after [0-9]* frames, \([0-9]*\) passes.*|\1|p" "$log")
		[ "${passes:-0}" -ge 2 ] || fail "video-previews didn't go round $game's clip again: ${passes:-no} passes"
	done
	grep -qF "plugin video-previews: stopped $VP_DIR/Charlie.gb" "$log" && fail "video-previews played Charlie's broken clip"
	grep -F "plugin video-previews: " "$log" | grep -qF "Delta" && fail "video-previews played a clip Delta doesn't have"
	grep -qF "plugins: video-previews's" "$log" && fail "video-previews' hooks were turned off"
	grep -qF "art_replace refused" "$log" && fail "art_replace refused video-previews on the game list"

	# under a plugin layout that asks for it, the layout draws the clip in
	# its own art spot: gridder with swap=1 draws it in the selected cell
	mkdir -p "$CARD/Gridder/.videos"
	cp "$VP_DIR/.videos/Alpha.mp4" "$CARD/Gridder/.videos/Apple.mp4"
	cp "$VP_DIR/.videos/Alpha.mp4" "$CARD/Gridder/.videos/Banana.mp4"
	build_plugin gridder 'id=gridder\nname=Gridder\napi=1\nslots=layout\n'
	printf 'swap=1\n' >"$STATE/gridder.cfg"
	printf 'gridder=on\nvideo-previews=on\n' >"$STATE/plugins.txt"
	SDL_AUDIODRIVER=dummy run_launcher "$WORK/video-previews-grid.log" "plugin gridder: renders" \
		"plugin video-previews: playing " || fail "the launcher exited early with video-previews under gridder"
	log="$WORK/video-previews-grid.log"
	grep -qE "plugin video-previews: playing (Alpha|Apple|Banana).mp4: 64x48, with sound" "$log" \
		|| fail "video-previews didn't play under a layout: $(grep "plugin video-previews: " "$log")"
	grep -qE "plugin gridder: render replacement [A-Za-z]+ 1 " "$log" \
		|| fail "gridder didn't draw video-previews' clip: $(grep "plugin gridder: render replacement" "$log")"
	grep -qF "art_replace refused" "$log" && fail "art_replace refused video-previews under a layout that shows it"
	grep -qF "plugins: gridder's" "$log" && fail "gridder's layout was turned off with video-previews"
	rm -rf "$WORK/plugins/gridder.plugin" "$CARD/Gridder" "$STATE/gridder.cfg"
fi
rm -rf "$WORK/plugins/video-previews.plugin" "$WORK/plugins/clipper.plugin" "$VP_DIR" \
	"$STATE/video-previews.cfg" "$STATE/clipper.cfg" "$STATE/plugins.txt"
