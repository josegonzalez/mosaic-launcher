# shellcheck shell=sh
#
# Sourced by the test scripts that time what they run.

# now_ms: milliseconds since the epoch; macOS's date has no %N
now_ms() {
	now_ns=$(date +%s%N)
	case "$now_ns" in
		*N) perl -MTime::HiRes=time -e 'printf "%d\n", time * 1000' ;;
		*) echo $((now_ns / 1000000)) ;;
	esac
}
