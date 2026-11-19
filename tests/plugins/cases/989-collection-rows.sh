# shellcheck shell=sh

# collections-plus with an order that names systems: as collector reads the
# root back, the Picks collection sits between the two Rows systems, named
# by display name and folder name, the unnamed Zeta stays after the
# consoles and activity-stats' Most Played folder (named in the order, but
# not a system or collection) keeps its place. Picks' icon in
# Collections/.icons goes at the start of its row, moving its name right,
# while the systems and Zeta, with none, keep theirs, and the icon isn't
# Picks' thumbnail. With the plugin's
# settings empty, or an order naming only collections, the root is the
# same as with the plugin off.
CR_DIR="$CARD/Collections"
CR_ONE="$CARD/Roms/Rows One (GB)"
CR_TWO="$CARD/Roms/Rows Two (GB)"
[ -e "$CR_DIR" ] && mv "$CR_DIR" "$WORK/Collections.saved"
mkdir -p "$CR_DIR/.icons" "$CR_ONE" "$CR_TWO" "$STATE"
: >"$CR_ONE/One.gb"
: >"$CR_TWO/Two.gb"
echo "/Roms/Rows One (GB)/One.gb" >"$CR_DIR/Picks.txt"
echo "/Roms/Rows Two (GB)/Two.gb" >"$CR_DIR/Zeta.txt"
cp "$ROOT/tests/plugins/drawer/art.png" "$CR_DIR/.icons/Picks.png"
make -s -C "$ROOT/plugins" collections-plus activity-stats ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null \
	|| fail "building collections-plus and activity-stats"
build_plugin collector 'id=collector\nname=Collector\napi=1\n'
printf 'collection=Collections/Picks.txt\n' >"$STATE/collector.cfg"
printf 'folder=on\n' >"$STATE/activity-stats.cfg"

# the root's names, as collector logged them in $1
cr_root() {
	grep "plugin collector: root " "$1" | tail -n 1 | sed 's/.*plugin collector: root //'
}

printf 'activity-stats=on\ncollector=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/collection-rows-off.log" "plugin collector: done" || fail "the launcher exited early without collections-plus"
stock=$(cr_root "$WORK/collection-rows-off.log")
[ -n "$stock" ] || fail "collector didn't log the root without collections-plus"

printf 'activity-stats=on\ncollections-plus=on\ncollector=on\n' >"$STATE/plugins.txt"
rm -f "$STATE/collections-plus.cfg"
run_launcher "$WORK/collection-rows-empty.log" "plugin collector: done" || fail "the launcher exited early with collections-plus' defaults"
got=$(cr_root "$WORK/collection-rows-empty.log")
[ "$got" = "$stock" ] || fail "the root changed with collections-plus' settings empty: $got, not $stock"

printf 'order=Zeta, Picks\n' >"$STATE/collections-plus.cfg"
run_launcher "$WORK/collection-rows-collections.log" "plugin collector: done" || fail "the launcher exited early with an order of collections"
got=$(cr_root "$WORK/collection-rows-collections.log")
[ "$got" = "$stock" ] || fail "the root changed with an order naming no systems: $got, not $stock"

printf 'promote=*\norder=Most Played, rows one, Picks, Rows Two (GB)\n' >"$STATE/collections-plus.cfg"
run_launcher "$WORK/collection-rows.log" "plugin collector: done" || fail "the launcher exited early with collection rows"
log="$WORK/collection-rows.log"
got=$(cr_root "$log")
case ",$got," in *",Rows One,Picks,Rows Two,"*) ;; *) fail "Picks isn't between the Rows systems: $got" ;; esac
case ",$got," in *",Most Played,Zeta,"*) ;; *) fail "Most Played and Zeta moved: $got" ;; esac
case ",$got," in *",Rows Two,"*"Most Played,"*) ;; *) fail "Most Played isn't after the consoles: $got" ;; esac
plugin=collector
expect "art Picks thumbnail 0 box 0"
expect "art Zeta thumbnail 0 box 0"
stock_at=$(grep -m 1 -oE "plugin collector: row Rows One name at [0-9]+" "$log" | sed 's/.* //')
picks_at=$(grep -m 1 -oE "plugin collector: row Picks name at [0-9]+" "$log" | sed 's/.* //')
zeta_at=$(grep -m 1 -oE "plugin collector: row Zeta name at [0-9]+" "$log" | sed 's/.* //')
[ -n "$stock_at" ] && [ -n "$picks_at" ] || fail "collector didn't log the rows' names: $(grep "plugin collector: row " "$log")"
# Zeta is drawn only when the screen has room for its row
[ -z "$zeta_at" ] || [ "$zeta_at" = "$stock_at" ] || fail "Zeta's name moved without an icon: $zeta_at, not $stock_at"
[ "$picks_at" -gt "$stock_at" ] || fail "Picks' name didn't move for its icon: $picks_at, not past $stock_at"

rm -rf "$WORK/plugins/collections-plus.plugin" "$WORK/plugins/activity-stats.plugin" "$WORK/plugins/collector.plugin" \
	"$CR_DIR" "$CR_ONE" "$CR_TWO" "$STATE/collections-plus.cfg" "$STATE/activity-stats.cfg" "$STATE/collector.cfg" \
	"$STATE/plugins.txt"
[ -e "$WORK/Collections.saved" ] && mv "$WORK/Collections.saved" "$CR_DIR"
