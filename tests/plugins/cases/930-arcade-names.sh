# shellcheck shell=sh

# the bundled arcade-names, with search, seeker and looker, and a made-up
# Logiqx XML DAT, ClrMamePro text DAT and catver.ini in DATs/: a test
# system whose tag is in the tags setting shows its sets by their clean
# titles, the clone's region added back, BIOS and device sets hidden, a
# file the DATs don't know and a map.txt name left as they are, in the
# list, in the card index and in search, which loads before it so the
# index is named again once it has loaded. A system whose tag isn't in
# the setting is left alone. The next start reads the cache; full names
# and each hide setting (clones, not working, catver.ini's categories)
# change what shows; a changed DAT is read again; and with no DAT nothing
# changes and the cache goes.
AN_ROMS="$CARD/Roms/Arcade Names Test (ANT)"
AN_OTHER="$CARD/Roms/Not Arcade (NAT)"
AN_EMU="$CARD/Emus/desktop/ANT.pak"
AN_OTHER_EMU="$CARD/Emus/desktop/NAT.pak"
AN_DATS="$CARD/DATs"
AN_CACHE="$STATE/arcade-names.cache"
rm -rf "$AN_ROMS" "$AN_OTHER" "$AN_DATS" "$AN_CACHE"
mkdir -p "$AN_ROMS" "$AN_OTHER" "$AN_EMU" "$AN_OTHER_EMU" "$AN_DATS" "$STATE"
: >"$AN_EMU/launch.sh"
: >"$AN_OTHER_EMU/launch.sh"
for set in zfight zfightu zbios zdevice zbroken zmahj zmine cquest unknown; do
	: >"$AN_ROMS/$set.zip"
done
: >"$AN_OTHER/zfight.zip"
printf 'zmine.zip\tOwn Name\n' >"$AN_ROMS/map.txt"
cat >"$AN_DATS/arcade.dat" <<'EOF'
<?xml version="1.0"?>
<!DOCTYPE datafile [ <!ENTITY xxe SYSTEM "file:///etc/passwd"> ]>
<datafile>
	<header><name>Made up</name></header>
	<game name="zfight"><description>Zed Fighter (World 910522)</description></game>
	<game name="zfightu" cloneof="zfight" romof="zfight"><description>Zed Fighter (US 910522)</description></game>
	<game name="zbios" isbios="yes"><description>Zed BIOS</description></game>
	<machine name="zdevice" isdevice="yes" runnable="no"><description>Zed Sound</description></machine>
	<machine name="zbroken"><description>Broken Bash (Japan)</description><driver status="preliminary"/></machine>
	<game name="zmahj"><description>Mahjong Mania (Japan)</description></game>
	<game name="zmine"><description>Mine Mapper &amp; Co (set 1) &xxe;</description></game>
</datafile>
EOF
printf 'clrmamepro (\n\tname "Made up"\n)\n\ngame (\n\tname cquest\n\tdescription "Comet Quest (Europe)"\n\trom ( name a.bin size 1 crc 00000000 )\n)\n' \
	>"$AN_DATS/text.dat"
printf '[Category]\nzmahj=Tabletop / Mahjong\nzfight=Fighter / Versus\n' >"$AN_DATS/catver.ini"

make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" arcade-names search >/dev/null || fail "building arcade-names"
build_plugin seeker 'id=seeker\nname=Seeker\napi=1\n'
build_plugin looker 'id=looker\nname=Looker\napi=1\n'

# the plugins named, in this order, and every other plugin built so far off,
# as a plugin plugins.txt doesn't name loads
an_plugins() {
	: >"$STATE/plugins.txt"
	for id in "$@"; do printf '%s=on\n' "$id" >>"$STATE/plugins.txt"; done
	for dir in "$WORK"/plugins/*.plugin; do
		id=$(basename "$dir" .plugin)
		case " $* " in
		*" $id "*) ;;
		*) printf '%s=off\n' "$id" >>"$STATE/plugins.txt" ;;
		esac
	done
}
AN_ALL="zfight.zip,zfightu.zip,zbios.zip,zdevice.zip,zbroken.zip,zmahj.zip,zmine.zip,cquest.zip,unknown.zip"

# the list, the index and search, with the default settings
an_plugins search seeker looker arcade-names
printf 'tags=ant\n' >"$STATE/arcade-names.cfg"
printf 'folder=%s\nindex=%s\n' "$AN_ROMS" "$AN_ALL" >"$STATE/looker.cfg"
printf 'recent1=system:ant\n' >"$STATE/search.cfg"
printf 'steps=action:search.open,down,a\n' >"$STATE/seeker.cfg"
log="$WORK/arcade-names-first.log"
run_launcher "$log" "plugin seeker: done" "plugin looker: done" || fail "the launcher exited early with arcade-names"
plugin=arcade-names
expect "7 sets in arcade.dat"
expect "1 sets in text.dat"
expect "1 mahjong, casino and quiz sets in catver.ini"
grep -qE 'plugin arcade-names: 8 sets from 2 DATs in [0-9.]+ ms, [0-9]+ KB$' "$log" || fail "arcade-names didn't log what it read"
[ -s "$AN_CACHE" ] || fail "arcade-names didn't keep a cache"
plugin=looker
expect "entries zbroken.zip,cquest.zip,zmahj.zip,zmine.zip,unknown.zip,zfightu.zip,zfight.zip"
expect 'index zfight.zip 1 "Zed Fighter (World)"  added 0 filtered 0'
expect 'index zfightu.zip 1 "Zed Fighter (US)"  added 0 filtered 0'
expect 'index zbios.zip 1 "zbios"  added 0 filtered 1'
expect 'index zdevice.zip 1 "zdevice"  added 0 filtered 1'
expect 'index zbroken.zip 1 "Broken Bash"  added 0 filtered 0'
expect 'index zmine.zip 1 "Own Name"  added 0 filtered 0'
expect 'index cquest.zip 1 "Comet Quest"  added 0 filtered 0'
expect 'index unknown.zip 1 "unknown"  added 0 filtered 0'
plugin=seeker
expect "list $AN_ROMS: Broken Bash, Comet Quest, Mahjong Mania, Own Name, unknown, Zed Fighter (US), Zed Fighter (World) selected 0"
expect "list $CARD/Search: Broken Bash, Comet Quest, Mahjong Mania, Own Name, unknown, Zed Fighter (US), Zed Fighter (World) selected 0"

# a system whose tag isn't in the setting keeps its names
an_plugins looker seeker arcade-names
printf 'folder=%s\nindex=zfight.zip\n' "$AN_OTHER" >"$STATE/looker.cfg"
printf 'steps=\n' >"$STATE/seeker.cfg"
log="$WORK/arcade-names-other.log"
run_launcher "$log" "plugin seeker: done" "plugin looker: done" || fail "the launcher exited early in a system that isn't arcade"
plugin=arcade-names
grep -qE 'plugin arcade-names: 8 sets from the cache in [0-9.]+ ms$' "$log" || fail "arcade-names didn't read its cache"
plugin=seeker
expect "list $AN_OTHER: zfight selected 0"
plugin=looker
expect 'index zfight.zip 1 "zfight"  added 0 filtered 0'

# full names, and every hide setting: the clone with its parent there, the
# game that doesn't work and catver.ini's mahjong
an_plugins looker seeker arcade-names
printf 'tags=MAME,ANT\nnames=Full\nclones=on\nnot_working=on\ncategories=on\n' >"$STATE/arcade-names.cfg"
printf 'folder=%s\nindex=zfightu.zip,zbroken.zip,zmahj.zip\n' "$AN_ROMS" >"$STATE/looker.cfg"
log="$WORK/arcade-names-hide.log"
run_launcher "$log" "plugin seeker: done" "plugin looker: done" || fail "the launcher exited early hiding with arcade-names"
plugin=seeker
expect "list $AN_ROMS: Comet Quest (Europe), Own Name, unknown, Zed Fighter (World 910522) selected 0"
# hidden, and so not named
plugin=looker
expect 'index zfightu.zip 1 "zfightu"  added 0 filtered 1'
expect 'index zbroken.zip 1 "zbroken"  added 0 filtered 1'
expect 'index zmahj.zip 1 "zmahj"  added 0 filtered 1'

# each on its own: only the clone. looker reads the index kept on the card
# from its init, before arcade-names loads, so the index is named again
# once it has
printf 'tags=ANT\nclones=on\n' >"$STATE/arcade-names.cfg"
printf 'folder=%s\nindex=zfight.zip,zfightu.zip\n' "$AN_ROMS" >"$STATE/looker.cfg"
log="$WORK/arcade-names-clones.log"
run_launcher "$log" "plugin seeker: done" "plugin looker: done" || fail "the launcher exited early hiding clones"
plugin=seeker
expect "list $AN_ROMS: Broken Bash, Comet Quest, Mahjong Mania, Own Name, unknown, Zed Fighter selected 0"
plugin=looker
expect 'index zfight.zip 1 "Zed Fighter"  added 0 filtered 0'
expect 'index zfightu.zip 1 "zfightu"  added 0 filtered 1'
plugin=seeker
# only what doesn't work
printf 'tags=ANT\nnot_working=on\n' >"$STATE/arcade-names.cfg"
log="$WORK/arcade-names-working.log"
run_launcher "$log" "plugin seeker: done" "plugin looker: done" || fail "the launcher exited early hiding what doesn't work"
expect "list $AN_ROMS: Comet Quest, Mahjong Mania, Own Name, unknown, Zed Fighter (US), Zed Fighter (World) selected 0"
# only the categories
printf 'tags=ANT\ncategories=on\n' >"$STATE/arcade-names.cfg"
log="$WORK/arcade-names-categories.log"
run_launcher "$log" "plugin seeker: done" "plugin looker: done" || fail "the launcher exited early hiding categories"
expect "list $AN_ROMS: Broken Bash, Comet Quest, Own Name, unknown, Zed Fighter (US), Zed Fighter (World) selected 0"

# a DAT changed: read again
printf 'tags=ANT\n' >"$STATE/arcade-names.cfg"
printf 'game (\n\tname unknown\n\tdescription "Now Known (World)"\n)\n' >>"$AN_DATS/text.dat"
log="$WORK/arcade-names-changed.log"
run_launcher "$log" "plugin seeker: done" "plugin looker: done" || fail "the launcher exited early after a DAT changed"
plugin=arcade-names
expect "2 sets in text.dat"
grep -qF "plugin arcade-names: 9 sets from the cache" "$log" && fail "arcade-names used its cache after a DAT changed"
plugin=seeker
expect "list $AN_ROMS: Broken Bash, Comet Quest, Mahjong Mania, Now Known, Own Name, Zed Fighter (US), Zed Fighter (World) selected 0"

# no DAT: nothing changes, and the cache goes
rm -rf "$AN_DATS"
log="$WORK/arcade-names-none.log"
run_launcher "$log" "plugin seeker: done" "plugin looker: done" || fail "the launcher exited early with no DAT"
plugin=arcade-names
expect "no DAT in $AN_DATS, naming nothing"
plugin=looker
expect "entries cquest.zip,zmine.zip,unknown.zip,zbios.zip,zbroken.zip,zdevice.zip,zfight.zip,zfightu.zip,zmahj.zip"
expect 'index zfight.zip 1 "zfight"  added 0 filtered 0'
[ -e "$AN_CACHE" ] && fail "arcade-names kept its cache with no DAT"

rm -rf "$AN_ROMS" "$AN_OTHER" "$AN_EMU" "$AN_OTHER_EMU" "$AN_DATS" "$AN_CACHE"
rmdir "$CARD/Emus/desktop" 2>/dev/null
rm -rf "$WORK/plugins/arcade-names.plugin" "$WORK/plugins/search.plugin" "$WORK/plugins/seeker.plugin" "$WORK/plugins/looker.plugin" \
	"$STATE/arcade-names.cfg" "$STATE/search.cfg" "$STATE/seeker.cfg" "$STATE/looker.cfg" "$STATE/plugins.txt"
case_log=""
