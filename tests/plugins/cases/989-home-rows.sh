# shellcheck shell=sh

# home-dashboard's rows, shortcuts and modes (#329). With every row on, the
# Home tab lists continue (Third, the other recent game with a state),
# recent, added, random and the shortcuts (search's action by its plugin's
# id, random-game's quick menu tile, and an unknown name it logs and
# leaves out); presser goes down to the last row, the shortcuts, and A runs
# search. The minimal mode draws the continue card and the clock, and the
# consoles mode the systems with their game counts, A opening the first.
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
make -s -C "$ROOT/plugins" home-dashboard search random-game ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null \
	|| fail "building home-dashboard"
HR_ROMS="$CARD/Roms/Rows Test (HRT)"
HR_EMU="$CARD/Emus/desktop/HRT.pak"
HR_STATES="$CARD/.userdata/shared/.minui/HRT"
mkdir -p "$HR_ROMS" "$HR_EMU" "$HR_STATES" "$(dirname "$RECENTS")" "$STATE"
: >"$HR_ROMS/First.gb"
: >"$HR_ROMS/Second.gb"
: >"$HR_ROMS/Third.gb"
: >"$HR_EMU/launch.sh"
for game in First Third; do
	printf '1' >"$HR_STATES/$game.gb.txt"
	: >"$HR_STATES/$game.gb.1.bmp"
done
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/home-rows-recent.saved"
printf '/Roms/Rows Test (HRT)/First.gb\n/Roms/Rows Test (HRT)/Second.gb\n/Roms/Rows Test (HRT)/Third.gb\n' >"$RECENTS"
rm -f "$HOME_LEFT"

# rows_case <name> <keys>: home-dashboard, search and random-game, presser
# pressing keys; what it drew goes to $texts
rows_case() {
	texts="$WORK/$1.texts"
	: >"$texts"
	printf 'home-dashboard=on\nsearch=on\nrandom-game=on\n' >"$STATE/plugins.txt"
	printf 'tile=on\n' >"$STATE/random-game.cfg"
	PRESSER_KEYS="$2" PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER"
	export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	run_launcher "$WORK/$1.log" "presser: done" || fail "the launcher exited early with home-dashboard ($1)"
	unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	log="$WORK/$1.log"
	plugin=home-dashboard
	grep -qF "plugins: home-dashboard's screen home-dashboard.main is the home screen" "$log" \
		|| fail "home-dashboard's screen wasn't the home screen ($1)"
}
drew_rows() {
	grep -qxF -- "$1" "$texts" || fail "home-dashboard didn't draw \"$1\" ($log)"
}

printf 'rows=continue, recent, added, random, shortcuts, bogus\nshortcuts=search, random-game, nope\n' \
	>"$STATE/home-dashboard.cfg"
rows_case home-rows "wait wait down down down down down down a wait b"
expect "rows: no such row bogus"
expect "shortcut nope: no such action or plugin"
grep -qE "plugin home-dashboard: rows: continue 1 recent 2 added [0-9]+ random [0-9]+ shortcuts 2$" "$log" \
	|| fail "the rows weren't all there: $(grep "plugin home-dashboard: rows:" "$log")"
expect "shortcut search.open"
for text in CONTINUE First OPEN; do drew_rows "$text"; done
# the names under the tiles, cut to fit them
for text in Third SEARCH Random; do
	grep -qxE "${text%"${text#?}"}(${text#?}|.*\.\.\.)" "$texts" || fail "home-dashboard didn't draw \"$text\" ($log)"
done

printf 'mode=minimal\nclock=24h\n' >"$STATE/home-dashboard.cfg"
rows_case home-rows-minimal "wait x"
expect "home: mode minimal,"
expect "games"
for text in CONTINUE First RESUME; do drew_rows "$text"; done
grep -qxE "[0-9]{2}:[0-9]{2}" "$texts" || fail "the minimal mode drew no clock"
grep -qxF "Home" "$texts" && fail "the minimal mode drew the tabs"

printf 'mode=consoles\n' >"$STATE/home-dashboard.cfg"
rows_case home-rows-consoles "wait wait wait a"
expect "home: mode consoles,"
grep -qE "plugin home-dashboard: consoles [0-9]+:.* Rows Test 3" "$log" \
	|| fail "the consoles mode didn't count Rows Test's games: $(grep "plugin home-dashboard: consoles" "$log")"
expect "open $CARD/Roms/"
for text in Consoles "3 games" OPEN; do drew_rows "$text"; done

rm -f "$PRESSER" "$HOME_LEFT"
if [ -f "$WORK/home-rows-recent.saved" ]; then cp "$WORK/home-rows-recent.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
rm -rf "$HR_ROMS" "$HR_EMU" "$HR_STATES" "$WORK/home-rows-recent.saved" "$WORK/plugins/home-dashboard.plugin" \
	"$WORK/plugins/search.plugin" "$WORK/plugins/random-game.plugin" "$STATE/plugins.txt" "$STATE/home-dashboard.cfg" \
	"$STATE/random-game.cfg"
case_log=""
