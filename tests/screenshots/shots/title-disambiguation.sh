# shellcheck shell=sh

# Tobu Tobu Girl in Game Boy Color too, and in Favorites from both, with no
# thumbnails so the names have room
begin
on title-disambiguation
cp "$CARD/Roms/Game Boy (GB)/Tobu Tobu Girl.gb" "$CARD/Roms/Game Boy Color (GBC)/Tobu Tobu Girl.gbc"
cp "$CARD/Roms/Game Boy (GB)/.media/Tobu Tobu Girl.png" "$CARD/Roms/Game Boy Color (GBC)/.media/"
printf '/Roms/Game Boy (GB)/Tobu Tobu Girl.gb\n/Roms/Game Boy Color (GBC)/Tobu Tobu Girl.gbc\n' >>"$CARD/Collections/Favorites.txt"
rm -f "$CARD"/Roms/*/.media/*.png
press "down a a"
shot default
