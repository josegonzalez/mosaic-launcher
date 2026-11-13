# shellcheck shell=sh

# the Homebrew collection, with no thumbnails so the rows have room, on µCity
no_art() { rm -f "$CARD"/Roms/*/.media/*.png; }

begin
on playtime-captions
no_art
press "down a down a down down down"
shot default

begin
on playtime-captions
cfg playtime-captions format=compact
no_art
press "down a down a down down down"
shot format-compact

begin
on playtime-captions
cfg playtime-captions format=date
no_art
press "down a down a down down down"
shot format-date

begin
on playtime-captions
cfg playtime-captions sessions=on
no_art
press "down a down a down down down"
shot sessions

begin
on playtime-captions
cfg playtime-captions never=on
no_art
press "down a down a down down down"
shot never

begin
on playtime-captions row-badges
cfg row-badges unplayed=on
no_art
press "down a down a down down down"
shot with-row-badges
