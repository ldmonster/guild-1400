# 00 — Architecture Overview

*Europa 1400: The Guild* (`Die Gilde`) is a single-binary Win32 game: a real-time city/
life simulation with a 3D presentation layer, a deterministic command system that also
drives multiplayer, and a large data-driven content layer (cities, buildings, scripts,
text, audio banks). This document gives the map; the numbered docs walk the tree.

## 1. The binary at a glance

| Property | Value |
|---|---|
| Module | `gilde.exe`, PE32 (x86) |
| Image base | `0x400000`, image size `0x1088000` |
| Functions | 5413 (all named with a `VIBE_*` reverse-engineering symbol) |
| Strings | 5745 |
| Entry point | `start @0x6041f0` (MSVC CRT stub) |
| Real `WinMain` | `VIBE_GameLogic_MainEntryAndShutdown @0x534bbc` |

### Segments

| Segment | Range | Purpose |
|---|---|---|
| `AUTO` (`.text`) | `0x401000`–`0x60e000` | Code (and read-only data interleaved) |
| `.idata` | `0x60e000`–`0x610000` | Import address table |
| `DGROUP` | `0x610000`–`0x64f000` | Initialized data / globals |
| `.bss` | `0x64f000`–`0x140b000` | Zero-init globals (~14 MB — the world/entity tables live here) |
| `.cms_t` / `.cms_d` | `0x140b000`–`0x1467000` | A secondary code/data block (copy-protection / overlay region) |

The very large `.bss` is where the simulation state lives: person/entity arrays, the
universe scene slots, building tables, the command queue, GUI form storage, etc. Almost
every global referenced in these docs (`dword_63CC38`, `byte_122F4A0`, …) is in this range.

## 2. The third-party technologies (what gets swapped)

The imports reveal the original's platform dependencies. Per the project rules, three are
replaced; the rest are reconstructed 1:1.

| Vendor tech | Imports | Reimplementation |
|---|---|---|
| **DirectDraw / Direct3D** | `ddraw.DirectDrawCreate`, `d3_d3d.c` (most-referenced string, 128 xrefs) | **Vulkan** (rule 3) — the T&L/raster math is reconstructed 1:1, only the GPU API is swapped |
| **DirectInput** | `dinput.DirectInputCreateA` | **SDL** input (rule 4) |
| **Win32** window/timer | `user32`/`kernel32`/`winmm` (`CreateWindowExA`, `timeSetEvent`, …) | **SDL** window + message pump (rule 4) |
| **Miles Sound System** | `mss32._AIL_*` (≈70 imports) | **SDL** audio (rule 5) |
| GDI | `gdi32.BitBlt`/`TextOutA`/`CreateDIBSection` | Used for the windowed-mode blit and debug text |
| **wsock32** | `socket`/`connect`/`send`/`recv`/… | Reconstructed 1:1 (rule 6 decision: keep networking) |
| Registry | `advapi32.Reg*` | Settings persistence |

Everything else — the simulation, file formats, compression, the script VM, the projection
math — is original engine code reconstructed faithfully.

## 3. The lifecycle spine

The whole program is a short, legible spine. Read [01](01-entrypoint-and-winmain.md) for
the detail; in brief:

```
start @0x6041f0                              ; MSVC CRT stub
└─ VIBE_GameLogic_MainEntryAndShutdown @0x534bbc   ; "WinMain"
   ├─ read gilde.INI  (paths, gfx, sound, language, network)   → doc 03
   ├─ VIBE_App_CreateSingleInstanceMutex @0x527d48             → doc 04
   ├─ VIBE_Window_CreateMainWindow @0x52895c                   → doc 05
   ├─ VIBE_App_InitSubsystemsAndMovieDll @0x527de0            → doc 04
   ├─ VIBE_Movie_PlayIntroSequence @0x5347d4   (if show_intro) → doc 06
   ├─ VIBE_Render_InitDisplayAndPaths @0x527fa4               → doc 04/23
   ├─ VIBE_App_InitEngineAndScriptCommands @0x528560          → doc 04/22/30
   │
   └─ main loop:
      ├─ VIBE_Menu_RunMainMenu @0x529d08                       → doc 08
      ├─ VIBE_GameLogic_InitOrLoadSession @0x533a54            → doc 12
      └─ (inside those) VIBE_GameLogic_RunFrameLoop @0x4c09a0  → doc 14
```

## 4. The per-frame loop is the hub

`VIBE_GameLogic_RunFrameLoop @0x4c09a0` is the most important function to understand: it has
**342 callers** and **88 callees**. Every modal state in the game (menus, the city view,
combat, court trials, cutscenes, dialogs, trade panels, tavern card games…) is implemented
as a loop that repeatedly calls `RunFrameLoop` until its exit condition is met. A single
invocation = one rendered, simulated frame:

```
pump input  →  process command queue (net-deterministic)  →  step scripts
            →  update characters/AI  →  update weather/sound/music
            →  cull + render the 3D scene  →  draw HUD/overlays  →  present
```

See [14 — Per-frame game loop](14-per-frame-loop.md). The simulation advances in discrete
**rounds** driven by the game clock (doc 15); rendering and input run every frame.

## 5. The data model (where state lives)

- **Persons / entities** — fixed-capacity arrays in `.bss`, indexed by id.
  `VIBE_Person_FindRecordById @0x58bc6c` (678 xrefs) and `VIBE_GameObject_QueryFind @0x5857fc`
  are the universal lookups. Each record is a flat C struct (`alive` flag at +0x00, id, …).
- **The universe / scene graph** — up to several active "slots"
  (`VIBE_Universe_SwitchActiveSlot @0x5b4a24`); a slot holds the terrain, the object scene
  tree, lightmaps, and the camera.
- **Buildings** — a type table (`VIBE_Building_LookupTypeRecordA @0x589778`) plus per-instance
  records; plots (`Bauplatz`) map building footprints onto the terrain grid.
- **The command queue** — every state-changing action is a `cm_*` command packet that is
  queued, optionally sent over the network, then executed deterministically (doc 19). This
  is what keeps multiplayer in sync.
- **GUI forms** — a retained-mode toolkit: `Form` → `Window` → `Object`/`Widget` tree, with
  `VIBE_Form_SelectWindow @0x41e4cc` (923 xrefs) selecting the active window.

## 6. Content & file formats

The game is heavily data-driven. Paths come from `gilde.INI` (`\project\gfx\`, `\project\game\`,
`\project\movie\`). Key formats (covered in docs 22/26/27/29):

- `.cty` — city definition (`gamedata/cities/<name>.cty`)
- `.SAV` — save game (`Gamedata\Saves\…`)
- `.bgf` — 3D model/mesh; `.baf` — skeletal animation
- `.ed3` — 3D scene (`scenes/*ChooseCity.ed3`, …)
- `.scr` / `.def` — script and text-engine definition files
- `.sbf` — sample bank; `include_sfx.ini` — sound manifest
- `Objects.BIN` / `Textures.BIN` — packed asset archives

## 7. Naming conventions in these docs

- Functions: `VIBE_<Subsystem>_<Verb>` — the IDA symbol. The `VIBE_` prefix is an artifact
  of the reverse-engineering pass, not original; the subsystem tag is meaningful.
- The original German content strings are quoted as-is (`Stadt`, `Beruf`, `Bauplatz`,
  `Personalbuch`, …) — they double as reliable landmarks when reading the binary.
- `__usercall` / register args are noted where they affect call-site reconstruction.
