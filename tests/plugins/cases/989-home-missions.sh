# shellcheck shell=sh

# home-dashboard's missions row (#337): with gamification on and the row
# named, the Home tab has a card per quest gamification runs, from its
# quests.cards service, with its title, progress and reward; presser goes
# down to the row and A opens the Quests screen, and B comes back to the
# home. Without gamification the row has
# nothing and is left out, and unnamed it isn't asked for. There's no play
# history, so every quest is at 0; the card's recents and play history are
# put back afterwards.
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
make -s -C "$ROOT/plugins" home-dashboard gamification ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null \
	|| fail "building home-dashboard and gamification"
HM_ROMS="$CARD/Roms/Missions Test (HMT)"
HM_EMU="$CARD/Emus/desktop/HMT.pak"
mkdir -p "$HM_ROMS" "$HM_EMU" "$(dirname "$RECENTS")" "$STATE"
: >"$HM_ROMS/First.gb"
: >"$HM_ROMS/Second.gb"
: >"$HM_EMU/launch.sh"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/home-missions-recent.saved"
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$WORK/home-missions-play.saved"
printf '/Roms/Missions Test (HMT)/First.gb\n/Roms/Missions Test (HMT)/Second.gb\n' >"$RECENTS"
rm -rf "$STATE/gamification" "$HOME_LEFT"

# missions_case <name> <plugins.txt> <keys>: presser pressing keys; what
# home-dashboard drew goes to $texts
missions_case() {
	texts="$WORK/$1.texts"
	: >"$texts"
	printf '%b' "$2" >"$STATE/plugins.txt"
	PRESSER_KEYS="$3" PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER"
	export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	run_launcher "$WORK/$1.log" "presser: done" || fail "the launcher exited early with home-dashboard ($1)"
	unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	log="$WORK/$1.log"
	plugin=home-dashboard
}
drew() {
	grep -qxF -- "$1" "$texts" || fail "home-dashboard didn't draw \"$1\" ($log)"
}

printf 'rows=missions, recent\n' >"$STATE/home-dashboard.cfg"
missions_case home-missions 'home-dashboard=on\ngamification=on\n' "wait wait down wait a wait wait b wait"
expect "missions: play 0/30, games 0/3, fresh 0/1, revisit 0/1, week_days 0/5, week_hours 0/300"
expect "rows: missions 6 recent 1"
expect "mission play 0"
for text in MISSIONS "0m / 30m" "+1 quest done" "0 / 3"; do drew "$text"; done
grep -qE "^Play for 30 minutes today$|^Play for 30.*\.\.\.$" "$texts" || fail "home-dashboard didn't draw the play quest's title ($log)"
grep -qF "plugin gamification: showing No streak" "$log" || fail "A on a mission didn't open the Quests screen ($log)"
grep -qF "plugins: closed screen gamification.main" "$log" || fail "B didn't close the Quests screen ($log)"

# without gamification, nothing in the row
missions_case home-missions-off 'home-dashboard=on\ngamification=off\n' "wait wait"
expect "missions: none"
expect "rows: recent 1"

# off unless the row is named
printf 'rows=continue, recent\n' >"$STATE/home-dashboard.cfg"
missions_case home-missions-unnamed 'home-dashboard=on\ngamification=on\n' "wait wait"
expect "rows: recent 1"
grep -qF "plugin home-dashboard: missions:" "$log" && fail "home-dashboard asked for missions it wasn't set to show ($log)"

rm -f "$PRESSER" "$HOME_LEFT"
if [ -f "$WORK/home-missions-recent.saved" ]; then cp "$WORK/home-missions-recent.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
[ -f "$WORK/home-missions-play.saved" ] && mv "$WORK/home-missions-play.saved" "$PLAY_DB"
rm -rf "$HM_ROMS" "$HM_EMU" "$STATE/gamification" "$WORK/home-missions-recent.saved" "$WORK/plugins/home-dashboard.plugin" \
	"$WORK/plugins/gamification.plugin" "$STATE/plugins.txt" "$STATE/home-dashboard.cfg"
case_log=""
