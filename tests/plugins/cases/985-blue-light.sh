# shellcheck shell=sh

# the bundled blue-light, read back by warmer: its toggle action tints the
# game list, the quick menu, a plugin screen and a MOSAIC_SCREEN_FULL
# ambient screen, over warmer's overlay, and the screenshot has the tint;
# toggling it again puts the screen back as it was. Then on a schedule
# from 23:00 to 01:00, red at 30%, with a fake clock: it comes on and
# goes off across midnight with no button pressed, and the toggle turns
# it off only until 01:00.
BL_SHOTS="$CARD/Screenshots"
BL_CLOCK="$WORK/blue-light-clock"
[ -e "$BL_SHOTS" ] && mv "$BL_SHOTS" "$WORK/blue-light-screenshots.saved"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" blue-light >/dev/null || fail "building blue-light"
build_plugin warmer 'id=warmer\nname=Warmer\napi=1\nambient=warmer.ambient\n'

mkdir -p "$STATE"

# blue-light and warmer on, and every other plugin built so far off
: >"$STATE/plugins.txt"
for dir in "$WORK"/plugins/*.plugin; do
	id=$(basename "$dir" .plugin)
	case "$id" in
	blue-light | warmer) printf '%s=on\n' "$id" >>"$STATE/plugins.txt" ;;
	*) printf '%s=off\n' "$id" >>"$STATE/plugins.txt" ;;
	esac
done
rm -f "$STATE/blue-light.cfg" "$BL_CLOCK"

# bl_saw <label> <colour>: warmer's probe <label> read <colour> off the screen
bl_saw() {
	grep -q "plugin warmer: probe $1 on [a-z.]* $2 " "$log" || fail "warmer's probe $1 wasn't $2"
}

# bl_png <file> <x> <y>: the pixel at x, y of a PNG, as RRGGBB
bl_png() {
	python3 -I - "$@" <<'EOF'
import struct, sys, zlib
data = open(sys.argv[1], "rb").read()
x, y = int(sys.argv[2]), int(sys.argv[3])
pos, idat = 8, b""
while pos < len(data):
    size, kind = struct.unpack(">I4s", data[pos:pos + 8])
    body = data[pos + 8:pos + 8 + size]
    if kind == b"IHDR":
        w, h, depth, colour = struct.unpack(">IIBB", body[:10])
    elif kind == b"IDAT":
        idat += body
    pos += 12 + size
bpp = {2: 3, 6: 4}[colour]
raw, stride, rows, prev = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp)
for r in range(h):
    f, line = raw[r * (stride + 1)], bytearray(raw[r * (stride + 1) + 1:(r + 1) * (stride + 1)])
    for i in range(stride):
        a = line[i - bpp] if i >= bpp else 0
        b, c = prev[i], prev[i - bpp] if i >= bpp else 0
        if f == 1: line[i] = (line[i] + a) & 255
        elif f == 2: line[i] = (line[i] + b) & 255
        elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
        elif f == 4:
            p = a + b - c
            pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
            line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
    rows.append(line)
    prev = line
px = rows[y][x * bpp:x * bpp + 3]
print("%02X%02X%02X" % (px[0], px[1], px[2]))
EOF
}

# by hand: amber at 50% is FFDF7F on white
printf 'steps=%s\n' "probe:before,action:blue-light.toggle,probe:list,capture:list,save,quick,wait,probe:quick,b,screen,wait,probe:screen,capture:screen,close,ambient,wait,probe:ambient,capture:ambient,unambient,action:blue-light.toggle,probe:after,capture:after" \
	>"$STATE/warmer.cfg"
# its steps take longer than the usual wait
bl_wait=$WAIT
WAIT=$((WAIT + 10))
log="$WORK/blue-light.log"
run_launcher "$log" "plugin warmer: done" || fail "the launcher exited early with blue-light"
plugin=blue-light
expect "ready, tint FFDF7F, not scheduled"
expect "tint on FFDF7F"
expect "tint off"
grep -qF "plugins: screen tint FFDF7F" "$log" || fail "the launcher didn't log the tint"
plugin=warmer
expect "ready"
bl_saw before FFFFFF
grep -q "plugin warmer: probe before on gamelist FFFFFF shown off" "$log" || fail "the game list had a tint before the toggle"
bl_saw list FFDF7F
grep -q "plugin warmer: probe list on gamelist FFDF7F shown FFDF7F" "$log" || fail "the game list wasn't tinted"
expect "capture list on gamelist FFDF7F"
grep -q "plugin warmer: probe quick on quickmenu FFDF7F shown FFDF7F" "$log" || fail "the quick menu wasn't tinted"
grep -q "plugin warmer: probe screen on warmer.screen FFDF7F shown FFDF7F" "$log" || fail "warmer's screen wasn't tinted"
expect "capture screen on warmer.screen FFDF7F"
grep -q "plugin warmer: probe ambient on warmer.ambient FFDF7F shown FFDF7F" "$log" || fail "the ambient screen wasn't tinted"
expect "capture ambient on warmer.ambient FFDF7F"
grep -q "plugin warmer: probe after on gamelist FFFFFF shown off" "$log" || fail "turning it off didn't put the game list back"
expect "capture after on gamelist FFFFFF"
# the screenshot as seen
shot=$(sed -n 's/.*plugin warmer: save 0 at \([0-9]* [0-9]*\) path \(.*\)$/\1 \2/p' "$log" | head -n 1)
if [ -z "$shot" ]; then
	fail "warmer didn't save a screenshot"
else
	# shellcheck disable=SC2086 # x, y and the path
	set -- $shot
	[ "$(bl_png "$3" "$1" "$2")" = "FFDF7F" ] || fail "the screenshot at $3 doesn't have the tint"
fi

# on a schedule across midnight, red at 30% (FFB2B2), on a fake clock in
# UTC: 22:59:50, 23:00:10, 00:30, then the toggle until 01:00, 01:00:05,
# and 23:00:05 the next day
printf 'schedule=on\nfrom=23:00\nto=01:00\ncolour=red\nstrength=30\n' >"$STATE/blue-light.cfg"
printf '169190\n' >"$BL_CLOCK"
printf 'steps=%s\n' "probe:early,time:169210,wait,wait,probe:start,time:174600,wait,wait,probe:midnight,action:blue-light.toggle,wait,probe:forced,time:176405,wait,wait,probe:end,time:255605,wait,wait,probe:next" \
	>"$STATE/warmer.cfg"
log="$WORK/blue-light-schedule.log"
TZ=UTC0 MOSAIC_BLUE_LIGHT_FAKE="$BL_CLOCK" run_launcher "$log" "plugin warmer: done" \
	|| fail "the launcher exited early with blue-light's schedule"
plugin=blue-light
expect "ready, tint FFB2B2, scheduled"
expect "tint on FFB2B2"
expect "forced off until the schedule's next edge"
expect "the toggle's forcing ended at the schedule's edge"
bl_saw early FFFFFF
bl_saw start FFB2B2
bl_saw midnight FFB2B2
bl_saw forced FFFFFF
bl_saw end FFFFFF
bl_saw next FFB2B2
grep -qF "forced=" "$STATE/blue-light.cfg" || fail "blue-light didn't keep its forcing in its cfg"

WAIT=$bl_wait

rm -rf "$BL_SHOTS" "$WORK/plugins/blue-light.plugin" "$WORK/plugins/warmer.plugin"
rm -f "$BL_CLOCK" "$STATE/blue-light.cfg" "$STATE/warmer.cfg" "$STATE/plugins.txt"
[ -e "$WORK/blue-light-screenshots.saved" ] && mv "$WORK/blue-light-screenshots.saved" "$BL_SHOTS"
case_log=""
