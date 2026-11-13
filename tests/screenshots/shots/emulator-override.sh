# shellcheck shell=sh

# Tobu Tobu Girl, in Game Boy
begin
on emulator-override
press "down down a down y"
shot default

begin
on emulator-override
cfg emulator-override all=on
press "down down a down y"
shot all
