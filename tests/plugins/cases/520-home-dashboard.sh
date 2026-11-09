# shellcheck shell=sh
# shard: 3

# home-dashboard owns the home slot: the launcher starts on it, with a
# continue card for the last game (which has a save state to resume) and
# favorites' game on the Pinned tab. presser switches the tabs with R1 and
# L1, opens the switcher with SELECT and comes back with B, still on the tab
# it left, and X closes it on to the game list. The other modes each draw
# their own home; what they all draw goes to $texts.
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
make -s -C "$ROOT/plugins" home-dashboard favorites ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building home-dashboard"
HD_ROMS="$CARD/Roms/Home Test (HDT)"
HD_PICO="$CARD/Roms/Home Pico (P8)"
HD_EMU="$CARD/Emus/desktop/HDT.pak"
HD_STATES="$CARD/.userdata/shared/.minui/HDT"
HD_FAVORITES="$CARD/Collections/Favorites.txt"
mkdir -p "$HD_ROMS" "$HD_PICO" "$HD_EMU" "$HD_STATES" "$(dirname "$HD_FAVORITES")" "$(dirname "$RECENTS")"
: >"$HD_ROMS/Hit Game.gb"
: >"$HD_ROMS/Second.gb"
: >"$HD_PICO/Cart.p8"
: >"$HD_EMU/launch.sh"
printf '1' >"$HD_STATES/Hit Game.gb.txt"
: >"$HD_STATES/Hit Game.gb.1.bmp"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/home-dashboard-recent.saved"
[ -f "$HD_FAVORITES" ] && cp "$HD_FAVORITES" "$WORK/home-dashboard-favorites.saved"
printf '/Roms/Home Test (HDT)/Hit Game.gb\n/Roms/Home Test (HDT)/Second.gb\n' >"$RECENTS"
printf '/Roms/Home Test (HDT)/Second.gb\n' >"$HD_FAVORITES"
rm -f "$HOME_LEFT"

# home_case <name> <keys>: home-dashboard with favorites, presser pressing
# keys; what it drew goes to $texts
home_case() {
	texts="$WORK/$1.texts"
	: >"$texts"
	printf 'favorites=on\nhome-dashboard=on\n' >"$STATE/plugins.txt"
	PRESSER_KEYS="$2" PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER"
	export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	run_launcher "$WORK/$1.log" "presser: done" || fail "the launcher exited early with home-dashboard ($1)"
	unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	log="$WORK/$1.log"
	plugin=home-dashboard
	grep -qF "plugins: home-dashboard's screen home-dashboard.main is the home screen" "$log" \
		|| fail "home-dashboard's screen wasn't the home screen ($1)"
	grep -qF "plugins: home-dashboard's screen home-dashboard.main's" "$log" \
		&& fail "home-dashboard's screen was turned off: $(grep -F "home-dashboard.main's" "$log")"
}
# drew_home <text>: home-dashboard drew exactly this text
drew_home() {
	grep -qxF -- "$1" "$texts" || fail "home-dashboard didn't draw \"$1\" ($log)"
}

home_case home-dashboard "r1 r1 r1 l1 select b x"
expect "mode dashboard, pinned on, stats on, shelf on, favorites on"
expect "home: mode dashboard, tab Home of 3, recents 2, pinned 1, carts 0, history"
[ "$(sed -n 's/.*plugin home-dashboard: tab //p' "$log" | tr '\n' ' ')" = "Pinned Stats Home Stats " ] \
	|| fail "R1 and L1 didn't step through the tabs: $(grep "plugin home-dashboard: tab" "$log")"
expect "home: mode dashboard, tab Stats of 3"
expect "games"
for text in CONTINUE "Hit Game" Second Home Pinned Stats GAMES RESUME; do drew_home "$text"; done
grep -qxE "Played today|No play history yet" "$texts" || fail "the Stats tab drew neither its play time nor that there's none"
grep -qF "plugins: closed screen home-dashboard.main, as the switcher opened over it" "$log" \
	|| fail "SELECT didn't open the switcher over the home screen"

for hd_mode in minui retroachievements pico8; do
	printf 'mode=%s\n' "$hd_mode" >"$STATE/home-dashboard.cfg"
	home_case "home-dashboard-$hd_mode" "down x"
	expect "home: mode $hd_mode,"
	expect "games"
	case "$hd_mode" in
		minui) drew_home "Recently played"; drew_home "Games"; drew_home "Second" ;;
		retroachievements) drew_home "Achievements"; grep -qxE "Checking achievements|No achievements on the card yet" "$texts" \
			|| fail "the retroachievements mode drew neither its rows' state" ;;
		pico8) drew_home "Pico-8"; drew_home "Cart"; grep -qF ", carts 1, " "$log" || fail "the pico8 mode didn't find its cart" ;;
	esac
done
rm -f "$STATE/home-dashboard.cfg"

rm -f "$PRESSER" "$HOME_LEFT"
if [ -f "$WORK/home-dashboard-recent.saved" ]; then cp "$WORK/home-dashboard-recent.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
if [ -f "$WORK/home-dashboard-favorites.saved" ]; then cp "$WORK/home-dashboard-favorites.saved" "$HD_FAVORITES"; else rm -f "$HD_FAVORITES"; fi
rm -rf "$HD_ROMS" "$HD_PICO" "$HD_EMU" "$HD_STATES" "$WORK/home-dashboard-recent.saved" "$WORK/home-dashboard-favorites.saved" \
	"$WORK/plugins/home-dashboard.plugin" "$WORK/plugins/favorites.plugin" "$STATE/plugins.txt"
case_log=""
