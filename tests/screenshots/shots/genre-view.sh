# shellcheck shell=sh

# the platformers in Platformer folders in Game Boy and Game Boy Color
genres() {
	for sys in "Game Boy (GB)/Tobu Tobu Girl.gb" "Game Boy Color (GBC)/Tobu Tobu Girl Deluxe.gbc"; do
		dir="$CARD/Roms/${sys%/*}"
		game="${sys##*/}"
		mkdir -p "$dir/Platformer/.media"
		mv "$dir/$game" "$dir/Platformer/"
		mv "$dir/.media/${game%.*}.png" "$dir/Platformer/.media/"
	done
}

begin
on genre-view
genres
press "down down down down down"
shot default

begin
on genre-view
genres
press "down down down down down a a"
shot genre

begin
on genre-view
cfg genre-view tags=always
genres
press "down down down down down a a"
shot tags-always
