# Launcher plugin template

A starting point for a [mosaic-launcher](https://github.com/josegonzalez/mosaic-launcher) plugin. mosaic-launcher is an experiment and isn't released or supported.

The API is in `sdk/mosaic_plugin.h`, and [PLUGINS.md](https://github.com/josegonzalez/mosaic-launcher/blob/main/PLUGINS.md) explains it. The `makefile` lists the build targets.

## The helper

The example plugin starts a helper program, `example-helper`, built from `helper/` beside `plugin.so` in each `lib/<arch>/` and packed into the pak with it. It answers a ping on a socket, as an example of work that runs outside the launcher's process. PLUGINS.md's "Helper programs" section has the rules a helper follows; `src/helper.c` follows them. The helper is named after `id=` in `plugin.txt`, as `<id>-helper`, and logs to `.userdata/<platform>/logs/<id>.txt`.

To go without it, delete `helper/`, `src/helper.c` and `src/helper.h`, then the `startHelper` and `quit` functions and the lines marked `// helper` in `src/plugin.c`. With only `helper/` deleted, the plugin builds and loads without a helper and logs that there's none to start.

## License

`sdk/` is MIT licensed. Your plugin can use any license.
