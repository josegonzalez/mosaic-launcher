# shellcheck shell=sh

# Platformers folders in Game Boy Color (GBC) and Game Boy Color (SGB), which stock lists as one system
begin
on collate-subfolders
mkdir -p "$CARD/Emus/desktop/SGB.pak" "$CARD/Roms/Game Boy Color (SGB)/Platformers/.media" "$CARD/Roms/Game Boy Color (GBC)/Platformers/.media"
: >"$CARD/Emus/desktop/SGB.pak/launch.sh"
mv "$CARD/Roms/Game Boy Color (GBC)/Tobu Tobu Girl Deluxe.gbc" "$CARD/Roms/Game Boy Color (GBC)/Platformers/"
mv "$CARD/Roms/Game Boy Color (GBC)/.media/Tobu Tobu Girl Deluxe.png" "$CARD/Roms/Game Boy Color (GBC)/Platformers/.media/"
mv "$CARD/Roms/Game Boy (GB)/Tobu Tobu Girl.gb" "$CARD/Roms/Game Boy Color (SGB)/Platformers/"
mv "$CARD/Roms/Game Boy (GB)/.media/Tobu Tobu Girl.png" "$CARD/Roms/Game Boy Color (SGB)/Platformers/.media/"
press "down down down a a"
shot default
