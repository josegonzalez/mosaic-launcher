# shellcheck shell=sh
# shard: 2

# infoer reads .info sidecars through entry_info: display_name names games
# by them, each entry reads its sidecar once, and an oversized sidecar or a
# folder in its place is ignored and logged
INFO_DIR="$CARD/Infoer"
mkdir -p "$INFO_DIR/.media/Delta.info" "$STATE"
for game in Alpha Bravo Charlie Delta Echo; do : >"$INFO_DIR/$game.gb"; done
printf '# scraped\r\nname=Alpha Prime\r\nyear=1990\r\ngenre=Platformer\r\n' >"$INFO_DIR/.media/Alpha.info"
printf 'YEAR = 1985 \ngenre=Puzzle\nbroken line\n' >"$INFO_DIR/.media/Bravo.info"
{ echo "year=1999"; head -c 5000 /dev/zero | tr '\0' '#'; } >"$INFO_DIR/.media/Charlie.info"
printf 'infoer=on\n' >"$STATE/plugins.txt"
build_plugin infoer 'id=infoer\nname=Infoer\napi=1\n'
run_launcher "$WORK/infoer.log" "plugin infoer: done" || fail "the launcher exited early with infoer loaded"
log="$WORK/infoer.log"
plugin=infoer
expect "open folder 1"
grep -qE "plugin infoer: named [1-9][0-9]*$" "$log" || fail "display_name didn't name a game from its sidecar"
expect 'game Alpha Prime year 1 "1990" genre "Platformer" players 0 ""'
expect 'game Bravo year 1 "1985" genre "Puzzle" players 0 ""'
expect 'game Charlie year 0 "" genre "" players 0 ""'
expect 'game Delta year 0 "" genre "" players 0 ""'
expect 'game Echo year 0 "" genre "" players 0 ""'
expect "bad args -1 -1 -1 -1"
expect 'cut 1 "Alp"'
expect "kept 1990"
expect "fresh 2000"
grep -qF "plugins: ignoring $INFO_DIR/.media/Charlie.info: it's over 4096 bytes" "$log" || fail "the oversized sidecar wasn't logged"
grep -qF "plugins: ignoring $INFO_DIR/.media/Delta.info: it isn't a regular file" "$log" || fail "the folder sidecar wasn't logged"
grep -qF "Echo.info" "$log" && fail "a missing sidecar was logged"
rm -rf "$WORK/plugins/infoer.plugin" "$INFO_DIR" "$STATE/plugins.txt"
