# shellcheck shell=sh

# layout-grid's Wrap columns setting: wrapper's folder of seven games g0 to
# g6 in 3 columns (g0 g3 g6, g1 g4 and g2 g5, so the last row is short) is
# walked with the d-pad, and wrapper logs each game that ends up selected.
# Off, the default, down from g6 wraps to the top of its own column (g0).
# On, down from a column's bottom goes to the top of the next (g6 to g1, g4
# to g2) and from the last column's bottom to g0, and up from g0 goes to
# the last column's bottom (g5) and so on back.
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" layout-grid >/dev/null || fail "building layout-grid"
build_plugin wrapper 'id=wrapper\nname=Wrapper\napi=1\n'
mkdir -p "$STATE"
printf 'layout-grid=on\nwrapper=on\nslot.layout=layout-grid\n' >"$STATE/plugins.txt"
plugin=wrapper

# wrap_walk <log> <keys>: the games wrapper saw selected, in order, on one line
wrap_walk() {
	PRESSER_KEYS="wait wait $2" SMOKE_PRELOAD="$PRESSER" PRESSER_LOG="$WORK/wrap.texts"
	export PRESSER_KEYS SMOKE_PRELOAD PRESSER_LOG
	run_launcher "$1" "presser: done" || fail "the launcher exited early with layout-grid's wrap"
	unset PRESSER_KEYS SMOKE_PRELOAD PRESSER_LOG
	grep -q "plugin layout-grid: grid of 3x" "$1" || fail "layout-grid's grid isn't 3 columns: $(grep "plugin layout-grid:" "$1")"
	sed -n 's/.*plugin wrapper: selected \(g[0-9]\)$/\1/p' "$1" | tr '\n' ' '
}

printf 'columns=3\n' >"$STATE/layout-grid.cfg"
got=$(wrap_walk "$WORK/grid-wrap-off.log" "down down down down")
[ "$got" = "g0 g3 g6 g0 g3 " ] || fail "with wrap off, down went $got"

printf 'columns=3\nwrap=on\n' >"$STATE/layout-grid.cfg"
got=$(wrap_walk "$WORK/grid-wrap-on.log" "down down down down down down down up up up up up")
[ "$got" = "g0 g3 g6 g1 g4 g2 g5 g0 g5 g2 g4 g1 g6 " ] || fail "with wrap on, the walk went $got"
rm -rf "$WORK/plugins/layout-grid.plugin" "$WORK/plugins/wrapper.plugin" "$STATE/layout-grid.cfg" "$STATE/plugins.txt" "$WORK/wrap.texts"
