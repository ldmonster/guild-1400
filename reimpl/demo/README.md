# Demos

Standalone `main()` programs (NOT unit/e2e tests) that exercise the reconstructed
subsystems against the portable `shim_impl` backends. They are not built by the
CMake target list (CMake globs `tests/`, not `demo/`); compile them explicitly.

Run all commands **from the repo root** (`reimpl/`).

## render_demo — software rasterizer → BMP

Rasterizes a few flat triangles into a `render::Surface` bridged over a
`FileDumpGraphicsDevice` backbuffer and dumps `/tmp/guild_demo0000.bmp`.

```sh
g++ -std=c++17 -I . -I include -I src \
  demo/render_demo.cpp \
  src/render/raster.cpp src/shim_impl/filedump_graphics.cpp \
  src/shim_impl/memory_graphics.cpp \
  -o /tmp/render_demo && /tmp/render_demo
```

(If the link complains about a missing symbol, add the `src/*.cpp` it names —
the rasterizer's exact dependency set may grow.)

## world_demo — full integration E2E

A fuller end-to-end integration: builds a synthetic world (persons + a building +
a city), installs all real sim hooks (`InstallRealSimHooks` / `2` / `3`), runs
several simulation ticks driving real command enqueue + entity updates, renders a
frame and dumps `demo/world_demo_frame0000.bmp`, then saves the world via the
`io/gamestate` (save) module and reloads it asserting a byte-exact round-trip.
Finally it runs the app spine (`app::RunHeadless`) init → frames → 13-step
shutdown. Prints a concise log and exits 0 when every self-check passes.

Because it touches the app spine + sim + world + io + render, the simplest build
links the whole `src/` tree (the file globs all `src/**/*.cpp`):

```sh
g++ -std=c++17 -I . -I include -I src \
  demo/world_demo.cpp $(find src -name '*.cpp') \
  -o /tmp/world_demo && /tmp/world_demo
```

Expected: `world_demo: ALL CHECKS PASSED (0 failures)`, a 160×120 24-bit
`demo/world_demo_frame0000.bmp`, and `demo/world_demo.SAV` written + reloaded OK.

Artifacts (`demo/world_demo_frame0000.bmp`, `demo/world_demo.SAV`) are written
into `demo/` via a `DiskFileSystem` rooted there.

Note: `world_demo.cpp` defines a one-line stub for
`guild::render::TextureLoadByName` — an upstream symbol that
`src/render/mesh_load.cpp` `extern`-declares but no TU defines yet. The demo
doesn't use BGF mesh loading; the stub just lets the full-tree link succeed. Drop
it once that function is reconstructed.

## boot_demo — REAL game-asset boot

Boots the reconstruction against a REAL "Die Gilde — Europe 1400" install
directory and loads real data end to end: a `DiskFileSystem` rooted at the game
dir, the real `Gilde.INI` parsed through the reconstructed INI parser, the real
`Resources/*.BIN` PKZIP archives mounted via the VFS/`ArchiveMount` (members
openable by name), the real `gfx/gilde.gfx` (1806 gfx objects) loaded through the
gui loader, the real city `Resources/gamedata/Cities/AUGSBURG.cty` loaded through
the VFS (transparent gunzip + the save/city header loader), then a few frames of
the headless app spine and shutdown. The INI-read + VFS-bind + archive-mount front
half is the reusable `app::MountRealGameAssets` (`src/app/real_boot.{h,cpp}`).

```sh
g++ -std=c++17 -I . -I include -I src \
  demo/boot_demo.cpp $(find src -name '*.cpp') \
  -o /tmp/boot_demo && /tmp/boot_demo
```

The game directory defaults to `europe_guild_1400_original/` (override with
`argv[1]`). Expected: `boot_demo: REAL ASSET BOOT COMPLETED (exit 0, 0 non-fatal
note(s))` — config values, ~8300 archive members across 9 archives, 1806 gfx
objects, the Augsburg city header, and `spine_exit=0`.

The matching guarded e2e test is `tests/e2e/app_real_boot_e2e_test.cpp` (skips
cleanly if the real dir is absent; set `GUILD_GAME_DIR` to relocate it).
