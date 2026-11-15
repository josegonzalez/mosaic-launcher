# shellcheck shell=sh
# shard: 3

# filter presets with favorites and search: the launcher starts in a test
# system (the last path), where seeker steps through the presets with
# filter-presets.next and back with .prev, reading the list after each:
# Unplayed (Bravo, in recents, is played without row-badges), Favorites
# (favorites' set), Has save stepped past with save-state-manager off, a
# genre and player counts from the .info sidecars through the card index,
# and a preset written as a search query, then all games again. Search's
# button is on L2, so the previous filter isn't put there and the next one
# is on R2. Backing out to the root puts all games back. The card's
# favorites and recents and /tmp/last.txt are put back afterwards.
FP_ROMS="$CARD/Roms/Filter Presets (FPT)"
FP_EMU="$CARD/Emus/desktop/FPT.pak"
FP_FAV="$CARD/Collections/Favorites.txt"
mkdir -p "$FP_ROMS/.media" "$FP_EMU" "$(dirname "$FP_FAV")" "$(dirname "$RECENTS")" "$STATE"
: >"$FP_EMU/launch.sh"
for game in Alpha Bravo Charlie Delta; do : >"$FP_ROMS/$game.gb"; done
printf 'genre=Puzzle\nplayers=1-2\n' >"$FP_ROMS/.media/Alpha.info"
printf 'genre=puzzle\n' >"$FP_ROMS/.media/Bravo.info"
printf 'genre=Action\nplayers=4\n' >"$FP_ROMS/.media/Charlie.info"
for saved in "$FP_FAV" "$RECENTS" /tmp/last.txt; do
	[ -f "$saved" ] && cp "$saved" "$WORK/filter-presets-$(basename "$saved").saved"
done
printf '/Roms/Filter Presets (FPT)/Alpha.gb\n/Roms/Filter Presets (FPT)/Charlie.gb\n' >"$FP_FAV"
printf '/Roms/Filter Presets (FPT)/Bravo.gb\tBravo\n' >"$RECENTS"
printf '%s' "$FP_ROMS/Alpha.gb" >/tmp/last.txt
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" filter-presets favorites search >/dev/null \
	|| fail "building filter-presets, favorites and search"
build_plugin seeker 'id=seeker\nname=Seeker\napi=1\n'

# every other plugin built so far off, as a plugin plugins.txt doesn't name loads
: >"$STATE/plugins.txt"
for dir in "$WORK"/plugins/*.plugin; do
	id=$(basename "$dir" .plugin)
	case "$id" in
	filter-presets | favorites | search | seeker) printf '%s=on\n' "$id" >>"$STATE/plugins.txt" ;;
	*) printf '%s=off\n' "$id" >>"$STATE/plugins.txt" ;;
	esac
done
printf 'button=L2\n' >"$STATE/search.cfg"
printf 'preset1=-fav: system:fpt\n' >"$STATE/filter-presets.cfg"
next=action:filter-presets.next
printf 'steps=%s\n' "$next,$next,$next,$next,$next,$next,$next,$next,action:filter-presets.prev,b" >"$STATE/seeker.cfg"
log="$WORK/filter-presets.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early with filter-presets"

plugin=filter-presets
expect "L2 is search.open's on the game list, so filter-presets.prev isn't on it"
expect "filter-presets.next on R2"
expect "unplayed (Unplayed): 3 of 4 games in $FP_ROMS"
expect "favorites (Favorites): 2 of 4 games in $FP_ROMS"
expect "genre:action (Genre: Action): 1 of 4 games in $FP_ROMS"
expect "genre:puzzle (Genre: Puzzle): 2 of 4 games in $FP_ROMS"
expect "players:2 (2+ players): 2 of 4 games in $FP_ROMS"
expect "players:4 (4+ players): 1 of 4 games in $FP_ROMS"
expect "user1 (-fav: system:fpt): 2 of 4 games in $FP_ROMS"
expect "all games"
expect "header: Favorites (2)"
expect "left the folder: all games"
grep -qF "plugin filter-presets: save (" "$log" && fail "Has save kept games without save-state-manager"
plugin=seeker
expect "run filter-presets.next 0"
expect "list $FP_ROMS: Alpha, Charlie, Delta selected"
expect "list $FP_ROMS: Alpha, Charlie selected"
expect "list $FP_ROMS: Charlie selected"
expect "list $FP_ROMS: Alpha, Bravo selected"
expect "list $FP_ROMS: Bravo, Delta selected"
expect "list $FP_ROMS: Alpha, Bravo, Charlie, Delta selected"
expect "run filter-presets.prev 0"

rm -rf "$FP_ROMS" "$FP_EMU"
rmdir "$CARD/Emus/desktop" 2>/dev/null
for saved in "$FP_FAV" "$RECENTS" /tmp/last.txt; do
	if [ -f "$WORK/filter-presets-$(basename "$saved").saved" ]; then mv "$WORK/filter-presets-$(basename "$saved").saved" "$saved"; else rm -f "$saved"; fi
done
rm -rf "$WORK/plugins/filter-presets.plugin" "$WORK/plugins/favorites.plugin" "$WORK/plugins/search.plugin" \
	"$WORK/plugins/seeker.plugin" "$STATE/search.cfg" "$STATE/seeker.cfg" "$STATE/filter-presets.cfg" "$STATE/plugins.txt" \
	"$STATE/favorites.cfg" /tmp/mosaic-search.last
case_log=""
