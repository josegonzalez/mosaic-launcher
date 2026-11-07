#!/bin/sh
# Shared by pin-minui.sh and package.sh. Expects LOCK to be set.
# shellcheck disable=SC2034 # sourced; the callers use these

# platform:flavour. The "-nextui" builds draw in the user's NextUI theme.
TARGETS="tg5040:tg5040-nextui tg5050:tg5050-nextui h700:h700-nextui"

# Linux has sha256sum; macOS has shasum.
sha256() {
	if command -v sha256sum >/dev/null 2>&1; then
		sha256sum "$1" | cut -d' ' -f1
	else
		shasum -a 256 "$1" | cut -d' ' -f1
	fi
}

lock_value() {
	[ -f "$LOCK" ] || return 0
	awk -v key="$1" '$1 == key { print $2 }' "$LOCK"
}

# The pinned hash for one file: tool and platform (or "LICENSE").
lock_hash() {
	[ -f "$LOCK" ] || return 0
	awk -v tool="$1" -v platform="$2" '$1 == "sha256" && $2 == tool && $3 == platform { print $5 }' "$LOCK"
}

lock_flavour() {
	[ -f "$LOCK" ] || return 0
	awk -v tool="$1" -v platform="$2" '$1 == "sha256" && $2 == tool && $3 == platform { print $4 }' "$LOCK"
}

tool_version() {
	case "$1" in
		minui-presenter) echo "$2" ;;
		*) echo "$3" ;;
	esac
}

# POSIX sh has no local variables, so each helper prefixes its own to avoid
# clobbering the caller's.

# Prefers gh, which authenticates and so avoids anonymous rate limits.
fetch_asset() {
	fa_tool="$1" fa_version="$2" fa_asset="$3" fa_out="$4"
	if command -v gh >/dev/null 2>&1; then
		gh release download "$fa_version" -R "josegonzalez/$fa_tool" -p "$fa_asset" -O "$fa_out" --clobber
	else
		curl -fsSL -o "$fa_out" "https://github.com/josegonzalez/$fa_tool/releases/download/$fa_version/$fa_asset"
	fi
}

fetch_license() {
	fl_tool="$1" fl_version="$2" fl_out="$3"
	if command -v gh >/dev/null 2>&1; then
		gh api -H "Accept: application/vnd.github.raw" "repos/josegonzalez/$fl_tool/contents/LICENSE?ref=$fl_version" >"$fl_out"
	else
		curl -fsSL -o "$fl_out" "https://raw.githubusercontent.com/josegonzalez/$fl_tool/$fl_version/LICENSE"
	fi
}
