# shellcheck shell=sh

# the Homebrew collection filtered to its unplayed games, picked from the Filter tile
begin
on filter-presets
press "down a down a menu left a down a wait wait b wait"
shot default

# the Filter tile's list, with favorites and save-state-manager for Favorites and Has save
begin
on filter-presets favorites save-state-manager
press "down a down a menu left left a"
shot filter-tile

# with favorites: the Homebrew collection filtered to the favorites
begin
on filter-presets favorites
press "down a down a menu left a down down a wait wait b wait"
shot with-favorites
