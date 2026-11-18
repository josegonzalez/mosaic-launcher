# shellcheck shell=sh

# the card index: indexer and lister read it from a card the launcher
# hasn't kept one for, so it isn't ready from init (nor ever from a
# plugin's own thread) until launcher.index_ready, which has Tetris with
# its .info fields. indexer then adds Added.gb and calls index_refresh, and
# both see it after the next launcher.index_ready; lister adds Later.gb and
# reloads the lists, and both see that too. The index is kept on the card,
# so the next start answers from init and, with the card unchanged, isn't
# announced again.
IX_ROMS="$CARD/Roms/Index Test (IXT)"
IX_EMU="$CARD/Emus/desktop/IXT.pak"
IX_FILE="$STATE/card-index.txt"
mkdir -p "$IX_ROMS/.media" "$IX_EMU" "$STATE"
: >"$IX_EMU/launch.sh"
: >"$IX_ROMS/Tetris (World).gb"
: >"$IX_ROMS/Kirby.gb"
printf 'year=1989\npublisher=Nintendo\ngenre=Puzzle\n' >"$IX_ROMS/.media/Tetris (World).info"
rm -f "$IX_FILE"
build_plugin indexer 'id=indexer\nname=Indexer\napi=1\n'
build_plugin lister 'id=lister\nname=Lister\napi=1\n'
printf 'indexer=on\nlister=on\n' >"$STATE/plugins.txt"

log="$WORK/card-index.log"
run_launcher "$log" "plugin indexer: done" "plugin lister: done" || fail "the launcher exited early with indexer and lister"
plugin=indexer
expect "init not-ready -1"
expect "ready 2 games, Added.gb: no, Later.gb: no"
expect "entry $IX_ROMS/Tetris (World).gb Tetris 1989 Nintendo Puzzle IXT 1"
expect "added 0"
expect "ready 3 games, Added.gb: yes, Later.gb: no"
expect "ready 4 games, Added.gb: yes, Later.gb: yes"
plugin=lister
expect "ready 2 games, Added.gb: no, Later.gb: no, count matches"
expect "ready 3 games, Added.gb: yes, Later.gb: no, count matches"
expect "reloaded"
expect "ready 4 games, Added.gb: yes, Later.gb: yes, count matches"
grep -qF "plugins: index: nothing kept on the card, reading it" "$log" || fail "the launcher didn't read the card for the index"
grep -qF "$IX_ROMS/Later.gb" "$IX_FILE" 2>/dev/null || fail "the index wasn't kept on the card with Later.gb"

# the next start: from the file at init, and nothing to announce
log="$WORK/card-index-again.log"
run_launcher "$log" "plugins: index: the card hasn't changed" || fail "the launcher exited early with the kept index"
plugin=indexer
expect "init cached -1"
grep -qF "plugin indexer: ready" "$log" && fail "the index was announced again with the card unchanged"
grep -qE "plugins: index: [0-9]+ entries read from .*card-index.txt in [0-9.]+ ms, checking the card" "$log" \
	|| fail "the launcher didn't read the kept index"

rm -rf "$IX_ROMS" "$IX_EMU"
rmdir "$CARD/Emus/desktop" 2>/dev/null
rm -rf "$WORK/plugins/indexer.plugin" "$WORK/plugins/lister.plugin" "$STATE/plugins.txt" "$IX_FILE"
case_log=""
