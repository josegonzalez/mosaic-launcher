# shellcheck shell=sh
# shard: 1

# sayer owns the strings slot and translates Recently Played and the Game
# Boy system: the root shows them translated, in stock's order, with their
# paths English, so the Game Boy row's art (Roms/.media/Game Boy (GB).png)
# still loads, and a map.txt name stays the user's. The bundled
# quick-menu-plus's toast and theme-plus's choose show the text the
# plugins translated with host->tr. presser writes down the text drawn.
# Without the slot the root is stock's English.
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
build_plugin sayer 'id=sayer\nname=Sayer\napi=1\nslots=strings\n'
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" quick-menu-plus theme-plus >/dev/null \
	|| fail "building quick-menu-plus and theme-plus"
RN_GB="$CARD/Roms/Game Boy (GB)"
RN_GBC="$CARD/Roms/Game Boy Color (GBC)"
RN_MAP="$CARD/Roms/map.txt"
mkdir -p "$RN_GB" "$RN_GBC" "$CARD/Roms/.media" "$(dirname "$RECENTS")" "$STATE"
: >"$RN_GB/RootNames.gb"
: >"$RN_GBC/RootNames.gbc"
cp "$ROOT/tests/plugins/peeker/art.png" "$CARD/Roms/.media/Game Boy (GB).png"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/recent.txt.saved"
[ -f "$RN_MAP" ] && cp "$RN_MAP" "$WORK/map.txt.saved"
echo "/Roms/Game Boy (GB)/RootNames.gb" >"$RECENTS"
printf 'Game Boy Color (GBC)\tPocket Colour\n' >"$RN_MAP"
printf 'sayer=on\nquick-menu-plus=on\ntheme-plus=on\n' >"$STATE/plugins.txt"
printf 'mode=root\n' >"$STATE/sayer.cfg"
rm -f "$STATE/quick-menu-plus.cfg" "$STATE/theme-plus.cfg"
plugin=sayer
texts="$WORK/root-names-texts.txt"

# rn_run <name>: the launcher with sayer, until it's done with the root and
# presser has seen a few frames
rn_run() {
	log="$WORK/$1.log"
	# quick-menu-plus starts awake when the last run left it so
	rm -f "$texts" /tmp/quick-menu-plus.awake
	PRESSER_KEYS="wait wait wait wait wait wait wait wait wait wait" PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER"
	export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	run_launcher "$log" "$2" "presser: done" || fail "the launcher exited early with sayer ($1)"
	unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
}

rn_run root-names "plugin sayer: root done"
expect "root row Joués récemment at $CARD/Recently Played"
expect "root row Vieille Game Boy at $RN_GB"
expect "root row Pocket Colour at $RN_GBC"
expect "root art for Vieille Game Boy: ready, $CARD/Roms/.media/Game Boy (GB).png"
# stock's order, by the English names (Game Boy before Pocket Colour), not
# the shown ones
sed -n 's/.*plugin sayer: root row \(.*\) at .*/\1/p' "$log" | grep -xE "Joués récemment|Vieille Game Boy|Pocket Colour" \
	| tr '\n' ',' >"$WORK/root-names-order.txt"
[ "$(cat "$WORK/root-names-order.txt")" = "Joués récemment,Vieille Game Boy,Pocket Colour," ] \
	|| fail "the root's order changed: $(cat "$WORK/root-names-order.txt")"
expect "asked - systems Game Boy"
grep -qF "plugin sayer: asked - systems Game Boy Color" "$log" && fail "a map.txt name was translated"
expect "listed quick-menu-plus at $WORK/plugins/quick-menu-plus.plugin/res"
expect "listed theme-plus at $WORK/plugins/theme-plus.plugin/res"
drew "Joués récemment"
drew "Vieille Game Boy"
expect "ran quick-menu-plus.awake 0"
drew "Reste éveillé"
expect "asked quick-menu-plus - Staying awake"
expect "ran theme-plus.pick 0"
drew "Thème"
drew "D'origine"
grep -qxF "Recently Played" "$texts" && fail "Recently Played was drawn with a translation for it"

# without the slot, stock's English
printf 'id=sayer\nname=Sayer\napi=1\n' >"$WORK/plugins/sayer.plugin/plugin.txt"
rn_run root-names-unslotted "plugin sayer: root done"
expect "root row Recently Played at $CARD/Recently Played"
expect "root row Game Boy at $RN_GB"
expect "root art for Game Boy: ready, $CARD/Roms/.media/Game Boy (GB).png"
drew "Staying awake"
drew "Stock"

rm -rf "$WORK/plugins/sayer.plugin" "$WORK/plugins/quick-menu-plus.plugin" "$WORK/plugins/theme-plus.plugin" "$RN_GB/RootNames.gb" \
	"$RN_GBC/RootNames.gbc" "$CARD/Roms/.media/Game Boy (GB).png" "$STATE/plugins.txt" "$STATE/sayer.cfg" \
	"$STATE/quick-menu-plus.cfg" "$STATE/theme-plus.cfg" "$texts" /tmp/quick-menu-plus.awake
if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
if [ -f "$WORK/map.txt.saved" ]; then cp "$WORK/map.txt.saved" "$RN_MAP"; else rm -f "$RN_MAP"; fi
rm -f "$WORK/recent.txt.saved" "$WORK/map.txt.saved"
