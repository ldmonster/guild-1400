# Hardening sweep — MENU-1 (main menu spine)

Agent: MENU-1. MCP live. Diffed every `gilde.exe 0xADDR` function in the owned
play-side main-menu files against the binary (decompile + disasm reference of record).

Owned files:
`native_main_menu.cpp`, `sdl_menu.cpp`, `menu_assets.cpp`,
`menu_recon_network_screens.cpp`, `menu_recon_transition.cpp`,
`choosecity_scene.cpp`, `sdl_city_screen.cpp`, `full_session.cpp`.

## Summary
- FIXED: 1 (RNG draw for the menu music pick in `native_main_menu.cpp`).
- VERIFIED-1:1: 11 reconstructed functions (transition/hotspot/fade + network screens
  pure cores) + the dispatch wiring of `Menu_RunMainMenu @0x529d08` into the sub-screens.
- BOUNDARY: SDL/Vulkan presentation bridges (rules 3–5) and the multiplayer/mission
  network sub-screens behind `ChooseNetworkMode @0x529a64` (inert in the SP spine).

## FIXED

### native_main_menu.cpp — menu music track pick (RNG draw)
- Site: `gilde.exe 0x529d08` (the random CD-track selector at **0x52a259**).
- Binary: `v22 = (int)VIBE_Util_RandNext() % 3;`
  - v22==0 -> `cd1\Rittersleut.mp3`
  - v22==1 -> `cd1\MauerUndTor.mp3`
  - v22==2 -> `cd2\KraeuterUndPhiolen.mp3`
  (gated by `dword_63C8F8` = CD-audio-enabled; `VIBE_Util_RandNext` @0x5cb8bc).
- Before: `const int pick = (int)(plat.timeMs() % 3u);`  — a wall-clock pick, NOT the
  deterministic RNG draw, and `% 3u` is unsigned vs the binary's signed `(int) % 3`.
- After: `const int pick = crt::RandNext() % 3;` (added `#include "crt/rand.h"`).
  `crt::RandNext()` IS `VIBE_Util_RandNext` (the shared 15-bit ANSI LCG, range
  [0,0x7FFF], so `% 3` ∈ {0,1,2} — signed-modulo-equivalent). This is the same RNG
  the 1:1 `gui::Menu_RunMainMenu` consumes (gui/main_menu_run.h:155), so the track
  choice is now bit-faithful and deterministic.
- The track-string table order (Rittersleut / MauerUndTor / KraeuterUndPhiolen) was
  already correct (matches the v22==0/1/2 arms exactly) — only the draw source changed.
- Syntax-verified clean (`g++ -std=c++17 -fsyntax-only -Isrc -I. -Iinclude`).

## VERIFIED-1:1

### menu_recon_transition.cpp
- `Hotspot_Register` @0x421ab8 — pre-increment count==index, mirror to dword_62D31C,
  `if (v6 > 64) return 0` (count stays incremented), 16-byte record fill. Field map
  confirmed against disasm: `a1(ax)->+0(w0)`, `a2(dx)->+2(w1)`, `a4(bx)->+4(w2)`,
  `a3(cx)->+6(w3)`, `a5->+8(payload)`, `+0xC(active)=1`. Golden (test) matches.
- `Hotspot_Remove` @0x421b18 — `if (count>=1 && index>=0) { memset 16 bytes 0; --count; }`.
- `Fade_UpdateAll` @0x41f47c — byte-step-4 sweep over dword_672280, inner while skips
  empties, outer re-tests; calls `Fade_Update(slot, dword_62D210)`. Loop modeled in
  dword units (i!=32) — equivalent to the original byte loop (i!=128 step 4).
- `Fade_UnregisterAll` @0x41f4b8 — same sweep; `Fade_Unregister(slot,arg)` then zero the
  just-processed slot. Reconstruction zeroes `base[i]` before `++i` — matches the
  original's `*(dword_672280+v3)=0` (v3 == pre-increment offset).
- `Transition_FadeOutToBlack` @0x56d2cc — `v8 = (dword_11BC2D0 | 0x100000)` with
  `BYTE1(v8) = BYTE1(dword_11BC2D0) & 0xDF`; 30/1 BLACK fade + RunFrameLoop-until-done;
  scene rect `x80=scrH, x88=0, x84=0, x8c=scrW`; `dword_11BC2D0=147591`; hud/render/
  unregister/groundplan; returns the trailing `Fade_Register(0,0,scrW,scrH,"BLACK",50,10)`.
  scrW = `(dword_69FFB8+2)>>16`, scrH = `dword_69FFBC>>16` (host-supplied; hook boundary).
- `Transition_FadeInScene` @0x56d3b0 — 30/1 fade + RunFrameLoop(147591,147591) until done;
  snapshot path (`dword_63CC30`) -> RenderEntityScene + Unregister + `dword_631638=v4`;
  else restore rect from `dword_63CC4C/50/54/58` -> `dword_69FF80/84/88/8C`, render,
  groundplan visible+fadein, hud off, unregister, trailing 50/10 BLACK fade.
  ("BLACK" = aBlack_3 @0x625284 confirmed.)

### menu_recon_network_screens.cpp (pure cores; GUI loops are hook edges)
- `Menu_ChooseHistoryVariant` @0x52df18 — 7 rows; `HistoryVariantForRow`: row0->-1,
  rows1..6 -> 0..5 (matches the v24/v26/v29/v23/v28/v14 byte_63C8F4 assignments).
  `RadioSelectionIndex`: `sel==-1 ? 0 : sel+1` (matches `if(==−1)v16=0 else +1`).
- `Menu_ShowPlayerRoundEndReport` @0x530bcc — building stride **589** (`589*type+base`)
  confirmed. `RoundEndSaleLineId`: type∈{4,16,19} -> **7198 (0x1C1E)** else **7199 (0x1C1F)**.
  `RoundEndProfitLineId`: v21>0 -> **7201 (0x1C21)**, v21<0 -> **7202 (0x1C22)**, ==0 none.
  All match the disasm.
- `Menu_RunLoadNetworkGame` @0x56a4d4 — strides confirmed: record **544** (`v8+=544`),
  scan cap **8704** (`v8>=8704`), player stride **238** (`dword_67EB80[238*..]`), window
  index **952** (`952*dword_75BF08`), slider panel `(532,360,...,130)`; chosen-name
  fat-string copy from `&meta[544*v7+25]` into byte_122F530. Header constants match.
- `CopyFatString` — 2-byte-stride NUL copy: read lo, store, break if !lo, read attr@+1,
  src+=2, store attr@dst+1, dst+=2, while attr (matches 0x529181/0x56a63e do/while).
- `Menu_EnterNetworkIp` @0x529074 — INI section "Network"/key "Host"/default "127.0.0.1"
  (a127001 @0x622bc8) confirmed; two fat-string lifts (ReturnedString, byte_122EE90);
  WritePrivateProfileString on commit; v17 return. GUI loop body is a hook edge.

### native_main_menu.cpp — dispatch wiring (Rule 13)
`Menu_RunMainMenu @0x529d08` dispatches by button-id compare; the 1:1
`gui::Menu_RunMainMenu` (gui/main_menu_run.cpp) calls one hook per arm, and
`NativeMenuHooks` overrides each, so every owned screen IS reached from the live spine:
- v80 NewGame  -> `EnterChooseCity()` -> `VIBE_Menu_EnterChooseCity @0x52ee38`
  -> RunCityScreen3D -> CharIntro/ChooseHistory/ChoosePlayer/CharCreate chain.
- v85 Load     -> `RunLoadGame()`     -> `VIBE_Menu_RunLoadGame @0x56a270`.
- v86/v87/v73  -> `RunOptionsGfx/Sfx/Game()` -> `VIBE_Menu_RunOptions* @0x56c21c/808/c44`.
- v71/v83      -> `RunCreditsWindow/Scroll()` -> `@0x529c30 / @0x56e524`.
  (Order verified to match the binary's compare chain v80,v81,v88,v79,v70,v85,v82,
   v86,v87,v73,v84,v71,v83.)
- Music: `dword_63C8F8`-gated `VIBE_Util_RandNext()%3` (FIXED above).

## BOUNDARY (rules 3–5 / out-of-tree)
- `sdl_menu.cpp`, `menu_assets.cpp`, `sdl_city_screen.cpp`, `full_session.cpp` and the
  scale/blit/grayscale helpers in `native_main_menu.cpp` are the SDL/Vulkan presentation
  layer — host edges, no per-function `gilde.exe 0xADDR` reconstruction to diff.
- `choosecity_scene.cpp` — native ChooseCity adapter (SpawnCityTower 0x52ecb7, Enumerate
  0x569530, SpawnCityMarker 0x52e2d0): faithful adapters feeding the gui/ 1:1 logic;
  "stadt_"/"dummy_" marker naming + ".CTY" enumeration are correct. Presentation/VFS edge.
- `ChooseNetworkMode @0x529a64` (v82) and `BuildChooseMissionDialog @0x59b998` (v81):
  the SP native bridge leaves these inert (default hooks return false). The network
  sub-screens reconstructed in `menu_recon_network_screens.cpp` live behind
  ChooseNetworkMode (multiplayer) and are not part of the single-player menu spine —
  a documented handoff, not a broken wire. Wiring them requires the MP launch flow,
  which is out of the SP entry-point tree.

## Build / test status
- `crt::RandNext` fix syntax-verified clean for `native_main_menu.cpp`.
- Could NOT run the test targets: `cmake --build build --target
  menu_recon_transition_test native_main_menu_itest` fails to LINK because the shared
  `guild` library fails to compile in `src/play/slice_council.h:89` / `slice_council.cpp:63`
  (`sim::CommandPacket encode()` — `sim` namespace not declared; `sim::CommandPacket`
  is defined in src/sim/command.h, not included/forward-declared there). This is a
  PRE-EXISTING defect in a file NOT in the MENU-1 chunk (slice_council is another
  agent's chunk), unrelated to the RNG fix. Per the brief I edited only my chunk files
  and did not touch it. Once that unrelated compile error is resolved, the relevant
  suites (menu_recon_transition_test, native_main_menu_itest, gui_main_menu_run_test)
  exercise everything verified here.
