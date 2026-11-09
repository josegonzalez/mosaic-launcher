# shellcheck shell=sh
# shard: 1

# a plugin orders and renames a folder's entries, then reads the list and
# moves the selection
build_plugin sorter 'id=sorter\nname=Sorter\napi=1\n'
run_launcher "$WORK/sorter.log" "plugin sorter: next frame selected" || fail "the launcher exited early with sorter loaded"
plugin=sorter
log="$WORK/sorter.log"
expect "current list from init (null)"
expect "select from init -1"
expect "set_name outside list_ready -1"
expect "open folder 1"
expect "list_ready 5 entries"
expect "renamed Tetris (gbc) 0"
expect "renamed Tetris (gb) 0"
expect "order Pinned,Alpha,Beta,Tetris (gb),Tetris (gbc)"
expect "selected index 0"
expect "select 3 0, bad -1 -1"
expect "selected 3 Tetris (gb)"
expect "next frame selected 3 Tetris (gb)"
rm -rf "$WORK/plugins/sorter.plugin"
