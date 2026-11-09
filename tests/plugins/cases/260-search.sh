# shellcheck shell=sh
# shard: 2

# search: seeker runs search.open and types qz on the keyboard (a types
# the q it starts on, two downs reach z), and the results open in the
# Search folder, best first, with accents and map.txt names folded. It
# launches the second; back from the game the results open again with it
# selected, without asking for a query.
SE_ROMS="$CARD/Roms/Search Test (SRT)"
SE_EMU="$CARD/Emus/desktop/SRT.pak"
SE_LAST=/tmp/mosaic-search.last
mkdir -p "$SE_ROMS" "$SE_EMU" "$STATE"
: >"$SE_EMU/launch.sh"
: >"$SE_ROMS/Qzar.gb"
: >"$SE_ROMS/Pokémon Qzone.gb"
: >"$SE_ROMS/Other.gb"
: >"$SE_ROMS/Zelda.gb"
printf 'Other.gb\tTaqzu\n' >"$SE_ROMS/map.txt"
for saved in /tmp/next /tmp/last.txt "$RECENTS"; do
	[ -f "$saved" ] && cp "$saved" "$WORK/search-$(basename "$saved").saved"
done
make -s -C "$ROOT/plugins" search ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building search"
build_plugin seeker 'id=seeker\nname=Seeker\napi=1\n'
printf 'search=on\nseeker=on\n' >"$STATE/plugins.txt"
rm -f "$STATE/search.cfg" "$SE_LAST"
printf 'steps=action:search.open,a,down,down,a,start,down,a\n' >"$STATE/seeker.cfg"
run_launcher "$WORK/search.log" && fail "the launcher didn't quit to launch a search result"
log="$WORK/search.log"
plugin=seeker
expect "run search.open 0"
expect "screen dialog"
expect "list $CARD/Search: Qzar, Pokémon Qzone, Taqzu selected 0"
expect "list $CARD/Search: Qzar, Pokémon Qzone, Taqzu selected 1"
grep -qE 'plugin search: "qz": 3 of [0-9]+ games in [0-9.]+ ms$' "$log" || fail "search didn't log its results"
grep -qxF "query=qz" "$STATE/search.cfg" 2>/dev/null || fail "search didn't keep the query"
grep -qxF "recent1=qz" "$STATE/search.cfg" 2>/dev/null || fail "search didn't remember the query"
[ "$(cat "$SE_LAST" 2>/dev/null)" = "$SE_ROMS/Pokémon Qzone.gb" ] || fail "search didn't keep the game it launched: $(cat "$SE_LAST" 2>/dev/null)"
printf 'steps=\n' >"$STATE/seeker.cfg"
run_launcher "$WORK/search-return.log" "plugin seeker: done" || fail "the launcher exited early back from a search result"
log="$WORK/search-return.log"
expect "list $CARD/Search: Qzar, Pokémon Qzone, Taqzu selected 1"
grep -qF "plugin seeker: screen dialog" "$log" && fail "search asked for a query back from a game"
[ -f "$SE_LAST" ] && fail "search kept the launched game past its return"

rm -rf "$SE_ROMS" "$SE_EMU"
rmdir "$CARD/Emus/desktop" 2>/dev/null
for saved in /tmp/next /tmp/last.txt "$RECENTS"; do
	if [ -f "$WORK/search-$(basename "$saved").saved" ]; then cp "$WORK/search-$(basename "$saved").saved" "$saved"; else rm -f "$saved"; fi
	rm -f "$WORK/search-$(basename "$saved").saved"
done
rm -rf "$WORK/plugins/search.plugin" "$WORK/plugins/seeker.plugin" "$STATE/search.cfg" "$STATE/seeker.cfg" "$STATE/plugins.txt" \
	"$SE_LAST" /tmp/mosaic-launcher-home
case_log=""
