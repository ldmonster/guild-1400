# Die Gilde (Europa 1400: The Guild) — Reverse Engineering & Reimplementation

End-to-end reverse engineering of **Die Gilde** (*Europa 1400: The Guild*), the 2002
medieval life-sim by 4HEAD Studios / JoWooD, plus a from-scratch, behavior-faithful
**1:1 C++ reimplementation** of the game built from the recovered semantics.

The project has two halves:

1. **Analysis** — IDA Pro reverse engineering of every shipped binary (client, server,
   installer, resource DLL), with the findings written up as a durable Markdown report
   per binary.
2. **Reimplementation** (`reimpl/`) — a portable C++17 reconstruction translated 1:1 from
   the decompilation, with golden-vector tests and a differential test against the
   original machine code. It boots to the **real main menu** rendered from the original
   `gilde.gfx` art and runs a city session natively (Vulkan + SDL2, no Wine).

---

## Target binaries

| Binary | Arch | Role | Status |
|---|---|---|---|
| `gilde.exe` | x86 (32-bit) | Game client / main executable | Primary RE + reimpl target (~34% of 5,410 functions translated) |
| `server.dll` | x86 (Open Watcom) | Authoritative TCP multiplayer server (max 16 clients, UDP LAN discovery) | **Naming complete** — all 947 functions named |
| `gilde.dll` | x86 | **StarForce-packed**; on disk it is only the unpacking stub | Packed — game code exists only at runtime |
| `gildestp.dll` | x86 | VISE installer/uninstaller helper ("stp" = setup) | Named — no game/network code |
| `d3_resource.dll` | x86 | 3D resource DLL | — |

> **StarForce note:** `gilde.dll` is ~99% encrypted inside an RWX `.sforce` segment and is
> reconstructed in memory at runtime. Its static image contains only 3 loader functions and
> no WinSock imports, so the multiplayer code cannot be mapped from that file. The analyzable
> game logic lives in `gilde.exe` (client) and `server.dll` (server).

---

## Repository layout

```
.
├── gilde.exe / gilde.exe.i64        # client binary + IDA database
├── report/                          # client multiplayer + game-flow analysis (INDEX.md)
├── server_dll/                      # server.dll binary, IDB, and report/
├── gilde_dll/                       # gilde.dll (StarForce) binary, IDB, report/
├── gildestp_dll/                    # installer DLL binary, IDB, report/
├── 3d_resource_dll/                 # d3_resource.dll
├── reimpl/                          # the C++ 1:1 reimplementation (see below)
├── vibe_progress/                   # function-naming worklists & MCP automation scripts
└── plan                             # the analysis methodology / agent brief
```

Binaries, IDA databases (`*.i64`), DLLs, the original game assets, and build artifacts are
git-ignored (see `.gitignore`); the committed product is the **reports** and the **`reimpl/`
source tree**.

---

## Analysis reports

Each binary has a self-contained `report/` tree that is the single source of truth for that
target. Renamed functions use the `VIBE_` prefix.

- **Client (`report/`)** — start at [`report/INDEX.md`](report/INDEX.md). Covers the
  multiplayer architecture (TCP, 30 Hz synchronous tick, 16 connections), the full protocol
  spec (92 opcodes `0x03–0x5D`), the 5-phase game tick, zlib state decompression, collision
  detection, the 16.16 fixed-point coordinate system, and byte-accurate data structures.
- **Server (`server_dll/report/`)** — [`README.md`](server_dll/report/README.md):
  TCP/UDP stack, packet schema, per-slot state machine, the opcode→handler jump table
  (`0x439b60`), per-tick simulation pipeline, and the world-state data model. All 947
  functions named.
- **StarForce loader (`gilde_dll/report/`)** — the unpacker stub analysis and the evidence
  for the packing verdict.
- **Installer (`gildestp_dll/report/`)** — VISE setup/uninstall flow.

### Multiplayer architecture (server, in brief)

```
gilde.exe --Init_(ctx)--> CreateThread(VIBE_ServerThreadMain)
  read server.ini (Port=7531 / NumPlayers / GamePath)
  ServerInit -> LoadDataModel + CreateListenSocket (TCP, non-blocking, 256KB bufs)
  LOBBY: AcceptConnection + SendLanBroadcast (UDP 255.255.255.255) until NumPlayers
  GAME LOOP @30ms:
    GameTick            # inflate world state, run sim read-back, relink graph
    ServiceClientsRecv  # recv per client, run handshake/load state machine, enqueue cmds
    ServiceClientsSend  # flush outgoing queues
    DispatchGameCommands# run CommandHandlerTable[cmd]; broadcast/reply
  shutdown: NetErrorHandler / ServerShutdown
```

Client TCP packets are `[cmd][len][payload]`, accumulated and run through a per-slot state
machine, then dispatched via the command-handler jump table against the authoritative state.

---

## Reimplementation (`reimpl/`)

A portable, behavior-faithful C++17 translation of `gilde.exe`. Each function carries a
provenance comment (`// gilde.exe 0xADDR — VIBE_Name`) and is translated 1:1 from the
Hex-Rays pseudocode, with golden-vector unit tests and end-to-end flow tests.

**What works today** (see [`reimpl/PROGRESS.md`](reimpl/PROGRESS.md)):

- Compiles into one clean static library; ~34% of functions translated with verified
  provenance (~1,840 / 5,410), ~107K LOC.
- **Boots to the real main menu** rendered from the original `gilde.gfx` artwork, with the
  full menu → ChooseCity → character-create → session + Options/Credits flow — driven by a
  byte-faithful translation of the original `Menu_RunMainMenu` (`0x529d08`).
- **Native play** on a Vulkan + SDL2 window (render/present + window/input/audio), no Wine.
  A complete software-rasterizer path also exists for headless runs.
- Deterministic multi-tick simulation; byte-exact save/load.
- A **differential test** validates reconstructed functions 1:1 against the original machine
  code (see `reimpl/DIFFTEST.md`).
- Full suite: **1136/1136** passing with real assets.

### Architecture

All OS/vendor access goes through shim interfaces (`IPlatform`, `IGraphicsDevice`,
`IAudioDevice`, `INetSocket`, `IFileSystem`) so `src/` has no direct OS or third-party
library dependencies. Real backends (Vulkan, SDL2) are opt-in. Source is organized by
subsystem under `reimpl/src/`: `ai app audio compress config crt drm gui io mem net play
render sim util world` plus `shim_impl`.

### Build & test

```sh
# Portable headless build + full test suite (no third-party deps)
cmake -S reimpl -B reimpl/build
cmake --build reimpl/build
ctest --test-dir reimpl/build

# Native playable build (Vulkan + SDL2)
cmake -B reimpl/build-vk -S reimpl -DGUILD_BACKEND=ON
cmake --build reimpl/build-vk --target guild_run
./reimpl/build-vk/guild_run --play --game-dir europe_guild_1400_original

# One-shot reproduction (portable + real-asset + backends + diff test)
reimpl/scripts/reproduce.sh
```

Key reimpl docs: [`PLAN.md`](reimpl/PLAN.md), [`PROGRESS.md`](reimpl/PROGRESS.md),
[`FEASIBILITY.md`](reimpl/FEASIBILITY.md), [`CONVENTIONS.md`](reimpl/CONVENTIONS.md),
[`COVERAGE.md`](reimpl/COVERAGE.md), [`AGENT_GUIDE.md`](reimpl/AGENT_GUIDE.md),
[`DIFFTEST.md`](reimpl/DIFFTEST.md), [`BACKENDS.md`](reimpl/BACKENDS.md).

### Scope of "1:1"

A byte-identical `gilde.exe` is **not** achievable from C++ source (the original is ~2002
MSVC codegen + hand asm + self-modifying code). The achievable and pursued goal is a
**behavior-faithful** reimplementation: all functions translated, observable behavior
matches, builds into a working, playable program. The game's *content* (scripts, cutscenes,
city data, art) is **loaded, not translated** — it runs through the reconstructed engine.

---

## Methodology

The analysis follows the brief in [`plan`](plan): map all multiplayer-relevant entry points
first, trace call chains from user commands to network execution, reconstruct packet
structures and the protocol state machine, and keep every finding reconciled into the
per-domain report so it stays a single source of truth. Functions analyzed are renamed with
the `VIBE_` prefix; unresolved items are tracked and pruned as they're closed out.

---

## Legal

This is an independent reverse-engineering and reimplementation effort for interoperability,
preservation, and study. It contains **no original game code or assets** — only analysis
notes and clean-room-style C++ derived from observed behavior. You must own a legitimate copy
of *Die Gilde / Europa 1400: The Guild* to supply the game data the reimplementation loads.
All trademarks belong to their respective owners.
