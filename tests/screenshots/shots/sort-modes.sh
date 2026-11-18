# shellcheck shell=sh

# the Sort tile's list, from the quick menu
begin
on sort-modes
press "menu left a"
shot default

# Game Boy sorted by most played, the header showing it
begin
on sort-modes
cfg sort-modes "mode=most played"
press "down down a"
shot most-played

# the Homebrew collection sorted by year, the games with none last
begin
on sort-modes
cfg sort-modes mode=year
press "down a down a"
shot year

# with the Sort button on Y: its hint on the game list
begin
on sort-modes
cfg sort-modes mode=year button=Y
press "down down a"
shot button

# with jump-letter: the Homebrew collection by most played, SELECT held for its sections
begin
on sort-modes jump-letter
cfg sort-modes "mode=most played"
press "down a down a select/1500"
shot with-jump-letter
