# Die Gilde (Europa 1400) — Developer Documentation

This is a **developer's walkthrough of how the original game works**, reconstructed by
reverse-engineering `gilde.exe` (32-bit x86, imagebase `0x400000`, 5413 functions) with
the IDA Pro decompiler. It describes the **original binary's behavior** — the reference of
record for the 1:1 reimplementation — not the C++ port. Every claim is anchored to a
function address (`0x......`) and the symbol IDA assigned it (all prefixed `VIBE_`).

> **How to read an address.** `VIBE_Foo @0x534bbc` means: open `gilde.exe` in IDA at
> `0x534bbc`, that function is `VIBE_Foo`. Globals are written `dword_63CC38`,
> `byte_122F4A0`, etc. — fixed addresses in the `.bss`/`DGROUP` data segments.

## Reading order (entrypoint → leaves)

The docs are numbered to be read top-to-bottom. Each one starts at a root function and
descends its call tree to the leaves.

| # | Document | Root(s) | What it covers |
|---|----------|---------|----------------|
| 00 | [Architecture overview](00-architecture-overview.md) | — | The big picture: segments, subsystems, data model, conventions |
| 01 | [Entry point & WinMain](01-entrypoint-and-winmain.md) | `start @0x6041f0`, `MainEntryAndShutdown @0x534bbc` | CRT bootstrap, INI config, the app lifecycle spine |
| 02 | [CRT / runtime startup](02-crt-runtime-startup.md) | `VIBE_Runtime_Entry @0x5f8468` | C runtime init, TLS, heap, exit handling |
| 03 | [Config, INI, paths & locale](03-config-ini-paths-locale.md) | INI parse in `@0x534bbc`, `VIBE_Config_* ` | `gilde.INI`, gfx/sound settings, language strings |
| 04 | [App init & subsystem bring-up](04-app-init-subsystems.md) | `@0x527de0`, `@0x528560`, `@0x527fa4` | Mutex, movie DLL, engine/script/sound init |
| 05 | [Window & platform input](05-window-platform-input.md) | `@0x52895c`, `@0x4bea64`, `@0x40cd40` | Win32 window, message pump, DirectInput, timers |
| 06 | [Movie / intro playback](06-movie-intro.md) | `VIBE_Movie_PlayIntroSequence @0x5347d4` | `moveahead.dll`, intro video |
| 07 | [Shutdown sequence](07-shutdown-sequence.md) | the shutdown ladder in `@0x534bbc` | Ordered teardown of every subsystem |
| 08 | [Main menu](08-main-menu.md) | `VIBE_Menu_RunMainMenu @0x529d08` | The 3D main-menu loop and its sub-menus |
| 09 | [New-game flow](09-newgame-flow.md) | `@0x52ee38`, `@0x52e3d8`, `@0x52ccd8` | Choose city / history / player / character |
| 10 | [GUI: windows, widgets, forms](10-gui-widget-form.md) | `VIBE_Form_* `, `VIBE_Widget_* ` | The retained-mode UI toolkit |
| 11 | [Text & rich-string rendering](11-text-rendering.md) | `VIBE_Text_RenderRichString @0x59d6e8` | Markup, fonts, the text engine |
| 12 | [Session init & world load](12-session-init-worldload.md) | `VIBE_GameLogic_InitOrLoadSession @0x533a54` | City load, world data, scene sync |
| 13 | [Save / load & serialization](13-save-load.md) | `VIBE_Save_LoadGameFile @0x5a7604` | `.SAV` format, world-table (de)serialize |
| 14 | [Per-frame game loop](14-per-frame-loop.md) | `VIBE_GameLogic_RunFrameLoop @0x4c09a0` | The universal modal frame tick |
| 15 | [Game time, calendar & tick](15-game-time-tick.md) | `VIBE_GameTime_* `, `VIBE_GameTick_* ` | The simulation clock and round advance |
| 16 | [Characters & persons](16-characters-persons.md) | `VIBE_Character_CreateFromModel @0x402d10` | Person records, the actor factory |
| 17 | [Character actions & AI](17-character-actions-ai.md) | `VIBE_Character_Update @0x405148`, action queue | The per-actor action state machine |
| 18 | [NPC events & history](18-npc-events-history.md) | `VIBE_History_* `, NPC-event cluster | Chronicle, scripted events |
| 19 | [Command dispatcher & netcode](19-commands-netcode.md) | `VIBE_Command_Dispatcher`, `VIBE_Net_* ` | The deterministic command queue, networking |
| 20 | [Buildings, plots & city](20-buildings-city.md) | `VIBE_Building_* `, `VIBE_Bauplatz_* ` | Building types, construction, Bauplatz |
| 21 | [Economy, office & statistics](21-economy-office-stats.md) | `VIBE_Office_* `, `VIBE_Statistic_* ` | Trade, production, politics, stats |
| 22 | [Script engine](22-script-engine.md) | `VIBE_Script_RegisterCommands @0x43c850` | The `cm_*`/`ob_*` script VM and command table |
| 23 | [Render engine: universe chain](23-render-universe-chain.md) | `VIBE_Render_RenderMainViewFrame @0x5b6074` | Scene graph → draw list → present |
| 24 | [Projection & rasterizer](24-projection-rasterizer.md) | `VIBE_Coord_ProjectFramePoint`, raster fns | T&L math, the software/Vulkan raster |
| 25 | [Lighting, shadow, weather & FX](25-lighting-fx.md) | `VIBE_Light_* `, `VIBE_Particle_* ` | Day cycle, lightmaps, particles, snow/rain |
| 26 | [Mesh, asset, LOD & BGF](26-mesh-asset-lod.md) | `VIBE_Mesh_LoadOrFindByName @0x5d345c` | `.bgf` models, texture/LOD, the asset cache |
| 27 | [Animation & skeleton](27-animation-skeleton.md) | `VIBE_Anim_UpdateSkeletonPose @0x5cd1d8` | `.baf` skeletal animation, pose driver |
| 28 | [Memory management](28-memory-management.md) | `VIBE_Memory_* `, `VIBE_MemPool_* ` | The tracked heap and stack allocator |
| 29 | [VFS, file I/O & compression](29-vfs-fileio-compression.md) | `VIBE_Vfs_ReadStream @0x4514ac` | Archive mounts, streams, decompression |
| 30 | [Audio (Miles → SDL)](30-audio.md) | `VIBE_Sound_LibInit @0x445d90`, `VIBE_Sound3d_* ` | Samples, 3D sound, music, MSS32 |
| 31 | [Math, util & strings](31-math-util-strings.md) | `VIBE_Math_* `, `VIBE_Util_* ` | RNG, fixed-point, the string/CRT helpers |

## Conventions used throughout

- **Provenance first.** Every section names the original address and symbol so it can be
  re-derived in IDA. Where exact bytes matter (tables, constants), they are quoted.
- **Technology swaps.** The reimplementation replaces three vendor technologies — see each
  doc's *Platform boundary* note: 3D (DirectDraw/Direct3D → Vulkan), Win32 → SDL, audio
  (Miles/MSS32 → SDL). Everything else is reconstructed 1:1.
- **`__usercall`.** Many functions pass arguments in registers (`eax`, `ecx`, `edx`). The
  docs note the calling convention where it affects how a function is reached.
- These docs mirror the per-module status notes in [`../../progress/`](../../progress/);
  the progress files track *what is ported*, these docs explain *how the original behaves*.

## Symbol-name caveats

The `VIBE_*` symbols were assigned during reverse engineering and a few are **misnomers** —
the documentation describes what each function *actually does*. The notable ones found while
writing these docs:

| Symbol @addr | The name suggests | What it really is | Doc |
|---|---|---|---|
| `VIBE_GameTick_Finalize @0x41beb8` | a tick finalizer | the generic `.form` UI loader | 15 |
| `VIBE_DecompressState_Blob @0x423500`, `VIBE_Decompression_Finalize @0x4235dc` | decompression | DirectDraw surface **lock/unlock** | 14, 29 |
| `VIBE_Raster_RasterizeTexturedTriangleRgbz @0x5f6c30` | the textured rasterizer | the **mirror/reflection** triangle; the real textured raster is `@0x5f7d58` | 24 |
| `VIBE_ProcessSceneNodeAppend` / `VIBE_Render_SelectLodFrame` | (as named) | IDB names are `VIBE_Render_ProcessSceneNode @0x5add1c` / `VIBE_Mesh_SelectLodFrame @0x5adb6c` | 23 |

When an address and a name disagree with intuition, trust the address and the behavior the
doc describes.
