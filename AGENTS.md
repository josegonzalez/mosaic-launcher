# AGENTS.md

Rules for agents working on mosaic-launcher.

## Read first

- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for building, tests and the launcher's modules, and [PLUGINS.md](PLUGINS.md) for the plugin API. Read the parts that touch your change before changing code.
- README.md is only a short summary. Keep it that way, and put build, test and internals notes in docs/DEVELOPMENT.md.
- The launcher isn't released publicly: no Pak Store, no `pak.json`, no GitHub releases.
- The project was renamed from nextui-launcher. Older issues use the old names: `NUI_X` is now `MOSAIC_X`, `NUIType` is `MosaicType`, `nextui_plugin_get` is `mosaic_plugin_get`, and `NEXTUI_LAUNCHER_X` environment variables are `MOSAIC_LAUNCHER_X`.

## Launcher

- Stock parity: with no plugins on, the launcher must behave exactly like the pinned NextUI release.

## Plugins and the SDK

- Plugins include only `sdk/mosaic_plugin.h`, plus libc. They never link launcher or NextUI code.
- Plugins in `plugins/<name>/` and new SDK code are written fresh (MIT-style, no NextUI code). Don't port fork code; reimplement it from the described behavior.
- Keep plugin model logic (filters, sort, pick, parse) in its own SDL-free file with host unit tests under `tests/c/`. See `tests/c/test_favorites.c` for how a plugin is tested.
- Each new plugin gets a `plugin.txt` (with settings, `slots`, `requires` and `conflicts` as needed), a row in PLUGINS.md's "Bundled plugins" table crediting the forks the idea came from, and its tests.
- SDK changes:
  - fields are only ever added at the end of `MosaicHost` and `MosaicPlugin`;
  - `MOSAIC_API_VERSION` stays 1;
  - new host fields are documented under "Features added since the first API" in PLUGINS.md;
  - `template/sdk/mosaic_plugin.h` must stay an exact copy of `sdk/mosaic_plugin.h` (`make lint` checks it).

## Tests

- Quick, while working: `make lint` and `make -C tests/c`, plus any single test you touched.
- Full, before opening a PR and after rebasing onto a changed main, in this order: `make lint`, `make test`, `make plugins-desktop`, `make test-plugins`, `make smoke`, `make test-profiles`.
- Device builds: run `make devices` (Docker) only when you changed launcher or plugin C code that might not build for aarch64, and say whether you did.
- When main moves, rerun the full tier only if the new commits touched files your change depends on.
- `make test-plugins`, `make smoke`, `make test-profiles` and `tests/plugins/run.sh` all use the one shared fake card at `/var/tmp/nextui/sdcard`, so never run two of them at once.
- There are no device tests. Don't ssh to a device.

## Git and PRs

- Branches are named `feat/<issue>-...`, `fix/<issue>-...`, `test/<issue>-...` or `docs/<issue>-...`.
- Rebase onto origin/main, never merge main into a branch. Main is merged by fast-forwarding it to the branch, so a branch must be rebased onto the latest origin/main before it can merge.
- Force-push only feature branches, with `git push --force-with-lease`. Never force-push main.
- Never amend. To change a PR after review, add a commit on top.
- Commits are semantic, with no scope and at most 60 characters, and optionally one terse paragraph body. No bullets, headers, manual line breaks, test plans or attribution lines.
- PRs: for a single commit, the title is the commit subject and the body is the commit body. Bodies are plain paragraphs, with "Fixes #<issue>" and "Part of #<parent>" when there is one.
- Never commit `tmp/`.

## Writing style

Docs, comments, commits and PRs use sentence case, never em dashes (use spaced hyphens), and no embellishment. Match the surrounding code's comment density and naming.
