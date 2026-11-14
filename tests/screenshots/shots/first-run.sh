# shellcheck shell=sh

begin
on first-run
shot default

begin
on first-run
press "a"
shot settings

# the Plugin settings page, with the plugins it sets up on
begin
on first-run home-dashboard favorites theme-plus localization
press "a a"
shot setup

begin
on first-run
press "a a a"
shot plugins

# the Buttons page, with favorites' Y, scrolled down to it
begin
on first-run favorites
press "a a a a a down down down down"
shot buttons
