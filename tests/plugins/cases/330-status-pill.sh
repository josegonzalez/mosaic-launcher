# shellcheck shell=sh
# shard: 2

# shell paks' status files show as a pill in the header: the newest valid
# one that isn't stale, with files that are rejected logged once, and links,
# dotfiles and old files left out
STATUS_DIR="$STATE/status.d"
mkdir -p "$STATUS_DIR"
printf 'state=running\nlabel=Backup\n' >"$STATUS_DIR/backup.status"
printf 'state=running\n' >"$STATUS_DIR/old.status"
touch -t 200001010000 "$STATUS_DIR/old.status"
sleep 1
printf 'state=running\ndone=3\ntotal=10\nlabel=Syncing\n' >"$STATUS_DIR/sync.status"
printf 'state=maybe\n' >"$STATUS_DIR/bad.status"
head -c 2000 /dev/zero | tr '\0' '#' >"$STATUS_DIR/big.status"
printf 'state=running\nlabel=Linked\n' >"$WORK/outside.status"
ln -s "$WORK/outside.status" "$STATUS_DIR/link.status"
printf 'state=running\nlabel=Hidden\n' >"$STATUS_DIR/.tmp.status"
run_launcher "$WORK/status.log" "status: showing sync: Syncing 30%" "status: pill at " || fail "the launcher exited early with status files"
log="$WORK/status.log"
grep -qF "status: ignoring bad.status: state isn't running, done or error" "$log" || fail "the bad status file wasn't logged"
grep -qF "status: ignoring big.status: it's over 1024 bytes" "$log" || fail "the oversized status file wasn't logged"
grep -qF "status: ignoring link.status: it isn't a regular file" "$log" || fail "the linked status file wasn't refused"
[ "$(grep -cF "status: ignoring bad.status" "$log")" -eq 1 ] || fail "the bad status file was logged more than once"
grep -qE "status: showing (backup|old|link|\.tmp)" "$log" && fail "a status file other than the newest showed"
grep -qE "status: pill at [0-9]+,15 [0-9]+x90$" "$log" || fail "the pill isn't in the header: $(grep "status: pill" "$log")"

# the pill goes through the header row, so plugins' widgets sit left of it
# and have less room
printf 'badger=on\n' >"$STATE/plugins.txt"
build_plugin badger 'id=badger\nname=Badger\napi=1\n'
run_launcher "$WORK/status-badger.log" "status: pill at " "plugin badger: widget rect " || fail "the launcher exited early with badger and a status file"
log="$WORK/status-badger.log"
pill_x=$(sed -n 's/.*status: pill at \([0-9]*\),15 .*/\1/p' "$log" | tail -n 1)
pill_w=$(sed -n 's/.*status: pill at [0-9]*,15 \([0-9]*\)x90$/\1/p' "$log" | tail -n 1)
widget_x=$(sed -n 's/.*plugin badger: widget rect \([0-9]*\),.*/\1/p' "$log")
room=$(sed -n 's/.*plugin badger: width first: room \([0-9]*\) .*/\1/p' "$log")
if [ -z "$pill_x" ] || [ -z "$widget_x" ] || [ $((widget_x + 60 + 15)) -ne "$pill_x" ]; then
	fail "badger's widget isn't left of the status pill: pill at $pill_x, widget at $widget_x"
fi
# the stock pill starts a gap right of the status pill, and the widgets
# together keep to half the header left of it
stock_x=$((pill_x + pill_w + 15))
if [ -z "$room" ] || [ "$room" -ne $(((pill_x - 15) - (stock_x - (stock_x - 15) / 2))) ]; then
	fail "badger's room doesn't leave out the status pill: $room"
fi
rm -rf "$WORK/plugins/badger.plugin" "$STATE/plugins.txt"

# a file rewritten while the launcher runs is read again; done shows for a
# few seconds and then the pill goes
rm -rf "$STATUS_DIR" "$WORK/outside.status"
mkdir -p "$STATUS_DIR"
printf 'state=running\ndone=1\ntotal=4\nlabel=Scraping\n' >"$STATUS_DIR/scrape.status"
(
	sleep 2
	printf 'state=done\nlabel=Scraped\n' >"$STATUS_DIR/.scrape.tmp"
	mv "$STATUS_DIR/.scrape.tmp" "$STATUS_DIR/scrape.status"
) &
writer=$!
# done stays up for 5 seconds, so this case gets longer
wait_was=$WAIT
WAIT=$((WAIT + 10))
run_launcher "$WORK/status-done.log" "status: showing scrape: Scraped done" "status: none" || fail "the launcher exited early with a status file changing"
WAIT=$wait_was
wait "$writer"
log="$WORK/status-done.log"
grep -qF "status: showing scrape: Scraping 25%" "$log" || fail "the running status file didn't show"
rm -rf "$STATUS_DIR"
grep -qE "status: (showing|pill|ignoring|none)" "$WORK/hello.log" && fail "a status pill showed with no status files"
