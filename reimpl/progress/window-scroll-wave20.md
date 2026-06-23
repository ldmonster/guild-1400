# Wave-20 — W20-WINDOW: window scroll-content layout + small reachable leaves

**Agent:** W20-WINDOW · **MCP:** live (`gilde.exe`, imagebase 0x400000) · status: DONE, build green.

Scope (from the wave-20 brief): the window line-flush layout routine plus a cluster of
small entry-reachable leaves around it (shape-slot clear, resource flush, RNG seed/state
accessors, CRT/handle thunks, an empty callback stub).

---

## Reconstructed (genuine logic, 1:1)

| addr | name | bytes | home | notes |
|------|------|------:|------|-------|
| 0x41536c | VIBE_Window_LayoutScrollContent | 1646 | `src/gui/window_scroll.{h,cpp}` | **the meaty one** — line measure / alignment / justify / per-word pen advance + child-row stepping, drawn via inert render hooks |
| 0x5cb8e0 | VIBE_Util_RandSeed | 16 | `src/crt/rand.{h,cpp}` `RandSeed` | seed accessor: `ptr=GetRandStatePtr(); if(ptr)*ptr=seed; return ptr` |
| 0x5cb8b0 | VIBE_Util_GetRandStatePtr | 10 | `src/crt/rand.cpp` `RandStatePtr` | **already reconstructed** before wave 20; added the `@0x5cb8b0` provenance |
| 0x5d8f1c | VIBE_ShapeAnim_GetSlot | 29 | `src/util/leaves_wave20.{h,cpp}` `ShapeAnimClearSlot` | faithful: `memset(table+17*n, 0, 17)` (see flag below) |
| 0x5f8230 | VIBE_Util_ExitHandlerThunk | 5 | `src/util/leaves_wave20.{h,cpp}` `ExitHandlerThunk` | tail-jump to HandleTable_ClearEntry; forwards to `crt::HandleTable::ClearEntry` |

### LayoutScrollContent — what it is and how it was modelled

Despite the audit name, 0x41536c is the **line-flush stage of the window markup renderer**.
Its sole caller, `VIBE_Window_ParseMarkupAndBuild @0x416720` (the 8 KB markup builder,
`src/gui/markup_build.*`), accumulates one line's worth of "word" records into the global
parallel arrays `byte_672300 / byte_672301 / dword_672402 / byte_672400 / byte_672401`
(stride 262, count `dword_62D254`), then calls LayoutScrollContent at every line break /
alignment directive (`$L $R $C $J`-style) to:

1. **measure** the line: sum each word's pixel width (`*(i32*)(&dword_672402+j)>>16`);
2. add the widths of the **child widgets** already occupying the row;
3. compute the horizontal **start pen** from the window flags:
   - right (`flags & 0x40`): `pen = xEnd - (spacing*(n-1) + width)`
   - centre (byte+13 bit0): `pen = (xEnd-xStart-(spacing*(n-1)+width))/2 + xStart`
   - justify (low-byte sign 0x80, n>1, flushFlag==0): spread `xEnd-xStart-width`
     over `n-1` gaps via **signed** `idiv` — quotient = per-gap, remainder distributed
     one pixel at a time (`v30`);
   - else left: `pen = xStart`;
4. **draw** each word, stepping the pen past overlapping child widgets, via the bitmap-font
   helpers (`Animation_Basic/Advanced`, `Coord_Transform`, `Shape_SetMaskColor/
   DrawFromBankMode5`);
5. reset `dword_62D254 = 0` and return the window flags as they were on entry (`v35`).

Every `>> 16` is a signed arithmetic shift (`sar` in the disasm); the centre split and the
justify split are signed `idiv` (verified at 0x415506/0x415513 and the
`sar edx,31 / sub / sar 1` centre idiom at 0x4156b0). **No float / `fistp` / ConvertX is
involved** — this routine is pure integer fixed-point, so the wave-20 truncation caveat
does not apply here.

Following the established sibling pattern (`src/sim/misc_recon4_textlayout.cpp`,
`VIBE_Property_Set @0x4159dc`), the measure/alignment/justify/advance arithmetic is
reconstructed 1:1, while the actual glyph blits are routed through caller-supplied hooks in
`WinScrollEnv` (inert in the headless build — **rule 3** render boundary). The window /
child / word records are presented as plain structs (`WinScrollWindow` / `WinScrollChild` /
`WinScrollWord`) carrying exactly the fields the math reads, so the layout is exercised
without the renderer.

---

## Already reconstructed elsewhere (verified, only documented — no ODR redefinition)

| addr | name | existing home |
|------|------|---------------|
| 0x4f73ec | VIBE_Building_EmptyCallbackStub | `src/sim/building5.cpp` (`Building_EmptyCallbackStub`, no-op) |
| 0x5f8224 | VIBE_Util_NullStub | `src/sim/lasttail_recon.cpp` (bare `retn`) |
| 0x60447c | VIBE_HandleTable_ClearEntry | `src/crt/handle_recon.cpp` (`HandleTable::ClearEntry`) |
| 0x5d91d4 | VIBE_Resource_FreeEntryData | `src/render/modelio_recon.cpp` (`ResourceFreeEntryData`, hook-based) |
| 0x5fb574 | VIBE_File_FreeStream | `src/io/file_ops2.cpp` |

---

## Boundary / handoff items (rule 6 / rule 13 — documented, not faked)

- **VIBE_Util_NullThunk @0x5f821c** — *CRT per-thread TLS getter*. The disasm is
  `mov eax, ds:lpTlsValue; lea eax,[eax+0]` — it reads the per-thread CRT data block
  pointer (`__getptd`-style). It is the *uninstalled default* for the indirect table
  `off_64A90C / off_64A91C / off_64A920 …` that the real RNG-state / errno / signal
  getters are patched into. The reimpl uses a **single global** RNG/errno state
  (`crt::RandStatePtr`, see `src/crt/rand.h` PLAN §8 on TLS), so the TLS getter is fully
  bypassed. Classified as a CRT-TLS boundary; **not reconstructed** (a faithful version
  would require modelling the per-thread CRT data block, which the single-state model
  intentionally elides). Documented in `src/util/leaves_wave20.h`.

- **VIBE_Resource_FlushAndFree @0x5d9104** — `FreeEntryData(a1@eax,a2@edx)` then
  `File_FreeStream(a1)`, returning the FreeEntryData result (held in edx). Both callees
  are already reconstructed with rich, hook-based abstractions
  (`render::ResourceFreeEntryData` over `ResourceEntryView`, `io::*FreeStream`), and the
  existing CRT-FILE close path in `src/io/file_ops4.cpp` already routes through a
  `flushAndFree` hook (`DefaultFlushAndFree` + the call at line 673, tagged
  `// VIBE_Resource_FlushAndFree`). A bare 2-call thunk here would not compose cleanly with
  those abstractions and would risk an ODR/semantic clash. **Handoff:** wire a faithful
  `Resource_FlushAndFree` as a thin `ResourceFreeEntryData(ent,alsoClose); FreeStream(f)`
  wrapper inside the I/O cluster (file_ops4) that owns both abstractions, rather than in
  this GUI/util-leaf module.

- **VIBE_ShapeAnim_GetSlot @0x5d8f1c — divergence flag.** The disasm clears slot `n`
  (`memset(table+17*n, 0, 17)` via `Light_SetGrayColorThunk(0,17,ptr)`); the faithful
  version is `util::ShapeAnimClearSlot`. **However**, the symbol `ShapeAnimGetSlot` is
  *already* reconstructed at this same address in `src/render/shape_recon_cluster.cpp` as an
  **accessor that returns `table + 17*n`** (a different interpretation — it returns the slot
  pointer instead of zeroing 17 bytes). That file is owned by another cluster; **handoff:**
  its `ShapeAnimGetSlot` at 0x5d8f1c should be reconciled — the disasm shows a clear, not a
  pointer return. (`0x5d8f3c VIBE_ShapeAnim_GetSlotTable`, the *table-base* accessor, is a
  separate function and is correct as-is.)

---

## Wiring (rule 13)

- `RandSeed` / `RandStatePtr` are part of `crt::rand`, already consumed across the tree
  (`util/math_random`, etc.); seed callers in the binary are `Sound_LibInit @0x445e83`,
  `TimeBase_StartTimer @0x44e2b3`, `Scene_SyncWorldOnEnter @0x5046bb`,
  `World_LoadBuildingAndObjectData @0x583873`, `Math_RandomSeed_Thunk` — these should use
  `crt::RandSeed` where they set the generator.
- `ExitHandlerThunk` forwards to the reconstructed `crt::HandleTable::ClearEntry`.
- `Window_LayoutScrollContent`'s real caller is `VIBE_Window_ParseMarkupAndBuild @0x416720`
  (`src/gui/markup_build.*`), a bind-site file **not owned by this agent**. **Handoff:** the
  markup builder should call `gui::Window_LayoutScrollContent` at each of its 12 call sites,
  populating `WinScrollEnv` from the live globals (`dword_62D254` word array,
  `dword_62D270` spaceWidth, `dword_62D274` tracking, `dword_69FFB0` lineHeight,
  `dword_62D288` reentry guard) and the window/child records, and mirroring the
  `dword_62D254 = 0` reset on return.

---

## Tests (`tests/unit/window_scroll_test.cpp`) — 13 tests, 91 checks, 0 failures

- **WindowScroll** (7): re-entry guard early return (flags preserved, no word reset);
  left-align start + word-count reset; right-align start (224 for widths[10,20,30],
  spacing 8, xEnd 300); centre start (162); justify spacing/remainder via signed idiv;
  per-word pen advance (`+tracking+advance+space`); `'~'` word skips the gap advance.
- **Wave20Leaves** (2): `ShapeAnimClearSlot` zeroes exactly slot n's 17 bytes;
  `ExitHandlerThunk` zeroes the targeted handle-table slot.
- **UtilRng** (4): `RandSeed` sets state + returns the state pointer; `RandNext` MSVC-CRT
  LCG golden after seed 1 (`16838, 5758, 10113`); `RandSeed` == `Srand` on state;
  seed 0 first draw == 0.

Build: `cmake --build build --target window_scroll_test` — green; new module + `crt/rand`
edits compile clean into the shared `guild` lib (no ODR clashes; all new symbols are new).
