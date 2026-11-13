# shellcheck shell=sh
# shard: 2

# providers: patron calls each bundled provider's service for the games in
# a folder of its own: captions.text from playtime-captions (Played has 2h
# 5m in the play history), games.played from row-badges (Played by the
# history, Saved by its save, Fresh not at all), states.last from
# save-state-manager, emulator.choice from emulator-override, battery.history
# from battery-logger and info.format from metadata-band. copycat, after
# playtime-captions, is refused captions.text, a name its provides= doesn't
# list and an entry set's, and its copycat.echo runs as copycat, while
# copycat.nest calling itself is refused. dropper provides dropper.gone and
# declines, which releases it. A name nobody provides, bad arguments, an
# out too small and a call from patron's own thread are refused.
PV_ROMS="$CARD/Roms/Providers Test (PVT)"
PV_SHARED="$CARD/.userdata/shared"
PV_EMU="$CARD/Emus/desktop/PVTX.pak"
mkdir -p "$PV_ROMS/.media" "$CARD/Saves/PVT" "$PV_SHARED/PVT-core" "$PV_SHARED/.minui/PVT" "$PV_EMU" "$STATE"
for game in Played Saved Fresh; do : >"$PV_ROMS/$game.gb"; done
: >"$CARD/Saves/PVT/Saved.gb.sav"
printf state >"$PV_SHARED/PVT-core/Played.gb.st1"
: >"$PV_SHARED/.minui/PVT/Played.gb.1.bmp"
: >"$PV_EMU/launch.sh"
printf 'year=1994-11-21\npublisher=Nintendo\ngenre=Puzzle\n' >"$PV_ROMS/.media/Played.info"
printf '/Roms/Providers Test (PVT)/Played.gb\tPVTX\n' >"$STATE/emulator-override.txt"
printf '%s\n%s\n' 'date,game,path,seconds,start_percent,end_percent,drain_percent,percent_per_hour,charged' \
	'2026-11-08 14:00:00,Played,Roms/Providers Test (PVT)/Played.gb,600,80,70,10,60.0,no' >"$STATE/battery-logger.csv"
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$WORK/providers-game_logs.sqlite.saved"
mkdir -p "$(dirname "$PLAY_DB")"
now=$(date +%s)
make_play_db <<SQL || fail "making the providers' play history"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Played', 'Providers Test (PVT)/Played.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 7500, $((now - 3600)));
SQL
for name in playtime-captions row-badges save-state-manager emulator-override battery-logger metadata-band; do
	make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" "$name" >/dev/null || fail "building $name"
done
build_plugin dropper 'id=dropper\nname=Dropper\napi=1\nprovides=dropper.gone\n'
build_plugin copycat 'id=copycat\nname=Copycat\napi=1\nprovides=captions.text,copycat.echo,copycat.nest,sets.favorites\n'
build_plugin patron 'id=patron\nname=Patron\napi=1\n'
printf 'last=off\n' >"$STATE/playtime-captions.cfg"
printf 'me=copycat\n' >"$STATE/copycat.cfg"
printf 'roms=%s\n' "$PV_ROMS" >"$STATE/patron.cfg"
printf 'playtime-captions=on\nrow-badges=on\nsave-state-manager=on\nemulator-override=on\nbattery-logger=on\nmetadata-band=on\ndropper=on\ncopycat=on\npatron=on\n' \
	>"$STATE/plugins.txt"

log="$WORK/providers.log"
run_launcher "$log" "plugin patron: done" || fail "the launcher exited early (providers)"
dot=$(printf '\302\267')
plugin=patron
expect "captions.text 0 [text=2h 5m,width="
expect "games.played 0 [played=1]"
grep -qF "plugin patron: games.played 0 [played=0]" "$log" || fail "Fresh counted as played"
[ "$(grep -cF "plugin patron: games.played 0 [played=1]" "$log")" -eq 2 ] || fail "Played and Saved didn't both count as played"
expect "states.last 0 [count=1,slot=1,label=Slot 2,saved="
expect ",preview=$PV_SHARED/.minui/PVT/Played.gb.1.bmp]"
expect "states.last 0 [count=0]"
expect "emulator.choice 0 [pak=PVTX,launch="
expect "/Emus/desktop/PVTX.pak/launch.sh]"
expect "emulator.choice 0 [pak=,launch=]"
expect "battery.history 0 [count=1,seconds=600,session.0="
expect ",600,80,70,0]"
expect "info.format 0 [count=3,year=1994,publisher=Nintendo,genre=Puzzle,line=1994 $dot Nintendo $dot Puzzle]"
expect "copycat.echo 0 [me=copycat,x=1]"
expect "copycat.nest 0 [same=-3,echo=18,x=inner]"
expect "nobody.here -1 []"
expect "dropper.gone -1 []"
expect "copycat.echo -3 []"
expect "copycat.echo -2 []"
expect "thread -3"
expect "capabilities 1 1 1 1 1 1 0 0"
plugin=copycat
expect "provide captions -1 echo 0 nest 0 undeclared -1 set -1"
plugin=dropper
expect "provide 0, declining"
grep -qF "plugins: copycat can't provide captions.text: playtime-captions provided it first" "$log" \
	|| fail "copycat's captions.text wasn't refused for playtime-captions"
grep -qF "plugins: copycat can't provide copycat.undeclared: its plugin.txt's provides= doesn't list it" "$log" \
	|| fail "a name copycat didn't declare wasn't refused"
grep -qF "plugins: copycat can't provide sets.favorites: sets. and events. names" "$log" || fail "an entry set's name wasn't refused"
grep -qF "plugins: dropper's services are released (1)" "$log" || fail "dropper's service wasn't released when it declined"
grep -qF "plugins: call refused: it only works from" "$log" || fail "the call from patron's thread wasn't logged"

rm -f "$PLAY_DB"
[ -f "$WORK/providers-game_logs.sqlite.saved" ] && mv "$WORK/providers-game_logs.sqlite.saved" "$PLAY_DB"
rm -rf "$PV_ROMS" "$CARD/Saves/PVT" "$PV_SHARED/PVT-core" "$PV_SHARED/.minui/PVT" "$PV_EMU" \
	"$STATE/emulator-override.txt" "$STATE/battery-logger.csv" "$STATE/playtime-captions.cfg" "$STATE/copycat.cfg" \
	"$STATE/patron.cfg" "$STATE/plugins.txt"
for name in playtime-captions row-badges save-state-manager emulator-override battery-logger metadata-band dropper copycat patron; do
	rm -rf "$WORK/plugins/$name.plugin"
done
case_log=""
