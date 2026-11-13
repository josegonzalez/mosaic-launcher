# shellcheck shell=sh
# shard: 3

# entry sets: the bundled favorites and hidden plugins register theirs, and
# setter reads them through the host: set_has and set_list see the
# collection and the hidden list, a second registrant of favorites and a
# name that isn't its own are refused, a set nobody registered lists -1, a
# set asked from inside its own walk is refused while another set answers,
# and is_filtered follows hidden's filter. decliner registers favorites
# first and declines, which releases it for the favorites plugin.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" favorites hidden >/dev/null || fail "building favorites and hidden"
build_plugin decliner 'id=decliner\nname=Decliner\napi=1\n'
build_plugin setter 'id=setter\nname=Setter\napi=1\n'
SETS_DIR="$CARD/Roms/.sets-test"
SETS_FAV="$CARD/Collections/Favorites.txt"
mkdir -p "$SETS_DIR" "$CARD/Collections" "$STATE"
: >"$SETS_DIR/Fav.gb"
: >"$SETS_DIR/Hid.gb"
: >"$SETS_DIR/Plain.gb"
[ -f "$SETS_FAV" ] && cp "$SETS_FAV" "$WORK/sets-favorites.saved"
printf '/Roms/.sets-test/Fav.gb\n/Roms/.sets-test/Gone.gb\n' >"$SETS_FAV"
printf '%s\n' "$SETS_DIR/Hid.gb" >"$STATE/hidden.txt"
printf 'fav=%s\nhid=%s\nplain=%s\n' "$SETS_DIR/Fav.gb" "$SETS_DIR/Hid.gb" "$SETS_DIR/Plain.gb" >"$STATE/setter.cfg"
printf 'decliner=on\nfavorites=on\nhidden=on\nsetter=on\n' >"$STATE/plugins.txt"

log="$WORK/entry-sets.log"
run_launcher "$log" "plugin setter: done" || fail "the launcher exited early (entry sets)"
plugin=setter
grep -qF "plugins: decliner's sets are released (1)" "$log" || fail "decliner's set wasn't released when it declined"
expect "register favorites -1"
grep -qF "plugins: setter can't register set favorites: favorites registered it first" "$log" \
	|| fail "the second registrant of favorites wasn't refused as taken"
expect "register random -1"
grep -qF "plugins: setter can't register set random: it may only register" "$log" || fail "a name setter can't have wasn't refused"
expect "register setter.mine 0"
expect "has favorites 1 0"
expect "has hidden 1 0"
expect "list favorites 2 $SETS_DIR/Fav.gb,$SETS_DIR/Gone.gb"
expect "count favorites 2"
expect "list missing -1"
expect "list mine 2 nested 1 0"
grep -qF "plugins: set_has on set setter.mine refused: it was asked from inside its own callback" "$log" \
	|| fail "a set asked from inside its own walk wasn't refused"
expect "filtered 1 0"
expect "unregister 0 -1"

if [ -f "$WORK/sets-favorites.saved" ]; then mv "$WORK/sets-favorites.saved" "$SETS_FAV"; else rm -f "$SETS_FAV"; fi
rm -rf "$SETS_DIR" "$WORK/plugins/favorites.plugin" "$WORK/plugins/hidden.plugin" "$WORK/plugins/decliner.plugin" \
	"$WORK/plugins/setter.plugin" "$STATE/hidden.txt" "$STATE/setter.cfg" "$STATE/plugins.txt"
case_log=""
