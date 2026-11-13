# shellcheck shell=sh
# shard: 1

# events: herald publishes from init and from a thread of its own, and
# listener hears them, as itself, on the launcher's thread. A name outside
# herald's prefix and a bad payload are refused. listener adds a favorite
# through the bundled favorites plugin and hears launcher.set_changed for
# it, then lists the set names. A launch marker left from before the start
# is heard as launcher.return. decliner subscribes and declines, which
# releases its subscription before anything is delivered to it.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" favorites >/dev/null || fail "building favorites"
build_plugin decliner 'id=decliner\nname=Decliner\napi=1\n'
build_plugin herald 'id=herald\nname=Herald\napi=1\n'
build_plugin listener 'id=listener\nname=Listener\napi=1\n'
# a folder inside one, as stock lists a folder right under Roms as a console
EVENTS_DIR="$CARD/Roms/.events-test/Games"
EVENTS_FAV="$CARD/Collections/Favorites.txt"
mkdir -p "$EVENTS_DIR" "$CARD/Collections" "$STATE"
: >"$EVENTS_DIR/Fav.gb"
if [ -f "$EVENTS_FAV" ]; then cp "$EVENTS_FAV" "$WORK/events-favorites.saved"; fi
: >"$EVENTS_FAV"
printf 'folder=%s\nprobe=listener\n' "$EVENTS_DIR" >"$STATE/listener.cfg"
printf 'decliner=on\nfavorites=on\nlistener=on\nherald=on\n' >"$STATE/plugins.txt"
printf 'path=/Roms/Before.gb\ntype=rom\n' >/tmp/mosaic-launcher-launched

log="$WORK/events.log"
run_launcher "$log" "plugin listener: done" "plugin herald: thread" || fail "the launcher exited early (events)"
grep -qF "plugins: decliner's subscriptions are released (1)" "$log" || fail "decliner's subscription wasn't released when it declined"
plugin=herald
expect "init 0 -1 -1"
expect "thread 0 -1"
grep -qF "plugins: herald can't publish music.track: it may only publish herald.<name>" "$log" \
	|| fail "a name outside herald's prefix wasn't refused"
grep -qF "plugins: herald's event herald.bad was refused: its payload isn't key=value lines" "$log" \
	|| fail "a bad payload wasn't refused"
plugin=listener
expect "subscribe 0 0 0 0 -1"
expect "got herald.hello [] as listener"
expect "got herald.ping [n=1,from=thread] as listener"
expect "ping n=1 from=thread missing=-1"
expect "got launcher.return [path=/Roms/Before.gb,type=rom,] as listener"
expect "open folder 1"
expect "toggle 0"
expect "got launcher.set_changed [name=favorites] as listener"
expect "names 1 favorites"
grep -qF "/Roms/.events-test/Games/Fav.gb" "$EVENTS_FAV" || fail "listener's toggle didn't add the favorite"

if [ -f "$WORK/events-favorites.saved" ]; then mv "$WORK/events-favorites.saved" "$EVENTS_FAV"; else rm -f "$EVENTS_FAV"; fi
rm -rf "$CARD/Roms/.events-test" "$WORK/plugins/favorites.plugin" "$WORK/plugins/decliner.plugin" "$WORK/plugins/herald.plugin" \
	"$WORK/plugins/listener.plugin" "$STATE/listener.cfg" "$STATE/plugins.txt" /tmp/mosaic-launcher-launched
case_log=""
