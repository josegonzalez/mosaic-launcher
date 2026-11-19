# shellcheck shell=sh

# the bundled screensaver's random source and its logo and clock styles,
# with idle=1 and screener pressing the keys. The random source picks a
# game from the card index and screener's A closes it and launches that
# game, so the launcher quits to launch it. The logo style over the games
# source shows a fixture system's Tetris by its .media/wheel logo and
# Plain, which has none, by its name, changing each second, and B closes
# it. The full clock face shows no images, and the face over the dimmed
# images fades between a folder's two. The card's recents and the launch's
# /tmp files are put back afterwards
SY_ROMS="$CARD/Roms/Screensaver Styles (SST)"
SY_EMU="$CARD/Emus/desktop/SST.pak"
SY_DIR="$CARD/Screensaver Styles"
SY_SAVED="$WORK/screensaver-styles-saved"
rm -rf "$SY_SAVED"
mkdir -p "$SY_ROMS/.media/wheel" "$SY_EMU" "$SY_DIR" "$SY_SAVED" "$STATE" "$(dirname "$RECENTS")"
: >"$SY_EMU/launch.sh"
: >"$SY_ROMS/Tetris.gb"
: >"$SY_ROMS/Plain.gb"
cp "$ROOT/tests/plugins/peeker/art.png" "$SY_ROMS/.media/wheel/Tetris.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$SY_DIR/a.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$SY_DIR/b.png"
for saved in /tmp/next /tmp/last.txt /tmp/resume_slot.txt "$RECENTS"; do
	[ -f "$saved" ] && cp "$saved" "$SY_SAVED/$(basename "$saved")"
done
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" screensaver >/dev/null || fail "building screensaver"
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
plugin=screensaver
sy_wait=$WAIT

# the random source: A launches the game it picked
printf 'idle=1\nsource=random\ninterval=60\n' >"$STATE/screensaver.cfg"
printf 'steps=wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,a\n' >"$STATE/screener.cfg"
log="$WORK/screensaver-random.log"
WAIT=$((sy_wait + 10))
run_launcher "$log" "plugin screener: done" && fail "the launcher didn't quit to launch the screensaver's random game"
expect "opens after 1s idle on the list or home: random (box art), every 60s, fade, clock off"
expect "opened after 1s idle: random games"
picked=$(sed -n 's/.*plugin screensaver: picked \(.*\)/\1/p' "$log" | head -n 1 | sed 's/ (its name)$//')
[ -n "$picked" ] || fail "screensaver didn't pick a random game"
expect "closed by A: launching $picked"
grep -qF "plugin screensaver: couldn't launch" "$log" && fail "screensaver couldn't launch $picked"
rm -f /tmp/mosaic-launcher-launched

# the logo style over recents: Tetris' wheel, then Plain's name
printf '/Roms/Screensaver Styles (SST)/Tetris.gb\n/Roms/Screensaver Styles (SST)/Plain.gb\n' >"$RECENTS"
printf 'idle=1\nsource=games\ninterval=1\nstyle=logo\n' >"$STATE/screensaver.cfg"
printf 'steps=wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,b\n' >"$STATE/screener.cfg"
log="$WORK/screensaver-logo.log"
run_launcher "$log" "plugin screener: done" || fail "the launcher exited early with screensaver's logo style"
expect "style logo"
expect "opened after 1s idle: 2 images from games"
expect "showing 1 of 2: Tetris.png"
expect "changing to 2 of 2"
expect "showing 2 of 2: Plain.gb"
expect "closed by a button"

# the full clock face, with no images
printf 'idle=1\nsource=folder\nfolder=Screensaver Styles\nstyle=clock\n' >"$STATE/screensaver.cfg"
printf 'steps=wait,wait,wait,wait,wait,wait,wait,wait,b\n' >"$STATE/screener.cfg"
log="$WORK/screensaver-clock.log"
run_launcher "$log" "plugin screener: done" || fail "the launcher exited early with screensaver's clock style"
expect "style clock, face full"
expect "opened after 1s idle: the clock face"
expect "closed by a button"
grep -qF "plugin screensaver: showing" "$log" && fail "screensaver's full clock face showed an image"

# the face over the folder's images, which still fade
printf 'idle=1\nsource=folder\nfolder=Screensaver Styles\ninterval=1\nstyle=clock\nface=over\n' >"$STATE/screensaver.cfg"
printf 'steps=wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,b\n' >"$STATE/screener.cfg"
log="$WORK/screensaver-clock-over.log"
run_launcher "$log" "plugin screener: done" || fail "the launcher exited early with screensaver's clock over the images"
WAIT=$sy_wait
expect "style clock, face over"
expect "opened after 1s idle: 2 images from folder"
expect "fading to 2 of 2"
expect "closed by a button"

rm -f "$RECENTS" /tmp/next /tmp/last.txt /tmp/resume_slot.txt
for saved in /tmp/next /tmp/last.txt /tmp/resume_slot.txt "$RECENTS"; do
	[ -f "$SY_SAVED/$(basename "$saved")" ] && mv "$SY_SAVED/$(basename "$saved")" "$saved"
done
rm -rf "$SY_ROMS" "$SY_EMU" "$SY_DIR" "$SY_SAVED" "$WORK/plugins/screensaver.plugin" "$WORK/plugins/screener.plugin" \
	"$STATE/screener.cfg" "$STATE/screensaver.cfg"
case_log=""
