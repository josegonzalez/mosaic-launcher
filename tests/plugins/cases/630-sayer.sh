# shellcheck shell=sh
# shard: 2

# sayer owns the strings slot: the game list's hints, its own action's hint
# (in its id's context) and a status pill's format show its French, each
# string asked once however many frames draw it, and host->tr and
# host->tr_plural give it its own strings by context and by its plural
# rule, English from init, for a missing one and for a translation with
# other printf conversions. presser writes down the text drawn. An answer
# that isn't UTF-8 turns the slot off, and without slots=strings it's
# never asked.
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
build_plugin sayer 'id=sayer\nname=Sayer\napi=1\nslots=strings\n'
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
[ "$(grep -c "plugin sayer: asked - OPEN$" "$log")" -eq 1 ] || fail "OPEN was asked more than once"
expect "asked sayer SHOUT"
expect "init tr Hello = Hello"
expect "tr Hello = Bonjour"
expect "tr Open = Ouvert, button Ouvrir"
expect "tr Missing = Missing"
expect "tr refused = %d games"
grep -qF "plugins: sayer's translation of \"%d games\" is refused, as its printf conversions differ from the English" "$log" \
	|| fail "the refused translation wasn't logged"
expect "plural 0 = %d jeu, 2 = %d jeux"
expect "plural other 0 = %d dirs"
expect "launcher OPEN = OPEN"

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

rm -rf "$WORK/plugins/sayer.plugin" "$SY_STATUS/sayer.status" "$STATE/plugins.txt" "$texts"
grep -qF "translates the launcher's strings" "$WORK/hello.log" && fail "strings were translated with no plugin taking the slot"
