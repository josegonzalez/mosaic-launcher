# shellcheck shell=sh
# shard: 1

# hinter owns the theme slot and draws single buttons: hint_style is the
# user's Settings style until a theme sets one, which then wins, and
# stock's settings are never written; the d-pad draws as icons in the icon
# styles and as words in the text style. A style the launcher doesn't know
# is refused.
build_plugin hinter 'id=hinter\nname=Hinter\napi=1\nslots=theme\n'
mkdir -p "$STATE"
printf 'hinter=on\n' >"$STATE/plugins.txt"
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/hint-settings.saved"
plugin=hinter

# hint_run <name> <Settings style or ""> <theme style>
hint_run() {
	log="$WORK/$1.log"
	rm -f "$SETTINGS"
	[ -n "$2" ] && printf 'inputPromptStyle=%s\n' "$2" >"$SETTINGS"
	printf 'style=%s\n' "$3" >"$STATE/hinter.cfg"
	SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$log" "plugin hinter: done" \
		|| fail "the launcher exited early with hinter ($1)"
	# stock writes the whole file back as it reads it, with the user's style
	[ -z "$2" ] || grep -qx "inputPromptStyle=$2" "$SETTINGS" \
		|| fail "stock's settings were written with another style ($1): $(grep inputPromptStyle "$SETTINGS")"
}
# hint_size <name>: the size hinter logged for a button, as <w>x<h>
hint_size() { sed -n "s|.*plugin hinter: $1 0 \([0-9]*x[0-9]*\) ink [1-9].*|\1|p" "$log" | head -n 1; }
# hint_round <name>: the button is a circle (an icon or one letter), drawn
# and measured the same
hint_round() {
	size=$(hint_size "$1")
	[ -n "$size" ] || fail "hinter didn't draw $1: $(grep "plugin hinter: $1 " "$log")"
	[ "${size%x*}" = "${size#*x}" ] || fail "$1 isn't a circle: $size"
	grep -q "plugin hinter: $1 0 $size ink [0-9]* measure ${size%x*}$" "$log" \
		|| fail "$1 wasn't measured as drawn: $(grep "plugin hinter: $1 " "$log")"
}
# hint_pill <name>: the button is a pill wider than it's high (a word)
hint_pill() {
	size=$(hint_size "$1")
	[ -n "$size" ] || fail "hinter didn't draw $1: $(grep "plugin hinter: $1 " "$log")"
	[ "${size%x*}" -gt "${size#*x}" ] || fail "$1 isn't a pill: $size"
	grep -q "plugin hinter: $1 0 $size ink [0-9]* measure ${size%x*}$" "$log" \
		|| fail "$1 wasn't measured as drawn: $(grep "plugin hinter: $1 " "$log")"
}

# stock's default, text, with nothing set anywhere: the d-pad as words
hint_run hint-default "" 0
expect "init style 1"
expect "style 1"
hint_round A
hint_pill START
hint_pill POWER
for b in UP DOWN LEFT RIGHT U/D L/R DPAD; do hint_pill "$b"; done
expect "dpad differs 1"
expect "refused unknown -1 fn -1 stale -1 far -1"
grep -qF "theme draws button hints" "$log" && fail "a theme with no hint style set one"

# the user's Settings style, shapes, with the theme's unset: the d-pad as icons
hint_run hint-user 2 0
expect "init style 4"
expect "style 4"
for b in A POWER UP DOWN LEFT RIGHT U/D L/R DPAD; do hint_round "$b"; done
hint_pill START
expect "dpad differs 1"

# the theme's ABXY over the user's shapes, without writing Settings
hint_run hint-theme 2 2
expect "init style 4"
expect "style 2"
grep -qF "plugins: hinter's theme draws button hints as ABXY, over the user's style" "$log" \
	|| fail "the theme's hint style wasn't logged"
for b in A UP DPAD; do hint_round "$b"; done

# the theme's text over the user's ABXY
hint_run hint-text 0 1
expect "init style 2"
expect "style 1"
for b in UP L/R; do hint_pill "$b"; done

# a style the launcher doesn't know is refused, leaving the user's
hint_run hint-bad 1 9
expect "style 3"
grep -qF "plugins: hinter's theme hint_style 9 isn't from 0 to 4, so stock's is used" "$log" \
	|| fail "the bad hint style wasn't refused"
hint_round UP

if [ -f "$WORK/hint-settings.saved" ]; then mv "$WORK/hint-settings.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -rf "$WORK/plugins/hinter.plugin" "$STATE/hinter.cfg" "$STATE/plugins.txt"
case_log=""
