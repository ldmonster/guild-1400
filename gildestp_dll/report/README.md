# gildestp.dll — Reverse Engineering Report

Game: **Die Gilde** (English: *The Guild* / *Europa 1400: The Guild*), a German
medieval life-sim by 4HEAD Studios / JoWooD (2002).

Module: `gildestp.dll` — the **"stp" = Setup** helper DLL. It is a companion
DLL for a **VISE** installer (MindVision Software Installer Engine VISE; the
`ViseEntry` export is the standard VISE custom-action callback). It implements
install-completion launch prompt, desktop shortcut creation, a single-instance
guard, and the game's uninstaller / failed-install rollback.

## TL;DR — multiplayer scope

**There is no networking code in this binary.** It is an installer/uninstaller
helper. Evidence:

- Imports: only KERNEL32, USER32, SHELL32, ole32 — **no WS2_32 / WSOCK32 /
  WININET / sockets / send / recv / connect**. (`survey_binary` network
  category is empty.)
- No crypto, compression, serialization, or packet code.
- The only token even resembling "network" is a **directory name**,
  `Resources\gamedata\network`, which the uninstaller *deletes* — it is game
  content installed on disk, not code in this DLL.

The actual multiplayer/network implementation of Die Gilde lives in **other
binaries** (the game references a `Server` folder and `gamedata\network`
content that this installer manages but does not contain). Packet structures,
protocol state machines, handshake/auth/keepalive, etc., **cannot be
reconstructed from this file** — none of that exists here. If multiplayer
mapping is the goal, the target binary is the game's main executable / its
network DLLs, not `gildestp.dll`.

## Binary facts

| | |
|---|---|
| Arch | x86 (32-bit) PE DLL |
| Image base | 0x10000000, size 0xF000 |
| MD5 | 5ff354b748081f871c584b61ad357001 |
| SHA256 | 637c5dd4ecbcecb8b59468083c53ed9c789af62effa83a3adfc5b602e016c550 |
| Total functions | 164 (all now named) |
| Custom (game-setup) functions | 6 |
| MSVC CRT / runtime functions | 158 |
| Exports | AskGildeStart(#1), CreateShellLink(#2), ViseEntry(#3) |
| CRT | static MSVC 6.x runtime (small-block-heap variants present) |

## Naming convention

- Custom Die-Gilde setup functions reverse-engineered here are prefixed
  **`VIBE_`**.
- The 158 MSVC runtime functions were given their **canonical CRT names**
  (e.g. `free`, `calloc`, `__heap_select`, `__sbh_alloc_block_v2`) so they
  integrate with the FLIRT-identified CRT already in the IDB. They are library
  code, not part of the game logic, and are listed only briefly below.

## Document map

- [setup/flow.md](setup/flow.md) — entry points, full call flow, exports.
- [setup/uninstaller.md](setup/uninstaller.md) — uninstall/rollback details.
- [runtime/crt.md](runtime/crt.md) — CRT internals that were renamed.

## Open questions / next targets

- None **within this binary** — every function is named and the architecture is
  fully understood (installer helper, no network).
- To pursue the multiplayer goal, obtain and load the game's **main executable**
  and any dedicated **Server / network DLLs**; this report's networking
  sections would then move there.
