# shellcheck shell=sh

# Thwaite, in the Homebrew collection, with no thumbnails
no_art() { rm -f "$CARD"/Roms/*/.media/*.png; }

begin
on art-placeholder
no_art
press "down a down a up"
shot default

begin
on art-placeholder
cfg art-placeholder style=pattern
no_art
press "down a down a up"
shot style-pattern

begin
on art-placeholder layout-grid
slot layout layout-grid
no_art
press "right a right a left"
shot with-layout-grid
