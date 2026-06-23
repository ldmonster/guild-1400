# Wave-23 — Rule-7 Coverage Audit (FINAL / definitive)

**Agent:** W23-RECONCILE · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`,
imagebase 0x400000, hexrays ready)

The **definitive** re-run of the wave-21/22 BFS-from-5-roots coverage audit, taken
*after* the full wave-22/23 module landing (the wave-22 audit
[`coverage-audit-wave22.md`](coverage-audit-wave22.md) ran **concurrently** with the
wave-22 agents and therefore undercounted). Same question, same method, same
range-matching classifier.

---

## Method (identical to wave-21/22, re-run from scratch this wave)

1. **Roots (boot spine):** `0x52895c`, `0x527de0`, `0x527fa4`, `0x528560`, `0x4c09a0`.
2. **Reachable set:** BFS over real code-ref edges (`get_first/next_fcref_from` over every
   instruction, mapped to function starts) from the five roots, via IDA `py_eval`.
   Result: **2164 functions** — **identical to wave 21 and wave 22**. The boot spine is
   stable; no new indirect targets resolved.
3. **Reconstruction check (range-cite):** a function counts reconstructed iff **any**
   `0xADDR` inside its `[start, start+size)` byte range appears in a `.cpp` body
   (**13,909** distinct cited addrs across `src/**/*.cpp`). Header-only mentions do not
   count.
4. **Boundary vs logic** by name + decompile profile (DDraw/D3D/DirectSound3D → Vulkan/SDL;
   DirectInput → SDL; Miles/MSS32/`Music_`/`Sound_`/`Voice_`/`Audio_` → SDL audio;
   `Crt_`/`File_`/`Math_`/`Float_`/`Memory_`/`Time_`/`Format_`/`Mbcs_`/`Runtime_`/
   `Gzip_`/`Inflate_`/`Deflate_`/`Util_` → host CRT/zlib/MBCS runtime).

Artifacts: `/tmp/w23_reach.txt` (2164 rows), `/tmp/w23_cited.txt` (13,909 addrs),
`/tmp/w23_missing.txt` (175 still-missing, size-sorted).

---

## Headline coverage number — FINAL

> **91.91% of entry-reachable functions are reconstructed (1989 / 2164).**
> Up from **90.85% (1966/2164)** at wave 22 — a **+1.06 pt** gain (**+23 bodies**).
> Of the **175** remaining with no in-range cite, **exactly 1 is genuine game logic**
> still missing a body; the rest are intentional tech boundaries, host CRT/zlib/MBCS
> runtime, thunks, or a single already-reconstructed body lacking a cite credit.

| bucket | wave-21 | wave-22 | **wave-23** | Δ vs w22 |
|--------|------:|------:|------:|------:|
| reachable from boot spine | 2164 | 2164 | **2164** | 0 |
| reconstructed (in-range cite in src) | 1958 | 1966 | **1989** | **+23** |
| missing (no in-range cite) | 206 | 198 | **175** | **−23** |
| — **genuine game logic still missing a body** | 33 | 25 | **1** | **−24** |
| — reconstructed body, cite-credit gap only | — | — | **1** | — |
| — DDraw/D3D + DirectSound3D render boundary | 11 | 11 | **16** | +5* |
| — DirectInput input boundary | — | 5 | **5** | 0 |
| — Miles/MSS32 audio boundary | 33 | 33 | **28** | −5* |
| — MSVC-CRT / File / Math / MBCS / zlib runtime | ~120 | 121 | **122** | +1 |
| — thunks | 3 | 3 | **2** | −1 |

\* The render/audio shift is a **reclassification**, not regression: the 7
`VIBE_Sound3d_*` functions are DirectSound3D positional-audio device plumbing. Wave-22
folded them under the audio line; wave-23 groups all DirectX-device boundary (DDraw / D3D /
DSound3D) together. Totals are consistent (boundary count unchanged net).

---

## What wave 22/23 CLOSED since the wave-22 audit (24 of the wave-22 25-logic list)

The wave-22 audit's 25-function "still-missing logic" list was an undercount caused by
concurrency. Re-checked with the range-cite classifier against the now-landed modules,
**24 of the 25 are reconstructed**. Only `Privilege_BuildEvidenceEntry` remains.

| addr | bytes | name | landed in (verified in-range cite) |
|------|------:|------|-----------------------------------|
| 0x4294d4 | 1891 | VIBE_Rain_UpdateDrop | src/render/rain.cpp (W23-RAIN — cites 0x429a00…0x42a3e4) |
| 0x565b88 | 1042 | VIBE_Privilege_PanelEvidenceDetails | src/world/privilege_panels_b.cpp |
| 0x4bfc48 |  838 | VIBE_ChatConsole_BuildWindow | chatconsole module |
| 0x413220 |  719 | VIBE_Widget_LayoutBounds | widget_layout |
| 0x57b480 |  570 | VIBE_Amt_ComputeOfficeWages | office_wages |
| 0x50bf08 |  565 | VIBE_TradePanel_RefreshSellColumns | src/gui/trade_panel_windows.cpp (provenance added THIS wave) |
| 0x50b1c4 |  394 | VIBE_TradePanel_RefreshItemColumns | src/gui/trade_panel_windows.cpp (provenance added THIS wave) |
| 0x40fb4c |  330 | VIBE_Input_SetIconTextById | input_icon_text |
| 0x40eaf0 |  277 | VIBE_Gui_ResolveObjectState | gui_object_state |
| 0x40e9e8 |  262 | VIBE_State_Update | gui_object_state |
| 0x40e728 |  237 | VIBE_State_GetCurrent | gui_object_state |
| 0x4152cc |  159 | VIBE_Property_Get | property |
| 0x4c2ba0 |  158 | VIBE_Gesetz_ComputeMaxWantedLevel | wanted_level |
| 0x5437d8 |  150 | VIBE_MapView_AddCornerObjects | mapview AddCornerObjects |
| 0x4ad508 |  138 | VIBE_DragSlot_BeginDragText | dragtext |
| 0x412ea4 |  113 | VIBE_Gui_MarkObjectUsed | gui_object_state |
| 0x5929f0 |  105 | VIBE_Person_QueryByGoodType | person_query |
| 0x494d68 |   40 | VIBE_Command_QueueRequestQuad46 | command_builders2 |
| 0x495124 |   40 | VIBE_Command_QueueRequestQuad60 | command_builders2 |
| 0x495070 |   39 | VIBE_Command_QueueRequestQuad54 | command_builders2 |
| 0x4ac9c0 |   39 | VIBE_Cutscene_GetRandSeed | cutscene_rand |
| 0x4ac9a0 |   31 | VIBE_Cutscene_SetRandSeed | cutscene_rand |
| 0x5d9104 |   21 | VIBE_Resource_FlushAndFree | resource leaf |
| 0x5d8b00 |   16 | VIBE_Coord_Transform | coord_transform_leaf |

---

## THE DEFINITIVE STILL-MISSING-LOGIC LIST

### A. Genuine game logic with NO body — RECONSTRUCT (1 function / 747 B)

| addr | bytes | name | verdict |
|------|------:|------|---------|
| 0x56589c | 747 | VIBE_Privilege_BuildEvidenceEntry | **RECONSTRUCT.** Last member of the privilege evidence sub-cluster. Sibling `PanelEvidenceDetails` (0x565b88) and `EvidenceReviewAlt` (0x5667a0) both landed; this helper is referenced as a `-> BuildEvidenceEntry` callee placeholder in `src/world/privilege_panels_b.cpp` (lines 193, 550, 569) but has no actual body. Pure game logic (privilege/evidence record builder). This is the **entire** remaining game-logic reconstruction debt reachable from the boot spine. |

### B. Reconstructed body, cite-credit gap only — NO ACTION NEEDED for coverage (1)

| addr | bytes | name | verdict |
|------|------:|------|---------|
| 0x494910 | 178 | VIBE_Command_QueueRequestBuffer28 | **ALREADY RECONSTRUCTED** — faithful 1:1 body exists at `src/sim/command_builders2.cpp:128` (`QueueRequestBuffer28`, opcode-28 pending speech-buffer builder). The classifier missed it because the `.cpp` body carries no `0xADDR` cite inside `[0x494910, 0x4949c2)` — the address lives only in `command_builders2.h:88`. Behaviorally complete; this is a **provenance-cite gap**, not missing logic. (File owned by the command-builders agent — left for that owner to add the in-range `// gilde.exe 0x494910` cite to the `.cpp` body.) |

### C. Genuine tech boundaries — DO NOT reconstruct (173 functions)

Covered by the shim layer (rules 3–6) or the host runtime. Reconstructing them as game
logic would violate the rules.

| bucket | count | substitution | shim/host |
|--------|------:|--------------|-----------|
| Render — DDraw/D3D fixed-function + DirectSound3D | 16 | → Vulkan / SDL (rules 3,5) | `shim::IGraphicsDevice` / `shim::IAudioDevice` |
| Input — DirectInput devices/scancode | 5 | → SDL (rule 4) | `shim::IPlatform` |
| Audio — Miles/MSS32 + Music/Sound/Voice | 28 | → SDL audio (rule 5) | `shim::IAudioDevice` |
| CRT / File / Math / Float / Memory / Time / MBCS / zlib | 122 | host runtime | C++ runtime + `src/compress/` zlib port |
| Thunks (DirectDrawCreate/EnumerateExA, Vfs CloseHandle, Heap alloc) | 2 | import thunks | n/a |

**Render-boundary detail (16):** the 7 `VIBE_Sound3d_*` DirectSound3D listener/pool
functions (vtable plumbing), the DDraw/D3D pipeline functions
`Render_ApplyRenderStates` (0x5dda1c — pure D3D `SetRenderState`/`SetTextureStageState`
vtable dispatch with state-delta tracking), `Render_BuildSnowTexture` (0x42d220 —
procedural dither/mipmap/value-noise generation, but **DDraw-surface Lock/Unlock bound**;
the generation math belongs to the texture pipeline that targets the Vulkan shim),
`Render_CreateDynamicTexture`, `Render_EnumTextureFormats`, `TextureCache_Init`,
`Texture_FindGroupMember`, `Render_SetZEnable`, `Render_FillBackBuffer`,
`Render_CreateSurfacePalette`, and the `DirectDrawCreate`/`DirectDrawEnumerateExA`
import thunks. All sit behind the DDraw/D3D → Vulkan boundary (rule 3) and have
neighbouring functions in the same `src/render/**` / `src/play/**` files already
reconstructed — the file owners correctly deferred these device-API leaves to the shim.

---

## Reclassification carried from wave-22 (confirmed)

| addr | bytes | name | verdict |
|------|------:|------|---------|
| 0x5d4174 | 127 | VIBE_Util_StrToDouble | `strtod` impl → host CRT/Float runtime |
| 0x40c950 | 103 | VIBE_Input_FormatErrorMessage | thin `wsprintfA("...Error = %08x")` wrapper → host CRT/Format runtime (the `Input_` prefix is misleading) |

---

## Reconstructed-but-NOT-WIRED (flag for integration, rule 13)

Both wave-22 carry-over entry points have **bodies but no live external caller** — confirmed
this wave by `grep` for actual call invocations:

| addr | bytes | name | body | wiring status |
|------|------:|------|------|---------------|
| 0x4c9dec | 2154 | VIBE_NpcAction_NotifyJoinLeaveGroup | src/sim/npcaction_notify.cpp (`NpcActionNotifyJoinLeaveGroup`) | **NOT WIRED.** The documented call site in `src/sim/npcaction10.cpp:491,501` calls `H->notifyJoinLeaveGroup(tag,occupant,seat)` through a hook field (`npcaction10.h:132`), but that field is **never bound** to the real function anywhere. Note also the hook is **3-arg** while the reconstructed body is **4-arg** (`tag, occupant, seat, personId @0x4c9dec`) — wiring needs the personId binding decided by the npcaction owner. **Follow-up, not a one-line bind; left for the npcaction10/wiring owner.** |
| 0x594100 |  430 | VIBE_Building_OpenUpgradeTreeWindow | src/sim/building_upgrade_window.cpp (`Building_OpenUpgradeTreeWindow`) | **NOT WIRED.** No caller at all in `src/`. The `building.cpp:144` reference is a doc comment, not a live call. The real caller is the building upgrade-tree UI open-click; binding it touches `src/sim/building.cpp` (owned elsewhere). **Follow-up for the building owner.** |

W23-RECONCILE did not wire these (per task scope: only a one-line, behavior-neutral,
value-verified bind in an unowned file). Both require non-trivial, owner-specific
decisions (hook binding + arg shape; UI open-click site) and live in files owned by other
agents.

---

## LOGIC FLOOR — reached?

> **The logic floor is effectively reached.** Of 2164 entry-reachable functions, **1989
> (91.91%) are reconstructed**, and **exactly ONE genuine game-logic function
> (`VIBE_Privilege_BuildEvidenceEntry` @0x56589c, 747 B) still lacks a body**. Everything
> else missing is a deliberate tech boundary (Vulkan/SDL/host-runtime, 173 functions), one
> already-reconstructed body needing only a provenance-cite credit
> (`QueueRequestBuffer28`), or two reconstructed-but-unwired entry points flagged for their
> owners. Closing `BuildEvidenceEntry` reaches the absolute tech-boundary floor.

---

## Reproduce

- `/tmp/w23_reach.txt` — full 2164-row reachable set (addr·size·name) from the IDA BFS.
- `/tmp/w23_cited.txt` — every distinct `0xADDR` cited in any `src/**/*.cpp` (13,909).
- `/tmp/w23_missing.txt` — the 175 still-missing functions (range-cite), size-sorted.
- Classifier: reconstructed iff a cited addr falls in `[start, start+size)`; bucketing by
  `VIBE_`-stripped name prefix + decompile profile (render/input/audio/crt/thunk/logic).

*All addresses are gilde.exe (imagebase 0x400000). Snapshot 2026-06-16. FINAL headline =
91.91% (1989/2164). Genuine remaining game-logic debt: 1 function / 747 bytes
(VIBE_Privilege_BuildEvidenceEntry @0x56589c). The tech-boundary floor is in sight.*
