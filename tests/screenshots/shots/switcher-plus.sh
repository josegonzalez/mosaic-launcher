# shellcheck shell=sh

begin
on switcher-plus
press "select"
shot default

begin
on switcher-plus
cfg switcher-plus style=strip
press "select"
shot style-strip

begin
on switcher-plus
cfg switcher-plus style=list
press "select"
shot style-list

begin
on switcher-plus playtime-captions
cfg switcher-plus captions=on
press "select"
shot captions-with-playtime-captions
