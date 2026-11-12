# shellcheck shell=sh
# shard: 3

# binder and racer both want FN3: the first to bind it keeps it, the user's
# own choice in Settings wins over both, and binder's binding is released
# when it releases it or declines to start. Desktop has no FN buttons, so
# MOSAIC_LAUNCHER_FN_FAKE stands in for a device's.
build_plugin binder 'id=binder\nname=Binder\napi=1\n'
build_plugin racer 'id=racer\nname=Racer\napi=1\n'
mkdir -p "$STATE"
printf 'binder=on\nracer=on\n' >"$STATE/plugins.txt"
rm -f "$STATE/binder.cfg"

# fn_case <name> [binder settings]: runs both with FN1 and FN3, as L3 and HOME
fn_case() {
	printf '%b' "${2:-}" >"$STATE/binder.cfg"
	MOSAIC_LAUNCHER_FN_FAKE="L3,,HOME" run_launcher "$WORK/$1.log" "plugin racer: done" \
		|| fail "the launcher exited early in the $1 case"
	log="$WORK/$1.log"
}

# a device without FN buttons, as desktop is: nothing can be bound
run_launcher "$WORK/fn-none.log" "plugin racer: done" || fail "the launcher exited early without FN buttons"
log="$WORK/fn-none.log"
plugin=binder
expect "names ,,"
expect "bind -1"
grep -qF "plugins: binder can't bind FN3: this device doesn't have it" "$log" || fail "the missing button's refusal wasn't logged"
plugin=racer
expect "before -1 by 0 action '' plugin ''"
expect "bind FN3 -1"

# two plugins racing for FN3: binder, first in load order, keeps it
fn_case fn-race
plugin=binder
expect "names L3,,HOME"
expect "bind 0"
expect "bind again 0"
grep -qF "plugins: binder bound FN3 (HOME) to action:binder.go" "$log" || fail "binder's binding wasn't logged"
plugin=racer
expect "before 0 by 2 action 'action:binder.go' plugin 'binder'"
expect "bind FN3 -1"
expect "after 0 by 2 action 'action:binder.go' plugin 'binder'"
expect "refused A -1 bad -1 empty -1 null -1 release -1 binding A -1 null -1 name A ''"
grep -qF "plugins: racer can't bind FN3: binder bound it first" "$log" || fail "racer's refusal wasn't logged"

# a button the device lacks
fn_case fn-missing 'button=2\n'
plugin=binder
expect "bind -1"

# the user's own choice in Settings wins over both
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
printf 'fn3action=pak:Bootlogo.pak\n' >"$SETTINGS"
printf '' >"$STATE/binder.cfg"
SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" MOSAIC_LAUNCHER_FN_FAKE="L3,,HOME" \
	run_launcher "$WORK/fn-user.log" "plugin racer: done" || fail "the launcher exited early with FN3 assigned in Settings"
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -f "$WORK/minuisettings.txt.saved"
log="$WORK/fn-user.log"
plugin=binder
expect "bind -1"
grep -qF "plugins: binder can't bind FN3: the user assigned it in Settings" "$log" || fail "the user's assignment didn't win"
plugin=racer
expect "before 0 by 1 action 'pak:Bootlogo.pak' plugin ''"
expect "bind FN3 -1"

# binder releasing it frees it for racer
fn_case fn-release 'release=1\n'
plugin=binder
expect "bind 0"
expect "release 0 again -1"
plugin=racer
expect "before 0 by 0 action '' plugin ''"
expect "bind FN3 0"
expect "after 0 by 2 action 'pak:Clock.pak' plugin 'racer'"

# binder declining to start, which unloads it, frees it too
fn_case fn-decline 'decline=1\n'
plugin=binder
expect "bind 0"
grep -qF "plugins: binder declined to start" "$log" || fail "binder didn't decline"
grep -qF "plugins: binder's FN bindings are released" "$log" || fail "binder's bindings weren't released as it declined"
plugin=racer
expect "before 0 by 0 action '' plugin ''"
expect "bind FN3 0"

rm -f "$STATE/binder.cfg"
