# shellcheck shell=sh
# shard: 3

# badger decorates the built-in list's rows and adds a header widget beside
# the status pill; its drawing lands in the row or widget and is clipped to
# it, select_index refuses during both, and the top row stops short of the
# widget
mkdir -p "$STATE"
printf 'badger=on\n' >"$STATE/plugins.txt"
build_plugin badger 'id=badger\nname=Badger\napi=1\n'
run_launcher "$WORK/badger.log" "plugin badger: rows " || fail "the launcher exited early with badger loaded"
log="$WORK/badger.log"
plugin=badger
expect "open folder 1"
expect "decorate outside a layout -1"
grep -qE "plugin badger: width first: room [0-9]+ height 90$" "$log" || fail "badger wasn't asked its width: $(grep "plugin badger: width" "$log")"
grep -qE "plugin badger: widget rect [0-9]+,15 60x90 fill 0 0000FF, clipped 1 \(fill 0\), select -1$" "$log" \
	|| fail "badger's widget is wrong: $(grep "plugin badger: widget" "$log")"
grep -qE "plugin badger: row Apple index 0 selected 1 rect 15,15 [0-9]+x90 name 15,15 [0-9]+x90, row size 1$" "$log" \
	|| fail "badger's selected row is wrong: $(grep "plugin badger: row Apple index" "$log")"
grep -qE "plugin badger: row Banana index 1 selected 0 rect 15,105 [0-9]+x90 name 15,105 [0-9]+x90, row size 1$" "$log" \
	|| fail "badger's second row is wrong: $(grep "plugin badger: row Banana index" "$log")"
expect "row Apple dot 0 FF0000, clipped 1 (fill 0), select -1"
expect "row Fig dot 0 FF0000, clipped 1 (fill 0), select -1"
expect "row Apple clear of the widget 1, widget 0000FF"
expect "rows 4, widths 3, widgets 3"
grep -qF "plugins: badger's" "$log" && fail "badger's hooks were turned off"
# the widget's room is half the header left of the pill (past the
# padding), less the gap
pill_x=$(($(sed -n 's/.*plugin badger: widget rect \([0-9]*\),.*/\1/p' "$log") + 60 + 15))
grep -qF "plugin badger: width first: room $(((pill_x - 15) / 2 - 15)) height 90" "$log" || fail "badger's room isn't half the header"
# the widget takes its width and the gap off the top row only
top_row() { sed -n "s/.*plugin badger: row $1 index [0-9]* selected [01] rect [0-9]*,[0-9]* \([0-9]*\)x90 .*/\1/p" "$2"; }
with_widget=$(top_row Apple "$log")
other_row=$(top_row Banana "$log")
grep -qF "plugins: badger has" "$log" && fail "badger's hooks were reported as unpaired"
grep -qE "decorate_row|header widget" "$WORK/hello.log" && fail "a plugin without decorators logged them"

# a widget needs both hooks; with one, it's reported and never asked
# (exported and unset, since an assignment before a function call outlives it in sh)
export BADGER_NO_WIDTH=1
run_launcher "$WORK/badger-unpaired.log" "plugin badger: rows " || fail "the launcher exited early with badger unpaired"
unset BADGER_NO_WIDTH
log="$WORK/badger-unpaired.log"
grep -qF "plugins: badger has header_widget but no header_width, so its header widget won't show" "$log" || fail "the unpaired header hook wasn't reported"
expect "rows 4, widths 0, widgets 0"

# with no widget, the top row is stock's, as wide as without plugins
printf 'width=0\n' >"$STATE/badger.cfg"
run_launcher "$WORK/badger-nowidget.log" "plugin badger: rows " || fail "the launcher exited early with badger's widget off"
log="$WORK/badger-nowidget.log"
expect "rows 4, widths 3, widgets 0"
without=$(top_row Apple "$log")
if [ -z "$without" ] || [ -z "$with_widget" ] || [ $((without - with_widget)) -ne 75 ]; then
	fail "the widget didn't take 75 off the top row: $without without, $with_widget with"
fi
[ "$(top_row Banana "$log")" = "$other_row" ] || fail "the widget changed a row below the header"
grep -qF "row Apple clear of the widget" "$log" && fail "badger saw a widget it didn't ask for"

# an error turns that plugin's hook off until the launcher restarts, and
# the rest keep running
for hook in row width widget; do
	printf 'fail=%s\n' "$hook" >"$STATE/badger.cfg"
	run_launcher "$WORK/badger-$hook.log" "plugin badger: rows " || fail "the launcher exited early with badger's $hook failing"
	log="$WORK/badger-$hook.log"
	case "$hook" in
	row) line="plugins: badger's decorate_row returned -1, leaving its decorations out until the launcher restarts"; want="rows 3, widths 3, widgets 3" ;;
	width) line="plugins: badger's header_width returned -1, leaving its header widget out until the launcher restarts"; want="rows 4, widths 2, widgets 1" ;;
	widget) line="plugins: badger's header_widget returned 1, leaving its header widget out until the launcher restarts"; want="rows 4, widths 2, widgets 2" ;;
	esac
	grep -qF "$line" "$log" || fail "badger's failing $hook wasn't logged"
	[ "$(grep -cF "plugins: badger's" "$log")" -eq 1 ] || fail "badger's failing $hook was logged more than once"
	expect "$want"
done
rm -f "$STATE/badger.cfg"

# decorators and widgets are chained in load order: the first decorates
# each row first, and its widget is next to the pill
build_plugin captioner 'id=captioner\nname=Captioner\napi=1\n'
printf 'badger=on\ncaptioner=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/captioner.log" "plugin badger: rows " "plugin captioner: row Apple" || fail "the launcher exited early with captioner loaded"
log="$WORK/captioner.log"
plugin=captioner
expect "row Apple after badger 1"
badger_x=$(sed -n 's/.*plugin badger: widget rect \([0-9]*\),.*/\1/p' "$log")
grep -qF "plugin captioner: widget rect $((badger_x - 15 - 40)),15 40x90, badger on the right 1" "$log" \
	|| fail "captioner's widget isn't left of badger's: $(grep "plugin captioner: widget" "$log")"
printf 'captioner=on\nbadger=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/captioner-first.log" "plugin badger: rows " "plugin captioner: row Apple" || fail "the launcher exited early with captioner first"
log="$WORK/captioner-first.log"
expect "row Apple after badger 0"
grep -qE "plugin captioner: widget rect [0-9]+,15 40x90, badger on the right 0$" "$log" || fail "captioner's widget isn't next to the pill"
rm -rf "$WORK/plugins/captioner.plugin"

# under a plugin layout, the layout passes each cell to decorate_cell, which
# runs the decorators clipped to the cell, and the view has the widget
build_plugin gridder 'id=gridder\nname=Gridder\napi=1\nslots=layout\n'
printf 'gridder=on\nbadger=on\n' >"$STATE/plugins.txt"
printf 'open=0\nfolder=Gridder\n' >"$STATE/badger.cfg"
run_launcher "$WORK/badger-grid.log" "plugin gridder: renders" "plugin badger: rows " || fail "the launcher exited early with badger under gridder"
log="$WORK/badger-grid.log"
plugin=gridder
expect "render header 75x90, beside status 1"
expect "render decorated 0, bad index -1 -1, no rect -1, outside -1, not the screen -1"
expect "after render fill -1, decorate -1"
plugin=badger
expect "row Apple index 0 selected 1 rect 4,4 248x172 name 4,4 0x172, row size 1"
expect "row Elder index 4 selected 0 rect 4,184 248x172 name 4,184 0x172, row size 1"
expect "row Apple dot 0 FF0000, clipped 1 (fill 0), select -1"
grep -qF "plugins: badger's" "$log" && fail "badger's hooks were turned off under gridder"
grep -qF "plugins: gridder's" "$log" && fail "gridder's layout was turned off with badger"
rm -rf "$WORK/plugins/gridder.plugin" "$WORK/plugins/badger.plugin" "$STATE/badger.cfg" "$STATE/plugins.txt"
