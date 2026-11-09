# shellcheck shell=sh
# shard: 3

# artist supplies art of every kind and loads it through the art cache: a
# provider answering each kind, a load then a hit, the entries either side
# of the selection loaded ahead, loads asked for before the selection moved
# dropped, and the built-in list falling back from a thumbnail to a box, 3D
# box or screenshot. framer answers boxes too, so the first in load order
# wins. Then, with a cache that holds two images, the least recently used
# one goes.
ART_DIR="$WORK/art"
mkdir -p "$ART_DIR" "$STATE"
for name in Alpha-thumb Bravo-box Charlie-3d framer-Bravo plain; do
	cp "$ROOT/tests/plugins/drawer/art.png" "$ART_DIR/$name.png"
done
for game in Alpha Bravo Charlie Delta Echo Foxtrot; do
	cp "$ROOT/tests/plugins/drawer/art.png" "$ART_DIR/$game-shot.png"
	cp "$ROOT/tests/plugins/drawer/art.png" "$ART_DIR/$game-back.png"
done
printf 'dir=%s\nimage=%s\n' "$ART_DIR" "$ART_DIR/plain.png" >"$STATE/artist.cfg"
printf 'dir=%s\n' "$ART_DIR" >"$STATE/framer.cfg"
printf 'artist=on\nframer=on\n' >"$STATE/plugins.txt"
build_plugin artist 'id=artist\nname=Artist\napi=1\nslots=layout\n'
build_plugin framer 'id=framer\nname=Framer\napi=1\n'
run_launcher "$WORK/artist.log" "plugin artist: done" || fail "the launcher exited early with artist loaded"
log="$WORK/artist.log"
plugin=artist
expect "open folder 1"
expect 'list art Alpha "Alpha-thumb.png"'
expect 'find Alpha thumbnail 1 "Alpha-thumb.png"'
expect 'find Bravo box 1 "Bravo-box.png"'
expect 'find Charlie box3d 1 "Charlie-3d.png"'
expect 'find Alpha logo 0 "Alpha-logo.png"'
expect 'find Delta screenshot 1 "Delta-shot.png"'
expect 'find Echo backdrop 1 "Echo-back.png"'
expect 'find Delta box3d 0 ""'
expect 'find Alpha unknown 0 ""'
expect "refused get -1 -1, cleared 1, missing -1, load -1 -1"
expect "shot Alpha first 0"
expect "shot Alpha in 1"
expect "hit 1 same 1, 4x2, pixel FF0000, blit 0, copy 1, free -1, fill -1, text -1"
expect "still FF0000, copy free 0"
expect "ahead Bravo 1 Foxtrot 1 Delta 0"
expect "before move Delta 0 Echo 0 Foxtrot 0"
expect "move 0"
expect "after move Alpha 0"
expect "dropped Delta 0 Echo 0 Foxtrot 0, kept Alpha 1"
expect 'list art Charlie "Charlie-3d.png"'
expect 'list art Bravo "Bravo-box.png"'
expect 'list art Delta "Delta-shot.png"'
expect "load first 0"
expect "load in 1, pixel FF0000"

printf 'framer=on\nartist=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/artist-framer.log" "plugin artist: done" || fail "the launcher exited early with framer first"
log="$WORK/artist-framer.log"
expect 'find Bravo box 1 "framer-Bravo.png"'
expect 'list art Bravo "framer-Bravo.png"'
# framer's missing box for Charlie still wins the box, and the list goes on to the 3D box
expect 'list art Charlie "Charlie-3d.png"'

printf 'mode=evict\n' >>"$STATE/artist.cfg"
printf 'artist=on\n' >"$STATE/plugins.txt"
# each 4x2 image decodes to 32 bytes
MOSAIC_LAUNCHER_ART_CACHE_BYTES=80 run_launcher "$WORK/artist-evict.log" "plugin artist: done" \
	|| fail "the launcher exited early with a small art cache"
unset MOSAIC_LAUNCHER_ART_CACHE_BYTES
log="$WORK/artist-evict.log"
expect "fill Charlie in 1"
expect "fill Delta in 1"
expect "touch Charlie 1"
expect "fill Echo in 1"
expect "kept Charlie 1 Echo 1, evicted Delta 0"
rm -rf "$WORK/plugins/artist.plugin" "$WORK/plugins/framer.plugin" "$ART_DIR" "$STATE/artist.cfg" "$STATE/framer.cfg" "$STATE/plugins.txt"
