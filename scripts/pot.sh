#!/bin/sh
# Writes po/launcher.pot, the gettext template of every string the launcher
# passes to TR(), TRC(), TRN() and TRCN() or marks with TR_MARK(), for the
# strings slot's owner to translate. With --check, fails when the committed
# one is out of date instead. Needs xgettext (gettext).
set -eu

cd "$(dirname "$0")/.."
pot=po/launcher.pot

if ! command -v xgettext >/dev/null; then
	echo "pot.sh: xgettext not found; install gettext (brew install gettext, apt install gettext)" >&2
	exit 1
fi

# Locations name the file only and the entries are sorted, so the template
# changes only when a string does; the creation date is left out for the
# same reason.
make_pot() {
	xgettext --language=C --from-code=UTF-8 --no-wrap --sort-output --add-location=file \
		--package-name=mosaic-launcher --msgid-bugs-address=https://github.com/josegonzalez/mosaic-launcher/issues \
		--keyword --keyword=TR --keyword=TRC:1c,2 --keyword=TRN:1,2 --keyword=TRCN:1c,2,3 --keyword=TR_MARK \
		--output=- launcher/*.c | sed '/^"POT-Creation-Date: /d'
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
	exit 0
fi

mkdir -p po
make_pot >"$pot"
