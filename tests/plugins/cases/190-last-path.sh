# shellcheck shell=sh
# shard: 3
# follows the last path 080-drawer's launch saved, so it shares its shard

# a saved last path the launcher can't follow up to the card's root, such as
# the pak off the card drawer's launch left, starts at the root rather than
# crashing; one missing from the card is followed as far as it goes, as stock
build_plugin hello 'id=hello\nname=Hello\napi=1\n'
rm -f "$STATE/hello.cfg"
i=0
for last in "$PAK" "" "garbage" "${CARD}X/Game.gb" "$CARD/Roms/Missing (MISS)/Game.gb"; do
	i=$((i + 1))
	# the first is what drawer's launch saved
	[ "$i" -gt 1 ] && printf '%s' "$last" >/tmp/last.txt
	run_launcher "$WORK/last-$i.log" "plugin hello: hello from desktop" || fail "the launcher exited with \"$last\" as the last path"
	case "$last" in
	"$CARD"/*) grep -qF "loadLast: ignoring" "$WORK/last-$i.log" && fail "\"$last\" was ignored as the last path" ;;
	*) grep -qF "loadLast: ignoring \"$last\", it isn't on the card" "$WORK/last-$i.log" || fail "\"$last\" wasn't ignored as the last path" ;;
	esac
done
rm -rf "$WORK/plugins/hello.plugin" "$STATE/hello.cfg"
if [ -f "$WORK/last.txt.saved" ]; then cp "$WORK/last.txt.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
rm -f "$WORK/last.txt.saved"
