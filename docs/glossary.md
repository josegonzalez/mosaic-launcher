# Glossary

Short explanations of the terms the [plugin pages](plugins.md) use.

## CFW

Custom firmware: software you put on a handheld's SD card to replace or extend what it ships with. NextUI is one.

## NextUI

The custom firmware mosaic-launcher is built for. It runs on the tg5040, tg5050 and h700 devices, which the plugin pages call platforms. It comes from MinUI, and the plugin pages mention MinUI, Onion and other forks as the places their ideas came from.

## Launcher

The program that draws the game list, the quick menu and the game switcher, and starts games. mosaic-launcher replaces NextUI's own launcher and adds plugins.

## Plugin

A feature added to the launcher that you can turn on or off. [plugins.md](plugins.md) lists the bundled ones, and each has its own page.

## Pak

A folder ending in `.pak` that holds a program to launch, with a `launch.sh` script that starts it. An emulator is a pak, and so is a tool such as the Launcher pak, whose Plugins page turns plugins on and off.

## Tools

The folder on the first screen that lists the paks that are tools rather than games, such as the Launcher pak and Game Tracker.

## minarch

The emulator front end NextUI uses. It runs a [core](#core) for a game, and keeps its save states and settings.

## Core

The emulator itself, a libretro library that minarch loads to run a system's games.

## Save state

A snapshot of a game you can come back to, as opposed to the game's own save file. minarch keeps them under `.userdata/shared/` and previews with them.

## Slot

Some plugin jobs can only have one owner at a time, such as the game list's layout. Each of these jobs is a slot, named in the plugin pages, such as `layout`, `root`, `theme` or `switcher`. When two plugins want the same slot, only one can be on.

## Quick menu

The menu of tiles you open with the **MENU** button. Some plugins add or remove its tiles.

## FN button

The device's function buttons. In NextUI's Settings you can give each one an action, and plugins add actions to that list. A plugin page names an action as `action:<id>`, such as `action:search.open`.

## Collection

A text file in `Collections/` on the card that lists games, so they show together as a folder. Favorites is a collection.

## Play tracker

What NextUI's Game Tracker tool keeps: how long and how often each game was played, in `.userdata/shared/game_logs.sqlite`. Plugins only read it.

## map.txt

A file in a folder that gives games and folders other names to show. Each line is a file name, a tab and the name to show; a name starting with a dot hides the game.

## System tag

The tag in brackets at the end of a system folder's name, such as `GB` in `Game Boy (GB)`.

## Entry set

For plugin authors: a named list of games a plugin shares with other plugins, such as `favorites` or `hidden`. A plugin that provides one says so as `sets.<name>` in its `plugin.txt`.
