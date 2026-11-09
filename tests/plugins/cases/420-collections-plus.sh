# shellcheck shell=sh
# shard: 1

# the bundled collections-plus puts every collection on the root in the
# order setting's order, with the Collections folder off it and the
# Games tile off the quick menu, as collector reads the lists back: the
# Collections folder in the same order, and Alpha in its file's order,
# though Collections/map.txt renames one of its games, with its own map's
# names (Secret hidden) and stock's shared name for the rest. Alpha's art
# is its thumbnail and box. Then with the defaults, the root and the
# folder are stock's, and the file order and own map still hold.
COLL_DIR="$CARD/Collections"
# a system of its own, so stock keeps the collections in their folder
COLL_GAMES="$CARD/Roms/Collector (GB)"
[ -e "$COLL_DIR" ] && mv "$COLL_DIR" "$WORK/Collections.saved"
mkdir -p "$COLL_DIR/.media" "$COLL_GAMES" "$STATE"
for game in Zed Apple Mango Secret; do : >"$COLL_GAMES/$game.gb"; done
for game in Zed Apple Mango Secret; do echo "/Roms/Collector (GB)/$game.gb"; done >"$COLL_DIR/Alpha.txt"
echo "/Roms/Collector (GB)/Apple.gb" >"$COLL_DIR/Beta.txt"
echo "/Roms/Collector (GB)/Apple.gb" >"$COLL_DIR/Gamma.txt"
printf 'Beta.txt\tBee\nMango.gb\tMango Shared\n' >"$COLL_DIR/map.txt"
printf 'Zed.gb\tZed Prime\nSecret.gb\t.secret\n' >"$COLL_DIR/.media/Alpha.map.txt"
cp "$ROOT/tests/plugins/drawer/art.png" "$COLL_DIR/.media/Alpha.png"
make -s -C "$ROOT/plugins" collections-plus ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building collections-plus"
build_plugin collector 'id=collector\nname=Collector\napi=1\n'
printf 'collection=Collections/Alpha.txt\n' >"$STATE/collector.cfg"
printf 'promote=*\norder=Gamma, Bee\nhide_folder=on\nhide_games=on\n' >"$STATE/collections-plus.cfg"
printf 'collections-plus=on\ncollector=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/collections-plus.log" "plugin collector: done" "quick menu: top row" || fail "the launcher exited early with collections-plus loaded"
log="$WORK/collections-plus.log"
plugin=collections-plus
expect "order 2, promote all 0, hide folder on, file order on, own map on, art on, hide games on"
plugin=collector
expect "open root 1"
grep -qE "plugin collector: root .*Collector,Gamma,Bee,Alpha(,|$)" "$log" || fail "the collections aren't on the root in order: $(grep "plugin collector: root" "$log")"
grep -qE "plugin collector: root (.*,)?Collections(,|$)" "$log" && fail "the Collections folder stayed on the root"
expect "art Gamma thumbnail 0 box 0"
expect "art Alpha thumbnail 1 box 1"
expect "folder Gamma,Bee,Alpha"
expect "collection Zed Prime,Apple,Mango Shared"
top=$(grep "quick menu: top row" "$log" | tail -n 1)
case "$top" in *Games*) fail "the Games tile stayed: $top" ;; esac
case "$top" in *Collections*) ;; *) fail "the Collections tile went: $top" ;; esac

rm -f "$STATE/collections-plus.cfg"
run_launcher "$WORK/collections-plus-defaults.log" "plugin collector: done" || fail "the launcher exited early with collections-plus' defaults"
log="$WORK/collections-plus-defaults.log"
grep -qE "plugin collector: root (.*,)?Collections(,|$)" "$log" || fail "the Collections folder isn't on the root: $(grep "plugin collector: root" "$log")"
grep -qE "plugin collector: root .*(Gamma|Bee|Alpha)" "$log" && fail "a collection is on the root by default"
expect "folder Alpha,Bee,Gamma"
expect "collection Zed Prime,Apple,Mango Shared"
grep "quick menu: top row" "$log" | grep -qv "Games" && fail "the Games tile went by default"

# with file_order and own_map off, stock's names and order: sorted by
# the shared map's rename
printf 'file_order=off\nown_map=off\n' >"$STATE/collections-plus.cfg"
run_launcher "$WORK/collections-plus-stock.log" "plugin collector: done" || fail "the launcher exited early with collections-plus' options off"
log="$WORK/collections-plus-stock.log"
expect "collection Apple,Mango Shared,Secret,Zed"
rm -rf "$WORK/plugins/collections-plus.plugin" "$WORK/plugins/collector.plugin" "$COLL_DIR" "$COLL_GAMES" \
	"$STATE/collections-plus.cfg" "$STATE/collector.cfg" "$STATE/plugins.txt"
[ -e "$WORK/Collections.saved" ] && mv "$WORK/Collections.saved" "$COLL_DIR"
