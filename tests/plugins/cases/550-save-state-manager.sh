# shellcheck shell=sh
# shard: 3

# the bundled save-state-manager: the launcher starts on Alpha (the last
# path), where screener runs the game action, steps to slot 3, opens its
# preview and deletes it after the confirm (its state, preview and the
# last-slot file naming it), backs out to the list, then runs the tile's
# action, which lists every game with states, and closes it. A state that's
# a link to a file outside isn't listed, and the link and its file stay;
# /tmp/last.txt is put back afterwards
SS_ROMS="$CARD/Roms/Save States Test (SST)"
SS_EMU="$CARD/Emus/desktop/SST.pak"
SS_CORE="$CARD/.userdata/shared/SST-core"
SS_MINUI="$CARD/.userdata/shared/.minui/SST"
SS_OUTSIDE="$CARD/Save States Outside"
mkdir -p "$SS_ROMS" "$SS_EMU" "$SS_CORE" "$SS_MINUI" "$SS_OUTSIDE"
: >"$SS_ROMS/Alpha.gb"
: >"$SS_ROMS/Beta.gb"
: >"$SS_EMU/launch.sh"
printf state >"$SS_CORE/Alpha.gb.st0"
printf state >"$SS_CORE/Alpha.gb.st2"
printf state >"$SS_CORE/Beta.gb.st1"
# Alpha's newest, after anything else on the card
touch -t 203001010900 "$SS_CORE/Alpha.gb.st0"
touch -t 203001011000 "$SS_CORE/Alpha.gb.st2"
touch -t 202901010900 "$SS_CORE/Beta.gb.st1"
cp "$ROOT/tests/plugins/peeker/art.png" "$SS_MINUI/Alpha.gb.0.bmp"
cp "$ROOT/tests/plugins/peeker/art.png" "$SS_MINUI/Alpha.gb.2.bmp"
printf 2 >"$SS_MINUI/Alpha.gb.txt"
printf secret >"$SS_OUTSIDE/Secret.st3"
ln -s "$SS_OUTSIDE/Secret.st3" "$SS_CORE/Alpha.gb.st3"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/ssm-last.saved"
printf '%s' "$SS_ROMS/Alpha.gb" >/tmp/last.txt
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" save-state-manager >/dev/null || fail "building save-state-manager"
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
printf 'steps=action:save-state-manager.game,right,a,y,a,b,b,action:save-state-manager.open,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/save-state-manager.log" "plugin screener: done" || fail "the launcher exited early with save-state-manager"
log="$WORK/save-state-manager.log"
plugin=save-state-manager
expect "Alpha: 2 save states"
expect "slots 0 of 2: Alpha, Slot 1, 2030-01-01 09:00, "
expect "slots 1 of 2: Alpha, Slot 3, 2030-01-01 10:00, "
expect "view 1 of 2: Alpha, Slot 3, 2030-01-01 10:00, preview drawn"
expect "deleted Slot 3 of Alpha.gb: 3 files, 1 left"
expect "view 0 of 1: Alpha, Slot 1, 2030-01-01 09:00, preview drawn"
expect "slots 0 of 1: Alpha, Slot 1, 2030-01-01 09:00, "
grep -qE "plugin save-state-manager: [0-9]+ games with save states" "$log" || fail "save-state-manager didn't list every game"
grep -qE "plugin save-state-manager: games 0 of [0-9]+: Alpha, 1 states" "$log" || fail "save-state-manager didn't list Alpha first"
[ "$(seen)" = "gamelist save-state-manager.main dialog save-state-manager.main gamelist save-state-manager.main gamelist " ] \
	|| fail "save-state-manager didn't open, confirm and close: $(seen)"
[ -e "$SS_CORE/Alpha.gb.st2" ] && fail "save-state-manager didn't delete the state"
[ -e "$SS_MINUI/Alpha.gb.2.bmp" ] && fail "save-state-manager didn't delete the preview"
[ -e "$SS_MINUI/Alpha.gb.txt" ] && fail "save-state-manager didn't delete the last-slot file naming the slot"
if [ ! -f "$SS_CORE/Alpha.gb.st0" ] || [ ! -f "$SS_MINUI/Alpha.gb.0.bmp" ] || [ ! -f "$SS_CORE/Beta.gb.st1" ]; then
	fail "save-state-manager deleted another state"
fi
if [ ! -L "$SS_CORE/Alpha.gb.st3" ] || [ ! -f "$SS_OUTSIDE/Secret.st3" ]; then
	fail "save-state-manager touched the link"
fi
grep -qF "plugins: save-state-manager's screen save-state-manager.main's" "$log" && fail "save-state-manager's screen was turned off"
if [ -f "$WORK/ssm-last.saved" ]; then cp "$WORK/ssm-last.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
rm -rf "$SS_ROMS" "$SS_EMU" "$SS_CORE" "$SS_MINUI" "$SS_OUTSIDE" "$WORK/ssm-last.saved"
rm -rf "$WORK/plugins/save-state-manager.plugin" "$WORK/plugins/screener.plugin" "$STATE/screener.cfg"
case_log=""
