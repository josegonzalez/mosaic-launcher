# shellcheck shell=sh

# the collections from rules, then the franchises
begin
on smart-collections
press "down down down down down a"
shot default

# two rules of the user's, after the built-in ones
begin
on smart-collections
mkdir -p "$STATE/smart-collections"
printf 'Tobu Tobu games = tobu\nThe 2010s on NES = decade:2010s system:fc\n' >"$STATE/smart-collections/rules.txt"
press "down down down down down a"
shot rules

begin
on smart-collections
cfg smart-collections "extra=Tobu Tobu" never=off recent=off long=off added=off decades=off genres=off
press "down down down down down a"
shot extra

# Y on the Never played collection, with tags on: Dismiss and Save as tag
begin
on smart-collections tags
press "down down down down down a y"
shot with-tags
