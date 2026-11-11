#!/bin/sh
# Writes po/launcher.pot, the gettext template of every string the launcher
# passes to TR(), TRC(), TRN() and TRCN() or marks with TR_MARK(), plus the
# names of the systems NextUI ships under msgctxt "systems", and
# po/plugins.pot, the bundled plugins' names, setting labels and choice
# options from their plugin.txt files and the fixed text their code passes
# to tr(), trPlural() and register_action() or marks with TR_MARK(), under
# msgctxt set to each plugin's id, for the strings slot's owner to
# translate. With --check, fails when a committed one is out of date
# instead. Needs xgettext and msgfmt (gettext) and a C compiler ($CC, else
# cc).
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
	systems_po
	xgettext --from-code=UTF-8 --no-wrap --sort-output --add-location=file \
		--package-name=mosaic-launcher --msgid-bugs-address=https://github.com/josegonzalez/mosaic-launcher/issues \
		--keyword --keyword=TR --keyword=TRC:1c,2 --keyword=TRN:1,2 --keyword=TRCN:1c,2,3 --keyword=TR_MARK \
		--output=- launcher/*.c build/pot/systems.po | sed '/^"POT-Creation-Date: /d'
}

# The system folders in NextUI's skeleton by the name the root shows and
# Listing_translateNames asks for (the folder's name before its tag, as
# RootName_split cuts it), each with the skeleton folders it's in.
skeleton=vendor/NextUI/skeleton
systems_po() {
	mkdir -p build/pot
	{
		printf 'msgid ""\nmsgstr ""\n"Content-Type: text/plain; charset=UTF-8\\n"\n'
		for dir in "$skeleton"/*/Roms/*/; do
			name=$(basename "$dir")
			base=$(printf '%s\n' "$name" | sed -e 's/^\(.[^([]*\)[([].*$/\1/' -e 's/[[:space:]]*$//' -e 's/["\\]/\\&/g')
			printf '%s\t%s\n' "$base" "$(dirname "$dir")"
		done | LC_ALL=C sort -u | awk -F '\t' '
			function entry() { printf "\n#:%s\nmsgctxt \"systems\"\nmsgid \"%s\"\nmsgstr \"\"\n", where, last }
			NR > 1 && $1 != last { entry(); where = "" }
			{ last = $1; where = where " " $2 }
			END { if (NR) entry() }'
	} >build/pot/systems.po
}

# plugin-pot reads plugin.txt with the launcher's own parser, so the
# template has exactly what the Plugins page shows. Each plugin's own C files are read by xgettext, with the plugins'
# convention for fixed text: a tr() or trPlural() helper over host->tr and
# host->tr_plural, TR_MARK() for English kept in data and translated where
# it's used, and register_action()'s label, which the launcher translates.
# plugin-pot puts those strings (-c, after the plugin's plugin.txt) under
# the plugin's id too.
tool=build/pot/plugin-pot
make_plugins_pot() {
	mkdir -p build/pot/code
	"${CC:-cc}" -std=gnu99 -Wall -Werror -Ilauncher scripts/plugin-pot.c launcher/pluginset.c -o "$tool"
	args=""
	for txt in $(find plugins -mindepth 2 -maxdepth 2 -name plugin.txt | LC_ALL=C sort); do
		dir=$(dirname "$txt")
		code="build/pot/code/$(basename "$dir").pot"
		rm -f "$code"
		# writes nothing for a plugin with no strings
		xgettext --language=C --from-code=UTF-8 --no-wrap --add-location=file \
			--keyword --keyword=tr --keyword=trPlural:1,2 --keyword=TR_MARK --keyword=register_action:2 \
			--output="$code" "$dir"/*.c
		args="$args $txt"
		if [ -f "$code" ]; then args="$args -c $code"; fi
	done
	# shellcheck disable=SC2086 # one argument per file; plugin folders have no spaces
	"$tool" $args
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
