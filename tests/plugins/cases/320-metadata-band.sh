# shellcheck shell=sh
# shard: 3

# metadata-band draws the year, publisher and genre from a game's .info
# sidecar in a pill under its art, which dater, after it in load order,
# reads back off the screen. Apple has all three (its year a full date)
# under a square thumbnail, Cherry only a genre under a wide one, Banana a
# sidecar with none of the keys and Durian no sidecar, so neither gets a
# band, and Elder's long publisher leaves out what doesn't fit
BAND_DIR="$CARD/Roms/Metadata Band (MBD)"
mkdir -p "$BAND_DIR/.media" "$STATE"
for game in Apple Banana Cherry Durian Elder; do
	: >"$BAND_DIR/$game.gb"
	cp "$ROOT/tests/plugins/peeker/art.png" "$BAND_DIR/.media/$game.png"
done
cp "$ROOT/tests/plugins/drawer/art.png" "$BAND_DIR/.media/Cherry.png"
printf 'year=1994-11-21\npublisher=Nintendo\ngenre=Platformer\ndeveloper=Nintendo\n' >"$BAND_DIR/.media/Apple.info"
printf 'tier=3\nname=Banana\n' >"$BAND_DIR/.media/Banana.info"
printf 'genre= Puzzle \n' >"$BAND_DIR/.media/Cherry.info"
printf 'year=2001\npublisher=A Publisher With a Name Much Too Long to Fit Under Any Art at All\ngenre=Racing\n' >"$BAND_DIR/.media/Elder.info"
make -s -C "$ROOT/plugins" metadata-band ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building metadata-band"
build_plugin dater 'id=dater\nname=Dater\napi=1\n'
printf 'folder=Roms/Metadata Band (MBD)\nApple=1994 \302\267 Nintendo \302\267 Platformer\nCherry=Puzzle\nElder=2001\n' >"$STATE/dater.cfg"
printf 'metadata-band=on\ndater=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/metadata-band.log" "plugin dater: done" || fail "the launcher exited early with metadata-band loaded"
log="$WORK/metadata-band.log"
plugin=metadata-band
expect "fields year,publisher,genre"
plugin=dater
expect "open folder 1"
expect "band Apple under art 1, centred 1, text 1, as wide as \"1994 · Nintendo · Platformer\" 1"
expect "band Banana none"
expect "band Cherry under art 1, centred 1, text 1, as wide as \"Puzzle\" 1"
expect "band Durian none"
expect "band Elder under art 1, centred 1, text 1, as wide as \"2001\" 1"
grep -qF "plugins: metadata-band's" "$log" && fail "metadata-band's decorations were turned off"

# the fields setting picks the keys and their order, skipping unknown ones
printf 'fields=genre, Developer ,bogus,year\n' >"$STATE/metadata-band.cfg"
printf 'folder=Roms/Metadata Band (MBD)\nApple=Platformer \302\267 Nintendo \302\267 1994\nCherry=Puzzle\nElder=Racing \302\267 2001\n' >"$STATE/dater.cfg"
run_launcher "$WORK/metadata-band-fields.log" "plugin dater: done" || fail "the launcher exited early with metadata-band's fields set"
log="$WORK/metadata-band-fields.log"
plugin=metadata-band
expect "fields genre,developer,year"
plugin=dater
expect "band Apple under art 1, centred 1, text 1, as wide as \"Platformer · Nintendo · 1994\" 1"
expect "band Elder under art 1, centred 1, text 1, as wide as \"Racing · 2001\" 1"
expect "band Durian none"

# with no field it knows, no band
printf 'fields=bogus\n' >"$STATE/metadata-band.cfg"
run_launcher "$WORK/metadata-band-none.log" "plugin dater: done" || fail "the launcher exited early with metadata-band's fields empty"
log="$WORK/metadata-band-none.log"
plugin=metadata-band
expect "fields none"
plugin=dater
expect "band Apple none"
rm -rf "$WORK/plugins/dater.plugin" "$WORK/plugins/metadata-band.plugin" "$BAND_DIR" \
	"$STATE/dater.cfg" "$STATE/metadata-band.cfg" "$STATE/plugins.txt"

# with no plugin asking, nothing reads the play history
grep -q "play history:" "$WORK/hello.log" "$WORK/off.log" && fail "the play history was read with no plugin asking"
