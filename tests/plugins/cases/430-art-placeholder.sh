# shellcheck shell=sh
# shard: 3

# the bundled art-placeholder makes a thumbnail and a box for games with no
# art: by default a title card in the theme's colours with the name on it,
# or with the style setting a pattern from the rom's path, the same pixels
# on every start and different for each game. A game with a real thumbnail
# gets no box either, and the launcher never asks for generated art a file
# answers. carder, loaded first, logs each ask and makes nothing.
HOLD_DIR="$CARD/Placeholder"
mkdir -p "$HOLD_DIR/.media" "$STATE"
for game in Alpha Bravo Charlie; do : >"$HOLD_DIR/$game.gb"; done
cp "$ROOT/tests/plugins/drawer/art.png" "$HOLD_DIR/.media/Bravo.png"
make -s -C "$ROOT/plugins" art-placeholder ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building art-placeholder"
build_plugin carder 'id=carder\nname=Carder\napi=1\n'
printf 'carder=on\nart-placeholder=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/placeholder.log" "plugin carder: done" || fail "the launcher exited early with art-placeholder loaded"
log="$WORK/placeholder.log"
plugin=carder
grep -qF "plugins: loaded art-placeholder" "$log" || fail "art-placeholder didn't load"
expect "open folder 1"
expect "card Alpha 0 1 460x460 edge 1 panel 1 text 1 sig "
expect "card Alpha 3 1 345x460 edge 1 panel 1 text 1 sig "
expect "card Charlie 0 1 460x460 edge 1 panel 1 text 1 sig "
expect "card Charlie 3 1 345x460 edge 1 panel 1 text 1 sig "
expect "card Bravo 3 -1"
expect "real Bravo 1 FF0000"
expect "asked Alpha 0"
expect "asked Bravo 3"
grep -qF "plugin carder: asked Bravo 0" "$log" && fail "generated art was asked for Bravo's real thumbnail"

# sig <name> <kind> <log>: a card's pixel signature
sig() { sed -n "s/.*plugin carder: card $1 $2 1 .* sig \([0-9A-F]*\)$/\1/p" "$3"; }
title_alpha=$(sig Alpha 0 "$log")
printf 'style=pattern\n' >"$STATE/art-placeholder.cfg"
run_launcher "$WORK/placeholder-pattern.log" "plugin carder: done" || fail "the launcher exited early with art-placeholder's patterns"
log="$WORK/placeholder-pattern.log"
expect "card Alpha 0 1 460x460 "
expect "card Alpha 3 1 345x460 "
expect "card Bravo 3 -1"
run_launcher "$WORK/placeholder-again.log" "plugin carder: done" || fail "the launcher exited early with art-placeholder started again"
for card in "Alpha 0" "Alpha 3" "Charlie 0" "Charlie 3"; do
	# shellcheck disable=SC2086 # name and kind
	first=$(sig $card "$WORK/placeholder-pattern.log")
	# shellcheck disable=SC2086
	again=$(sig $card "$WORK/placeholder-again.log")
	[ -n "$first" ] || fail "no $card pattern"
	[ "$first" = "$again" ] || fail "the $card pattern changed between starts: $first, then $again"
done
pattern_alpha=$(sig Alpha 0 "$WORK/placeholder-pattern.log")
[ "$pattern_alpha" != "$(sig Charlie 0 "$WORK/placeholder-pattern.log")" ] || fail "Alpha and Charlie have the same pattern"
[ "$pattern_alpha" != "$title_alpha" ] || fail "the pattern style made a title card"
rm -rf "$WORK/plugins/art-placeholder.plugin" "$WORK/plugins/carder.plugin" "$HOLD_DIR" "$STATE/art-placeholder.cfg" "$STATE/plugins.txt"
