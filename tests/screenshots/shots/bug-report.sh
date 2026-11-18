# shellcheck shell=sh

# the Bug report tile on the quick menu
begin
on bug-report
press "menu wait left"
shot tile

# the message naming the report once it's saved
begin
on bug-report
press "menu wait left a"
shot saved
