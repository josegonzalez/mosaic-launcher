# shellcheck shell=sh

# the bundled screenshot-viewer's media of a game, with video-previews'
# test frames for the clip and presser pressing the keys: the launcher
# starts on Alpha, whose media are its .media thumbnail, its boxart and
# wheel, its Imgs picture, its screenshot and its clip (any file:
# video-previews' test frames don't read it); a link to a file in
# .media/mix, a linked .media/screenshot folder and a macOS ._ file aren't
# shown. Y opens them; right steps through to the clip, which plays on
# video-previews' screen with its file as a toast, right there wraps to
# the first, left goes back to the clip and B closes both. Then the
# Screenshots tile's grid: X on Alpha's shot opens Alpha's media, and X on
# a shot no game is named after shows that shot alone. With video-previews
# off, the clip is left out. The card's Screenshots and /tmp/last.txt are
# put back afterwards.
MD_ROMS="$CARD/Roms/Media Test (MDT)"
MD_EMU="$CARD/Emus/desktop/MDT.pak"
MD_SHOTS="$CARD/Screenshots"
MD_OUTSIDE="$WORK/media-outside"
MD_ART="$ROOT/tests/plugins/peeker/art.png"
rm -rf "$MD_ROMS" "$MD_OUTSIDE" "$WORK/media-screenshots.saved"
[ -e "$MD_SHOTS" ] && mv "$MD_SHOTS" "$WORK/media-screenshots.saved"
mkdir -p "$MD_ROMS/.media/boxart" "$MD_ROMS/.media/wheel" "$MD_ROMS/.media/mix" "$MD_ROMS/Imgs" "$MD_ROMS/.videos" \
	"$MD_EMU" "$MD_SHOTS" "$MD_OUTSIDE/screenshot" "$STATE"
: >"$MD_EMU/launch.sh"
: >"$MD_ROMS/Alpha.gb"
: >"$MD_ROMS/Plain.gb"
cp "$MD_ART" "$MD_ROMS/.media/Alpha.png"
cp "$MD_ART" "$MD_ROMS/.media/boxart/Alpha.png"
cp "$MD_ART" "$MD_ROMS/.media/wheel/Alpha.png"
cp "$MD_ART" "$MD_ROMS/.media/wheel/._Alpha.png"
cp "$MD_ART" "$MD_ROMS/Imgs/Alpha.png"
printf 'not a clip' >"$MD_ROMS/.videos/Alpha.mp4"
cp "$MD_ART" "$MD_OUTSIDE/Secret.png"
cp "$MD_ART" "$MD_OUTSIDE/screenshot/Alpha.png"
ln -s "$MD_OUTSIDE/Secret.png" "$MD_ROMS/.media/mix/Alpha.png"
ln -s "$MD_OUTSIDE/screenshot" "$MD_ROMS/.media/screenshot"
cp "$MD_ART" "$MD_SHOTS/Alpha.2026-11-09-08-30-00.png"
cp "$MD_ART" "$MD_SHOTS/Nobody.2026-11-01-00-00-00.png"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/media-last.saved"
printf '%s' "$MD_ROMS/Alpha.gb" >/tmp/last.txt
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" screenshot-viewer video-previews >/dev/null \
	|| fail "building screenshot-viewer and video-previews"

# the plugins named on, and every other plugin built so far off, as a
# plugin plugins.txt doesn't name loads
md_plugins() {
	: >"$STATE/plugins.txt"
	for dir in "$WORK"/plugins/*.plugin; do
		id=$(basename "$dir" .plugin)
		case " $* " in
		*" $id "*) printf '%s=on\n' "$id" >>"$STATE/plugins.txt" ;;
		*) printf '%s=off\n' "$id" >>"$STATE/plugins.txt" ;;
		esac
	done
}

md_plugins screenshot-viewer video-previews
texts="$WORK/media-viewer.texts"
: >"$texts"
MOSAIC_VIDEO_PREVIEWS_FAKE=1
PRESSER_KEYS="y wait wait right right right right right wait wait wait right wait left wait wait wait b wait menu left a wait wait x wait wait b right x wait wait b b"
PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER"
export MOSAIC_VIDEO_PREVIEWS_FAKE PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
# the keys take longer than the usual wait
md_wait=$WAIT
WAIT=$((WAIT + 15))
run_launcher "$WORK/media-viewer.log" "presser: done" || fail "the launcher exited early with screenshot-viewer's media"
WAIT=$md_wait
unset MOSAIC_VIDEO_PREVIEWS_FAKE PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
log="$WORK/media-viewer.log"
plugin=screenshot-viewer
expect "6 media for Alpha ($MD_ROMS/Alpha.gb)"
# in order: each one, then the clip, right wrapping to the first and left
# back to the clip, whatever its image or clip was doing when the next
# press came; the clip played at least once
got=$(sed -n 's/.*plugin screenshot-viewer: media //p' "$log" | sed 's/, image [a-z]*$//; s/, clip .*$//' | uniq | tr '\n' '|')
want="0 of 6: .media/Alpha.png|1 of 6: .media/boxart/Alpha.png|2 of 6: .media/wheel/Alpha.png|3 of 6: Imgs/Alpha.png|"
want="${want}4 of 6: Screenshots/Alpha.2026-11-09-08-30-00.png|5 of 6: .videos/Alpha.mp4|0 of 6: .media/Alpha.png|"
want="${want}5 of 6: .videos/Alpha.mp4|"
case "$got" in
"$want"*) ;;
*) fail "the media showed \"$got\", not \"$want\" first" ;;
esac
expect "media 5 of 6: .videos/Alpha.mp4, clip playing"
plugin=video-previews
expect "playing Alpha.mp4 for the screensaver as test frames"
expect "stopped $MD_ROMS/.videos/Alpha.mp4 after showing"
plugin=screenshot-viewer
expect "2 screenshots in $MD_SHOTS"
expect "grid 0 of 2: Alpha, 2026-11-09 08:30, "
[ "$(grep -c "plugin screenshot-viewer: 6 media for Alpha ($MD_ROMS/Alpha.gb)" "$log")" -eq 2 ] \
	|| fail "X on Alpha's shot didn't open its media"
expect "grid 1 of 2: Nobody, 2026-11-01 00:00, "
expect "1 media for Nobody (no game)"
expect "media 0 of 1: Screenshots/Nobody.2026-11-01-00-00-00.png, image "
for text in "1/6 · .media/Alpha.png" "4/6 · Imgs/Alpha.png" ".videos/Alpha.mp4" MEDIA; do
	grep -qxF -- "$text" "$texts" || fail "screenshot-viewer didn't draw \"$text\""
done
grep -qF "Alpha.png, image none" "$log" && fail "screenshot-viewer couldn't show one of Alpha's images"
grep -qF ".media/mix/" "$log" && fail "screenshot-viewer showed the link in .media/mix"
grep -qF ".media/screenshot/" "$log" && fail "screenshot-viewer showed the linked .media/screenshot"
grep -qF "._Alpha" "$log" && fail "screenshot-viewer showed the ._ file"
grep -qF "plugins: screenshot-viewer's screen screenshot-viewer.media-view's" "$log" && fail "the media screen was turned off"
grep -qF "plugins: screenshot-viewer's input_filter" "$log" && fail "screenshot-viewer's input_filter was turned off"
if [ ! -L "$MD_ROMS/.media/mix/Alpha.png" ] || [ ! -f "$MD_OUTSIDE/Secret.png" ]; then
	fail "screenshot-viewer touched the link"
fi

# video-previews off: the clip is left out
md_plugins screenshot-viewer
PRESSER_KEYS="y wait wait left wait wait b" PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER"
export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
run_launcher "$WORK/media-viewer-off.log" "presser: done" || fail "the launcher exited early with screenshot-viewer's media, video-previews off"
unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
log="$WORK/media-viewer-off.log"
plugin=screenshot-viewer
expect "5 media for Alpha ($MD_ROMS/Alpha.gb)"
expect "media 4 of 5: Screenshots/Alpha.2026-11-09-08-30-00.png, image "
grep -qF ".videos/" "$log" && fail "screenshot-viewer showed the clip with video-previews off"

rm -rf "$MD_ROMS" "$MD_EMU" "$MD_SHOTS" "$MD_OUTSIDE" "$texts"
[ -e "$WORK/media-screenshots.saved" ] && mv "$WORK/media-screenshots.saved" "$MD_SHOTS"
if [ -f "$WORK/media-last.saved" ]; then mv "$WORK/media-last.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
rm -rf "$WORK/plugins/screenshot-viewer.plugin" "$WORK/plugins/video-previews.plugin"
case_log=""
