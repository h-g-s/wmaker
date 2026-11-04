# Repository guide

## Build and checks

This is an Autotools project. In a checkout without a generated `configure`
script, run `./autogen.sh` first. Configure and build with:

```sh
./configure
make
```

Run the repository checks after building:

```sh
make check
```

`make check` includes command-line/man-page consistency checks, checks that
configuration-default callbacks match their field types, and the focused
RandR and `wmsetbg` tests. The tests can also be built and run individually:

```sh
make -C src randr_hotplug_test
(cd src && ./randr_hotplug_test)

make -C util wmsetbg_refresh_test
(cd util && ./wmsetbg_refresh_test)
```

`wmsetbg_refresh_test` requires `DISPLAY` to point to an isolated X server
because it tests root-window backgrounds. The RandR test reports a skip (exit
status 77) when RandR support is not compiled in. Build prerequisites and
configuration options are documented in `INSTALL-WMAKER`.

## Architecture

The project is being modernized while preserving what makes Window Maker
distinctive. Prioritize stability, robustness, performance, and improvements to
the overall desktop experience without casually discarding established
behavior.

- The top-level `Makefile.am` defines the recursive build order. `src/` builds
  `wmaker`, the X11 window manager; `WPrefs.app/` builds its preferences UI;
  and `util/` contains command-line utilities.
- `src/main.c` initializes the process and hands off to the real window-manager
  process. `src/event.c` is the central X event loop; client/window management,
  workspaces, stacking, menus, and X protocol support are implemented in
  neighboring `src/` modules.
- `wrlib/` provides image loading and raster operations. `WINGs/` builds
  `libWUtil` (utility, property-list, and user-defaults code) and `libWINGs`
  (the widget toolkit); the manager and applications link these shared
  libraries.
- User preferences are stored as GNUstep property lists. `WINGs` provides the
  property-list and user-defaults machinery, while `src/defaults.c` maps
  Window Maker settings to fields and applies them through conversion and
  update callbacks. `WPrefs.app/` edits the same preferences.

## Repository-specific conventions

- Edit Autotools inputs (`Makefile.am`, `configure.ac`, and `*.am` files), not
  generated `Makefile.in`, `configure`, or configured `Makefile` files.
- When changing a Window Maker default, keep the option table, its conversion
  and update callbacks, and the corresponding preference field in
  `src/WindowMaker.h` consistent. `make check` runs the static and dynamic
  callback/type checks from `src/Makefile.am`.
- Focused C tests are registered from their component `Makefile.am` files.
  Existing tests include the implementation `.c` file directly when they need
  to exercise its internal/static functions; follow the nearby test pattern
  when adding a similarly scoped test.
- User-visible strings use gettext (`_()`); translations and their checks live
  alongside each component. To refresh translation sources for a language,
  use `make update-lang PO=<language>` as documented in the top-level
  `Makefile.am`.
