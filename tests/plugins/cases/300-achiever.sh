# shellcheck shell=sh
# shard: 1
# reads 290-tracker's log, so it shares its shard

# achiever reads local RetroAchievements progress from fixture .ra files
# in minarch's format: Hit has a cached set of 3 (unlocks 1, and 2 in
# hardcore; 2 muted), Bare none, the A5200 game can't be hashed and
# Missing isn't there. Every answer is unknown at first, the hashes land in
# the launcher's cache, and a restart reads them back instead of hashing.
# Nothing is hashed, and no thread starts, when no plugin asks.
RA_ROMS="$CARD/Roms/RA Test (GB)"
RA_5200="$CARD/Roms/RA Test 5200 (A5200)"
grep -qF "retroachievements:" "$WORK/tracker.log" && fail "the launcher read RetroAchievements progress with no plugin asking"
[ -e "$RA_HASHES" ] && fail "the launcher made $RA_HASHES with no plugin asking"
[ -d "$RA_DIR" ] && mv "$RA_DIR" "$WORK/ra.saved"
mkdir -p "$RA_ROMS" "$RA_5200" "$RA_DIR/offline/cache" "$RA_DIR/muted" "$STATE"
printf 'mosaic retroachievements test cart %.0s' 1 2 3 4 5 6 7 8 >"$RA_ROMS/Hit.gb"
printf 'another cart' >"$RA_ROMS/Bare.gb"
printf 'a 5200 cart' >"$RA_5200/Game.a52"
HIT_HASH=$(md5_of "$RA_ROMS/Hit.gb")
BARE_HASH=$(md5_of "$RA_ROMS/Bare.gb")
frame_json '{"Success":true,"GameId":99,"Sets":[{"Type":"core","Achievements":[{"ID":1,"Flags":3,"Points":5},{"ID":2,"Flags":3,"Points":10},{"ID":3,"Flags":3,"Points":20},{"ID":4,"Flags":5,"Points":50}]}]}' \
	"$RA_DIR/offline/cache/achievementsets_$HIT_HASH.bin"
frame_json '{"Success":true,"HardcoreUnlocks":[{"ID":2,"When":1}],"Unlocks":[{"ID":1,"When":2}],"ServerNow":3}' \
	"$RA_DIR/offline/cache/startsession_$HIT_HASH.bin"
printf '2\n' >"$RA_DIR/muted/$HIT_HASH.txt"
printf 'game1=%s\ngame2=%s\ngame3=%s\ngame4=%s\n' "$RA_ROMS/Hit.gb" "$RA_ROMS/Bare.gb" "$RA_5200/Game.a52" \
	"$RA_ROMS/Missing.gb" >"$STATE/achiever.cfg"
printf 'achiever=on\n' >"$STATE/plugins.txt"
build_plugin achiever 'id=achiever\nname=Achiever\napi=1\n'
run_launcher "$WORK/achiever.log" "plugin achiever: done" || fail "the launcher exited early with achiever loaded"
log="$WORK/achiever.log"
plugin=achiever
expect "first Hit.gb -1"
expect "first Missing.gb -1"
expect "game Hit.gb rc 1 total 3 unlocked 2 hardcore 1 points 15/35 muted 1 id 99 hash 1 $HIT_HASH"
expect "game Bare.gb rc 0 total 0 unlocked 0 hardcore 0 points 0/0 muted 0 id 0 hash 1 $BARE_HASH"
expect "game Game.a52 rc 0 total 0 unlocked 0 hardcore 0 points 0/0 muted 0 id 0 hash 0 -"
expect "game Missing.gb rc 0 total 0 unlocked 0 hardcore 0 points 0/0 muted 0 id 0 hash 0 -"
expect "null -1"
expect "short rc 1 total 3 rest untouched 1"
grep -qF "retroachievements: Roms/RA Test (GB)/Hit.gb is $HIT_HASH, hashed as console 4" "$log" || fail "Hit.gb's hash wasn't logged"
[ "$(grep -c ", hashed as console" "$log")" -eq 3 ] || fail "the games weren't hashed once each: $(grep "retroachievements:" "$log")"
grep -qF "$HIT_HASH	$(wc -c <"$RA_ROMS/Hit.gb" | tr -d ' ')	" "$RA_HASHES" || fail "Hit.gb's hash isn't in $RA_HASHES"
grep -qF "Roms/RA Test 5200 (A5200)/Game.a52" "$RA_HASHES" || fail "the unhashable game isn't in $RA_HASHES"
# a restart reads the hashes back
run_launcher "$WORK/achiever-cached.log" "plugin achiever: done" || fail "the launcher exited early with achiever after a restart"
log="$WORK/achiever-cached.log"
expect "game Hit.gb rc 1 total 3 unlocked 2 hardcore 1 points 15/35 muted 1 id 99 hash 1 $HIT_HASH"
grep -qF ", hashed as console" "$log" && fail "games were hashed again: $(grep "retroachievements:" "$log")"
# a damaged cache file says nothing
printf 'x' >>"$RA_DIR/offline/cache/achievementsets_$HIT_HASH.bin"
run_launcher "$WORK/achiever-damaged.log" "plugin achiever: done" || fail "the launcher exited early with a damaged cache file"
log="$WORK/achiever-damaged.log"
expect "game Hit.gb rc 0 total 0 unlocked 0 hardcore 0 points 0/0 muted 0 id 0 hash 1 $HIT_HASH"
grep -qF "retroachievements: ignoring" "$log" || fail "the damaged cache file wasn't logged"
rm -rf "$WORK/plugins/achiever.plugin" "$STATE/achiever.cfg" "$STATE/plugins.txt" "$RA_ROMS" "$RA_5200" "$RA_DIR" "$RA_HASHES"
