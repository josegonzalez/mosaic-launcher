# shellcheck shell=sh

begin
on smart-collections
cfg smart-collections "extra=Tobu Tobu"
press "down down down down down a"
shot extra

# Y on the Tobu Tobu collection, with tags on: Dismiss and Save as tag
begin
on smart-collections tags
cfg smart-collections "extra=Tobu Tobu"
press "down down down down down a y"
shot with-tags
