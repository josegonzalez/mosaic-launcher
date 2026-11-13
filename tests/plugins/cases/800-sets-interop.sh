# shellcheck shell=sh
# shard: 3

# sets interop: the bundled plugins read each other's entry sets through
# the host. search's fav: lists the favorites plugin's set and -fav: the
# rest, picked from its recent queries by seeker; random-game, picking
# from the favorites set, never picks the one the hidden plugin hides.
SI_ROMS="$CARD/Roms/Sets Interop (SIT)"
SI_EMU="$CARD/Emus/desktop/SIT.pak"
SI_FAV="$CARD/Collections/Favorites.txt"
SI_LAST=/tmp/mosaic-search.last
mkdir -p "$SI_ROMS" "$SI_EMU" "$CARD/Collections" "$STATE"
: >"$SI_EMU/launch.sh"
: >"$SI_ROMS/Alpha.gb"
: >"$SI_ROMS/Bravo.gb"
: >"$SI_ROMS/Charlie.gb"
: >"$SI_ROMS/Delta.gb"
[ -f "$SI_FAV" ] && cp "$SI_FAV" "$WORK/sets-interop-favorites.saved"
for saved in /tmp/next /tmp/last.txt "$RECENTS"; do
	[ -f "$saved" ] && cp "$saved" "$WORK/sets-interop-$(basename "$saved").saved"
done
printf '/Roms/Sets Interop (SIT)/Alpha.gb\n/Roms/Sets Interop (SIT)/Charlie.gb\n' >"$SI_FAV"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" favorites hidden search random-game >/dev/null \
	|| fail "building favorites, hidden, search and random-game"
build_plugin seeker 'id=seeker\nname=Seeker\napi=1\n'

# search: fav: and -fav:, each picked from the recent queries
printf 'favorites=on\nsearch=on\nseeker=on\n' >"$STATE/plugins.txt"
printf 'steps=action:search.open,down,a\n' >"$STATE/seeker.cfg"
for query in "fav: system:sit" "-fav: system:sit"; do
	rm -f "$STATE/search.cfg" "$SI_LAST"
	printf 'recent1=%s\n' "$query" >"$STATE/search.cfg"
	log="$WORK/sets-interop-search.log"
	run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early (search $query)"
	plugin=seeker
	expect "run search.open 0"
	case "$query" in
	-*) expect "list $CARD/Search: Bravo, Delta selected 0" ;;
	*) expect "list $CARD/Search: Alpha, Charlie selected 0" ;;
	esac
	grep -qF "plugin search: \"$query\": 2 of " "$log" || fail "search didn't log 2 results for \"$query\""
done

# random-game: from favorites, with Charlie hidden, always Alpha
printf '%s\n' "$SI_ROMS/Charlie.gb" >"$STATE/hidden.txt"
printf 'favorites=on\nhidden=on\nrandom-game=on\nseeker=on\n' >"$STATE/plugins.txt"
printf 'from=favorites\n' >"$STATE/random-game.cfg"
printf 'steps=action:random-game.pick\n' >"$STATE/seeker.cfg"
for i in 1 2 3; do
	log="$WORK/sets-interop-random-$i.log"
	run_launcher "$log" && fail "the launcher didn't quit to launch random-game's pick ($i)"
	grep -qF "plugin random-game: picked $SI_ROMS/Alpha.gb" "$log" || fail "random-game didn't pick Alpha, the favorite that isn't hidden ($i)"
	grep -qF "plugin random-game: picked $SI_ROMS/Charlie.gb" "$log" && fail "random-game picked the hidden favorite ($i)"
done

# and with favorites off, there's nothing to pick from (named off, as a
# plugin plugins.txt doesn't name loads)
printf 'favorites=off\nhidden=on\nrandom-game=on\nseeker=on\n' >"$STATE/plugins.txt"
log="$WORK/sets-interop-random-off.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early (random-game without favorites)"
grep -qF "plugin random-game: picked" "$log" && fail "random-game picked a game from a set nobody registered"

rm -rf "$SI_ROMS" "$SI_EMU"
rmdir "$CARD/Emus/desktop" 2>/dev/null
if [ -f "$WORK/sets-interop-favorites.saved" ]; then mv "$WORK/sets-interop-favorites.saved" "$SI_FAV"; else rm -f "$SI_FAV"; fi
for saved in /tmp/next /tmp/last.txt "$RECENTS"; do
	if [ -f "$WORK/sets-interop-$(basename "$saved").saved" ]; then cp "$WORK/sets-interop-$(basename "$saved").saved" "$saved"; else rm -f "$saved"; fi
	rm -f "$WORK/sets-interop-$(basename "$saved").saved"
done
rm -rf "$WORK/plugins/favorites.plugin" "$WORK/plugins/hidden.plugin" "$WORK/plugins/search.plugin" \
	"$WORK/plugins/random-game.plugin" "$WORK/plugins/seeker.plugin" "$STATE/search.cfg" "$STATE/seeker.cfg" \
	"$STATE/random-game.cfg" "$STATE/hidden.txt" "$STATE/plugins.txt" "$SI_LAST" /tmp/mosaic-launcher-home
case_log=""
