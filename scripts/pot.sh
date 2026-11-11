#!/bin/sh
# Writes po/launcher.pot, the gettext template of every string the launcher
# passes to TR(), TRC(), TRN() and TRCN() or marks with TR_MARK(), and
# po/plugins.pot, the bundled plugins' names, setting labels and choice
# options from their plugin.txt files under msgctxt set to each plugin's
# id, for the strings slot's owner to translate. With --check, fails when
# a committed one is out of date instead. Needs xgettext and msgfmt
# (gettext) and a C compiler ($CC, else cc).
set -eu

cd "$(dirname "$0")/.."
pot=po/launcher.pot
plugins_pot=po/plugins.pot

for tool in xgettext msgfmt; do
	if ! command -v "$tool" >/dev/null; then
		echo "pot.sh: $tool not found; install gettext (brew install gettext, apt install gettext)" >&2
		exit 1
	fi
done

# Locations name the file only and the entries are sorted, so the template
# changes only when a string does; the creation date is left out for the
# same reason.
make_pot() {
	xgettext --language=C --from-code=UTF-8 --no-wrap --sort-output --add-location=file \
		--package-name=mosaic-launcher --msgid-bugs-address=https://github.com/josegonzalez/mosaic-launcher/issues \
		--keyword --keyword=TR --keyword=TRC:1c,2 --keyword=TRN:1,2 --keyword=TRCN:1c,2,3 --keyword=TR_MARK \
		--output=- launcher/*.c | sed '/^"POT-Creation-Date: /d'
}

# plugin-pot reads plugin.txt with the launcher's own parser, so the
# template has exactly what the Plugins page shows.
tool=build/pot/plugin-pot
make_plugins_pot() {
	mkdir -p build/pot
	"${CC:-cc}" -std=gnu99 -Wall -Werror -Ilauncher scripts/plugin-pot.c launcher/pluginset.c -o "$tool"
	# shellcheck disable=SC2046 # one argument per file; plugin folders have no spaces
	"$tool" $(find plugins -mindepth 2 -maxdepth 2 -name plugin.txt | LC_ALL=C sort)
}

# What --check compares: the entries without their locations, which gettext
# releases wrap differently.
entries() {
	grep -v '^#:' "$1"
}

if [ "${1:-}" = "--check" ]; then
	tmp=$(mktemp)
	want=$(mktemp)
	have=$(mktemp)
	trap 'rm -f "$tmp" "$want" "$have"' EXIT
	make_pot >"$tmp"
	entries "$tmp" >"$want"
	if [ -f "$pot" ]; then entries "$pot" >"$have"; fi
	if ! cmp -s "$want" "$have"; then
		diff -u "$have" "$want" >&2 || true
		echo "pot.sh: $pot is out of date; run make pot" >&2
		exit 1
	fi
	make_plugins_pot >"$tmp"
	if ! cmp -s "$tmp" "$plugins_pot"; then
		diff -u "$plugins_pot" "$tmp" >&2 || true
		echo "pot.sh: $plugins_pot is out of date; run make pot" >&2
		exit 1
	fi
	msgfmt --check-format --check-domain -o /dev/null "$plugins_pot"
	exit 0
fi

mkdir -p po
make_pot >"$pot"
make_plugins_pot >"$plugins_pot"
msgfmt --check-format --check-domain -o /dev/null "$plugins_pot"
