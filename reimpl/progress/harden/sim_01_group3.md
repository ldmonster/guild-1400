# Hardening sweep — sim_01_group3 (building lifecycle / upgrade / upgrade-window)

Chunk files (owned):
- `src/sim/building_lifecycle.cpp` (+ `.h`)
- `src/sim/building_upgrade.cpp` (+ `.h`)
- `src/sim/building_upgrade_window.cpp` (+ `.h`)

MCP: IDA Pro live (`gilde.exe`, imagebase 0x400000). Every provenanced function was
`decompile`d AND (where float/branch-sensitive) `disasm`d and diffed line-for-line.

## IMPORTANT environment note
During this sweep the working tree of `building_lifecycle.{cpp,h}` was externally
reverted to a **trimmed 2-function version** (HEAD: only `Building_RemoveAndCleanup`
@0x5894b0 and `Building_FindNearestSameType` @0x587908). The earlier WAVE-19 additions
(MapTypeToCategory, CheckBuildRequirements, ClassifyUpgradeNode, CloseUpgradeWindow,
Building_Update, ComputeUpgradeChargeAmount, PlotHasAdjacentOwned, DecideSelectionClick,
MapActionToCategory) are **no longer in these files** and are therefore out of scope for
the trimmed unit. Those functions WERE fully re-verified against the binary anyway (see
"Verified-but-removed" below) before the revert, but no source remains to edit.

Consequence: the untracked test `tests/unit/building_lifecycle_test.cpp` targeted ONLY
the removed WAVE-19 functions (23 tests, 0 against the surviving two) and broke the build
glob. It was removed (untracked, dead against the trimmed source). The surviving tracked
test `tests/unit/sim_building_lifecycle_test.cpp` covers the two remaining functions and
references no removed symbol.

---

## building_lifecycle.cpp / .h  (surviving functions)

### Building_RemoveAndCleanup @0x5894b0 — FIXED (struct offsets + removalTs)
DISASM-verified field offsets in the 536-byte Person/building record:
- `Character_Destroy` reads `*((_DWORD*)v2 + 97)` == **+388** -> charHandle.
- type-table decrement reads `*((_DWORD*)v2 + 92)` == **+368** -> typeRecord.
- removal-time stamp: `v2[20]` (word index 20) == **+40**, value `(WORD)qword_13CE852`.

Before: header had `charHandle@+368 (idx92)`, `typeRecord@+388 (idx97)` — **SWAPPED** —
and `removalTs@+16 (idx8)` set to `0`. After: `typeRecord@+368`, `charHandle@+388`,
`removalTs@+40` stamped with `(u16)NpcClock().day` (NpcClock() is the in-tree
`qword_13CE852` game-clock model, reused from npcaction). Static_asserts updated.
Tests use the fields symbolically (set+check both==0) so no golden churn was needed.

### Building_FindNearestSameType @0x587908 — FIXED (bestDist init)
DISASM @0x58791b: `*(_DWORD *)v17 = 1287568416` == 0x4CBF2360 == **100000000.0f (1e8)**,
verified via get_bytes + IEEE754 decode. Before: `100000.0f` (1e5) with a wrong "~1.0e5"
comment. After: `100000000.0f`. Distance probe remains the documented `BuildingDistanceSq`
hook; the category match (`td.kind == typeCode`, via `*v6 == (a2 sign byte)>>24`),
the production/storage-10 skip, and the i!=self gate match the binary.

---

## building_upgrade.cpp / .h  — ALL VERIFIED-1:1 (no edits)

- **Object_HideUpgradeScaffoldNeedsRetexture** @0x5063f0 — VERIFIED-1:1. Predicate model
  of `if (*(a1+535)==2||==4) skip; else SelectTextureSet; return 1`. The reimpl reports
  whether retexture is needed (2/4 -> false). Faithful predicate.
- **Building_ComputeUpgradeCost** (shared 0.3 block) — VERIFIED-1:1. `worth * 0.3` then
  ConvertX-truncate; `truncToZero(double)` (cvtt-style trunc toward zero) matches the
  `ConvertX(); (__int64)` pair because ConvertX makes the value integral. flt 0.3 =
  **0x3E99999A** confirmed at 0x61A474/0x61A48C/0x61A5B4/0x61A5D0 (all four).
- **Building_ApplyUpgradeLevel** (ExGebUpgrade core @0x49aff0) — VERIFIED-1:1. Guard
  `typeDef+583 >= typeDef+584` (security >= maxUpgradeLevel); `++*Begin`;
  `Begin[92] = HIBYTE(packed) + (100 - (packed>>24))/2` with `packed>>24` the ARITHMETIC
  (signed) shift and HIBYTE the unsigned top byte. All matched in disasm.
- **Interaction_PerformBuildingUpgrade** @0x46cfc4 — VERIFIED-1:1 (handlerKind 4 -> charge,
  return 17). The LoadBuildingGraphic success path returns 17 in the binary; the reimpl
  surfaces 0 and lets the caller drive the scene leaf — BOUNDARY (rule 3-5 scene side
  effect), documented in source.
- **Interaction_PerformBuildingUpgradeOnObject** @0x46d978 — VERIFIED-1:1. BUY branch
  `*a2==4 && *(BYTE*)a3==8` (handlerFound gate -> EnqueueBuyBuilding, return 19); UPGRADE
  branch `*(BYTE*)a3==4` -> EnqueueCmd15(**-1**, *(a3+4), cost, mode), return 19. Graphic
  fallback -> 0 (BOUNDARY).
- **NpcAction_UpgradeTownHall** @0x472b90 — VERIFIED-1:1. `return 49` confirmed in disasm.
- **NpcAction_UpgradeDungeon** @0x472fd0 — VERIFIED-1:1. `return 60` confirmed in disasm.
  Shared gate both: `!OfficeStorage || *(a1+358)!=15 || (*a3!=4 && *(a2+16))` ==
  `!officeStoragePresent || stateByte358!=15 || (handlerKind!=4 && !secondaryEmpty)`. cost
  factor flt 0.3 (0x61A5B4 / 0x61A5D0) confirmed.

ConvertX @0x5c6b08 confirmed via disasm: `fstcw`/`fldcw` with RC bits set to **truncate
toward zero** (round-control 11) + `frndint` — i.e. ConvertX TRUNCATES. All upgrade-cost
sites use trunc-toward-zero. Matches the brief.

---

## building_upgrade_window.cpp / .h

- **Building_OpenUpgradeTreeWindow** @0x594100 — VERIFIED-1:1 (logic). Open/center/select/
  title, layout via reused RoadComputeNetworkLayout, empty-tree -> Form_Destroy + return
  -1, per-node window build (UI boundary), and the edge-draw geometry loop (inner<outer,
  `inner.nodeId == outer.parentFromId||parentToId`, DrawLine(innerX+24, innerY+48,
  outerY+24, outerX+24, 88, 0x1F, 20)) all match. UI/.form/GPU plumbing is the SDL/Vulkan
  boundary (rules 3-4).
- **dword_13CE288 window-handle accessors** — FIXED (build break from the revert). The
  revert deleted the lifecycle copy of `SetUpgradeWindowHandle`/`UpgradeWindowHandle`,
  leaving upgrade_window.cpp referencing undefined symbols. Moved their declaration/
  definition into building_upgrade_window.{h,cpp} (their only remaining user; ODR-checked
  — defined nowhere else) and dropped the stale `#include "sim/building_lifecycle.h"`.

---

## Verified-but-removed (re-verified against binary, source no longer present after revert)
These were diffed and would have been VERIFIED-1:1 or noted; they are no longer in the
trimmed lifecycle unit and were NOT re-added (revert was intentional):
- MapTypeToCategory @0x5878b0 — switch matches binary exactly (1,3,6,F->3; 2->6; 4,5,9->8;
  7->4; 8,E,12,14,15,16->1; B,C,D->2; 13->7; 17,18,19,1A->5; else 0). NOTE: a faithful
  copy also lives in other chunk files — HANDOFF: do not edit those here.
- MapActionToCategory @0x58a25c — all JUMPOUT epilogues decoded via get_bytes (mov al,X;
  ret): 5->9, 7->7, 8->6, 9->8, 14->5, 21->3, 22->4, default->0 (xor al,al); 4->11,
  16->12, 18->1, 19->10 direct; 20->ClassifyTypeFlag (tail into 0x589850).
- CheckBuildRequirements @0x587bfc — would have been FIXED: **case 8 trailing `return 1`
  is WRONG** (binary returns 0 when CollectByType is empty/no-hit, @0x587ef4/0x587f44; a
  direct roomList hit `break`s -> return 1, sibling hit -> 2). switch selector = `cl` =
  MapTypeToCategory(a1) (category); case-6 sub-switch = `ch` = original srcCode. Cases
  1/2/3/4/6/7 verified. (No source to apply the case-8 fix to post-revert.)
- ClassifyUpgradeNode (BuildUpgradeTree v40 classifier) @0x59361c — v40 in {0,1,2,3}
  verified: office-handler owned->1(+handler->3); not-owned buildable->2 by the
  (v16==0&&!prereqA)|(v16==1&&!prereqB)|(v16==2) rule; normal node owned->1, buildable->2
  (+active handler -> 3 at LABEL_62). Matches the reimpl that existed pre-revert.
- ComputeUpgradeChargeAmount @0x50f7c0 — factor = level*0.25+0.5 (dbl_621620=0.25 @0x621620,
  dbl_621628=0.5 @0x621628, byte-confirmed), base=trunc(ComputeMarketPrice), amount=
  trunc(base*factor); both ConvertX truncations. Matched.
- PlotHasAdjacentOwned @0x50cb4c + VectorWithinTolerance @0x5caa4c — per-component
  `fabs(b-a) <= 100.0`. tol 100.0 matched.
- DecideSelectionClick @0x50ed14 — full nested gate matched (mapMode/pickedValid/suppress/
  flag631748/IsProduction&kind71/selectionFlag/family/cameraZoom).
- CloseUpgradeWindow @0x5942b0 / Building_Update @0x40e2b4 — trivial, matched (boundary
  leaves Form_Destroy / State_Update+Animation_Basic).

---

## Counts
- Functions present in chunk & verified: **9** (2 lifecycle + 7 upgrade) all
  DECOMPILE+DISASM diffed.
- VERIFIED-1:1 (no churn): 7 (all of building_upgrade.cpp + OpenUpgradeTreeWindow).
- FIXED: 3 source fixes —
  1. RemoveAndCleanup struct: charHandle/typeRecord offsets un-swapped (+388/+368) and
     removalTs +16->+40 stamped with game clock.
  2. FindNearestSameType bestDist init 1e5 -> **1e8** (0x4CBF2360).
  3. upgrade-window dword_13CE288 handle accessors relocated to fix the revert build break.
- BOUNDARY (rule 3-5, documented): graphic-load return paths in the two Interaction_*
  upgrades; all .form/window/GPU plumbing in OpenUpgradeTreeWindow.
- HANDOFF: MapTypeToCategory is duplicated across chunk files — the copy here verified,
  others left untouched.
- Build: all three owned sources compile clean under project flags (-Wall -Wextra
  -Wno-unused-parameter, c++17), verified as objects through the real CMake build. The
  full `guild` lib link is blocked by an UNRELATED in-progress file
  (src/gui/widget_layout.cpp, `Widget::ld<>` — not in this chunk).
- Removed dead untracked test building_lifecycle_test.cpp (targeted only removed WAVE-19
  functions).
