#!/usr/bin/env bats

load test_helper

setup() {
	setup_fake_card
	PAK_DIR="$PAK"
	# shellcheck source=pak/bin/common.sh
	. "$PAK/bin/common.sh"
}

@test "the installed short hash matches a pinned full hash" {
	version_supported
}

@test "a different build hash is unsupported" {
	printf 'NextUI-20991231-0\nabcdef0\n' >"$SDCARD_PATH/.system/version.txt"
	! version_supported
}

@test "a missing version.txt is unsupported" {
	rm "$SDCARD_PATH/.system/version.txt"
	! version_supported
}

@test "a hash too short to be unambiguous is unsupported" {
	printf 'NextUI-20261013-0\n39a0\n' >"$SDCARD_PATH/.system/version.txt"
	! version_supported
}

@test "a hash with non-hex characters is unsupported" {
	printf 'NextUI-20261013-0\n.*\n' >"$SDCARD_PATH/.system/version.txt"
	! version_supported
}

@test "CRLF line endings in version.txt still match" {
	printf 'NextUI-20261013-0\r\n39a06de\r\n' >"$SDCARD_PATH/.system/version.txt"
	version_supported
	[ "$(nextui_release_name)" = "NextUI-20261013-0" ]
}
