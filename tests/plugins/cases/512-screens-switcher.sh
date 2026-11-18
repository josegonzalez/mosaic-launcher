# shellcheck shell=sh

# plugin screens in the switcher slot, with screener and layerer as in 510-screens
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
build_plugin layerer 'id=layerer\nname=Layerer\napi=1\n'
mkdir -p "$STATE"
printf 'screener=on\nlayerer=on\n' >"$STATE/plugins.txt"

# what home_screen said, from the first tick on
homes() { sed -n 's/.*plugin screener: home //p' "$log" | tr '\n' ' '; }

[ -f "$HOME_LEFT" ] && cp "$HOME_LEFT" "$WORK/home-left.saved"

# the switcher slot: recents for it (one with a save state and its
# preview, one without, one whose emulator isn't installed), put back after
SWITCHED="$CARD/.userdata/shared/.minui/game_switcher.txt"
SW_ROMS="$CARD/Roms/Switcher Test (SWT)"
SW_GONE="$CARD/Roms/Switcher Gone (SWGONE)"
SW_EMU="$CARD/Emus/desktop/SWT.pak"
SW_STATES="$CARD/.userdata/shared/.minui/SWT"
mkdir -p "$SW_ROMS" "$SW_GONE" "$SW_EMU" "$SW_STATES"
: >"$SW_ROMS/Game.gb"
: >"$SW_ROMS/Other.gb"
: >"$SW_GONE/Gone.gb"
: >"$SW_EMU/launch.sh"
printf '2' >"$SW_STATES/Game.gb.txt"
: >"$SW_STATES/Game.gb.2.bmp"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/switcher-recent.saved"
for saved in /tmp/next /tmp/last.txt /tmp/resume_slot.txt "$SWITCHED"; do
	[ -f "$saved" ] && cp "$saved" "$WORK/switcher-$(basename "$saved").saved"
done
rm -f "$SWITCHED" "$HOME_LEFT"
sw_recents() {
	printf '/Roms/Switcher Test (SWT)/Game.gb\tSaved Game\n/Roms/Switcher Test (SWT)/Other.gb\n/Roms/Switcher Gone (SWGONE)/Gone.gb\n' >"$RECENTS"
}
sw_recents

# a screen marked switcher without slots=switcher is an ordinary one
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
printf 'switcher=bare\nsteps=action:gamelist.switcher,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-noslot.log" "plugin screener: done" || fail "the launcher exited early with a switcher screen but no slot"
log="$WORK/switcher-noslot.log"
grep -qF "plugins: screener's screen screener.bare is marked switcher but screener has no slots=switcher, so it's an ordinary screen" "$log" \
	|| fail "the switcher flag without the slot wasn't logged"
[ "$(seen)" = "gamelist switcher gamelist " ] || fail "the built-in switcher didn't show without the slot: $(seen)"

# the slot with no screen marked switcher keeps the built-in one
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition,switcher\n'
printf 'steps=action:gamelist.switcher,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-none.log" "plugin screener: done" || fail "the launcher exited early owning the switcher slot with no screen"
log="$WORK/switcher-none.log"
grep -qF "plugins: screener owns the switcher slot but registered no MOSAIC_SCREEN_GAME_SWITCHER screen other than its home screen, using the built-in switcher" "$log" \
	|| fail "the switcher slot with no switcher screen wasn't logged"
[ "$(seen)" = "gamelist switcher gamelist " ] || fail "the built-in switcher didn't show with no switcher screen: $(seen)"

# SELECT and gamelist.switcher open the switcher screen over the game
# list, at once; screens open over it and close back to it without
# sliding, open_screen can't open it, it reads the recents, and B goes
# back to the list
printf 'switcher=bare\nsteps=select,b,action:gamelist.switcher,open:screener.main,open:screener.bare,b,recents,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher.log" "plugin screener: done" || fail "the launcher exited early with a switcher screen"
log="$WORK/switcher.log"
plugin=screener
grep -qF "plugins: screener's screen screener.bare is the game switcher" "$log" || fail "the switcher screen wasn't claimed"
[ "$(seen)" = "gamelist screener.bare gamelist screener.bare screener.main screener.bare gamelist " ] \
	|| fail "the switcher screen didn't open and close as the switcher: $(seen)"
[ "$(grep -cF "plugin screener: enter screener.bare over gamelist" "$log")" -eq 2 ] || fail "the switcher screen didn't open over the game list twice"
expect "enter screener.main over screener.bare"
expect "open screener.bare -1"
grep -qF "plugins: can't open screen screener.bare: it's the game switcher" "$log" || fail "open_screen on the switcher screen wasn't refused"
grep -qF "plugin screener: transition" "$log" && fail "something slid to or from the switcher screen: $(grep "plugin screener: transition" "$log")"
expect "recents from init info -1 null -1 open 0"
expect "recents 3"
expect "recent 0 0 name Saved Game type 2 available 1 resume 1 preview Game.gb.2.bmp"
expect "recent 1 0 name Other type 2 available 1 resume 0 preview -"
expect "recent 2 0 name Gone type 2 available 0 resume 0 preview -"
expect "recent 3 -1 name  type 0 available 0 resume 0 preview -"
grep -qE "plugin screener: old size [0-9]+ name Saved Game type 2$" "$log" || fail "a smaller MosaicRecent wasn't filled as far as it goes"
grep -qF "plugin layerer: over screener.bare" "$log" || fail "layerer's overlay wasn't drawn over the switcher screen"

# remove takes a recent out; one whose emulator isn't installed doesn't open
printf 'switcher=bare\nsteps=action:gamelist.switcher,play:2:1,remove:1,recents,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-remove.log" "plugin screener: done" || fail "the launcher exited early removing a recent"
log="$WORK/switcher-remove.log"
expect "play 2 0"
expect "remove 1 0"
expect "recents 2"
expect "recent 1 0 name Gone type 2 available 0 resume 0 preview -"
[ "$(grep -c . "$RECENTS")" -eq 2 ] || fail "the removed recent wasn't saved: $(cat "$RECENTS")"
sw_recents

# a game resumed from the switcher screen comes back to it
printf 'switcher=bare\nsteps=action:gamelist.switcher,play:0:1\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-play.log" && fail "the launcher didn't quit to resume a recent"
log="$WORK/switcher-play.log"
expect "play 0 1"
grep -qE "SWT\.pak/launch\.sh.*Game\.gb" /tmp/next 2>/dev/null || fail "the recent didn't launch with its emulator: $(cat /tmp/next 2>/dev/null)"
[ "$(cat /tmp/resume_slot.txt 2>/dev/null)" = "2" ] || fail "the recent didn't resume its state's slot: $(cat /tmp/resume_slot.txt 2>/dev/null)"
[ -f "$SWITCHED" ] || fail "a launch from the switcher screen didn't save that it came from the switcher"
grep -qF "Saved Game" "$RECENTS" || fail "the recent lost its name: $(cat "$RECENTS")"
printf 'switcher=bare\nsteps=b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-return.log" "plugin screener: done" || fail "the launcher exited early back from a game from the switcher"
log="$WORK/switcher-return.log"
[ "$(seen)" = "screener.bare gamelist " ] || fail "back from a game, the launcher didn't start on the switcher screen: $(seen)"
expect "enter screener.bare over gamelist"
[ -f "$SWITCHED" ] && fail "the switcher file outlived the start it was for"

# fresh from the switcher screen, a game starts over; from the list, it
# doesn't come back to the switcher
printf 'switcher=bare\nsteps=action:gamelist.switcher,play:0:0\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-fresh.log" && fail "the launcher didn't quit to start a recent"
[ "$(cat /tmp/resume_slot.txt 2>/dev/null)" = "8" ] || fail "a fresh start resumed a state: $(cat /tmp/resume_slot.txt 2>/dev/null)"
rm -f "$SWITCHED"
printf 'switcher=bare\nsteps=play:1:1\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-list-play.log" && fail "the launcher didn't quit to open a recent from the list"
log="$WORK/switcher-list-play.log"
expect "play 1 1"
[ -f "$SWITCHED" ] && fail "a recent opened from the list came back to the switcher"
sw_recents

# Settings' start screen opens the switcher screen
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
printf 'defaultView=1\n' >"$SETTINGS"
printf 'switcher=bare\nsteps=b\n' >"$STATE/screener.cfg"
SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/switcher-view.log" "plugin screener: done" \
	|| fail "the launcher exited early starting on the switcher screen"
log="$WORK/switcher-view.log"
[ "$(seen)" = "screener.bare gamelist " ] || fail "the start screen wasn't the switcher screen: $(seen)"
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -f "$WORK/minuisettings.txt.saved"

# an enter that declines shows the built-in switcher that time
printf 'switcher=bare\ndecline=1\nsteps=action:gamelist.switcher,b,action:gamelist.switcher,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-decline.log" "plugin screener: done" || fail "the launcher exited early with a declining switcher screen"
log="$WORK/switcher-decline.log"
expect "declined screener.bare over gamelist"
[ "$(seen)" = "gamelist switcher gamelist screener.bare gamelist " ] || fail "a declined switcher screen didn't give way once: $(seen)"

# a switcher screen that fails gives way to the built-in switcher for the run
printf 'switcher=fail\nfail=render\nsteps=action:gamelist.switcher,b,action:gamelist.switcher,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-fail.log" "plugin screener: done" || fail "the launcher exited early with a failing switcher screen"
log="$WORK/switcher-fail.log"
grep -qF "plugins: using the built-in game switcher in place of screener.fail until the launcher restarts" "$log" \
	|| fail "the failing switcher screen's fallback wasn't logged"
expect "exit screener.fail"
[ "$(seen)" = "gamelist screener.fail switcher gamelist switcher gamelist " ] || fail "the failed switcher screen didn't give way: $(seen)"
[ "$(grep -cF "plugin screener: enter screener.fail" "$log")" -eq 1 ] || fail "the failed switcher screen opened again"

# with a home screen: the switcher screen over it closes it, and B, or the
# quick menu's B over the switcher screen, goes back to it
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition,home,switcher\n'
printf 'home=main\nswitcher=bare\nsteps=action:gamelist.switcher,b,action:gamelist.switcher,action:gamelist.quick_menu,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-home.log" "plugin screener: done" || fail "the launcher exited early with home and switcher screens"
log="$WORK/switcher-home.log"
[ "$(seen)" = "screener.main screener.bare screener.main screener.bare quickmenu screener.main " ] \
	|| fail "the switcher screen didn't go back to the home screen: $(seen)"
grep -qF "plugins: closed screen screener.main, as the switcher opened over it" "$log" || fail "the switcher screen didn't close the home screen"
grep -qF "plugins: closed screen screener.bare, as the quickmenu opened over it" "$log" || fail "the quick menu didn't close the switcher screen"

# a screen marked both home and switcher is only the home screen
printf 'home=main\nswitcher=main\nsteps=\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-both.log" "plugin screener: done" || fail "the launcher exited early with one screen marked home and switcher"
log="$WORK/switcher-both.log"
grep -qF "plugins: screener's screen screener.main is the home screen" "$log" || fail "the screen marked both wasn't the home screen"
grep -qF "is the game switcher" "$log" && fail "the home screen was the switcher screen too"

# a game from the switcher screen over the home screen comes back to the
# switcher screen, and B on it to the home screen
printf 'home=main\nswitcher=bare\nsteps=action:gamelist.switcher,play:1:1\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-home-play.log" && fail "the launcher didn't quit to open a recent over the home screen"
[ "$(cat "$HOME_LEFT" 2>/dev/null)" = "home" ] || fail "a launch from the switcher screen over home didn't save home: $(cat "$HOME_LEFT" 2>/dev/null)"
printf 'home=main\nswitcher=bare\nsteps=b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-home-return.log" "plugin screener: done" || fail "the launcher exited early back from a game over the home screen"
log="$WORK/switcher-home-return.log"
[ "$(seen)" = "screener.bare screener.main " ] || fail "back from a game, the switcher screen didn't go back to the home screen: $(seen)"

# the bundled switcher-plus, in stock's style: SELECT opens it in place of
# the built-in switcher, showing the first recent's save state preview (a
# real image this time) and stock's hints; right moves on, Y removes, and
# B goes back to the list. Screener, without the switcher slot, presses the
# buttons and logs what shows
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" switcher-plus >/dev/null || fail "building switcher-plus"
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
rm -rf "$WORK/plugins/layerer.plugin"
rm -f "$HOME_LEFT" "$SWITCHED"
cp "$ROOT/tests/plugins/peeker/art.png" "$SW_STATES/Game.gb.2.bmp"
sw_recents
printf 'steps=select,right,y,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus.log" "plugin screener: done" || fail "the launcher exited early with switcher-plus"
log="$WORK/switcher-plus.log"
plugin=switcher-plus
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher is the game switcher" "$log" || fail "switcher-plus didn't take the switcher"
[ "$(seen)" = "gamelist switcher-plus.switcher gamelist " ] || fail "switcher-plus didn't open and close as the switcher: $(seen)"
expect "style full"
expect "showing 0 of 3: Saved Game, preview drawn, resume, style full"
expect "showing 1 of 3: Other, preview none, open, style full"
expect "removed 1, 2 left"
expect "showing 0 of 2: Saved Game, preview drawn, resume, style full"
[ "$(grep -c . "$RECENTS")" -eq 2 ] || fail "switcher-plus's removal wasn't saved: $(cat "$RECENTS")"
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher's" "$log" && fail "switcher-plus's screen was turned off"
sw_recents

# A resumes the selected recent, and the launcher comes back to switcher-plus
printf 'steps=action:gamelist.switcher,a\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus-play.log" && fail "the launcher didn't quit to resume a recent from switcher-plus"
log="$WORK/switcher-plus-play.log"
expect "opening 0: Saved Game"
grep -qE "SWT\.pak/launch\.sh.*Game\.gb" /tmp/next 2>/dev/null || fail "switcher-plus's recent didn't launch: $(cat /tmp/next 2>/dev/null)"
[ "$(cat /tmp/resume_slot.txt 2>/dev/null)" = "2" ] || fail "switcher-plus's recent didn't resume: $(cat /tmp/resume_slot.txt 2>/dev/null)"
printf 'steps=b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus-return.log" "plugin screener: done" || fail "the launcher exited early back from a game from switcher-plus"
log="$WORK/switcher-plus-return.log"
[ "$(seen)" = "switcher-plus.switcher gamelist " ] || fail "back from a game, the launcher didn't start on switcher-plus: $(seen)"
sw_recents

# the strip and list styles; in the list, down past the last recent wraps,
# and a recent whose emulator isn't installed doesn't open
printf 'style=strip\n' >"$STATE/switcher-plus.cfg"
printf 'steps=action:gamelist.switcher,right,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus-strip.log" "plugin screener: done" || fail "the launcher exited early with switcher-plus's strip"
log="$WORK/switcher-plus-strip.log"
expect "showing 0 of 3: Saved Game, preview drawn, resume, style strip"
expect "showing 1 of 3: Other, preview none, open, style strip"
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher's" "$log" && fail "switcher-plus's strip was turned off"
printf 'style=list\n' >"$STATE/switcher-plus.cfg"
printf 'steps=action:gamelist.switcher,down,down,a,down,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus-list.log" "plugin screener: done" || fail "the launcher exited early with switcher-plus's list"
log="$WORK/switcher-plus-list.log"
expect "showing 0 of 3: Saved Game, preview drawn, resume, style list"
expect "showing 2 of 3: Gone, preview none, open, style list"
expect "can't open 2: Gone"
[ "$(grep -cF "plugin switcher-plus: showing 0 of 3: Saved Game, preview drawn" "$log")" -eq 2 ] || fail "down past the last recent didn't wrap"
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher's" "$log" && fail "switcher-plus's list was turned off"

# switcher-plus's captions: Game was played 2h 15m over 2 sessions 3 days
# ago and Other never, none while playtime-captions isn't loaded and its
# captions.text, in its settings, while it is. Local time is about noon, as for
# tracker
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$WORK/game_logs.sqlite.saved"
now=$(date +%s)
hours_behind=$(( $(date -u +%H | sed 's/^0//') - 12 ))
make_play_db <<SQL || fail "making the switcher captions fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Game', 'Switcher Test (SWT)/Game.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 7500, $((now - 10 * 86400)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 600, $((now - 3 * 86400)));
SQL
TZ="TST$(printf '%+d' "$hours_behind")"
export TZ
printf 'captions=on\n' >"$STATE/switcher-plus.cfg"
printf 'steps=action:gamelist.switcher,right,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus-captions.log" "plugin screener: done" || fail "the launcher exited early with switcher-plus's captions"
log="$WORK/switcher-plus-captions.log"
expect "style full, captions on"
expect "captions on, but nobody provides captions.text, so there are none"
expect "showing 0 of 3: Saved Game, preview drawn, resume, style full, no caption"
expect "showing 1 of 3: Other, preview none, open, style full, no caption"
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher's" "$log" && fail "switcher-plus's screen was turned off with captions"
make -s -C "$ROOT/plugins" playtime-captions ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building playtime-captions"
printf 'sessions=on\nnever=on\n' >"$STATE/playtime-captions.cfg"
run_launcher "$WORK/switcher-plus-captions-set.log" "plugin screener: done" || fail "the launcher exited early with switcher-plus's captions and playtime-captions"
log="$WORK/switcher-plus-captions-set.log"
expect "showing 0 of 3: Saved Game, preview drawn, resume, style full, caption 2h 15m · 3 days ago · 2 sessions"
expect "showing 1 of 3: Other, preview none, open, style full, caption Never played"
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher's" "$log" && fail "switcher-plus's screen was turned off with playtime-captions"
# the list's, under the selected recent's preview
printf 'style=list\ncaptions=on\n' >"$STATE/switcher-plus.cfg"
printf 'steps=action:gamelist.switcher,down,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus-captions-list.log" "plugin screener: done" || fail "the launcher exited early with switcher-plus's captions in the list"
log="$WORK/switcher-plus-captions-list.log"
expect "showing 0 of 3: Saved Game, preview drawn, resume, style list, caption 2h 15m · 3 days ago · 2 sessions"
expect "showing 1 of 3: Other, preview none, open, style list, caption Never played"
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher's" "$log" && fail "switcher-plus's list was turned off with captions"
# with the history unknown, nothing, even with never on
printf 'style=strip\ncaptions=on\n' >"$STATE/switcher-plus.cfg"
printf 'steps=action:gamelist.switcher,right,b\n' >"$STATE/screener.cfg"
MOSAIC_LAUNCHER_SQLITE="$WORK/no-such-libsqlite3"
export MOSAIC_LAUNCHER_SQLITE
run_launcher "$WORK/switcher-plus-captions-unknown.log" "plugin screener: done" || fail "the launcher exited early with switcher-plus's captions and no sqlite"
unset MOSAIC_LAUNCHER_SQLITE
log="$WORK/switcher-plus-captions-unknown.log"
expect "showing 0 of 3: Saved Game, preview drawn, resume, style strip, no caption"
expect "showing 1 of 3: Other, preview none, open, style strip, no caption"
unset TZ
rm -f "$PLAY_DB"
[ -f "$WORK/game_logs.sqlite.saved" ] && mv "$WORK/game_logs.sqlite.saved" "$PLAY_DB"
rm -rf "$WORK/plugins/playtime-captions.plugin" "$STATE/playtime-captions.cfg"
: >"$SW_STATES/Game.gb.2.bmp"
rm -rf "$WORK/plugins/switcher-plus.plugin" "$STATE/switcher-plus.cfg"

rm -rf "$SW_ROMS" "$SW_GONE" "$SW_EMU" "$SW_STATES"
rmdir "$CARD/Emus/desktop" 2>/dev/null
if [ -f "$WORK/switcher-recent.saved" ]; then cp "$WORK/switcher-recent.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
for saved in /tmp/next /tmp/last.txt /tmp/resume_slot.txt "$SWITCHED"; do
	if [ -f "$WORK/switcher-$(basename "$saved").saved" ]; then cp "$WORK/switcher-$(basename "$saved").saved" "$saved"; else rm -f "$saved"; fi
	rm -f "$WORK/switcher-$(basename "$saved").saved"
done
rm -f "$WORK/switcher-recent.saved"

rm -f "$HOME_LEFT"
if [ -f "$WORK/home-left.saved" ]; then cp "$WORK/home-left.saved" "$HOME_LEFT"; fi
rm -f "$WORK/home-left.saved"
rm -rf "$WORK/plugins/screener.plugin" "$WORK/plugins/layerer.plugin" "$STATE/screener.cfg" "$STATE/layerer.cfg" "$STATE/plugins.txt"
case_log=""
