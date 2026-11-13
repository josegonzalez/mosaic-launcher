# shellcheck shell=sh
# shard: 2

# action paths: reader runs pather.show, which takes an entry's path, on a
# game with run_action, and pather.plain, which doesn't, gets no path. With
# an empty path on the game list pather.show gets the selected entry, on
# the quick menu it's refused, and pressing Y, the button pather binds it
# to, gives it the selected entry. The listing has the game list's own
# actions with their buttons and pather's with its bind_button, the setting
# its plugin.txt marks for it, and its FN1 binding, and copies only as much
# as a smaller struct holds. Desktop has no FN buttons, so
# MOSAIC_LAUNCHER_FN_FAKE stands in for one.
AP_GAME="$CARD/Roms/Action Paths (APT)/Game.gb"
mkdir -p "$(dirname "$AP_GAME")" "$STATE"
: >"$AP_GAME"
build_plugin pather 'id=pather\nname=Pather\napi=1\nsetting.button=choice|Show button|Y|Y,L2|button:pather.show\nsetting.fn=toggle|Plain on FN1|on||button:pather.plain\n'
build_plugin reader 'id=reader\nname=Reader\napi=1\n'
printf 'probe=pather\n' >"$STATE/pather.cfg"
printf 'game=%s\n' "$AP_GAME" >"$STATE/reader.cfg"
printf 'pather=on\nreader=on\n' >"$STATE/plugins.txt"

log="$WORK/action-paths.log"
MOSAIC_LAUNCHER_FN_FAKE="L3,," run_launcher "$log" "plugin reader: done" || fail "the launcher exited early (action paths)"
selected=$(sed -n 's/.*plugin reader: selected //p' "$log" | head -n 1)
if [ -z "$selected" ] || [ "$selected" = "(none)" ]; then fail "reader saw no selected entry"; fi
plugin=pather
expect "bind 0 fn 0"
expect "ran 1 $AP_GAME as pather"
expect "plain (null) as pather"
expect "ran 2 $selected as pather"
expect "ran 3 $selected as pather"
grep -qF "plugin pather: ran 4" "$log" && fail "pather.show ran more than three times"
plugin=reader
expect "run path 0"
expect "run plain 0"
expect "run empty 0"
expect "run menu 0"
expect "run none -1 on quickmenu"
grep -qF "plugins: can't run action pather.show: it acts on an entry, and none was given or is selected" "$log" \
	|| fail "pather.show without a path wasn't refused"
# the launcher's own, with the game list's buttons: B, A, X on release and
# MENU on release
expect "action gamelist.back 'BACK' owner '' path 0 binds 1 [0:0:1:0::]"
expect "action gamelist.open 'OPEN' owner '' path 0 binds 1 [0:0:0:0::]"
expect "action gamelist.resume 'RESUME' owner '' path 0 binds 1 [0:0:2:1::]"
expect "action gamelist.quick_menu 'MENU' owner '' path 0 binds 1 [0:0:14:1::]"
# pather's: Y by bind_button, following its button setting, and FN1 by
# fn_bind, following its fn setting
expect "action pather.show 'SHOW' owner 'pather' path 1 binds 1 [1:0:3:0:pather:button]"
expect "action pather.plain 'PLAIN' owner 'pather' path 0 binds 1 [4:-1:16:0:pather:fn]"
expect "small 0 gamelist.back x"
expect "bad -1 -1 -1"

rm -rf "$(dirname "$AP_GAME")" "$STATE/pather.cfg" "$STATE/reader.cfg" "$STATE/plugins.txt" \
	"$WORK/plugins/pather.plugin" "$WORK/plugins/reader.plugin"
