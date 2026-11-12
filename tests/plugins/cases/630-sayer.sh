# shellcheck shell=sh
# shard: 2

# sayer owns the strings slot: the game list's hints, its own action's hint
# (under its id) and a status pill's format show its French, each string
# asked once however many frames draw it, with the plugin id and context
# it belongs to, and host->tr and host->tr_plural give it its own strings
# by context and by its plural rule, English from init, for a missing one,
# for no plugin id and for a translation with other printf conversions.
# host->plugin_info lists the plugins loaded, one installed as its own
# pak too, but not one turned off or one that declined to start, and none
# from init. presser writes down the text drawn. An answer that isn't
# UTF-8 turns the slot off, without slots=strings it's never asked, and
# built for API 1 it isn't loaded.
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
build_plugin sayer 'id=sayer\nname=Sayer\napi=2\nslots=strings\n'
SY_STATUS="$STATE/status.d"
mkdir -p "$SY_STATUS"
printf 'state=done\nlabel=Sync\n' >"$SY_STATUS/sayer.status"
printf 'sayer=on\n' >"$STATE/plugins.txt"
rm -f "$STATE/sayer.cfg"
plugin=sayer
texts="$WORK/sayer-texts.txt"

# sayer_run <name>: the launcher with sayer, until it's done and presser has
# seen a few frames
sayer_run() {
	log="$WORK/$1.log"
	rm -f "$texts"
	PRESSER_KEYS=wait PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER"
	export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
	run_launcher "$log" "plugin sayer: done" "presser: done" || fail "the launcher exited early with sayer ($1)"
	unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
}

sayer_run sayer
grep -qF "plugins: sayer translates the launcher's strings" "$log" || fail "sayer didn't take the strings slot"
drew "OUVRIR"
drew "CRIER"
grep -qxF "OPEN" "$texts" && fail "the game list drew OPEN with a translation for it"
grep -qF "status: showing sayer: Sync fini" "$log" || fail "the status pill wasn't translated: $(grep "status: showing" "$log")"
[ "$(grep -c "plugin sayer: asked - - OPEN$" "$log")" -eq 1 ] || fail "OPEN was asked more than once"
# the launcher's own text has no plugin id, a plugin's has its id, and its
# own context when it gives one
expect "asked - - %s done"
expect "asked sayer - SHOUT"
expect "asked sayer - Hello"
expect "asked sayer sayer.button Open"
expect "asked sayer  OPEN"
expect "no id OPEN = OPEN"
expect "init tr Hello = Hello"
expect "init plugins 0, first -1"
expect "listed sayer at $WORK/plugins/sayer.plugin/res"
expect "tr Hello = Bonjour"
expect "tr Open = Ouvert, button Ouvrir"
expect "tr Missing = Missing"
expect "tr refused = %d games"
grep -qF "plugins: sayer's translation of \"%d games\" is refused, as its printf conversions differ from the English" "$log" \
	|| fail "the refused translation wasn't logged"
expect "plural 0 = %d jeu, 2 = %d jeux"
expect "plural other 0 = %d dirs"
expect "launcher OPEN = OPEN"

# the listing: sayer, and hello installed as its own pak, but not sorter,
# turned off, or decliner, which declines to start
SY_PAK="$CARD/Tools/desktop/HelloListed.pak"
build_plugin hello 'id=hello\nname=Hello\napi=1\n'
build_plugin sorter 'id=sorter\nname=Sorter\napi=1\n'
build_plugin decliner 'id=decliner\nname=Decliner\napi=1\n'
rm -rf "$SY_PAK"
mkdir -p "$CARD/Tools/desktop"
mv "$WORK/plugins/hello.plugin" "$SY_PAK"
printf 'sayer=on\nsorter=off\n' >"$STATE/plugins.txt"
sayer_run sayer-list
grep -qF "plugins: loaded hello" "$log" || fail "the hello pak didn't load"
grep -qF "plugins: decliner declined to start" "$log" || fail "decliner didn't decline"
expect "listed sayer at $WORK/plugins/sayer.plugin/res"
expect "listed hello at $SY_PAK/res"
grep -qF "plugin sayer: listed sorter" "$log" && fail "a plugin turned off was listed"
grep -qF "plugin sayer: listed decliner" "$log" && fail "a plugin that declined to start was listed"
rm -rf "$SY_PAK" "$WORK/plugins/sorter.plugin" "$WORK/plugins/decliner.plugin"
printf 'sayer=on\n' >"$STATE/plugins.txt"

# an answer that isn't UTF-8 is an error: English from then on, and no more asks
printf 'mode=bad\n' >"$STATE/sayer.cfg"
sayer_run sayer-bad
grep -qF "plugins: sayer translated \"OPEN\" to text that it isn't valid UTF-8, showing English until the launcher restarts" "$log" \
	|| fail "sayer's error wasn't logged"
drew "OPEN"
grep -q "OUV" "$texts" && fail "sayer's bad OPEN was drawn"
sed -n '/translated "OPEN" to text/,$p' "$log" | grep -qF "plugin sayer: asked" && fail "sayer was asked after its error"

# without the slot it's never asked
rm -f "$STATE/sayer.cfg"
printf 'id=sayer\nname=Sayer\napi=1\n' >"$WORK/plugins/sayer.plugin/plugin.txt"
sayer_run sayer-unslotted
grep -qF "plugins: sayer translates but has no slots=strings, so it won't be asked" "$log" || fail "the missing strings slot wasn't reported"
grep -qF "plugin sayer: asked" "$log" && fail "sayer was asked without the slot"
expect "tr Hello = Hello"
drew "OPEN"

# built for API 1, whose translate took no plugin id, the strings slot's
# owner isn't loaded, and the launcher shows English
"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -DSAYER_API=1 -I"$ROOT/sdk" "$ROOT/tests/plugins/sayer/sayer.c" \
	-o "$WORK/plugins/sayer.plugin/lib/$ARCH/plugin.so" || fail "building sayer for API 1"
printf 'id=sayer\nname=Sayer\napi=1\nslots=strings\n' >"$WORK/plugins/sayer.plugin/plugin.txt"
log="$WORK/sayer-old.log"
rm -f "$texts"
PRESSER_KEYS=wait PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER"
export PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
run_launcher "$log" "plugins: sayer is built for API 1" "presser: done" || fail "the launcher exited early with sayer for API 1"
unset PRESSER_KEYS PRESSER_LOG SMOKE_PRELOAD
grep -qF "plugins: sayer is built for API 1, but the strings slot's owner needs API 2, skipping" "$log" \
	|| fail "the old strings owner wasn't refused"
grep -qF "plugins: loaded sayer" "$log" && fail "a strings owner for API 1 loaded"
grep -qF "plugin sayer:" "$log" && fail "a strings owner for API 1 ran"
drew "OPEN"

rm -rf "$WORK/plugins/sayer.plugin" "$SY_STATUS/sayer.status" "$STATE/plugins.txt" "$texts"
grep -qF "translates the launcher's strings" "$WORK/hello.log" && fail "strings were translated with no plugin taking the slot"
