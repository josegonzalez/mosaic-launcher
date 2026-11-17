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

@test "profile N gives each slot its owner N mod the number of owners" {
	manifest a slots=layout
	manifest b slots=layout
	manifest c slots=layout
	manifest solo slots=home
	[ "$(print_profile all 2>/dev/null | grep '^slot')" = "$(printf 'slot.layout=a\nslot.home=solo')" ]
	[ "$(print_profile alternate 2>/dev/null | grep '^slot')" = "$(printf 'slot.layout=b\nslot.home=solo')" ]
	[ "$(print_profile alternate-2 2>/dev/null | grep '^slot')" = "$(printf 'slot.layout=c\nslot.home=solo')" ]
	[ "$(print_profile alternate-3 2>/dev/null | grep '^slot')" = "$(printf 'slot.layout=a\nslot.home=solo')" ]
	[ "$(print_profile alternate-2 2>&1 >/dev/null)" = "$(printf 'a off: the layout slot goes to c\nb off: the layout slot goes to c')" ]
}

@test "as many profiles run as the largest slot has owners" {
	manifest a slots=layout
	manifest b slots=layout
	manifest c slots=layout
	manifest solo slots=home
	run "$REPO_ROOT/scripts/plugin-profiles.sh" count "$DIR"
	[ "$output" = 3 ]
	manifest d slots=layout,home
	run "$REPO_ROOT/scripts/plugin-profiles.sh" count "$DIR"
	[ "$output" = 4 ]
}

@test "there are always at least the all and alternate profiles" {
	manifest solo slots=home
	run "$REPO_ROOT/scripts/plugin-profiles.sh" count "$DIR"
	[ "$output" = 2 ]
	rm -rf "$DIR"/*
	run "$REPO_ROOT/scripts/plugin-profiles.sh" count "$DIR"
	[ "$output" = 2 ]
}

@test "an unknown profile name is refused" {
	manifest a slots=layout
	run print_profile alternate-x
	[ "$status" -ne 0 ]
	run print_profile bogus
	[ "$status" -ne 0 ]
}

# check_profile <log lines> <plugins.txt lines>: the profile's log check on
# a made-up log
check_profile() {
	printf '%s\n' "$1" >"$BATS_TEST_TMPDIR/run.log"
	printf '%s\n' "$2" >"$BATS_TEST_TMPDIR/plugins.txt"
	"$REPO_ROOT/scripts/plugin-profiles.sh" check "$BATS_TEST_TMPDIR/run.log" "$BATS_TEST_TMPDIR/plugins.txt"
}

@test "a profile passes when every plugin it turned on loaded" {
	run check_profile "$(printf '[INFO] plugins: loaded alpha\n[INFO] plugins: not loading beta: turned off')" \
		"$(printf 'alpha=on\nbeta=off\nslot.layout=alpha')"
	[ "$status" -eq 0 ]
}

@test "a profile fails when a plugin it turned on is missing, as one the loader never found" {
	run check_profile '[INFO] plugins: loaded alpha' "$(printf 'alpha=on\nomega=on')"
	[ "$status" -ne 0 ]
	[[ "$output" == *"omega didn't load"* ]]
}

@test "a profile fails when the loader warns, as about a field cut to fit" {
	run check_profile "$(printf "[INFO] plugins: loaded alpha\n[WARN] plugins: alpha's plugin.txt: name is over 127 bytes, cut to fit")" 'alpha=on'
	[ "$status" -ne 0 ]
	[[ "$output" == *"the loader warned"* ]]
}

@test "a profile fails when a plugin is refused for a reason it didn't choose" {
	run check_profile "$(printf '[INFO] plugins: loaded alpha\n[INFO] plugins: not loading beta: requires gamma, which isn'"'"'t loading')" \
		"$(printf 'alpha=on\nbeta=off')"
	[ "$status" -ne 0 ]
	[[ "$output" == *"beta was refused"* ]]
}
