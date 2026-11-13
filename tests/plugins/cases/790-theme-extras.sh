# shellcheck shell=sh
# shard: 1

# theme extras: the bundled theme-plus passes on a theme's keys with a dot
# in them, the launcher refuses a bad one, and dresser reads its own key
# (its fallback from init, before the theme is in place, and for a key the
# theme doesn't set). The bundled led-status, in its default auto mode,
# says the LEDs show the theme's led-status.colour (desktop has no LEDs to
# set), and the bundled art-backdrop keeps the user's own dim over the
# theme's art-backdrop.dim. dresser then writes a new theme.txt and reloads
# the lists: on launcher.theme_changed it reads its new key, and led-status
# follows the new colour.
for name in theme-plus led-status art-backdrop; do
	make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" "$name" >/dev/null || fail "building $name"
done
build_plugin dresser 'id=dresser\nname=Dresser\napi=1\n'
TX_DIR="$CARD/Themes/Dressed"
mkdir -p "$TX_DIR" "$STATE"
printf 'version=1\nname=Dressed\nled-status.colour=0x112233\nart-backdrop.dim=75\ndresser.word=hello\nshadow=on\nodd.=1\n' \
	>"$TX_DIR/theme.txt"
printf 'theme=Dressed\n' >"$STATE/theme-plus.cfg"
printf 'dim=25\n' >"$STATE/art-backdrop.cfg"
printf 'theme_file=%s\n' "$TX_DIR/theme.txt" >"$STATE/dresser.cfg"
printf 'theme-plus=on\nled-status=on\nart-backdrop=on\ndresser=on\n' >"$STATE/plugins.txt"

log="$WORK/theme-extras.log"
run_launcher "$log" "plugin dresser: done" "plugin led-status: the LEDs show the theme colour hint, from the theme" \
	|| fail "the launcher exited early (theme extras)"
plugin=theme-plus
expect "$TX_DIR/theme.txt: line 6: unknown key shadow, ignored"
grep "plugin theme-plus:" "$log" | grep -qF "led-status.colour" && fail "theme-plus logged a key it passes on"
grep -qF "plugins: theme-plus's theme extra line 4 (odd.=1) has no <plugin>.<name> key, so it's left out" "$log" \
	|| fail "the launcher didn't refuse the bad key: $(grep "extra line" "$log")"
grep -qF "plugins: theme-plus's theme passes on 3 keys to other plugins" "$log" \
	|| fail "the launcher didn't take theme-plus's keys: $(grep "passes on" "$log")"
plugin=dresser
expect "init unset"
expect "first word=hello missing=fallback"
expect "wrote 0"
expect "changed word=changed"
plugin=led-status
expect "the LEDs show 0x112233, from the theme"
expect "the LEDs show the theme colour hint, from the theme"
plugin=art-backdrop
expect "gradient, dimmed by 64 of 255"
grep -qF "plugin art-backdrop: dimmed by" "$log" && fail "art-backdrop took the theme's dim over the user's: $(grep "dimmed by" "$log")"

rm -rf "$WORK/plugins/theme-plus.plugin" "$WORK/plugins/led-status.plugin" "$WORK/plugins/art-backdrop.plugin" \
	"$WORK/plugins/dresser.plugin" "$CARD/Themes" "$STATE/theme-plus.cfg" "$STATE/art-backdrop.cfg" "$STATE/dresser.cfg" \
	"$STATE/plugins.txt"
case_log=""
