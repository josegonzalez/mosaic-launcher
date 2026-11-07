#!/usr/bin/env bats

# The plugin profiles scripts/plugin-profiles.sh derives from plugin.txt
# files, checked against throwaway manifests rather than the bundled plugins.

REPO_ROOT="$(cd "$BATS_TEST_DIRNAME/.." && pwd)"

setup() {
	DIR="$BATS_TEST_TMPDIR/plugins"
	mkdir -p "$DIR"
}

# manifest <name> <plugin.txt lines...>
manifest() {
	mkdir -p "$DIR/$1"
	name="$1"
	shift
	printf 'id=%s\napi=1\n' "$name" >"$DIR/$name/plugin.txt"
	for line in "$@"; do echo "$line" >>"$DIR/$name/plugin.txt"; done
}

print_profile() {
	"$REPO_ROOT/scripts/plugin-profiles.sh" print "$1" "$DIR"
}

@test "additive plugins are on in both profiles" {
	manifest alpha
	manifest beta
	run print_profile all
	[ "$output" = "$(printf 'alpha=on\nbeta=on')" ]
	run print_profile alternate
	[ "$output" = "$(printf 'alpha=on\nbeta=on')" ]
}

@test "a slot goes to its first owner, then flips to the next" {
	manifest grid slots=layout
	manifest list slots=layout
	manifest solo slots=home
	[ "$(print_profile all 2>/dev/null)" = "$(printf 'grid=on\nlist=off\nsolo=on\nslot.layout=grid\nslot.home=solo')" ]
	[ "$(print_profile all 2>&1 >/dev/null)" = "list off: the layout slot goes to grid" ]
	[ "$(print_profile alternate 2>/dev/null)" = "$(printf 'grid=off\nlist=on\nsolo=on\nslot.layout=list\nslot.home=solo')" ]
	[ "$(print_profile alternate 2>&1 >/dev/null)" = "grid off: the layout slot goes to list" ]
}

@test "an unmet requirement is left off with a note" {
	manifest favorites
	manifest netplay requires=minarch-netplay
	[ "$(print_profile all 2>/dev/null)" = "$(printf 'favorites=on\nnetplay=off')" ]
	[ "$(print_profile all 2>&1 >/dev/null)" = "netplay off: requires minarch-netplay, which isn't on" ]
}

@test "a requirement that's on is met, and one left off strands its dependents" {
	manifest base slots=theme
	manifest extra requires=base
	manifest other slots=theme
	[ "$(print_profile all 2>/dev/null)" = "$(printf 'base=on\nextra=on\nother=off\nslot.theme=base')" ]
	[ "$(print_profile alternate 2>/dev/null)" = "$(printf 'base=off\nextra=off\nother=on\nslot.theme=other')" ]
}

@test "the later of two conflicting plugins is left off" {
	manifest first conflicts=second
	manifest second
	[ "$(print_profile all 2>/dev/null)" = "$(printf 'first=on\nsecond=off')" ]
	[ "$(print_profile all 2>&1 >/dev/null)" = "second off: conflicts with first" ]
}

@test "the bundled plugins make a profile" {
	run "$REPO_ROOT/scripts/plugin-profiles.sh" print all
	[ "$status" -eq 0 ]
	[[ "$output" == *"favorites=on"* ]]
}
