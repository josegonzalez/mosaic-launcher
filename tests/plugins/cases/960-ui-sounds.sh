# shellcheck shell=sh

# the bundled ui-sounds, with SDL's dummy driver, seeker pressing the keys
# and chimer opening a toast, a dialog and the PIN keyboard: moves, the
# quick menu opening, B going back and closing it, the dialog opening and
# closing, the toast, a wrong PIN's error and A launching a game each play
# their sound on the launcher's sound device, which the launcher closes at
# quit once the launch's sound has played out. chimer hears the launcher's
# own events in order. Muted (a system volume of 0), nothing plays and the
# sound device never opens. With theme-plus on a folder theme with a
# sounds/ folder, the theme's move.wav wins over the bundled one, and a
# select.wav that isn't a WAV file falls back to the bundled one. The
# card's /tmp/next and /tmp/last.txt are put back afterwards.
US_ROMS="$CARD/Roms/UI Sounds (UIS)"
US_EMU="$CARD/Emus/desktop/UIS.pak"
US_THEME="$CARD/Themes/Chime"
rm -rf "$US_ROMS" "$US_THEME"
mkdir -p "$US_ROMS" "$US_EMU" "$US_THEME/sounds" "$STATE"
: >"$US_EMU/launch.sh"
for game in Alpha Bravo Charlie; do : >"$US_ROMS/$game.gb"; done
[ -f /tmp/next ] && cp /tmp/next "$WORK/ui-sounds-next.saved"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/ui-sounds-last.saved"
[ -f "$STATE/pin.txt" ] && cp "$STATE/pin.txt" "$WORK/ui-sounds-pin.saved"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" ui-sounds theme-plus >/dev/null \
	|| fail "building ui-sounds and theme-plus"
build_plugin seeker 'id=seeker\nname=Seeker\napi=1\n'
build_plugin chimer 'id=chimer\nname=Chimer\napi=1\n'

# the plugins named on, and every other plugin built so far off
us_plugins() {
	: >"$STATE/plugins.txt"
	for dir in "$WORK"/plugins/*.plugin; do
		id=$(basename "$dir" .plugin)
		case " $* " in
		*" $id "*) printf '%s=on\n' "$id" >>"$STATE/plugins.txt" ;;
		*) printf '%s=off\n' "$id" >>"$STATE/plugins.txt" ;;
		esac
	done
}

# us_played <log>: the sounds ui-sounds played, joined by |
us_played() {
	sed -n 's/.*plugin ui-sounds: played //p' "$1" | tr '\n' '|'
}

us_plugins ui-sounds seeker chimer
printf '1234\n' >"$STATE/pin.txt"
printf '%s' "$US_ROMS/Alpha.gb" >/tmp/last.txt
printf 'steps=%s\n' "down,up,action:gamelist.quick_menu,right,b,action:chimer.confirm,b,action:chimer.toast,action:chimer.pin,start,wait,a" \
	>"$STATE/seeker.cfg"
printf 'volume=80\n' >"$STATE/ui-sounds.cfg"
log="$WORK/ui-sounds.log"
SDL_AUDIODRIVER=dummy MOSAIC_LAUNCHER_VOLUME_FAKE=12 MOSAIC_UI_SOUNDS_LOG=1 run_launcher "$log" \
	&& fail "the launcher didn't quit to launch a game with ui-sounds"
plugin=ui-sounds
expect "sounds: 8 bundled, at volume 80"
got=$(us_played "$log")
want="move|move|menu_open|move|back|menu_close|menu_open|back|menu_close|toast|menu_open|menu_close|error|select|launch|"
[ "$got" = "$want" ] || fail "ui-sounds played \"$got\", not \"$want\""
plugin=chimer
expect "pin wrong"
got=$(sed -n 's/.*plugin chimer: event //p' "$log" | tr '\n' '|')
want="launcher.menu_open menu=quick|launcher.menu_close menu=quick|launcher.menu_open menu=dialog|launcher.menu_close menu=dialog|"
want="${want}launcher.toast|launcher.menu_open menu=dialog|launcher.menu_close menu=dialog|launcher.error what=pin|"
[ "$got" = "$want" ] || fail "chimer heard \"$got\", not \"$want\""
grep -qF "plugins: ui-sounds opened the sound device (dummy)" "$log" || fail "ui-sounds' sound device wasn't opened"
grep -qF "plugin ui-sounds: the system is muted" "$log" && fail "ui-sounds was muted at a volume of 12"
# the launch's sound plays out before the launcher quits for the game
launch_line=$(grep -nF "plugin ui-sounds: played launch" "$log" | cut -d: -f1)
closed_line=$(grep -nF "plugins: closed the sound device at quit, after" "$log" | cut -d: -f1)
if [ -z "$launch_line" ] || [ -z "$closed_line" ] || [ "$closed_line" -lt "$launch_line" ]; then
	fail "the launcher didn't close the sound device at quit after the launch's sound"
else
	drained=$(sed -n 's/.*plugins: closed the sound device at quit, after \([0-9]*\) ms.*/\1/p' "$log")
	[ "${drained:-0}" -ge 200 ] || fail "the launcher waited $drained ms for the launch's sound, not most of its 0.37 s"
fi

# muted: nothing plays, and the sound device never opens
printf 'steps=%s\n' "down,up,action:chimer.toast,b" >"$STATE/seeker.cfg"
log="$WORK/ui-sounds-muted.log"
SDL_AUDIODRIVER=dummy MOSAIC_LAUNCHER_VOLUME_FAKE=0 MOSAIC_UI_SOUNDS_LOG=1 run_launcher "$log" "plugin seeker: done" \
	|| fail "the launcher exited early with ui-sounds muted"
plugin=ui-sounds
expect "the system is muted, so sounds are silent"
[ -z "$(us_played "$log")" ] || fail "ui-sounds played \"$(us_played "$log")\" muted"
grep -qF "plugins: ui-sounds opened the sound device" "$log" && fail "the sound device opened while muted"
plugin=chimer
expect "event launcher.toast"

# the theme's sounds/ folder over the bundled set
cp "$ROOT/plugins/ui-sounds/res/sounds/toast.wav" "$US_THEME/sounds/move.wav"
printf 'not a sound\n' >"$US_THEME/sounds/select.wav"
printf 'name=Chime\n' >"$US_THEME/theme.txt"
us_plugins ui-sounds theme-plus seeker
rm -f "$STATE/ui-sounds.cfg"
printf 'theme=Chime\n' >"$STATE/theme-plus.cfg"
printf 'steps=%s\n' "down" >"$STATE/seeker.cfg"
log="$WORK/ui-sounds-theme.log"
SDL_AUDIODRIVER=dummy MOSAIC_LAUNCHER_VOLUME_FAKE=12 MOSAIC_UI_SOUNDS_LOG=1 run_launcher "$log" "plugin seeker: done" \
	|| fail "the launcher exited early with ui-sounds and theme-plus"
plugin=ui-sounds
sounds_dir=$(cd "$US_THEME/sounds" && pwd -P)
expect "the theme's select.wav isn't a WAV file, so the bundled one plays"
expect "sounds: 1 from the theme's $sounds_dir, 7 bundled, at volume 50"
expect "played move"

rm -rf "$US_ROMS" "$US_EMU" "$US_THEME"
rmdir "$CARD/Emus/desktop" "$CARD/Themes" 2>/dev/null
if [ -f "$WORK/ui-sounds-next.saved" ]; then mv "$WORK/ui-sounds-next.saved" /tmp/next; else rm -f /tmp/next; fi
if [ -f "$WORK/ui-sounds-last.saved" ]; then mv "$WORK/ui-sounds-last.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
if [ -f "$WORK/ui-sounds-pin.saved" ]; then mv "$WORK/ui-sounds-pin.saved" "$STATE/pin.txt"; else rm -f "$STATE/pin.txt"; fi
rm -rf "$WORK/plugins/ui-sounds.plugin" "$WORK/plugins/theme-plus.plugin" "$WORK/plugins/seeker.plugin" \
	"$WORK/plugins/chimer.plugin" "$STATE/seeker.cfg" "$STATE/ui-sounds.cfg" "$STATE/theme-plus.cfg" "$STATE/plugins.txt"
case_log=""
