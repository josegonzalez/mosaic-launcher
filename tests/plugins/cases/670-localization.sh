# shellcheck shell=sh
# shard: 2

# the bundled localization owns the strings slot: off by default, so the
# game list draws OPEN; with language=es its bundled Spanish pack draws
# ABRIR; a test pack in its folder comes first, string by string; and a
# broken one is refused with its line, leaving the bundled one. A font
# beside the pack, or the font setting's, is the fallback font. Then
# screener runs its localization.map action and presses A on the confirm:
# a test system's map.txt gets the names list's name, and the toast is the
# plugin's own string in Spanish. presser writes down the text drawn. Then
# the bundled activity-stats, over a fixture play history of two sessions
# today, builds its chart caption's durations and its games list's session
# count in Spanish. The test system and the history are taken off the card
# afterwards.
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" localization >/dev/null || fail "building localization"
build_plugin screener 'id=screener\nname=Screener\napi=1\n'
LC_CFG="$STATE/localization.cfg"
LC_LANGS="$CARD/LocalizationTest"
LC_ROMS="$CARD/Roms/Localization Test (LCT)"
LC_RES="$WORK/plugins/localization.plugin/res"
rm -rf "$LC_LANGS" "$LC_ROMS"
mkdir -p "$STATE" "$LC_LANGS" "$LC_ROMS"
LC_REAL=$(cd "$LC_LANGS" && pwd -P)
rm -f "$LC_CFG"
plugin=localization
texts="$WORK/localization-texts.txt"

# localization_run <name> [line...]: the launcher until presser and the lines are done
localization_run() {
	log="$WORK/$1.log"
	shift
	rm -f "$texts"
	PRESSER_KEYS=wait PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER"
	export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	run_launcher "$log" "presser: done" "$@" || fail "the launcher exited early with localization ($(basename "$log" .log))"
	unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
}

# off by default
printf 'localization=on\nscreener=off\n' >"$STATE/plugins.txt"
localization_run localization-off
grep -qF "plugins: localization translates the launcher's strings" "$log" || fail "localization didn't take the strings slot"
expect "language off, so nothing is translated"
drew "OPEN"

# the bundled Spanish
printf 'language=es\nfolder=LocalizationTest\n' >"$LC_CFG"
localization_run localization-es
expect "bundled pack $LC_RES/es.po read, "
expect "language es"
drew "ABRIR"
grep -qxF "OPEN" "$texts" && fail "the game list drew OPEN with Spanish on"
grep -qF "is refused" "$log" && fail "the launcher refused a bundled translation"
grep -qF "fallback font" "$log" && fail "a fallback font was used with none given: $(grep -F "fallback font" "$log")"

# a test pack in the folder comes first
printf '# a test pack, not a translation\nmsgid "OPEN"\nmsgstr "TEST OPEN"\n' >"$LC_LANGS/es.po"
localization_run localization-user
expect "user pack $LC_LANGS/es.po read, 1 translated"
drew "TEST OPEN"

# a broken one is refused whole
printf 'msgid "OPEN"\nmsgstr "TEST OPEN\n' >"$LC_LANGS/es.po"
localization_run localization-broken
expect "user pack $LC_LANGS/es.po is refused, line 2: a string with no closing quote"
drew "ABRIR"
rm -f "$LC_LANGS/es.po"

# a fallback font beside the language's pack (glyphs.ttf, a test font with
# Hangul), and the font setting's, which works with the language off too
cp "$ROOT/tests/plugins/glypher/glyphs.ttf" "$LC_LANGS/es.ttf"
localization_run localization-font
expect "fallback font $LC_LANGS/es.ttf, beside the language's pack"
grep -qF "plugins: localization supplies the fallback font $LC_REAL/es.ttf" "$log" || fail "localization's fallback font wasn't used"
drew "ABRIR"
rm -f "$LC_LANGS/es.ttf"
cp "$ROOT/tests/plugins/glypher/glyphs.ttf" "$LC_LANGS/glyphs.ttf"
printf 'language=off\nfont=LocalizationTest/glyphs.ttf\n' >"$LC_CFG"
localization_run localization-font-setting
expect "fallback font $LC_LANGS/glyphs.ttf, from the font setting"
grep -qF "plugins: localization supplies the fallback font $LC_REAL/glyphs.ttf" "$log" || fail "the font setting's fallback font wasn't used"
drew "OPEN"
rm -f "$LC_LANGS/glyphs.ttf"
printf 'language=es\nfolder=LocalizationTest\n' >"$LC_CFG"

# the map.txt writer
mkdir -p "$LC_LANGS/es"
: >"$LC_ROMS/Tetris (World).gb"
: >"$LC_ROMS/Unknown.gb"
printf 'Tetris\tTetris (prueba)\n' >"$LC_LANGS/es/LCT.txt"
printf 'localization=on\nscreener=on\n' >"$STATE/plugins.txt"
printf 'steps=action:localization.map,a\n' >"$STATE/screener.cfg"
localization_run localization-map "plugin screener: done"
expect "Localization Test (LCT): a new map.txt, 1 to add"
expect "wrote $LC_ROMS/map.txt, 1 added"
expect "toast: Nombres añadidos a map.txt en 1 carpeta"
[ "$(cat "$LC_ROMS/map.txt" 2>/dev/null)" = "$(printf 'Tetris (World).gb\tTetris (prueba)')" ] \
	|| fail "localization wrote the wrong map.txt: $(cat "$LC_ROMS/map.txt" 2>/dev/null)"

# text a plugin's model builds, a duration and a plural count, in Spanish;
# local time is about noon, so sessions an hour or two ago are today
LC_GAMES="$CARD/Roms/Activity Localized"
mkdir -p "$LC_GAMES" "$(dirname "$PLAY_DB")"
: >"$LC_GAMES/Tetris.gb"
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$WORK/game_logs.sqlite.saved"
now=$(date +%s)
make_play_db <<SQL || fail "making the localized activity fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Tetris', 'Activity Localized/Tetris.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 1200, $((now - 3600)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 1200, $((now - 7200)));
SQL
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" activity-stats >/dev/null || fail "building activity-stats"
TZ="TST$(printf '%+d' "$(( now / 3600 % 24 - 12 ))")"
export TZ
printf 'localization=on\nscreener=on\nactivity-stats=on\n' >"$STATE/plugins.txt"
printf 'steps=action:activity-stats.open,right,right,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/localization-activity.log" "plugin screener: done" \
	|| fail "the launcher exited early with localization and activity-stats"
unset TZ
log="$WORK/localization-activity.log"
plugin=activity-stats
dot=$(printf '\302\267')
expect "showing 7 days: days 2400 0 0 0 0 0 0, axis 3600, 7 días: 40 min $dot 5 min al día"
expect "showing Games: 0 of 1: Tetris, 40 min $dot 2 sesiones, can open"
rm -f "$PLAY_DB"
[ -f "$WORK/game_logs.sqlite.saved" ] && mv "$WORK/game_logs.sqlite.saved" "$PLAY_DB"

rm -rf "$WORK/plugins/localization.plugin" "$WORK/plugins/screener.plugin" "$WORK/plugins/activity-stats.plugin" \
	"$LC_LANGS" "$LC_ROMS" "$LC_GAMES" "$LC_CFG" "$STATE/screener.cfg" "$STATE/plugins.txt" "$texts"
case_log=""
