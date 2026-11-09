# shellcheck shell=sh
# shard: 2

# tags: the launcher starts on Alpha (the last path), where presser opens
# the checklist with Y, tags Alpha with Puzzle, deletes the unused tag after
# its confirm, names a new tag q on the keyboard (a types the q it starts
# on), which goes on Alpha, and backs out to the root, which has the Tags
# folder; /tmp/last.txt is put back afterwards
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
make -s -C "$ROOT/plugins" tags ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building tags"
TG_ROMS="$CARD/Roms/Tags Test (TGT)"
TG_EMU="$CARD/Emus/desktop/TGT.pak"
mkdir -p "$TG_ROMS" "$TG_EMU" "$STATE"
: >"$TG_ROMS/Alpha.gb"
: >"$TG_ROMS/Second.gb"
: >"$TG_EMU/launch.sh"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/tags-last.saved"
printf '%s' "$TG_ROMS/Alpha.gb" >/tmp/last.txt
printf 'Puzzle\nPuzzle\t/Roms/Tags Test (TGT)/Second.gb\nUnused\n' >"$STATE/tags.txt"
printf 'tags=on\n' >"$STATE/plugins.txt"
texts="$WORK/tags.texts"
: >"$texts"
PRESSER_KEYS="y a down x a a a start b b" PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER"
export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
run_launcher "$WORK/tags.log" "presser: done" || fail "the launcher exited early with tags"
unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
log="$WORK/tags.log"
plugin=tags
expect "2 tags, 1 with games, root on"
expect "1 quick menu tiles"
expect "root: 1 tags with games"
expect "checklist for /Roms/Tags Test (TGT)/Alpha.gb: 2 tags"
expect "tagged /Roms/Tags Test (TGT)/Alpha.gb with Puzzle"
expect "deleted tag Unused"
expect "added tag q"
expect "tagged /Roms/Tags Test (TGT)/Alpha.gb with q"
expect "root: 2 tags with games"
grep -qF "plugins: tags's screen tags.checklist's" "$log" && fail "the checklist was turned off: $(grep -F "tags.checklist's" "$log")"
for text in Alpha Puzzle Unused "New tag" "1 game" "2 games" "Delete the tag Unused?" Tags; do
	grep -qxF -- "$text" "$texts" || fail "tags didn't draw \"$text\""
done
[ "$(cat "$STATE/tags.txt")" = "$(printf 'Puzzle\nPuzzle\t/Roms/Tags Test (TGT)/Second.gb\nPuzzle\t/Roms/Tags Test (TGT)/Alpha.gb\nq\nq\t/Roms/Tags Test (TGT)/Alpha.gb')" ] \
	|| fail "tags.txt isn't what the checklist made: $(cat "$STATE/tags.txt")"
[ -f "$STATE/tags.txt.tmp" ] && fail "tags left its temporary file"
if [ -f "$WORK/tags-last.saved" ]; then cp "$WORK/tags-last.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
rm -rf "$TG_ROMS" "$TG_EMU" "$PRESSER" "$WORK/tags-last.saved" "$WORK/plugins/tags.plugin" "$STATE/tags.txt" "$STATE/plugins.txt"
case_log=""
