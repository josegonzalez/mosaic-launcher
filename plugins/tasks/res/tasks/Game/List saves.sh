#!/bin/sh
# Lists the selected game's saves and save states. It only reads.
# $1 is the game's full path, such as /mnt/SDCARD/Roms/Game Boy (GB)/Tetris.gb
card="${SDCARD_PATH:-/mnt/SDCARD}"
game="$1"
# the system's tag, such as GB for Roms/Game Boy (GB)
rel="${game#"$card/Roms/"}"
folder="${rel%%/*}"
tag="${folder##*\(}"
tag="${tag%\)}"
name=$(basename "$game")

saves=0
for file in "$card/Saves/$tag/$name".*; do
	[ -e "$file" ] || continue
	echo "save: $(basename "$file")"
	saves=$((saves + 1))
done
states=0
for file in "$card/.userdata/shared/$tag"-*/"$name".st*; do
	[ -e "$file" ] || continue
	echo "state: $(basename "$file")"
	states=$((states + 1))
done
if [ "$saves" -eq 0 ] && [ "$states" -eq 0 ]; then
	echo "No saves or states"
else
	echo "Saves: $saves, states: $states"
fi
