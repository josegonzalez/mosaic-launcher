# shellcheck shell=sh
# shard: 1

# selector hears about the selection once it settles: the starting one, a
# folder it opens, the end of a scroll through it a row a frame, nothing
# for a reload that keeps the same game, and the root's when it goes back.
# select_index is refused during the event. With fail=1, an error on the
# second event turns its selection_changed off.
build_plugin selector 'id=selector\nname=Selector\napi=1\n'
run_launcher "$WORK/selector.log" "plugin selector: done" || fail "the launcher exited early with selector loaded"
log="$WORK/selector.log"
plugin=selector
expect "settle 250"
expect "open folder 1"
grep -qE "plugin selector: event 1 .* settled 1, select -1$" "$log" || fail "selector's first event is missing"
expect "event 2 Apple in Selector, index 0, settled 1, select -1"
expect "event 3 Grape in Selector, index 6, settled 1, select -1"
expect "after scrolling 3 events"
expect "after reload 3 events"
expect "open root 1"
grep -qE "plugin selector: event 4 .* in root, index [0-9]+, settled 1, select -1$" "$log" || fail "selector didn't hear of the root's selection"
expect "done"
grep -qF "plugin selector: event 5" "$log" && fail "selector heard of the selection more than once a settle"
printf 'fail=1\n' >"$STATE/selector.cfg"
run_launcher "$WORK/selector-fail.log" "plugin selector: done" || fail "the launcher exited early with selector failing"
log="$WORK/selector-fail.log"
expect "event failing"
expect "after scrolling 2 events"
grep -qF "plugins: selector's selection_changed returned -1, no longer telling it about the selection until the launcher restarts" "$log" \
	|| fail "selector's failing selection_changed wasn't logged"
grep -qF "plugin selector: event 3" "$log" && fail "selector's selection_changed was called after it failed"
# open_path from tick, from a folder's last game into folders of one game,
# a subfolder and one elsewhere, leaves each on its own game
printf 'jump=1\n' >"$STATE/selector.cfg"
run_launcher "$WORK/selector-jump.log" "plugin selector: done" || fail "the launcher exited early with selector opening smaller folders"
log="$WORK/selector-jump.log"
expect "open sub 1"
expect "in Selector/Tiny, index 0 of 1"
expect "open tiny 1"
expect "in Tiny, index 0 of 1"
# open_path into a folder with no games keeps the launcher up through its
# frames, and the folder's event has no entry
printf 'empty=1\n' >"$STATE/selector.cfg"
run_launcher "$WORK/selector-empty.log" "plugin selector: done" || fail "the launcher exited early with selector opening an empty folder"
log="$WORK/selector-empty.log"
expect "open empty 1"
expect "event 3 NULL in Selector/Empty, index -1, settled 1, select -1"
expect "in Selector/Empty, 0 games, selected NULL, after 3 events"
expect "open folder 1"
expect "back in Selector, index 0 of 7"
rm -rf "$WORK/plugins/selector.plugin" "$STATE/selector.cfg"
