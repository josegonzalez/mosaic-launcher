# shellcheck shell=sh

# theme-plus's packs, rotation and picker: a system's icon and wallpapers
# come from a theme's own icons/ and wallpapers/ folders, a picked icon
# or wallpaper pack wins over them, and stock's own files on the card win
# over both, which packer asks art_find for. With theme stock and no
# packs nothing is supplied. Rotate start picks a theme from Rotate
# between and a different one the next start; daily picks the same one
# all day. Y opens the picker, which draws the preview, and A picks.
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" theme-plus >/dev/null || fail "building theme-plus"
build_plugin packer 'id=packer\nname=Packer\napi=1\n'
printf 'theme-plus=on\npacker=on\n' >"$STATE/plugins.txt"

TK_SYSTEM="Pack Test (PKT)"
TK_ROMS="$CARD/Roms/$TK_SYSTEM"
TK_THEME="$CARD/Themes/Packs"
TK_PNG="$ROOT/tests/screenshots/card/Roms/Game Boy (GB)/.media/Tobu Tobu Girl.png"
mkdir -p "$TK_ROMS" "$TK_THEME/wallpapers" "$TK_THEME/icons" "$CARD/Wallpapers/Grid" "$CARD/Icons/Round"
: >"$TK_ROMS/One.pkt"
for file in "$TK_THEME/wall.png" "$TK_THEME/wallpapers/PKT.png" "$TK_THEME/icons/PKT.png" "$CARD/Wallpapers/Grid/PKT.png" \
	"$CARD/Icons/Round/PKT.png"; do
	cp "$TK_PNG" "$file"
done
printf 'version=1\nname=Packs\ncolor1=0x335577\nwallpaper=wall.png\n' >"$TK_THEME/theme.txt"
# stock's bg.png would win over every system's wallpaper
[ -f "$CARD/bg.png" ] && mv "$CARD/bg.png" "$WORK/theme-packs-bg.png"
real() { (cd "$(dirname "$1")" && printf '%s/%s\n' "$(pwd -P)" "$(basename "$1")"); }

# tk_run <name> <config> [waits]: the launcher with theme-plus's config, until packer's done
tk_run() {
	log="$WORK/$1.log"
	printf '%b' "$2" >"$STATE/theme-plus.cfg"
	printf 'system=%s\ngame=One.pkt\n' "$TK_SYSTEM" >"$STATE/packer.cfg"
	shift 2
	run_launcher "$log" "plugin packer: done" "$@" || fail "the launcher exited early with theme-plus ($(basename "$log" .log))"
}
# tk_art <kind> <path or ->: packer found that art
tk_art() {
	if [ "$2" = "-" ]; then
		grep -qF "plugin packer: $1 0 -" "$log" || fail "theme-plus supplied a $1: $(grep "plugin packer: $1" "$log")"
	else
		grep -qF "plugin packer: $1 1 $2" "$log" || fail "the $1 wasn't $2: $(grep "plugin packer: $1" "$log")"
	fi
}

# stock's theme and no packs: nothing
tk_run packs-stock 'theme=stock\n' "plugin theme-plus: theme stock"
tk_art icon -
tk_art folder -
tk_art list -

# the theme's own
tk_run packs-theme 'theme=Packs\n' "plugin theme-plus: theme Packs (card)"
tk_art icon "$(real "$TK_THEME/icons/PKT.png")"
tk_art folder "$(real "$TK_THEME/wallpapers/PKT.png")"
tk_art list "$(real "$TK_THEME/wallpapers/PKT.png")"

# the packs win over it, and mix with stock's theme too
tk_run packs-picked 'theme=Packs\nwallpaper_pack=Grid\nicon_pack=Round\n' "plugin theme-plus: theme Packs (card)"
plugin=theme-plus
expect "wallpaper_pack Grid"
expect "icon_pack Round"
tk_art icon "$(real "$CARD/Icons/Round/PKT.png")"
tk_art folder "$(real "$CARD/Wallpapers/Grid/PKT.png")"
tk_art list "$(real "$CARD/Wallpapers/Grid/PKT.png")"
tk_run packs-stock-theme 'theme=stock\nicon_pack=Round\nwallpaper_pack=Nope\n' "plugin theme-plus: theme stock"
expect "wallpaper_pack: $CARD/Wallpapers/Nope isn't a folder, so none is used"
tk_art icon "$(real "$CARD/Icons/Round/PKT.png")"
tk_art folder -

# stock's own files on the card win over both
mkdir -p "$CARD/Roms/.media" "$TK_ROMS/.media"
cp "$TK_PNG" "$CARD/Roms/.media/$TK_SYSTEM.png"
cp "$TK_PNG" "$TK_ROMS/.media/bg.png"
tk_run packs-stock-wins 'theme=Packs\nwallpaper_pack=Grid\nicon_pack=Round\n' "plugin theme-plus: theme Packs (card)"
tk_art icon "$CARD/Roms/.media/$TK_SYSTEM.png"
tk_art folder "$TK_ROMS/.media/bg.png"
tk_art list "$(real "$CARD/Wallpapers/Grid/PKT.png")"
rm -rf "$CARD/Roms/.media/$TK_SYSTEM.png" "$TK_ROMS/.media"

# rotate start: one of the two, then the other the next start
tk_run packs-start 'theme=stock\nrotate=start\nrotate_between=nocturne, ember\n' "plugin theme-plus: rotate: "
plugin=theme-plus
second=""
first=$(sed -n 's/.*plugin theme-plus: rotate: \([a-z]*\), of 2$/\1/p' "$log" | head -n 1)
case "$first" in
nocturne) second=ember ;;
ember) second=nocturne ;;
*) fail "rotate start didn't pick nocturne or ember: $(grep "rotate" "$log")" ;;
esac
expect "theme $first (bundled)"
grep -qx "rotated=$first" "$STATE/theme-plus.cfg" || fail "theme-plus didn't save the rotation: $(cat "$STATE/theme-plus.cfg")"
log="$WORK/packs-start-again.log"
run_launcher "$log" "plugin packer: done" "plugin theme-plus: rotate: " || fail "the launcher exited early with theme-plus (packs-start-again)"
expect "rotate: $second, of 2"
expect "theme $second (bundled)"

# rotate daily: the day's one
tk_run packs-daily 'theme=stock\nrotate=daily\nrotate_between=mint\n' "plugin theme-plus: rotate: "
expect "rotate: mint for "
expect "theme mint (bundled)"

# the picker: Y opens it with the preview, down and A pick the next theme
: >"$WORK/theme-packs.texts"
texts="$WORK/theme-packs.texts"
PRESSER_KEYS="y down a" PRESSER_LOG="$texts" PRESSER_STEP=150 SMOKE_PRELOAD="$PRESSER"
export PRESSER_KEYS PRESSER_LOG PRESSER_STEP SMOKE_PRELOAD
tk_run packs-picker 'theme=stock\nbutton=Y\n' "presser: done" "plugin theme-plus: picked "
unset PRESSER_KEYS PRESSER_LOG PRESSER_STEP SMOKE_PRELOAD
drew "Theme"
drew "Stock"
drew "Sample row"
picked=$(sed -n 's/.*plugin theme-plus: picked //p' "$log" | head -n 1)
case "$picked" in
"" | stock) fail "the picker didn't pick the theme after Stock: $(grep "picked" "$log")" ;;
esac
grep -qx "theme=$picked" "$STATE/theme-plus.cfg" || fail "theme-plus didn't save the pick: $(cat "$STATE/theme-plus.cfg")"

[ -f "$WORK/theme-packs-bg.png" ] && mv "$WORK/theme-packs-bg.png" "$CARD/bg.png"
rm -rf "$WORK/plugins/theme-plus.plugin" "$WORK/plugins/packer.plugin" "$TK_ROMS" "$CARD/Themes/Packs" "$CARD/Wallpapers" "$CARD/Icons" \
	"$STATE/theme-plus.cfg" "$STATE/packer.cfg" "$STATE/plugins.txt"
rmdir "$CARD/Themes" 2>/dev/null || :
