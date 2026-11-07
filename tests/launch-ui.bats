#!/usr/bin/env bats

# Drives the Launcher pak's screens with fake minui tools that replay scripted
# choices, then checks what was written.

load test_helper

setup() {
	setup_fake_card
	mkdir -p "$PAK/plugins/favorites.plugin"
	cp "$REPO_ROOT/plugins/favorites/plugin.txt" "$PAK/plugins/favorites.plugin/plugin.txt"

	ANSWERS="$BATS_TEST_TMPDIR/answers"
	TYPED="$BATS_TEST_TMPDIR/typed"
	: >"$ANSWERS"
	: >"$TYPED"
	bin="$PAK/bin/$PLATFORM"
	mkdir -p "$bin"

	# each call takes the next answer, or cancels (exit 2) when there's none;
	# every list it was shown is kept for checking
	cat >"$bin/minui-list" <<EOF
#!/bin/sh
out=""; file=""
while [ \$# -gt 0 ]; do
	case "\$1" in
		--write-location) out="\$2"; shift ;;
		--file) file="\$2"; shift ;;
	esac
	shift
done
cat "\$file" >>"$BATS_TEST_TMPDIR/shown"
answer="\$(head -n 1 "$ANSWERS")"
[ -n "\$answer" ] || exit 2
sed -i.bak 1d "$ANSWERS"
printf '%s' "\$answer" >"\$out"
EOF
	cat >"$bin/minui-keyboard" <<EOF
#!/bin/sh
out=""
while [ \$# -gt 0 ]; do
	case "\$1" in
		--write-location) out="\$2"; shift ;;
		--initial-value) echo "initial: \$2" >>"$BATS_TEST_TMPDIR/keyboard"; shift ;;
	esac
	shift
done
answer="\$(head -n 1 "$TYPED")"
[ -n "\$answer" ] || exit 2
sed -i.bak 1d "$TYPED"
printf '%s' "\$answer" >"\$out"
EOF
	printf '#!/bin/sh\nexit 0\n' >"$bin/minui-presenter"
	chmod +x "$bin"/minui-*
}

answer() {
	printf '%s\n' "$@" >>"$ANSWERS"
}

@test "turns a plugin on and changes its text setting" {
	answer "Favorites: Off" "Status: Off" "Collection: Favorites"
	printf 'Games\n' >"$TYPED"
	run "$PAK/launch.sh" plugins
	[ "$status" -eq 0 ]
	grep -qx "favorites=on" "$STATE_DIR/plugins.txt"
	grep -qx "collection=Games" "$STATE_DIR/favorites.cfg"
	grep -qx "initial: Favorites" "$BATS_TEST_TMPDIR/keyboard"
	# the page showed the new values before going back
	grep -q '"Status: On"' "$BATS_TEST_TMPDIR/shown"
	grep -q '"Collection: Games"' "$BATS_TEST_TMPDIR/shown"
}

@test "cancelling the keyboard keeps the setting" {
	printf 'collection=Mine\n' >"$STATE_DIR/favorites.cfg"
	answer "Favorites: Off" "Collection: Mine"
	run "$PAK/launch.sh" plugins
	[ "$status" -eq 0 ]
	grep -qx "collection=Mine" "$STATE_DIR/favorites.cfg"
	grep -qx "initial: Mine" "$BATS_TEST_TMPDIR/keyboard"
}

@test "a plugin without settings flips straight from the list" {
	printf 'id=favorites\nname=Favorites\napi=1\n' >"$PAK/plugins/favorites.plugin/plugin.txt"
	answer "Favorites: Off"
	run "$PAK/launch.sh" plugins
	[ "$status" -eq 0 ]
	grep -qx "favorites=on" "$STATE_DIR/plugins.txt"
}

@test "the main screen offers Plugins when there are any" {
	run "$PAK/launch.sh"
	[ "$status" -eq 0 ]
	grep -q '"Plugins"' "$BATS_TEST_TMPDIR/shown"
}
