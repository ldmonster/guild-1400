# Wave-22 — Rule-7 Coverage Audit Refresh (post wave 22)

**Agent:** W22-COVERAGE · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000, hexrays ready)

Analysis-only refresh of [`coverage-audit-wave21.md`](coverage-audit-wave21.md). Same
question, same method as wave 21: *of the functions reachable from the binary's real
boot spine, which still have no reconstructed body in `src/`?* — re-measured after wave 22,
using the **range-matching** classifier (a function counts reconstructed iff **any** cited
`0xADDR` falls inside its `[start, end)` byte range, since modules cite internal instruction
addresses, not just the function-start).

---

## Method (identical to wave-21, re-run)

1. **Roots (boot spine), unchanged:** `0x52895c`, `0x527de0`, `0x527fa4`, `0x528560`,
   `0x4c09a0`.
2. **Reachable set:** BFS over real code-ref edges (`get_first/next_fcref_from` over every
   instruction, mapped to function starts) from the five roots, via IDA `py_eval`.
   Result: **2164 functions** (1,024,041 bytes) — **identical to wave 21** (no new indirect
   targets resolved this wave; the boot spine is stable).
3. **Reconstruction check:** range-cite. An address counts reconstructed iff a `0xADDR`
   inside its byte range appears in a `.cpp` body (13,932 distinct cited addrs across
   `src/**/*.cpp`). Header-only mentions do not count.
4. **Boundary vs logic** by name + decompile profile (DDraw/D3D/DInput → Vulkan/SDL;
   Miles/MSS32/`Music_`/`Sound_`/`Voice_`/`Audio_` → SDL audio;
   `Crt_`/`File_`/`Math_`/`Float_`/`Memory_`/`Time_`/`Format_`/`Mbcs_`/`Runtime_`/
   `Gzip_`/`Inflate_`/`Deflate_` → host CRT/zlib/MBCS runtime).

---

## Headline coverage number

> **≈90.85% of entry-reachable functions are reconstructed (1966 / 2164).**
> Up from **90.5% (1958/2164)** at wave 21 — a **+0.35 pt** gain (+8 bodies).
> Of the **198** remaining with no reconstructed body, only **25 are genuine game logic**
> (down from 33); the rest are intentional tech boundaries or host CRT/zlib/MBCS runtime.

| bucket | wave-21 | wave-22 | delta |
|--------|------:|------:|------:|
| reachable from boot spine | 2164 | 2164 | 0 |
| reconstructed (body in src) | 1958 | **1966** | **+8** |
| missing (no body) | 206 | **198** | **−8** |
| — **pure game logic** | **33** | **25** | **−8** |
| — DDraw/D3D/DInput render boundary | 11 | 11 | 0 |
| — DInput input boundary | (in render) | 5 | — |
| — Miles/MSS32 audio boundary | 33 | 33 | 0 |
| — MSVC-CRT / File / Math / MBCS / zlib runtime | ~120 | 121 | +1 |
| — thunks | 3 | 3 | 0 |

The headline gain is smaller than wave-21's because wave 22 deliberately spent its budget
on the **logic tail** (the genuinely-missing `.cpp` bodies) rather than on bulk CRT/zlib
provenance. Every closure this wave was a real logic function; the boundary buckets are
unchanged.

---

## What wave 22 CLOSED (9 of the wave-21 33-logic list) — sorted by size

The biggest single block of wave-21 debt — the morph-anim builder, save-load decompress
pair, snow particle physics, and the AI bank sub-planner — all landed this wave.

| addr | bytes | name | where (verified citation) |
|------|------:|------|---------------------------|
| 0x5cf150 | 3026 | VIBE_Anim_CreateMorphAnim | src/render/anim_morph.cpp |
| 0x41ceb4 | 1714 | VIBE_DecompressGameState | src/sim/entity_frame_update.cpp |
| 0x42a644 | 1329 | VIBE_Snow_UpdateFlake | src/render/snow_update.cpp |
| 0x459264 | 1150 | VIBE_Ai_CalcBankmeister | src/sim/ai_meister_bank.cpp |
| 0x5667a0 | 1017 | VIBE_Privilege_PanelEvidenceReviewAlt | src/world/privilege_panels_b.cpp |
| 0x494910 |  178 | VIBE_Command_QueueRequestBuffer28 | command-builders3 cluster |
| 0x40e50c |  111 | VIBE_Decompressor_Init | src/sim/entity_frame_update.cpp |
| 0x494b74 |   35 | VIBE_Command_QueueRequestPair35 | command-builders3 cluster |
| 0x494ca4 |   35 | VIBE_Command_QueueRequestPair42 | command-builders3 cluster |

> The five wave-21 "undercounted-due-to-concurrency" functions the brief flagged
> (Anim_CreateMorphAnim, Snow_UpdateFlake, Ai_CalcBankmeister, DecompressGameState +
> Decompressor_Init) are all **confirmed reconstructed** in wave 22 — they cite their
> function-start addresses in the named files, so the range-cite classifier now counts
> them. They were genuinely landed this wave (not merely a counting fix).

---

## Wave-21 over-count correction (1 function)

Wave 21 listed **VIBE_Rain_UpdateDrop @0x4294d4 (1891 B)** among the 27 it CLOSED, citing
particle addresses `0x4294b6..0x4294c6`. Those addresses actually belong to the **sibling**
function **VIBE_Rain_GrowDropList @0x4292b8** (reconstructed in
`src/render/rain_grow_misc_recon.cpp`), which ends at 0x4294d4 — exactly where
Rain_UpdateDrop *starts*. No citation falls inside `[0x4294d4, 0x42ac37)`. So Rain_UpdateDrop
was never reconstructed; it is **still-missing logic** and is restored to the list below.
This is the wave-22 analogue of the wave-21 "one byte short" Anim_CreateMorphAnim note.

---

## STILL MISSING — genuine game logic (25) — sorted by size

The real reconstruction debt for the next wave. Below the tech-boundary floor this is now
a small, well-defined set: the **privilege evidence sub-cluster remnant**, **two
trade-panel refresh halves**, the **State/Property/Gui-object mini-cluster**, the
**cutscene RNG + tiny command/coord leaves**, plus the restored **Rain_UpdateDrop**.

| addr | bytes | name | note |
|------|------:|------|------|
| 0x4294d4 | 1891 | VIBE_Rain_UpdateDrop | wave-21 over-count correction; sibling GrowDropList landed, this did not |
| 0x565b88 | 1042 | VIBE_Privilege_PanelEvidenceDetails | evidence sub-cluster remnant (panels_b gap) |
| 0x4bfc48 |  838 | VIBE_ChatConsole_BuildWindow | chatconsole cites 0x53629c, not this addr |
| 0x56589c |  747 | VIBE_Privilege_BuildEvidenceEntry | evidence sub-cluster helper |
| 0x413220 |  719 | VIBE_Widget_LayoutBounds | widget bounds layout |
| 0x57b480 |  570 | VIBE_Amt_ComputeOfficeWages | office wage computation |
| 0x50bf08 |  565 | VIBE_TradePanel_RefreshSellColumns | trade-panel sell column refresh |
| 0x50b1c4 |  394 | VIBE_TradePanel_RefreshItemColumns | trade-panel item column refresh |
| 0x40fb4c |  330 | VIBE_Input_SetIconTextById | cursor-icon text set (logic, not DInput) |
| 0x40eaf0 |  277 | VIBE_Gui_ResolveObjectState | object-state resolve |
| 0x40e9e8 |  262 | VIBE_State_Update | state-machine update |
| 0x40e728 |  237 | VIBE_State_GetCurrent | state-machine getter |
| 0x4152cc |  159 | VIBE_Property_Get | property getter (Property_Set landed wave 21) |
| 0x4c2ba0 |  158 | VIBE_Gesetz_ComputeMaxWantedLevel | law / wanted-level calc |
| 0x5437d8 |  150 | VIBE_MapView_AddCornerObjects | map corner-object placement |
| 0x4ad508 |  138 | VIBE_DragSlot_BeginDragText | dragselect uncited at this addr |
| 0x412ea4 |  113 | VIBE_Gui_MarkObjectUsed | gui object-used flag |
| 0x5929f0 |  105 | VIBE_Person_QueryByGoodType | person query by good type |
| 0x494d68 |   40 | VIBE_Command_QueueRequestQuad46 | cmd-queue leaf |
| 0x495124 |   40 | VIBE_Command_QueueRequestQuad60 | cmd-queue leaf |
| 0x495070 |   39 | VIBE_Command_QueueRequestQuad54 | cmd-queue leaf |
| 0x4ac9c0 |   39 | VIBE_Cutscene_GetRandSeed | cutscene RNG getter |
| 0x4ac9a0 |   31 | VIBE_Cutscene_SetRandSeed | cutscene RNG setter |
| 0x5d9104 |   21 | VIBE_Resource_FlushAndFree | tiny resource free |
| 0x5d8b00 |   16 | VIBE_Coord_Transform | tiny coord transform |

**Total remaining pure-logic debt: 25 functions / ~8,748 bytes** (down from 33 / 15,625 B).

**Cluster verdict:** the remaining real debt is now small and concentrated:
- **Privilege evidence remnant** (~2.8 kB): EvidenceDetails + BuildEvidenceEntry — the last
  two of the panels-b evidence sub-cluster (EvidenceReviewAlt closed this wave).
- **Particle leaf** (1.9 kB): Rain_UpdateDrop — the per-drop integrator (the Snow sibling
  closed this wave; GrowDropList/Render already done).
- **Trade-panel refresh** (~1.0 kB): the two RefreshColumns halves.
- **State / Property / Gui-object mini-cluster** (~1.0 kB): State_Update/GetCurrent,
  Property_Get, Gui_ResolveObjectState, Gui_MarkObjectUsed, Input_SetIconTextById.
- **Small leaves**: cutscene RNG get/set, the Quad46/54/60 command-queue leaves,
  Person_QueryByGoodType, MapView_AddCornerObjects, Gesetz_ComputeMaxWantedLevel,
  DragSlot_BeginDragText, Resource_FlushAndFree, Coord_Transform.

---

## Reclassified from wave-21 "logic" → CRT/runtime boundary (2)

Two functions that surfaced as logic-shaped in the raw bucketer are, on decompile, host CRT
runtime — **do not reconstruct as game logic** (they belong to the existing CRT port):

| addr | bytes | name | verdict |
|------|------:|------|---------|
| 0x5d4174 | 127 | VIBE_Util_StrToDouble | a `strtod` impl: calls Util_ParseDoubleString, sets errno, exponent-range clamp → host CRT/Float runtime |
| 0x40c950 | 103 | VIBE_Input_FormatErrorMessage | thin `wsprintfA("...Error = %08x")` wrapper → host CRT/Format runtime (the `Input_` prefix is misleading) |

---

## What remains — pure-logic debt vs genuine tech boundaries

Clear statement of the 198 missing, the whole point of this audit:

### A. Pure-logic debt — RECONSTRUCT (25 functions / ~8.7 kB)
The 25-function table above. This is the entire remaining game-logic reconstruction debt
reachable from the boot spine. It is **below the wave-21 level (33)** and is now a small,
enumerable tail — the next wave can close it and reach the genuine boundary floor.

### B. Genuine tech boundaries — DO NOT reconstruct (173 functions)
Covered by the shim layer (rules 3–6) or the host runtime; reconstructing them as logic
would violate the rules.

| bucket | count | substitution | shim/host |
|--------|------:|--------------|-----------|
| Render — DDraw/D3D fixed-function | 11 | → Vulkan (rule 3) | `shim::IGraphicsDevice` |
| Input — DirectInput devices/scancode | 5 | → SDL (rule 4) | `shim::IPlatform` |
| Audio — Miles/MSS32 + Music/Sound/Voice | 33 | → SDL audio (rule 5) | `shim::IAudioDevice` |
| CRT / File / Math / Float / Memory / Time / MBCS / zlib | 121 | host runtime | C++ runtime + `src/compress/` zlib port |
| Thunks (DirectDrawCreate/EnumerateExA + Vfs CloseHandle) | 3 | import thunks | n/a |

> Render/Input/Audio breakdown matches wave 21 (the wave-21 table folded the 5 DInput leaves
> into its "render boundary 11+" line; wave-22 splits them out for clarity — totals are
> consistent). The CRT bucket grew by +1 net only because the two reclassified functions
> (StrToDouble, Input_FormatErrorMessage) moved here from the wave-21 logic list. No
> boundary function regressed.

**Recommended boundary action (carried from wave 21, still open):** add `@0xADDR`
provenance to the existing zlib/CRT/audio shim bodies rather than re-reconstructing
(avoid ODR). This would not change the headline but would let the classifier *credit*
the boundary bodies that already exist behind the shim, raising the apparent coverage
toward 100% of the non-boundary set.

---

## Reconstructed-but-NOT-WIRED (flag for integration, rule 13)

The two wave-21 carry-over entry points remain documented but unbound to an external `src/`
caller (bodies exist; handoff comments present in their modules):

| addr | bytes | name | file | handoff |
|------|------:|------|------|---------|
| 0x4c9dec | 2154 | VIBE_NpcAction_NotifyJoinLeaveGroup | src/sim/npcaction.cpp | wire into the NPC group join/leave action-dispatch site (documented at npcaction.cpp:319) |
| 0x594100 |  430 | VIBE_Building_OpenUpgradeTreeWindow | src/sim/building.cpp | wire into the building upgrade-tree UI open click (documented at building.cpp:144) |

---

## Reproduce

- `/tmp/guild_w22/reach_rows.txt` — full 2164-row reachable set (addr·size·name) from the
  IDA BFS (`py_eval`, edges via `get_first/next_fcref_from` over every instruction).
- `/tmp/guild_w22/cited_addrs_cpp.txt` — every distinct `0xADDR` cited in any `src/**/*.cpp`
  body (13,932 addrs).
- `/tmp/guild_w22/missing.txt` — the 198 still-missing functions (range-cite), size-sorted.
- Classifier: reconstructed iff a cited addr falls in `[start, start+size)`; bucketing by
  `VIBE_`-stripped name prefix + decompile profile (render/input/audio/crt/thunk/logic).

*All addresses are gilde.exe (imagebase 0x400000). Snapshot 2026-06-16. Headline =
90.85% (1966/2164). Genuine game-logic debt: 25 functions / ~8,748 bytes — the
tech-boundary floor is in sight.*
