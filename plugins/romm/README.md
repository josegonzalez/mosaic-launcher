# romm

Browses a [RomM](https://github.com/rommapp/romm) server's library from the launcher, downloads games when you launch them, and backs up your saves to the server. It was tested against RomM 5.4.1.

## Turn it on

Turn on **RomM** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)), then set the Server setting. Until then, without a server, it adds nothing. The Server, User, Password, certificate and CA settings are protected by the launcher's PIN, and the password is write-only and never logged.

## Browsing and downloading

- A RomM folder on the first screen has a folder for each of the server's platforms with roms that the launcher has a system for, listing their roms. Platforms map to systems by RomM's slug (see the table below); other platforms are left out.
- The lists are kept in `.userdata/<platform>/launcher/romm/`, so the folders open at once, and fetched again in the background when a folder opens and its list is more than a minute old.
- A rom not on the card yet launches by downloading first, into `Roms/<system>/` (the card's folder whose name ends in the system's tag, or the name stock gives it). A download screen shows a progress bar, with **B** to cancel, and the status pill shows it too. The rom is written to a hidden temporary file in that folder, checked against the size the server gave, and renamed into place. Then the game launches.
- A rom of more than one file, which RomM serves as a zip, is unpacked into `Roms/<system>/<game>/`. A `<game>.m3u` lists its discs (the `.cue` files when it has them, as RomM picks them) in place of RomM's own when it has two or more, so NextUI shows the folder as one game. It's listed at its `.m3u` (or its one disc) and launches there, and its saves and states go by that name as minarch's do. A zip of one file lands as that file.
- Nothing goes over the network while Wi-Fi is off or not connected; it says so instead.

### Platform and system names

| RomM slug | System tag |
|---|---|
| `gb`, `gbc`, `gba` | GB, GBC, GBA |
| `nes` and `famicom`, `fds` | FC, FDS |
| `snes` and `sfam` | SFC |
| `genesis` | MD |
| `sms`, `gamegear`, `sg1000` | SMS, GG, SG1000 |
| `segacd`, `sega32` | SEGACD, 32X |
| `psx` | PS |
| `tg16` | PCE |
| `neo-geo-pocket`, `neo-geo-pocket-color` | NGP, NGPC |
| `virtualboy`, `pokemon-mini`, `lynx` | VB, PKM, LYNX |
| `atari2600`, `atari5200`, `atari7800` | A2600, A5200, A7800 |
| `colecovision` | COLECO |
| `c64`, `c128`, `vic-20`, `cpet`, `c-plus-4` | C64, C128, VIC, PET, PLUS4 |
| `acpc`, `amiga` | CPC, PUAE |
| `msx` and `msx2` | MSX |
| `pico`, `arcade`, `doom` | P8, FBN, PRBOOM |

## Saves and Flashback

When a game from RomM comes back, its save in `Saves/<tag>/` (whichever of minarch's save names is newest) is uploaded if its MD5 differs from the last one synced (the Upload saves after playing setting). It goes into a slot of its own on the server that keeps the last Saves kept on the server uploads. One that can't go up is tried again the next time the launcher starts.

Press **Y** on a game from RomM (the Flashback button setting picks another, shared with other plugins' actions there) to open Flashback: the game's saves and save states on the server, newest first, with their times and sizes. A game launched from its folder in `Roms` has Flashback there too.

| Button | What it does |
|---|---|
| **A** | Restores the selected one after a confirm, first copying the file on the card to `.userdata/<platform>/launcher/romm/backups/<tag>/` |
| **X** | Fetches the list again |
| **B** | Goes back |

With [`save-state-manager`](../save-state-manager/README.md) on, a state Flashback restored shows a SYNCED mark there until it changes, and a game from RomM gets a Restore from server cell after its states, which opens Flashback. Each finished download counts towards [`gamification`](../gamification/README.md)'s RomM quest and shows on [`home-dashboard`](../home-dashboard/README.md)'s Stats tab under Recently downloaded.

A state in any of the layouts minarch writes (its own `.st0` to `.st9`, or RetroArch's `.state`, `.state1` to `.state9`, `.state.0` to `.state.9` and `.state.auto`) restores under the name minarch's State format setting gives it. It goes into `.userdata/shared/<tag>-<core>/` for the core the system's emulator pak runs, else the only state folder the system has, else the one named for the emulator the server recorded. A state in another layout, or with no way to tell its folder, can't be restored.

## Logging in and security

- The User and Password settings log in with a RomM user (HTTP Basic). With User empty, the password holds a RomM API token.
- `http://` sends the login in clear text, so use it only on a network you trust.
- `https://` uses BearSSL (TLS 1.2). It checks the server's certificate against the device's CA certificates (NextUI's bundle on h700, else the system's at `/etc/ssl/certs/ca-certificates.crt` and the usual places) or the CA certificates file setting's, which needs the server's name, not an address. For a self-signed home server, set the certificate fingerprint to its certificate's SHA-256 fingerprint (`openssl x509 -noout -fingerprint -sha256 -in cert.pem`), which is then the only certificate trusted. Without either, `https://` is refused rather than left unchecked.
- Certificates are checked against the device's clock.
- Requests time out after 8 seconds connecting and 20 seconds without data, redirects are followed only on the same server (up to 3), and downloads, lists and saves are capped in size.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Server (`url`) | text | empty | The RomM server, such as `http://192.168.1.20:8080` or `https://romm.home`. `http://` sends the login in clear text |
| User (`user`) | text | empty | RomM user name. Leave it empty to log in with an API token instead |
| Password or API token (`password`) | text | empty | The user's password, or a RomM API token when User is empty. Write-only |
| Certificate fingerprint, SHA-256 (`pin`) | text | empty | `https://` only: the server certificate's SHA-256 fingerprint, for a self-signed home server |
| CA certificates file (`ca_file`) | text | empty | `https://` only: a PEM file of CA certificates to trust in place of the device's own |
| Upload saves after playing (`sync`) | on, off | on | Uploads a game's save after it returns, when it changed |
| Saves kept on the server (`keep`) | 5, 10, 20, 50 | 10 | How many of a game's uploaded saves the server keeps |
| Flashback button (`button`) | none, Y, L2, R2, SELECT | Y | The game list button that opens the selected game's Flashback |

`url`, `user`, `password`, `pin` and `ca_file` are protected.

## For plugin authors

- Another plugin can open a game's Flashback with `run_action("romm.flashback", <the game's full path>)`. The `button` setting follows the `romm.flashback` action.
- Provides the `romm.synced` and `romm.downloads` services and publishes `romm.download_done` (`provides=romm.synced,romm.downloads,events.romm.download_done`); see [plugin-api.md](../../docs/plugin-api.md#features-added-since-the-first-api). They answer from the files the plugin keeps, never the network, and say only whether a server is set, never its address, user or password:
  - `romm.synced`: with no arguments, `server=1` while a server is set; with `path=<a file>`, also `synced=1` when it's the file last uploaded or restored, its size and modification time unchanged since (with `how=upload` or `download` and `at=<Unix time>`), and `game=1` when the path is a game from RomM that Flashback opens for. `save-state-manager` shows its SYNCED mark and Restore from server cell from it, and `gamification` offers its RomM quest while `server=1`.
  - `romm.downloads`: optionally `count=<n>` (5 by default, up to 20) gives the games last downloaded that are still on the card, newest first, as `count=<n>` and `path.<i>=`, `name.<i>=` (RomM's name) and `at.<i>=<Unix time>` from 0. `home-dashboard`'s Stats tab lists them.
  - `romm.download_done`, with `path=<the file it launches>` and `name=<RomM's name>`, as each download finishes, before the game launches.
- Settings with the `protected` flag ask for the launcher's PIN; `password` is also `secret` (see [plugin-api.md](../../docs/plugin-api.md#plugintxt)).
- Downloads a zip to a hidden temporary file, then checks every entry before anything is written. It refuses the whole zip for:
  - an absolute path, `..`, a backslash, a NUL or another character the card can't hold
  - two names that differ only in case
  - a link, or anything but a plain file or folder
  - an encrypted entry
  - sizes that don't add up: over 2048 entries, a file over 4 GB, 16 GB in all, more than the size RomM gave plus 64 KB, or an entry claiming to inflate past 1032 to 1
- Files are written to temporary names in a hidden folder, never through a link, and the folder is moved into place only once every file is in. A cancel or error removes only what it made, and a folder or file of the game's name already on the card is never written over.
- Writes `.userdata/<platform>/launcher/romm/` (lists, `games.txt` of the games from RomM, `synced.txt` of the files last uploaded or restored, and `downloads.txt` of the last 20 downloads) and `.userdata/<platform>/launcher/romm/backups/<tag>/`, and reads and writes `Saves/<tag>/` and `.userdata/shared/<tag>-<core>/`.

## Credits

LodorOS.
