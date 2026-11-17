# shellcheck shell=sh

# the Tasks folder, with the examples copied to the card on the first start
begin
on tasks
press "down down down down down a"
shot default

# Y on Tobu Tobu Girl, in Game Boy, with the example game tasks
begin
on tasks
press "down down a down y"
shot game-menu

# List saves' last line and exit status, as a toast
begin
on tasks
press "down down a down y down a"
after 500
shot toast

# the wait screen, while a task runs
begin
on tasks
mkdir -p "$CARD/Tasks"
printf 'sleep 10\necho done\n' >"$CARD/Tasks/Wait ten seconds.sh"
press "down down down down down a a"
after 1500
shot running
