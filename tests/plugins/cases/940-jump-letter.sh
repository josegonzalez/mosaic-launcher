# shellcheck shell=sh

# the bundled jump-letter, with seeker pressing the keys: the launcher
# starts on Delta in a test system (the last path). Holding SELECT opens
# the strip of the letters there are, # for 1942 and Árbol and été in A
# and E, lit on D; left twice and letting go jumps to A. Held again, right
# and B close it, and letting go then jumps nowhere. A tap of SELECT
# still opens the game switcher. The action run without its button, as
# from an FN button, opens the strip until A jumps. With search's button
# on SELECT, jump-letter leaves it be, and seeker's sort by the last word
# of each name gives its own sections, years, through section_key. The
# card's /tmp/last.txt is put back afterwards.
JL_ROMS="$CARD/Roms/Jump Letter (JLT)"
JL_YEARS="$CARD/Roms/Jump Years (JLY)"
JL_EMU="$CARD/Emus/desktop/JLT.pak"
JL_YEARS_EMU="$CARD/Emus/desktop/JLY.pak"
rm -rf "$JL_ROMS" "$JL_YEARS"
mkdir -p "$JL_ROMS" "$JL_YEARS" "$JL_EMU" "$JL_YEARS_EMU" "$STATE"
: >"$JL_EMU/launch.sh"
: >"$JL_YEARS_EMU/launch.sh"
for game in 1942 Alpha Bravo Delta Echo "$(printf '\303\201rbol')" "$(printf '\303\251t\303\251')"; do
	: >"$JL_ROMS/$game.gb"
done
for game in "Alpha 1993" "Bravo 1991" "Charlie 1993" "Delta 1992"; do : >"$JL_YEARS/$game.gb"; done
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/jump-letter-last.txt.saved"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" jump-letter search >/dev/null \
	|| fail "building jump-letter and search"
build_plugin seeker 'id=seeker\nname=Seeker\napi=1\n'

# the plugins named on, and every other plugin built so far off, as a
# plugin plugins.txt doesn't name loads
jl_plugins() {
	: >"$STATE/plugins.txt"
	for dir in "$WORK"/plugins/*.plugin; do
		id=$(basename "$dir" .plugin)
		case " $* " in
		*" $id "*) printf '%s=on\n' "$id" >>"$STATE/plugins.txt" ;;
		*) printf '%s=off\n' "$id" >>"$STATE/plugins.txt" ;;
		esac
	done
}

jl_plugins jump-letter seeker
printf '%s' "$JL_ROMS/Delta.gb" >/tmp/last.txt
printf 'steps=%s\n' "+select,wait,wait,left,left,-select,wait,+select,wait,wait,right,b,-select,wait,select,wait,b,wait,action:jump-letter.strip,right,a" \
	>"$STATE/seeker.cfg"
log="$WORK/jump-letter.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early with jump-letter"
plugin=jump-letter
expect "jump-letter.strip on SELECT"
# in order: the hold, the hold B closed, and the action run without its button
got=$(sed -n 's/.*plugin jump-letter: //p' "$log" | sed "s|$JL_ROMS|JLT|" | tr '\n' '|')
want="jump-letter.strip on SELECT|strip JLT: #, A, B, D, E at D|at B|at A|jump to A|strip JLT: #, A, B, D, E at A|at B|cancelled|"
want="${want}strip JLT: #, A, B, D, E at A|at B|jump to B|"
[ "$got" = "$want" ] || fail "jump-letter logged \"$got\", not \"$want\""
plugin=seeker
expect "list $JL_ROMS: 1942, Alpha, Bravo, Delta, Echo, $(printf '\303\201rbol'), $(printf '\303\251t\303\251') selected 3"
expect "list $JL_ROMS: 1942, Alpha, Bravo, Delta, Echo, $(printf '\303\201rbol'), $(printf '\303\251t\303\251') selected 1"
expect "list $JL_ROMS: 1942, Alpha, Bravo, Delta, Echo, $(printf '\303\201rbol'), $(printf '\303\251t\303\251') selected 2"
# the tap after B still opens the game switcher
expect "screen switcher"
expect "run jump-letter.strip 0"

# search on SELECT: jump-letter leaves it, and seeker's sort gives years
jl_plugins jump-letter search seeker
printf 'button=SELECT\n' >"$STATE/search.cfg"
printf '%s' "$JL_YEARS/Delta 1992.gb" >/tmp/last.txt
printf 'sections=%s\nsteps=%s\n' "$JL_YEARS" "action:jump-letter.strip,right,a" >"$STATE/seeker.cfg"
log="$WORK/jump-letter-taken.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early with search on SELECT"
plugin=jump-letter
expect "SELECT is search.open's on the game list, so jump-letter.strip isn't on it"
expect "strip $JL_YEARS: 1991, 1992, 1993 at 1992"
expect "jump to 1993"
grep -qF "plugin jump-letter: jump-letter.strip on SELECT" "$log" && fail "jump-letter took SELECT from search"
plugin=seeker
expect "inner entry_section 0"
expect "list $JL_YEARS: Bravo 1991, Delta 1992, Alpha 1993, Charlie 1993 selected 1"
expect "list $JL_YEARS: Bravo 1991, Delta 1992, Alpha 1993, Charlie 1993 selected 2"

rm -rf "$JL_ROMS" "$JL_YEARS" "$JL_EMU" "$JL_YEARS_EMU"
rmdir "$CARD/Emus/desktop" 2>/dev/null
if [ -f "$WORK/jump-letter-last.txt.saved" ]; then mv "$WORK/jump-letter-last.txt.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
rm -rf "$WORK/plugins/jump-letter.plugin" "$WORK/plugins/search.plugin" "$WORK/plugins/seeker.plugin" \
	"$STATE/search.cfg" "$STATE/seeker.cfg" "$STATE/plugins.txt" "$STATE/jump-letter.cfg"
case_log=""
