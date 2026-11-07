#!/usr/bin/env bats

load test_helper

setup() {
	setup_fake_card
	turn_on
	HOOK="$USERDATA_PATH/.hooks/boot.d/00-launcher.sync.sh"
	mkdir -p "$(dirname "$HOOK")"
	cp "$PAK/bin/hook-stub.sh" "$HOOK"
	chmod +x "$HOOK"
}

@test "runs the pak's boot logic while the pak is installed" {
	run "$HOOK"
	[ "$status" -eq 0 ]
	[ -f "$HOOK" ]
	start_launcher
	[ "$(ran_last)" = ours ]
}

@test "deletes itself once the pak is uninstalled" {
	rm -rf "$PAK"
	run "$HOOK"
	[ "$status" -eq 0 ]
	[ ! -f "$HOOK" ]
	start_launcher
	[ "$(ran_last)" = stock ]
}

@test "undoes a copy-mode install once the pak is uninstalled" {
	touch "$BATS_TEST_TMPDIR/mount.fail"
	"$HOOK"
	grep -q MOSAIC_LAUNCHER_WRAPPER "$STOCK_BIN"
	rm -rf "$PAK" "$MOSAIC_LAUNCHER_RUN_DIR"
	run "$HOOK"
	[ "$status" -eq 0 ]
	[ ! -f "$HOOK" ]
	[ ! -f "$SYSTEM_PATH/bin/nextui.stock.elf" ]
	start_launcher
	[ "$(ran_last)" = stock ]
}
