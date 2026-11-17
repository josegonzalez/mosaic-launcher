# shellcheck shell=sh
# shard: 1

# the bundled tasks plugin, with seeker: first in a test system, where
# seeker runs the game task on its game, whose name has spaces and quotes,
# which the task gets as $1 exactly, then the task limited to this system,
# while one limited to another isn't available; each one's last line and
# exit status logged as its toast and its output in its log. Then in the
# Tasks folder (the last path), A runs a task with no game and then a long
# one, which B stops, its background child with it. A task never goes
# through a launch: two Tools paks' pre-launch.sh hooks, one that cancels,
# and seeker's pre_launch, which cancels too, neither run nor stop one,
# while opening one of those paks still runs both hooks, the second
# cancelling it. Last, with no Tasks folder, the examples are copied to
# the card and Free space runs. The card's Tasks folder, recents and
# /tmp/last.txt are put back afterwards.
TK_ROMS="$CARD/Roms/Tasks Test (TKT)"
TK_EMU="$CARD/Emus/desktop/TKT.pak"
TK_GAME="$TK_ROMS/It's a \"test\" game.gb"
TK_DIR="$CARD/Tasks"
TK_LOGS="$CARD/.userdata/desktop/logs/tasks"
TK_WORK="$WORK/tasks"
TK_TOOLS="$CARD/Tools/desktop"
TK_HOOK_A="$TK_TOOLS/Tasks Hook A.pak"
TK_HOOK_B="$TK_TOOLS/Tasks Hook B.pak"
rm -rf "$TK_WORK" "$TK_LOGS"
mkdir -p "$TK_ROMS" "$TK_EMU" "$TK_WORK" "$(dirname "$RECENTS")" "$STATE"
: >"$TK_EMU/launch.sh"
: >"$TK_GAME"
[ -d "$TK_DIR" ] && mv "$TK_DIR" "$TK_WORK/Tasks.saved"
for saved in "$RECENTS" /tmp/last.txt; do
	[ -f "$saved" ] && cp "$saved" "$TK_WORK/$(basename "$saved").saved"
done

mkdir -p "$TK_DIR/Game/TKT" "$TK_DIR/Game/XYZ"
printf 'echo first line\necho global done\n' >"$TK_DIR/Echo.sh"
# shellcheck disable=SC2016 # the tasks expand these
printf 'sleep 30 &\necho "$!" >"%s/child.pid"\necho still going\nwait\n' "$TK_WORK" >"$TK_DIR/Long.sh"
# shellcheck disable=SC2016
printf 'printf "%%s" "$1" >"%s/arg.txt"\necho "args $#"\necho "got $(basename "$1")"\nexit 2\n' "$TK_WORK" >"$TK_DIR/Game/Path.sh"
printf 'echo "only for TKT"\n' >"$TK_DIR/Game/TKT/Only here.sh"
printf 'echo "never here"\n' >"$TK_DIR/Game/XYZ/Not here.sh"
# hooks run in name order: A records, B records and cancels
tk_tools_made=0
[ -d "$TK_TOOLS" ] || tk_tools_made=1
mkdir -p "$TK_HOOK_A" "$TK_HOOK_B"
: >"$TK_HOOK_A/launch.sh"
: >"$TK_HOOK_B/launch.sh"
# shellcheck disable=SC2016
printf 'echo "A $HOOK_TYPE $HOOK_PAK_PATH" >>"%s/hooks.txt"\n' "$TK_WORK" >"$TK_HOOK_A/pre-launch.sh"
# shellcheck disable=SC2016
printf 'echo "B $HOOK_TYPE $HOOK_PAK_PATH" >>"%s/hooks.txt"\nexit 1\n' "$TK_WORK" >"$TK_HOOK_B/pre-launch.sh"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" tasks >/dev/null || fail "building tasks"
build_plugin seeker 'id=seeker\nname=Seeker\napi=1\n'

# every other plugin built so far off, as a plugin plugins.txt doesn't name loads
: >"$STATE/plugins.txt"
for dir in "$WORK"/plugins/*.plugin; do
	id=$(basename "$dir" .plugin)
	case "$id" in
	tasks | seeker) printf '%s=on\n' "$id" >>"$STATE/plugins.txt" ;;
	*) printf '%s=off\n' "$id" >>"$STATE/plugins.txt" ;;
	esac
done
plugin=tasks

# the game tasks, on the game the launcher starts at
printf '%s' "$TK_GAME" >/tmp/last.txt
printf 'cancel_launches=on\nsteps=%s\n' "action:tasks.game.path,x,x,action:tasks.game.tkt.only-here,x,x,action:tasks.game.xyz.not-here" >"$STATE/seeker.cfg"
log="$WORK/tasks-game.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early with tasks"
expect "2 tasks, 3 game tasks in $TK_DIR"
expect "game task Path on Y as tasks.game.path"
expect "game task Only here on Y as tasks.game.tkt.only-here, for TKT games"
expect "game task Not here on Y as tasks.game.xyz.not-here, for XYZ games"
expect "running $TK_DIR/Game/Path.sh for $TK_GAME, pid "
expect "Path exited with 2, last line \"got It's a \"test\" game.gb\""
expect "toast: got It's a \"test\" game.gb (exit 2)"
expect "toast: only for TKT (exit 0)"
grep -qF "never here" "$log" && fail "a task for XYZ ran on a TKT game"
[ "$(cat "$TK_WORK/arg.txt" 2>/dev/null)" = "$TK_GAME" ] || fail "Path.sh didn't get the game's path as \$1"
grep -qxF "args 1" "$TK_LOGS/Game-Path.txt" 2>/dev/null || fail "Path.sh's log doesn't have its output"
grep -qF "== Path for $TK_GAME, " "$TK_LOGS/Game-Path.txt" 2>/dev/null || fail "Path.sh's log doesn't say what it ran for"
grep -qF "== exited with 2 after" "$TK_LOGS/Game-Path.txt" 2>/dev/null || fail "Path.sh's log doesn't end with its exit status"
[ -f "$TK_LOGS/Game-TKT-Only here.txt" ] || fail "Only here.sh has no log"
plugin=seeker
expect "run tasks.game.path 0"
expect "screen tasks.running"
expect "run tasks.game.tkt.only-here 0"
expect "run tasks.game.xyz.not-here -1"

# the Tasks folder: Echo with A, then Long, stopped with B
printf '%s' "$TK_DIR/Echo.sh" >/tmp/last.txt
printf 'cancel_launches=on\nsteps=%s\n' "a,x,x,down,a,x,x,x,b,x,x,x" >"$STATE/seeker.cfg"
log="$WORK/tasks-folder.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early with tasks"
plugin=tasks
expect "Tasks folder: 2 tasks"
expect "running $TK_DIR/Echo.sh, pid "
expect "toast: global done (exit 0)"
expect "running $TK_DIR/Long.sh, pid "
expect "wait screen: running Long"
expect "stopping Long, pid "
expect "wait screen: stopping Long"
expect "toast: Long stopped"
plugin=seeker
expect "list $TK_DIR: Echo, Long selected 0"
child=$(cat "$TK_WORK/child.pid" 2>/dev/null)
if [ -z "$child" ]; then
	fail "Long.sh didn't start its child"
else
	i=0
	while kill -0 "$child" 2>/dev/null && [ "$i" -lt 50 ]; do
		sleep 0.1
		i=$((i + 1))
	done
	if kill -0 "$child" 2>/dev/null; then
		fail "B left Long.sh's child running"
		kill "$child" 2>/dev/null
	fi
fi
grep -qF "plugins: tasks opened $TK_DIR/Echo.sh itself" "$log" || fail "Echo.sh wasn't opened by tasks itself"
[ -f "$TK_WORK/hooks.txt" ] && fail "a pak's pre-launch.sh ran for a task: $(cat "$TK_WORK/hooks.txt")"
grep -qF "plugin seeker: pre_launch" "$WORK/tasks-game.log" "$log" && fail "a plugin's pre_launch ran for a task"
grep -qF "pakhooks:" "$log" && fail "the pak hooks ran for a task"
grep -qxF "still going" "$TK_LOGS/Long.txt" 2>/dev/null || fail "Long.sh's log doesn't have its output"
grep -qF "== stopped after" "$TK_LOGS/Long.txt" 2>/dev/null || fail "Long.sh's log doesn't say it was stopped"

# a real pak still goes through the hooks, and B's cancels its launch
printf '%s' "$TK_HOOK_A" >/tmp/last.txt
printf 'steps=%s\n' "a,x,x" >"$STATE/seeker.cfg"
log="$WORK/tasks-pak.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early opening a pak with tasks"
[ "$(cat "$TK_WORK/hooks.txt" 2>/dev/null)" = "$(printf 'A pak %s\nB pak %s' "$TK_HOOK_A" "$TK_HOOK_A")" ] \
	|| fail "opening a pak didn't run both hooks: $(cat "$TK_WORK/hooks.txt" 2>/dev/null)"
grep -qF "pakhooks: $TK_HOOK_B/pre-launch.sh cancelled the launch of $TK_HOOK_A" "$log" || fail "the second hook didn't cancel the pak's launch"
grep -qF "opened $TK_HOOK_A itself" "$log" && fail "tasks took a pak's launch"
rm -rf "$TK_HOOK_A" "$TK_HOOK_B"
[ "$tk_tools_made" -eq 1 ] && rmdir "$TK_TOOLS" 2>/dev/null

# the examples, into a card without a Tasks folder
rm -rf "$TK_DIR"
printf '%s' "$TK_DIR/Free space.sh" >/tmp/last.txt
printf 'steps=%s\n' "a,x,x" >"$STATE/seeker.cfg"
log="$WORK/tasks-examples.log"
run_launcher "$log" "plugin seeker: done" || fail "the launcher exited early with tasks"
plugin=tasks
expect "copied 3 example tasks into $TK_DIR"
expect "1 tasks, 2 game tasks in $TK_DIR"
expect "running $TK_DIR/Free space.sh, pid "
grep -qF " free on the SD card (exit 0)" "$log" || fail "Free space's toast isn't its last line and exit status"
for example in "Free space.sh" "Game/List saves.sh" "Game/Game size.sh"; do
	cmp -s "$ROOT/plugins/tasks/res/tasks/$example" "$TK_DIR/$example" || fail "the example $example wasn't copied"
done

rm -rf "$TK_ROMS" "$TK_EMU" "$TK_DIR" "$TK_LOGS" "$TK_HOOK_A" "$TK_HOOK_B"
rmdir "$CARD/Emus/desktop" 2>/dev/null
[ -d "$TK_WORK/Tasks.saved" ] && mv "$TK_WORK/Tasks.saved" "$TK_DIR"
for saved in "$RECENTS" /tmp/last.txt; do
	if [ -f "$TK_WORK/$(basename "$saved").saved" ]; then mv "$TK_WORK/$(basename "$saved").saved" "$saved"; else rm -f "$saved"; fi
done
rm -rf "$WORK/plugins/tasks.plugin" "$WORK/plugins/seeker.plugin" "$STATE/seeker.cfg" "$STATE/plugins.txt" "$STATE/tasks.cfg"
case_log=""
