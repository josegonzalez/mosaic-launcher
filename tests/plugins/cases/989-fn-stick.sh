# shellcheck shell=sh

# button-remap's FN1, FN2 and HOME settings, through the launcher's FN
# bindings: on a stand-in device with FN1 as L3 and HOME, FN1 runs a Tools
# pak and HOME an action, and FN2, which it lacks, does nothing. Then the
# user's own choice in Settings wins over button-remap's
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" button-remap >/dev/null || fail "building button-remap"
mkdir -p "$STATE"
printf 'button-remap=on\n' >"$STATE/plugins.txt"
printf 'fn1=Tools/Files.pak\nfn2=gamelist.switcher\nhome=gamelist.switcher\n' >"$STATE/button-remap.cfg"

MOSAIC_LAUNCHER_FN_FAKE="L3,,HOME" run_launcher "$WORK/fn-stick.log" "plugins: button-remap bound FN3" \
	|| fail "the launcher exited early with button-remap's FN buttons"
log="$WORK/fn-stick.log"
plugin=button-remap
grep -qF "plugins: button-remap bound FN1 (L3) to pak:Files.pak" "$log" || fail "FN1 wasn't bound to the pak"
grep -qF "plugins: button-remap bound FN3 (HOME) to action:gamelist.switcher" "$log" || fail "HOME wasn't bound to the action"
expect "this device has no fn2 button, so fn2 does nothing"

# the user's choice in Settings wins
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
printf 'fn1action=pak:Bootlogo.pak\n' >"$SETTINGS"
SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" MOSAIC_LAUNCHER_FN_FAKE="L3,,HOME" \
	run_launcher "$WORK/fn-stick-user.log" "plugins: button-remap bound FN3" \
	|| fail "the launcher exited early with FN1 assigned in Settings"
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -f "$WORK/minuisettings.txt.saved"
log="$WORK/fn-stick-user.log"
grep -qF "plugins: button-remap can't bind FN1: the user assigned it in Settings" "$log" || fail "the user's FN1 didn't win"

rm -rf "$WORK/plugins/button-remap.plugin"
rm -f "$STATE/button-remap.cfg" "$STATE/plugins.txt"
