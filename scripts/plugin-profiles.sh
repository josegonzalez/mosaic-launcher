#!/bin/sh
#
# Runs the desktop build with the bundled plugins in two profiles built from
# their plugin.txt files:
#
#   all        every plugin on, with the first owner (by name) of each
#              exclusive slot and every additive plugin
#   alternate  the same, with each exclusive slot flipped to its next owner;
#              a slot with one owner keeps it
#
# Plugins whose requires can't be met, or that conflict with one already on,
# are left off with a note. For each profile this writes plugins.txt on the
# fake card, runs the desktop build for a few seconds and then the Launcher
# pak's plugin settings, and fails if either exits on its own, doesn't stop on
# SIGTERM, or the log shows a plugin refused for a reason the profile didn't
# choose.
#
#   scripts/plugin-profiles.sh                       run both profiles
#   scripts/plugin-profiles.sh print <profile> [dir] print a profile's
#       plugins.txt for the plugins in dir (default plugins/), notes on stderr
#
# Running expects `make desktop plugins-desktop` first.

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# profile <all|alternate> <dir>: plugins.txt on stdout, notes on stderr
profile() {
	case "$1" in
		all) alt=0 ;;
		alternate) alt=1 ;;
		*) echo "unknown profile $1" >&2; return 1 ;;
	esac
	set -- "$2"/*/plugin.txt
	[ -f "$1" ] || return 0
	awk -v alt="$alt" '
		function trim(s) { sub(/^[ \t\r]+/, "", s); sub(/[ \t\r]+$/, "", s); return s }
		function has(csv, item,    parts, k, n) {
			n = split(csv, parts, ",")
			for (k = 1; k <= n; k++) if (trim(parts[k]) == item) return 1
			return 0
		}
		function onId(want,    k) {
			for (k = 1; k <= n; k++) if (on[k] && id[k] == want) return 1
			return 0
		}
		function off(k, why) {
			on[k] = 0
			print id[k] " off: " why > "/dev/stderr"
		}
		FNR == 1 { n++; id[n] = ""; slots[n] = ""; requires[n] = ""; conflicts[n] = "" }
		{
			line = trim($0)
			if (line == "" || line ~ /^#/ || index(line, "=") == 0) next
			key = trim(substr(line, 1, index(line, "=") - 1))
			value = trim(substr(line, index(line, "=") + 1))
			if (key == "id") id[n] = value
			else if (key == "slots") slots[n] = value
			else if (key == "requires") requires[n] = value
			else if (key == "conflicts") conflicts[n] = value
		}
		END {
			for (i = 1; i <= n; i++) {
				on[i] = id[i] != ""
				count = split(slots[i], parts, ",")
				for (k = 1; k <= count; k++) {
					s = trim(parts[k])
					if (s != "" && !(s in seen)) { seen[s] = 1; slot[++slot_count] = s }
				}
			}

			# exclusive slots: one owner each, the first or the next
			for (k = 1; k <= slot_count; k++) {
				s = slot[k]
				owners = 0
				for (i = 1; i <= n; i++) if (on[i] && has(slots[i], s)) owner[++owners] = i
				if (!owners) continue
				pick[s] = owner[alt && owners > 1 ? 2 : 1]
				for (j = 1; j <= owners; j++) {
					if (owner[j] != pick[s]) off(owner[j], "the " s " slot goes to " id[pick[s]])
				}
			}

			# conflicts, either direction: the later one stays off
			for (i = 1; i <= n; i++) {
				for (j = 1; j < i && on[i]; j++) {
					if (on[j] && (has(conflicts[i], id[j]) || has(conflicts[j], id[i]))) off(i, "conflicts with " id[j])
				}
			}

			# requirements, until nothing changes
			changed = 1
			while (changed) {
				changed = 0
				for (i = 1; i <= n; i++) {
					if (!on[i] || requires[i] == "") continue
					count = split(requires[i], parts, ",")
					for (k = 1; k <= count; k++) {
						r = trim(parts[k])
						if (r != "" && !onId(r)) { off(i, "requires " r ", which isn'"'"'t on"); changed = 1; break }
					}
				}
			}

			for (i = 1; i <= n; i++) if (id[i] != "") print id[i] "=" (on[i] ? "on" : "off")
			for (k = 1; k <= slot_count; k++) {
				s = slot[k]
				if ((s in pick) && on[pick[s]]) print "slot." s "=" id[pick[s]]
			}
		}
	' "$@"
}

if [ "${1:-}" = print ]; then
	profile "${2:?usage: $0 print <all|alternate> [plugins dir]}" "${3:-$ROOT/plugins}"
	exit
fi

CARD=/var/tmp/nextui/sdcard
STATE="$CARD/.userdata/desktop/launcher"
PLUGINS="$ROOT/build/desktop-plugins"
WORK="$ROOT/build/profiles"
failures=0

fail() {
	echo "FAIL: $*" >&2
	failures=$((failures + 1))
}

# check_log <log> <plugins.txt>: every plugin the profile turned on loaded,
# and nothing was refused for a reason the profile didn't choose
check_log() {
	while IFS='=' read -r key value; do
		case "$key" in slot.*) continue ;; esac
		if [ "$value" = on ]; then
			grep -q "plugins: loaded $key\$" "$1" || fail "$key didn't load ($1)"
		fi
	done <"$2"
	grep "plugins: not loading " "$1" | while IFS= read -r line; do
		rest="${line#*plugins: not loading }"
		id="${rest%%: *}"
		why="${rest#*: }"
		if [ "$why" != "turned off" ] || ! grep -qx "$id=off" "$2"; then
			echo "FAIL: $id was refused: $why ($1)" >&2
			echo x
		fi
	done >"$WORK/refused"
	[ -s "$WORK/refused" ] && failures=$((failures + $(wc -l <"$WORK/refused")))
	if grep -q '\[WARN\] plugins' "$1"; then
		grep '\[WARN\] plugins' "$1" >&2
		fail "the loader warned ($1)"
	fi
}

[ -x "$ROOT/build/desktop/nextui.elf" ] || { echo "run make desktop first" >&2; exit 1; }
[ -d "$PLUGINS" ] || { echo "run make plugins-desktop first" >&2; exit 1; }

# before anything writes to the card: the prepare script skips a card that exists
bash "$ROOT/vendor/NextUI/workspace/desktop/prepare_fake_sd_root.sh" >/dev/null
rm -rf "$WORK"
mkdir -p "$WORK" "$STATE"
saved=
[ -f "$STATE/plugins.txt" ] && saved="$WORK/plugins.txt.saved" && cp "$STATE/plugins.txt" "$saved"

for name in all alternate; do
	txt="$WORK/$name.txt"
	profile "$name" "$ROOT/plugins" >"$txt" 2>"$WORK/$name.notes" || fail "building the $name profile"
	echo "profile $name:"
	sed 's/^/  /' "$txt"
	sed 's/^/  note: /' "$WORK/$name.notes"

	cp "$txt" "$STATE/plugins.txt"
	rm -f "$STATE/loading" "$STATE/plugins_paused"
	MOSAIC_LAUNCHER_PLUGIN_PATH="$PLUGINS" "$ROOT/scripts/desktop-smoke.sh" "$WORK/$name.log" 4 >/dev/null ||
		fail "the launcher failed in the $name profile"
	check_log "$WORK/$name.log" "$txt"

	SDCARD_PATH=$CARD USERDATA_PATH=$CARD/.userdata/desktop SYSTEM_PATH=$CARD/.system/desktop \
		PLATFORM=desktop MOSAIC_LAUNCHER_PAK_DIR="$ROOT/pak" MOSAIC_LAUNCHER_RUN_DIR="$ROOT/build/run" \
		MOSAIC_LAUNCHER_PLUGIN_PATH="$PLUGINS" \
		"$ROOT/scripts/desktop-smoke.sh" "$WORK/$name-settings.log" 4 --settings plugins >/dev/null ||
		fail "the plugin settings failed in the $name profile"
done

rm -f "$STATE/plugins.txt" "$STATE/loading" "$STATE/plugins_paused"
[ -n "$saved" ] && cp "$saved" "$STATE/plugins.txt"

if [ "$failures" -gt 0 ]; then
	echo "$failures profile check(s) failed; logs in $WORK" >&2
	exit 1
fi
echo "ok profiles"
