# shellcheck shell=sh

# the bundled manuals, with row-badges and seeker pressing the keys: a
# test system's .manuals folder has Paged's pages (a 900x1200 sample page
# shrunk to fit as it's read, a 4x2 and an 8x8 one enlarged, and one that
# isn't an image), Saga's pages, which Saga (Disc 1) finds without its
# disc marker, and Texty's text. The launcher starts on Plain, which has
# none: the action isn't available and Y does nothing. On Paged, Y opens
# its pages in 1, 2, 10, 11 order; R1 turns them, A zooms to twice the
# size, down pans it and A fits it again, and the broken page shows it
# can't be shown. B closes it on page 3, and Y opens it there again, as
# it does after a restart. Then Saga's and Texty's, where down scrolls,
# and B and Y open the text where it was left. row-badges' manual mark
# has the three games once the card index is in. The card's /tmp/last.txt
# is put back afterwards.
MN_ROMS="$CARD/Roms/Manuals Test (MNT)"
MN_REL="/Roms/Manuals Test (MNT)"
MN_EMU="$CARD/Emus/desktop/MNT.pak"
MN_SHELF="$MN_ROMS/.manuals"
rm -rf "$MN_ROMS" "$STATE/manuals.txt"
mkdir -p "$MN_SHELF/Paged" "$MN_SHELF/Saga" "$MN_EMU" "$STATE"
: >"$MN_EMU/launch.sh"
for game in Paged.gb Plain.gb "Saga (Disc 1).gb" Texty.gb; do : >"$MN_ROMS/$game"; done
cp "$ROOT/tests/screenshots/manuals/Thwaite/01.png" "$MN_SHELF/Paged/1.png"
cp "$ROOT/tests/plugins/drawer/art.png" "$MN_SHELF/Paged/2.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$MN_SHELF/Paged/10.png"
printf 'not an image' >"$MN_SHELF/Paged/11.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$MN_SHELF/Saga/01.png"
i=1
while [ "$i" -le 60 ]; do
	printf 'Line %s of the text manual\n' "$i" >>"$MN_SHELF/Texty.txt"
	i=$((i + 1))
done
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/manuals-last.txt.saved"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" manuals row-badges >/dev/null \
	|| fail "building manuals and row-badges"
build_plugin seeker 'id=seeker\nname=Seeker\napi=1\n'

# the plugins named on, and every other plugin built so far off, as a
# plugin plugins.txt doesn't name loads
mn_plugins() {
	: >"$STATE/plugins.txt"
	for dir in "$WORK"/plugins/*.plugin; do
		id=$(basename "$dir" .plugin)
		case " $* " in
		*" $id "*) printf '%s=on\n' "$id" >>"$STATE/plugins.txt" ;;
		*) printf '%s=off\n' "$id" >>"$STATE/plugins.txt" ;;
		esac
	done
}

mn_plugins manuals row-badges seeker
printf 'manual=on\n' >"$STATE/row-badges.cfg"
printf '%s' "$MN_ROMS/Plain.gb" >/tmp/last.txt
printf 'steps=%s\n' "action:manuals.open,y,wait,up,y,r1,a,down,a,r1,r1,l1,b,y,b" >"$STATE/seeker.cfg"
log="$WORK/manuals.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early with manuals"
plugin=seeker
expect "run manuals.open -1"
# Y on Plain did nothing: the manual's screen opens only on Paged
got=$(sed -n 's/.*plugin seeker: screen //p' "$log" | tr '\n' ' ')
want="gamelist manuals.view gamelist manuals.view gamelist "
[ "$got" = "$want" ] || fail "manuals' screen showed \"$got\", not \"$want\""
plugin=manuals
expect "manual for $MN_ROMS/Paged.gb: $MN_SHELF/Paged"
expect "4 pages in $MN_SHELF/Paged, from page 1"
expect "read 1.png as 576x768 in "
# in order: the pages, the zoom and its pan, the broken page, back a page,
# and the same page when it's opened again
got=$(sed -n 's/.*plugin manuals: page //p' "$log" | tr '\n' '|')
want="1 of 4: 1.png, fitted to 576x768|2 of 4: 2.png, fitted to 1024x512|2 of 4: 2.png, zoomed to 2048x1024 at 512,128|"
want="${want}2 of 4: 2.png, zoomed to 2048x1024 at 512,256|2 of 4: 2.png, fitted to 1024x512|3 of 4: 10.png, fitted to 768x768|"
want="${want}4 of 4: 11.png, can't show it|3 of 4: 10.png, fitted to 768x768|3 of 4: 10.png, fitted to 768x768|"
[ "$got" = "$want" ] || fail "manuals showed \"$got\", not \"$want\""
grep -qF "plugins: manuals can't load $MN_SHELF/Paged/11.png: not a PNG or JPEG" "$log" || fail "the launcher didn't refuse the broken page"
expect "4 pages in $MN_SHELF/Paged, from page 3"
grep -qF "plugins: screen manuals.view's" "$log" && fail "manuals' screen was turned off"
plugin=row-badges
expect "manual mark on 3 games"
grep -qF "2	$MN_REL/Paged.gb" "$STATE/manuals.txt" || fail "manuals didn't keep Paged's page: $(cat "$STATE/manuals.txt")"

# after a restart Paged opens where it was left; then Saga, and Texty's
# text, scrolled and opened again where it was left
printf '%s' "$MN_ROMS/Paged.gb" >/tmp/last.txt
printf 'steps=%s\n' "y,b,down,down,y,b,down,y,down,down,b,y,b" >"$STATE/seeker.cfg"
log="$WORK/manuals-again.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early with manuals again"
plugin=manuals
expect "4 pages in $MN_SHELF/Paged, from page 3"
expect "page 3 of 4: 10.png, fitted to 768x768"
expect "manual for $MN_ROMS/Saga (Disc 1).gb: $MN_SHELF/Saga"
expect "page 1 of 1: 01.png, fitted to 768x768"
bytes=$(wc -c <"$MN_SHELF/Texty.txt" | tr -d ' ')
expect "text $MN_SHELF/Texty.txt, $bytes bytes, from line 1"
expect "text Texty.txt: lines 1 to "
expect "text Texty.txt: lines 3 to "
expect "text $MN_SHELF/Texty.txt, $bytes bytes, from line 3"
grep -qF "2	$MN_REL/Texty.gb" "$STATE/manuals.txt" || fail "manuals didn't keep Texty's line: $(cat "$STATE/manuals.txt")"

rm -rf "$MN_ROMS" "$MN_EMU"
rmdir "$CARD/Emus/desktop" 2>/dev/null
if [ -f "$WORK/manuals-last.txt.saved" ]; then mv "$WORK/manuals-last.txt.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
rm -rf "$WORK/plugins/manuals.plugin" "$WORK/plugins/row-badges.plugin" "$WORK/plugins/seeker.plugin" \
	"$STATE/seeker.cfg" "$STATE/plugins.txt" "$STATE/row-badges.cfg" "$STATE/manuals.txt"
case_log=""
