# shellcheck shell=sh
# shard: 2

# writer copies RGBA8888 pixels into surfaces of its own and reads them
# back; writes into its lent row, an art cache image and, under hello's id,
# its own surface are refused
mkdir -p "$STATE"
printf 'art=%s\nother=hello\n' "$ROOT/tests/plugins/drawer/art.png" >"$STATE/writer.cfg"
printf 'hello=on\nwriter=on\n' >"$STATE/plugins.txt"
build_plugin hello 'id=hello\nname=Hello\napi=1\n'
build_plugin writer 'id=writer\nname=Writer\napi=1\n'
run_launcher "$WORK/writer.log" "plugin writer: done" || fail "the launcher exited early with writer loaded"
log="$WORK/writer.log"
plugin=writer
expect "write 0, FF0000 FF 00FF00 FF 0000FF 80 FF0000 FF, FF0000 FF 123456 78 000000 00 FF0000 FF"
expect "whole 0, 112233 44 112233 44"
expect "clipped 0, 505050 FF 909090 FF 112233 44, off 0, 112233 44"
expect "blit 00FF00 FF FF0000 FF"
expect "null pixels -1, null surface -1, unknown -1, short pitch -1, negative -1, not loaded -1, no id -1"
expect "image 0 112233 44"
expect "freed -1"
expect "frame 720x480 written 0,"
expect "lent -1"
expect "art -1 FF0000 FF"
expect "other -1, own 0"
grep -F "plugin writer: frame" "$log"
rm -rf "$WORK/plugins/writer.plugin" "$WORK/plugins/hello.plugin" "$STATE/writer.cfg" "$STATE/hello.cfg" "$STATE/plugins.txt"
