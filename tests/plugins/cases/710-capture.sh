# shellcheck shell=sh
# shard: 1

# snapper captures and saves the screen: refused from init, its own thread
# and its overlay, the game list's frame with its overlay on it, and the
# same frame while a dialog shows and from the dialog's pick callback,
# saved as Screenshots/Launcher.<date>.png. The notification follows
# stock's screenshot notification setting.
SHOTS="$CARD/Screenshots"
[ -e "$SHOTS" ] && mv "$SHOTS" "$WORK/capture-screenshots.saved"
build_plugin snapper 'id=snapper\nname=Snapper\napi=1\n'
mkdir -p "$STATE"
printf 'snapper=on\n' >"$STATE/plugins.txt"
rm -f "$STATE/snapper.cfg"
green=12AB34FF

# is_png <file>: the file starts with PNG's signature
is_png() {
	[ "$(head -c 4 "$1" | od -An -tx1 | tr -d ' \n')" = "89504e47" ]
}

# saved_as <when>: the file snapper's <when> save went to, checked
saved_as() {
	path=$(sed -n "s/.*plugin snapper: $1 path //p" "$log" | head -n 1)
	name=$(basename "$path")
	[ "$(dirname "$path")" = "$SHOTS" ] || fail "snapper's $1 save went to $path"
	printf '%s\n' "$name" | grep -Eq '^Launcher\.[0-9]{4}-[0-9]{2}-[0-9]{2}-[0-9]{2}-[0-9]{2}-[0-9]{2}\.png$' \
		|| fail "snapper's $1 save was named $name"
	if [ -f "$path" ] && is_png "$path"; then :; else fail "snapper's $1 save isn't a PNG at $path"; fi
}

run_launcher "$WORK/capture.log" "plugin snapper: done" || fail "the launcher exited early with snapper"
log="$WORK/capture.log"
plugin=snapper
expect "init capture null save -1"
expect "thread capture null save -1"
grep -qF "plugins: snapper's capture_screen refused: not from init" "$log" || fail "the refusal from init wasn't logged"
expect "overlay capture null"
grep -qF "plugins: snapper's capture_screen refused: not while drawing" "$log" || fail "the refusal while drawing wasn't logged"
# the game list's frame, its overlay included
expect "list on gamelist capture full 1 probe $green"
expect "list save 0 Launcher."
saved_as list
expect "choose 0"
# the dialog's own frame has no overlay: the frame from before it does
expect "menu on dialog capture full 1 probe $green"
expect "picked 0 on gamelist capture full 1 probe $green"
expect "pick save 0 Launcher."
saved_as pick
expect "after on gamelist capture full 1 probe $green"
expect "filled 1 over budget capture null"
grep -qF "plugins: refused a 1024x768 surface for snapper" "$log" || fail "the capture over the budget wasn't refused"
# stock's setting is on by default
grep -qF "capture: saved $SHOTS/Launcher." "$log" || fail "the save wasn't logged"
grep -q "capture: saved .*, with the notification" "$log" || fail "the notification didn't show with the setting on"

# with stock's screenshot notification setting off, none shows
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/capture-settings.saved"
printf 'notifyScreenshot=0\n' >"$SETTINGS"
printf 'quick=1\n' >"$STATE/snapper.cfg"
SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/capture-quiet.log" "plugin snapper: done" \
	|| fail "the launcher exited early with the notification off"
if [ -f "$WORK/capture-settings.saved" ]; then mv "$WORK/capture-settings.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
log="$WORK/capture-quiet.log"
expect "list save 0 Launcher."
saved_as list
grep -qF "with the notification" "$log" && fail "the notification showed with the setting off"

rm -rf "$SHOTS" "$WORK/plugins/snapper.plugin" "$STATE/snapper.cfg"
[ -e "$WORK/capture-screenshots.saved" ] && mv "$WORK/capture-screenshots.saved" "$SHOTS"
case_log=""
