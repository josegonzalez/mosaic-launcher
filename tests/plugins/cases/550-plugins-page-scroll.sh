# shellcheck shell=sh
# shard: 2

# the Launcher pak's Plugins page with more rows than fit: twelve plugins
# (only manifests, which is all the page reads) and the PIN row, each in a
# folder of its own so they're listed in this order. presser saves the
# last frame before it's done, with the texts drawn for it and their
# colours, so a case can check which rows are on screen, which one is drawn
# selected, and that the selected row's pill is under it. The desktop build
# shows MAIN_ROW_COUNT 7 rows: the title and six list rows.
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
# short folders: MOSAIC_LAUNCHER_PLUGIN_PATH holds at most 1023 characters
SCROLL_DIR=/tmp/mosaic-launcher-scroll
rm -rf "$SCROLL_DIR"
scroll_path=""
choices=""
for n in 01 02 03 04 05 06 07 08 09 10 11 12; do
	mkdir -p "$SCROLL_DIR/$n/s$n.plugin"
	printf 'id=s%s\nname=Plugin %s\napi=1\n' "$n" "$n" >"$SCROLL_DIR/$n/s$n.plugin/plugin.txt"
	scroll_path="$scroll_path${scroll_path:+:}$SCROLL_DIR/$n"
	choices="${choices}s$n=off\n"
done
# the last has more settings than fit, the last a choice with more options
# than fit
{
	for t in 1 2 3 4 5 6 7; do printf 'setting.t%s=toggle|Toggle %s|off\n' "$t" "$t"; done
	printf 'setting.pick=choice|Pick|o01|o01,o02,o03,o04,o05,o06,o07,o08,o09,o10\n'
} >>"$SCROLL_DIR/12/s12.plugin/plugin.txt"
SCROLL_CFG="$STATE/s12.cfg"

# scroll_page <name> <keys>: opens the Plugins page from all off and
# presses keys; the last frame goes to $WORK/<name>.bmp and .txt
scroll_page() {
	case_log="$WORK/$1.log"
	frame="$WORK/$1"
	rm -f "$frame.bmp" "$frame.txt" "$SCROLL_CFG"
	mkdir -p "$STATE"
	printf '%b' "$choices" >"$STATE/plugins.txt"
	PRESSER_KEYS="$2" PRESSER_FRAME="$frame" SMOKE_PRELOAD="$PRESSER" SMOKE_UNTIL="presser: done" \
		MOSAIC_LAUNCHER_PAK_DIR="$ROOT/pak" MOSAIC_LAUNCHER_PLUGIN_PATH="$scroll_path" \
		"$ROOT/scripts/desktop-smoke.sh" "$case_log" 40 --settings plugins >/dev/null 2>"$WORK/smoke.err"
	status=$?
	[ "$status" -eq 0 ] || fail "the Plugins page didn't run its presses (status $status)"
	if [ ! -s "$frame.bmp" ] || [ ! -s "$frame.txt" ]; then fail "presser saved no frame"; fi
}

# rows_drawn <pattern>: the texts drawn matching the pattern (a grep -E
# for whole texts), in order, separated by commas
rows_drawn() {
	sed -n 's/^[0-9a-f]\{6\} //p' "$frame.txt" | grep -xE -- "$1" | tr '\n' ',' | sed 's/,$//'
}

# selected_row <pattern>: the one text matching the pattern drawn in a
# colour none of the others has
selected_row() {
	grep -E -- "^[0-9a-f]{6} ($1)\$" "$frame.txt" | awk '{
		colour[NR] = $1; text[NR] = substr($0, 8); count[$1]++
	} END {
		for (i = 1; i <= NR; i++) if (count[colour[i]] == 1 && NR > 1) print text[i]
	}'
}

# pixel <x> <y>: the frame's pixel as "b g r", at 1024x768 coordinates
pixel() {
	w=$(od -An -tu4 -j18 -N4 "$frame.bmp" | tr -d ' ')
	s=$((w / 1024))
	[ "$s" -ge 1 ] || s=1
	od -An -tu1 -j$((54 + ($2 * s * w + $1 * s) * 4)) -N3 "$frame.bmp" | tr -s ' ' | sed 's/^ //'
}

# pill_rows: the list rows (1 to 6) with a pill: on the desktop each is 90
# pixels from y 105, and a pill starts at x 15, so x 30 differs from the
# background at x 5 only under one
pill_rows() {
	for j in 1 2 3 4 5 6; do
		y=$((105 + (j - 1) * 90 + 45))
		[ "$(pixel 30 "$y")" != "$(pixel 5 "$y")" ] && printf '%s ' "$j"
	done
}

# expect_page <rows> <selected> <pill row>
expect_page() {
	if [ ! -s "$frame.bmp" ] || [ ! -s "$frame.txt" ]; then return; fi
	got=$(rows_drawn "$row_pattern")
	[ "$got" = "$1" ] || fail "the rows drawn were $got, not $1"
	got=$(selected_row "$row_pattern")
	[ "$got" = "$2" ] || fail "the row drawn selected was \"$got\", not \"$2\""
	got=$(pill_rows)
	[ "$got" = "$3 " ] || fail "the pill was on rows \"$got\", not $3"
}

row_pattern="Plugin [0-9]+|PIN"

# down past the last row on screen scrolls by one, keeping the selection
# on the bottom row, and A changes the selected row
scroll_page scroll-down "down down down down down down down a"
expect_page "Plugin 03,Plugin 04,Plugin 05,Plugin 06,Plugin 07,Plugin 08" "Plugin 08" 6
grep -qxF "s08=on" "$STATE/plugins.txt" || fail "A didn't turn on the selected plugin: $(tr '\n' ' ' <"$STATE/plugins.txt")"
grep -qxF "s01=off" "$STATE/plugins.txt" || fail "A turned on a plugin it wasn't on"

# back up above the first row on screen scrolls the other way
scroll_page scroll-up "down down down down down down down up up up up up up"
expect_page "Plugin 02,Plugin 03,Plugin 04,Plugin 05,Plugin 06,Plugin 07" "Plugin 02" 1

# up from the first row wraps to the last, and down from it back
scroll_page scroll-wrap "up"
expect_page "Plugin 08,Plugin 09,Plugin 10,Plugin 11,Plugin 12,PIN" "PIN" 6
scroll_page scroll-wrap-back "up down"
expect_page "Plugin 01,Plugin 02,Plugin 03,Plugin 04,Plugin 05,Plugin 06" "Plugin 01" 1

# a plugin's settings and a choice's options scroll the same way: up from
# Status wraps to Pick, and the options open on o01, where up wraps to o10
row_pattern="o[0-9]+"
scroll_page scroll-choice "up up a up a up"
expect_page "o05,o06,o07,o08,o09,o10" "o10" 6
row_pattern="Status|Toggle [0-9]|Pick"
scroll_page scroll-settings "up up a up a up a"
expect_page "Toggle 3,Toggle 4,Toggle 5,Toggle 6,Toggle 7,Pick" "Pick" 6
grep -qxF "pick=o10" "$SCROLL_CFG" 2>/dev/null || fail "the option picked wasn't saved: $(cat "$SCROLL_CFG" 2>/dev/null)"
rm -rf "$SCROLL_DIR" "$SCROLL_CFG" "$STATE/plugins.txt"
case_log=""
