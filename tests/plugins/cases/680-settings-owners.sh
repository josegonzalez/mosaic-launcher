# shellcheck shell=sh
# shard: 2

# the Launcher pak's settings load only the strings and theme slots'
# owners: with sayer (strings) and themer (theme) on, the Plugins page's
# title and a group header are sayer's French, sayer's name, setting label
# and options are its own, asked under its id, a typed value stays as
# typed, and themer's background colour and sizes are drawn, with the rows
# scrolling to fit; hello, on but owning neither, isn't loaded, though
# host->plugin_info lists it with the other two, as the page shows it. An option
# picked from its translated name is saved as plugin.txt has it. With
# neither owner on the page is stock's English. A crash in the strings
# owner leaves it out of the next open, which shows English and can turn
# it off, and plugins the launcher paused aren't loaded either. presser
# presses the buttons and writes down the text drawn and the last frame.
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
# its own folder, so plugins other cases left in $WORK/plugins don't load
SO_PLUGINS="$WORK/settings-owners"
rm -rf "$SO_PLUGINS"
build_plugin sayer 'id=sayer\nname=Sayer\napi=2\nslots=strings\nsetting.greet=choice|Greeting|Open|Hello,Open,Missing\nsetting.note=text|Note|Hello\n'
build_plugin themer 'id=themer\nname=Themer\napi=1\nslots=theme\n'
build_plugin hello 'id=hello\nname=Hello\napi=1\n'
mkdir -p "$SO_PLUGINS"
mv "$WORK/plugins/sayer.plugin" "$WORK/plugins/themer.plugin" "$WORK/plugins/hello.plugin" "$SO_PLUGINS/"
SO_MARKER="$STATE/settings_loading"
rm -f "$STATE/sayer.cfg" "$STATE/themer.cfg" "$SO_MARKER"
printf 'mode=good\n' >"$STATE/themer.cfg"
texts="$WORK/settings-owners.texts"

# so_page <name> <keys>: opens the Plugins page, keeping the crash guard's
# state, and presses keys; what it drew goes to $texts, the last frame to
# $WORK/<name>.bmp and .txt, and the status to $status
so_page() {
	case_log="$WORK/$1.log"
	log="$case_log"
	frame="$WORK/$1"
	rm -f "$texts" "$frame.bmp" "$frame.txt"
	: >"$texts"
	PRESSER_KEYS="$2" PRESSER_LOG="$texts" PRESSER_FRAME="$frame" SMOKE_PRELOAD="$PRESSER" SMOKE_UNTIL="presser: done" \
		MOSAIC_LAUNCHER_PAK_DIR="$ROOT/pak" MOSAIC_LAUNCHER_PLUGIN_PATH="$SO_PLUGINS" \
		"$ROOT/scripts/desktop-smoke.sh" "$case_log" 40 --settings plugins >/dev/null 2>"$WORK/smoke.err"
	status=$?
}

# so_ran: the page ran its presses
so_ran() {
	[ "$status" -eq 0 ] || fail "the Plugins page didn't run its presses (status $status)"
}

# so_background: the frame's background, as rrggbb, at 1024x768's (5, 5)
so_background() {
	w=$(od -An -tu4 -j18 -N4 "$frame.bmp" | tr -d ' ')
	s=$((w / 1024))
	[ "$s" -ge 1 ] || s=1
	od -An -tu1 -j$((54 + (5 * s * w + 5 * s) * 4)) -N3 "$frame.bmp" | awk '{ printf "%02X%02X%02X\n", $3, $2, $1 }'
}

# so_rows: the frame's texts matching a grep -E for whole texts, in order,
# separated by commas
so_rows() {
	sed -n 's/^[0-9a-f]\{6\} //p' "$frame.txt" | grep -xE -- "$1" | tr '\n' ',' | sed 's/,$//'
}

clear_guard
printf 'sayer=on\nthemer=on\nhello=on\n' >"$STATE/plugins.txt"

# both owners: the page in sayer's French and themer's colours and sizes
so_page so-owners "wait"
so_ran
grep -qF "plugins: sayer translates the launcher's strings" "$log" || fail "sayer didn't take the strings slot in the settings"
grep -qF "plugins: themer supplies the theme: pill 36" "$log" || fail "themer's theme wasn't put in place in the settings"
grep -qF "plugins: not loading hello: the settings load only the strings and theme slots' owners" "$log" \
	|| fail "hello's not loading wasn't logged"
grep -qF "plugins: loaded hello" "$log" && fail "hello was loaded in the settings"
# the listing has every plugin the Plugins page shows, loaded or not
for so_id in sayer themer hello; do
	grep -qF "plugin sayer: listed $so_id at $SO_PLUGINS/$so_id.plugin/res" "$log" || fail "the settings' listing left out $so_id"
done
drew "Extensions"
drew "Langue"
drew "Diseur"
grep -qxF "Plugins" "$texts" && fail "the title was drawn in English with a translation for it"
[ "$(so_background)" = "123456" ] || fail "the background wasn't themer's: $(so_background)"
# themer's rows are 108 high, so four list rows fit under the title
got=$(so_rows "Langue|Diseur|Theme|Themer|Other plugins|Hello|PIN")
[ "$got" = "Langue,Diseur,Theme,Themer" ] || fail "the rows drawn were $got"
[ -e "$SO_MARKER" ] && fail "the settings left their crash marker after closing"

# sayer's own page and its choice: its name, label and options in its
# context, the typed note as typed, and the option picked saved in English
so_page so-plugin "a down a up a"
so_ran
drew "Diseur"
drew "Statut"
drew "Salutation"
drew "Ouvert"
drew "Bonjour"
drew "Missing"
drew "Hello"
grep -qxF "Greeting" "$texts" && fail "sayer's label was drawn in English"
grep -qxF "greet=Hello" "$STATE/sayer.cfg" 2>/dev/null || fail "the option picked wasn't saved as Hello: $(cat "$STATE/sayer.cfg" 2>/dev/null)"
grep -qF "plugin sayer: asked sayer - Greeting" "$log" || fail "sayer's label wasn't asked under its id"

# neither owner: stock's English, colours and rows
printf 'sayer=off\nthemer=off\nhello=on\n' >"$STATE/plugins.txt"
so_page so-stock "wait"
so_ran
drew "Plugins"
drew "Language"
drew "Sayer"
grep -qF "translates the launcher's strings" "$log" && fail "strings were translated with the owner off"
grep -qF "supplies the theme" "$log" && fail "a theme was put in place with the owner off"
grep -qF "plugins: loaded" "$log" && fail "a plugin was loaded with no owner on"
stock_bg=$(so_background)
[ "$stock_bg" = "123456" ] && fail "themer's background was drawn with it off"
got=$(so_rows "Language|Sayer|Theme|Themer|Other plugins|Hello|PIN")
[ "$got" = "Language,Sayer,Theme,Themer,Other plugins,Hello" ] || fail "stock's rows drawn were $got"

# a crashing owner: the open it crashes leaves the marker, the next leaves
# it out, in English, and turns it off, and then it isn't loaded again
printf 'sayer=on\nthemer=on\nhello=on\n' >"$STATE/plugins.txt"
printf 'mode=crash\n' >"$STATE/sayer.cfg"
so_page so-crash "wait"
[ "$status" -eq 0 ] && fail "the crashing sayer didn't crash the settings"
grep -qF "plugin sayer: crashing" "$log" || fail "sayer didn't crash: $(tail -n 5 "$log")"
[ -e "$SO_MARKER" ] || fail "no settings crash marker after the crash"
so_page so-after-crash "right"
so_ran
grep -qF "plugins: the settings crashed with plugins loaded, so they load none this time" "$log" \
	|| fail "the settings didn't leave plugins out after the crash"
grep -qF "plugins: loaded" "$log" && fail "a plugin was loaded right after the settings crashed"
drew "Plugins"
[ "$(so_background)" = "$stock_bg" ] || fail "the settings after the crash weren't stock's colours"
[ -e "$SO_MARKER" ] && fail "the crash marker was left after the open without plugins"
grep -qxF "sayer=off" "$STATE/plugins.txt" || fail "sayer wasn't turned off after the crash: $(tr '\n' ' ' <"$STATE/plugins.txt")"
so_page so-crash-off "wait"
so_ran
grep -qF "plugin sayer: crashing" "$log" && fail "sayer was loaded again once off"
grep -qF "plugins: themer supplies the theme" "$log" || fail "themer wasn't loaded once sayer was off"

# plugins the launcher paused after a crash aren't loaded here either
rm -f "$STATE/sayer.cfg"
printf 'sayer=on\nthemer=on\n' >"$STATE/plugins.txt"
: >"$STATE/plugins_paused"
so_page so-paused "wait"
so_ran
grep -qF "plugins: paused after a crash, so the settings load none" "$log" || fail "the launcher's pause wasn't kept in the settings"
grep -qF "plugins: loaded" "$log" && fail "a plugin was loaded in the settings while plugins were paused"
drew "Plugins"

clear_guard
rm -rf "$SO_PLUGINS" "$STATE/sayer.cfg" "$STATE/themer.cfg" "$STATE/plugins.txt" "$SO_MARKER" "$texts"
case_log=""
