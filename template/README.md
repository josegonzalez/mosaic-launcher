# Launcher plugin template

A starting point for a [mosaic-launcher](https://github.com/josegonzalez/mosaic-launcher) plugin. mosaic-launcher is an experiment and isn't released or supported.

The API is in `sdk/mosaic_plugin.h`, and [PLUGINS.md](https://github.com/josegonzalez/mosaic-launcher/blob/main/PLUGINS.md) explains it. The `makefile` lists the build targets.

## The helper

The example plugin starts a helper program, `example-helper`, built from `helper/` beside `plugin.so` in each `lib/<arch>/` and packed into the pak with it. It answers a ping on a socket, as an example of work that runs outside the launcher's process. PLUGINS.md's "Helper programs" section has the rules a helper follows; `src/helper.c` follows them. The helper is named after `id=` in `plugin.txt`, as `<id>-helper`, and logs to `.userdata/<platform>/logs/<id>.txt`.

To go without it, delete `helper/`, `src/helper.c` and `src/helper.h`, then the `startHelper` and `quit` functions and the lines marked `// helper` in `src/plugin.c`. With only `helper/` deleted, the plugin builds and loads without a helper and logs that there's none to start.

## Translations

The plugin's text is translated from its own language packs, `res/lang/<code>.po`, which `make pak` puts in the pak; `res/lang/es.po` is an example Spanish one, not reviewed by a native speaker. They're made from `po/example.pot`, the gettext template of the plugin's name, setting labels and options from `plugin.txt`, its action's label and the text `src/plugin.c` passes to its `tr()` helper, keyed on the English with no `msgctxt` for the plugin's id. `po/` stays out of the pak. In mosaic-launcher's repo, `make pot` writes `po/example.pot`. PLUGINS.md's "Translations" section has the layout, and how a user overrides a plugin's strings with `Languages/<id>/<code>.po` on the card.

## License

`sdk/` is MIT licensed. Your plugin can use any license.
