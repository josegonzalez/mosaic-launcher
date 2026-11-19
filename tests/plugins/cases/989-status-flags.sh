# shellcheck shell=sh

# status flags: the launcher starts on Alpha (the last path), where
# presser opens the shared Y menu (Tags, then Status, with the status
# button on Y too), picks Status, sets Completed and Beaten, and backs
# out. They're saved in status.txt apart from the tags, which don't
# change. Then row-badges, with every status icon and the trophy on,
# reads them back from the status sets, and finds Alpha has every
# achievement unlocked in a fixture .ra cache in minarch's format;
# /tmp/last.txt is put back afterwards
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
make -s -C "$ROOT/plugins" tags ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building tags"
make -s -C "$ROOT/plugins" row-badges ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building row-badges"
SF_ROMS="$CARD/Roms/Status Flags (GB)"
SF_EMU="$CARD/Emus/desktop/GB.pak"
SF_EMU_MADE=""
mkdir -p "$SF_ROMS" "$STATE"
[ -d "$SF_EMU" ] || { mkdir -p "$SF_EMU" && : >"$SF_EMU/launch.sh" && SF_EMU_MADE=1; }
printf 'mosaic status flags test cart %.0s' 1 2 3 4 5 6 7 8 >"$SF_ROMS/Alpha.gb"
printf 'another status cart' >"$SF_ROMS/Bravo.gb"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/status-last.saved"
printf '%s' "$SF_ROMS/Alpha.gb" >/tmp/last.txt
printf 'Puzzle\nPuzzle\t/Roms/Status Flags (GB)/Bravo.gb\n' >"$STATE/tags.txt"
rm -f "$STATE/status.txt"
printf 'status_button=Y\n' >"$STATE/tags.cfg"
printf 'tags=on\n' >"$STATE/plugins.txt"
texts="$WORK/status.texts"
: >"$texts"
PRESSER_KEYS="y down a a down a b" PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER"
export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
run_launcher "$WORK/status.log" "presser: done" || fail "the launcher exited early with tags' status menu"
unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
log="$WORK/status.log"
plugin=tags
expect "status: 0 completed, 0 beaten, 0 broken, 0 backlog"
expect "status for /Roms/Status Flags (GB)/Alpha.gb: 0"
expect "set completed on /Roms/Status Flags (GB)/Alpha.gb"
expect "set beaten on /Roms/Status Flags (GB)/Alpha.gb"
grep -qF "plugins: tags's screen tags.flags's" "$log" && fail "the status menu was turned off: $(grep -F "tags.flags's" "$log")"
for text in TAGS STATUS Alpha Completed Beaten Broken Backlog; do
	grep -qxF -- "$text" "$texts" || fail "the status menu didn't draw \"$text\""
done
[ "$(cat "$STATE/status.txt")" = "$(printf 'beaten\nbeaten\t/Roms/Status Flags (GB)/Alpha.gb\ncompleted\ncompleted\t/Roms/Status Flags (GB)/Alpha.gb')" ] \
	|| fail "status.txt isn't what the status menu made: $(cat "$STATE/status.txt")"
[ "$(cat "$STATE/tags.txt")" = "$(printf 'Puzzle\nPuzzle\t/Roms/Status Flags (GB)/Bravo.gb')" ] \
	|| fail "the status menu changed tags.txt: $(cat "$STATE/tags.txt")"
[ -f "$STATE/status.txt.tmp" ] && fail "tags left its status file's temporary file"

# row-badges' icons, from the status sets, and the trophy, from the .ra cache
[ -d "$RA_DIR" ] && mv "$RA_DIR" "$WORK/status-ra.saved"
[ -f "$RA_HASHES" ] && mv "$RA_HASHES" "$WORK/status-ra-hashes.saved"
mkdir -p "$RA_DIR/offline/cache"
ALPHA_HASH=$(md5_of "$SF_ROMS/Alpha.gb")
BRAVO_HASH=$(md5_of "$SF_ROMS/Bravo.gb")
frame_json '{"Success":true,"GameId":7,"Sets":[{"Type":"core","Achievements":[{"ID":1,"Flags":3,"Points":5},{"ID":2,"Flags":3,"Points":10}]}]}' \
	"$RA_DIR/offline/cache/achievementsets_$ALPHA_HASH.bin"
frame_json '{"Success":true,"HardcoreUnlocks":[{"ID":2,"When":1}],"Unlocks":[{"ID":1,"When":2}],"ServerNow":3}' \
	"$RA_DIR/offline/cache/startsession_$ALPHA_HASH.bin"
frame_json '{"Success":true,"GameId":8,"Sets":[{"Type":"core","Achievements":[{"ID":1,"Flags":3,"Points":5},{"ID":2,"Flags":3,"Points":10}]}]}' \
	"$RA_DIR/offline/cache/achievementsets_$BRAVO_HASH.bin"
frame_json '{"Success":true,"Unlocks":[{"ID":1,"When":2}],"ServerNow":3}' "$RA_DIR/offline/cache/startsession_$BRAVO_HASH.bin"
printf 'completed=on\nbeaten=on\nbroken=on\nbacklog=on\nperfect=on\n' >"$STATE/row-badges.cfg"
printf 'tags=on\nrow-badges=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/status-badges.log" "plugin row-badges: 100% mark on 1 games, 0 waiting" \
	|| fail "the launcher exited early with row-badges' status icons and trophy"
log="$WORK/status-badges.log"
plugin=row-badges
expect "unplayed off, tiers off, favorites off, tagged off, manual off, completed on, beaten on, broken on, backlog on, perfect on"
expect "status icons: completed 1, beaten 1, broken 0, backlog 0"
expect "100% mark on 0 games, 2 waiting"
expect "100% mark on 1 games, 0 waiting"
grep -qF "plugins: row-badges's" "$log" && fail "row-badges' decorations were turned off with its status icons"

# off by default: no progress asked for
rm -f "$STATE/row-badges.cfg"
run_launcher "$WORK/status-badges-off.log" "plugin row-badges: unplayed off" || fail "the launcher exited early with row-badges' defaults"
log="$WORK/status-badges-off.log"
expect "unplayed off, tiers off, favorites off, tagged off, manual off, completed off, beaten off, broken off, backlog off, perfect off"
grep -qF "100% mark" "$log" && fail "row-badges asked about achievements with the trophy off"

rm -rf "$RA_DIR" "$RA_HASHES"
[ -d "$WORK/status-ra.saved" ] && mv "$WORK/status-ra.saved" "$RA_DIR"
[ -f "$WORK/status-ra-hashes.saved" ] && mv "$WORK/status-ra-hashes.saved" "$RA_HASHES"
if [ -f "$WORK/status-last.saved" ]; then cp "$WORK/status-last.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
[ -n "$SF_EMU_MADE" ] && rm -rf "$SF_EMU"
rm -rf "$SF_ROMS" "$PRESSER" "$WORK/status-last.saved" "$WORK/plugins/tags.plugin" "$WORK/plugins/row-badges.plugin" \
	"$STATE/tags.txt" "$STATE/status.txt" "$STATE/tags.cfg" "$STATE/row-badges.cfg" "$STATE/plugins.txt"
case_log=""
