# shellcheck shell=sh

begin
on playtime-limit
cfg playtime-limit limit=60
shot limit

begin
on playtime-limit
cfg playtime-limit limit=30
shot warn

# Thwaite, launched past the limit
begin
on playtime-limit
cfg playtime-limit limit=15
press "down down down down a down a"
shot refused
