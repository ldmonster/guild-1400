# Wave-12 hardening — play slices + dialog + cutscene UI (W12-SLICE)

MCP DOWN → hardening only (no new 1:1 reconstruction). ASAN+UBSAN build of the
cluster's test targets in `build-asan-slice/` (cleaned at end). Every fix keeps
valid-input goldens BYTE-IDENTICAL; genuine OOB reads are bounded BEFORE the access.

## Cluster owned
`src/play/`: `slice_{bank,church,combat,council,estate,market,personnel,production,
tavern}`, `dialog_{bank,council,market}`, `cutscene_recon2_{movie,theatre,tutorial}`,
`interact_building`, `city_info`, `text_recon`, `text_recon3_itemlabel`,
`ui_recon{3_widget,4_hud_surface,5_panels}` + their tests.

## Method
1. `cmake -S . -B build-asan-slice -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF
   -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"`
2. Probed each module with malformed / boundary inputs; confirmed each bug under ASAN
   BEFORE fixing, then pinned the fix with a permanent test.
3. Verified the normal `build/` stays green for every edited target.

## Bugs FOUND + FIXED (faithful, goldens byte-identical)

### 1. `text_recon3_itemlabel.cpp` — NUL-less / overlong record field over-read
`VIBE_Text_FormatItemLabelWithIcon` (gilde.exe 0x59ccf4) reads the record's name
fields at +48 / +64 with `strlen` / `WideCopy` / `sprintf`, assuming a NUL inside the
536-byte (268-word) record. A malformed record whose name field has NO terminator
within the entry caused a stack/heap over-read past the record (ASan
stack-buffer-overflow READ, confirmed).
- FIX: bound every field read to the record's documented 536-byte extent.
  - new `BoundedFieldLen` / `BoundedField` cap the `strlen`/copy at `[off, 536)`.
  - `FieldEndChar` bounds its `record[off-1 + strlen]` index to `[0, 536)`.
  - `name48`/`name64` are now NUL-terminated bounded copies in local scratch.
  - the internal `sprintf` scratch (v61/v62/v63) enlarged from 256 → 552 bytes so a
    long bounded field spliced via `sprintf("%ss", name)` cannot overflow the scratch
    (a second over-WRITE that surfaced once the read was bounded — same root: the
    original's 256-byte scratch assumed short fields).
- On well-formed (in-record NUL-terminated) fields these bounds never trip → the 40
  existing goldens are unchanged (still pass byte-for-byte).
- Tests added (`text_recon3_itemlabel_test.cpp`): `Kind1NulLessName48NoOOB`,
  `Kind2NulLessName48SprintfNoOOB`, `Kind4PluralNulLessFieldEndCharNoOOB`,
  `Kind3NulLessName64NoOOB`, `ManyKindsNulLessRecordNoOOB`.

### 2. `slice_combat.cpp` — out-of-range crime ids over-read the relation matrix
The crime branch snapshots the perpetrator→victim standing via
`world::RelationGet(victim, perp)` BEFORE/AFTER the apply. `RelationGet`
(gilde.exe 0x5942fc) indexes the flat 768×768 grid as `g_relationMatrix[768*a + b]`
with NO internal bound, so a degenerate (stale/garbage) victim/perpetrator id is an
OOB read (ASan global-buffer-overflow, confirmed: `index 68365983 out of bounds`).
`ApplyCrimeCommand` already gated the WRITE on `kRelationDim`; the slice's READ
snapshots did not.
- FIX: added `RelationGetSafe(a,b)` in `slice_combat.cpp` (returns 0 — the unbound
  default cell — for any index outside `[0, kRelationDim)`), and routed the four
  before/after snapshot reads in `RunCombatCommand` / `RunCombatStepsSynthetic`
  through it. In-range pairs return exactly `RelationGet` → goldens unchanged.
- Tests added (`slice_combat_test.cpp`): `CrimeOutOfRangeIdsNoOOB`,
  `CrimeNegativeIdsNoOOB`, `CrimeBoundaryIdAtDimNoOOB`.

## BEHAVIORAL / cross-cluster items — DOCUMENTED for the owner (not edited here)

- `world/relation.cpp` `RelationGet`/`RelationSet` (gilde.exe 0x5942fc) have NO
  internal bound on `a`/`b` (index `768*a + b` into a 768×768 grid). Faithful to the
  binary's own unchecked indexing, but any other caller passing an out-of-range id
  will OOB. This wave guards the slice-side callers; the matrix accessors themselves
  are owned by the `world/relation` cluster — flagged for that owner to decide whether
  a bound is faithful (needs MCP to confirm the original's envelope).

## Probed and found ALREADY SAFE (added boundary tests, no fix needed)
- `dialog_market` / `dialog_bank` / `dialog_council`: 0-entry, many-entry, out-of-range
  ware (good index), out-of-range selected row / "bad node" click, tiny framebuffer +
  negative-origin render — all clip via the surface ops and guard the list index.
  Tests added: `DialogMarketUnit.{ManyRowsAndOOBWareNoOOB,EmptyRowsRenders}`,
  `DialogBankUnit.{ZeroLendersOutOfRangeRowNoOOB,ManyLendersRendersNoOOB}`,
  `DialogCouncilUnit.{ZeroOfficesNoHitNoOOB,ManyOfficesRendersNoOOB}`.
- `slice_market` / `slice_bank`: out-of-range good (ware), out-of-range / negative /
  unknown building & borrower account — `SceneTypeDefAt` / `Building_ComputeMarketPrice`
  clamp the ware, `BuildingFindById` / `PersonFindRecordById` return null for unknown
  ids (inert no-op apply). `ApplyDayPriceDrift` maps the ware into `g_goods` via modulo.
  Tests added: `PlaySliceMarketUnit.{OutOfRangeWareNoOOB,NegativeWareNoOOB,
  UnknownBuildingIdInertNoOOB}`, `PlaySliceBankUnit.{UnknownBorrowerAccountInertNoOOB,
  NegativeBorrowerAccountInertNoOOB}`.
- `ui_recon5_panels` / `ui_recon4_hud_surface`: every array writer is capacity-bounded
  (`PlayerBarResetSlots`, `InfoPanelTraitRows`, `HudShadowSelection`,
  `LenderInitRowSlots`, `MapViewSortMarkersByScreenY`). Tests added:
  `UiRecon5.{PlayerBarResetCapSmallerThanSlots,InfoPanelTraitRowsCapSmallerThanEmitted,
  HudShadowSelectionZeroLength,MapViewSortMarkersZeroAndOneElement}`,
  `UiRecon4Hud.LenderInitRowSlotsFillsExactlySixteen`.
- `cutscene_recon2_theatre`: participant gathering over empty / mismatched-length /
  oversized frame-data columns — every column access is index-checked against the
  vector size and capped at the original's 768-record / `v29<8` / `v21<32` bounds.
  Tests added: `Cutscene2ReconTheatre.{GatherTenancyEmptyTables,
  GatherTenancyMismatchedColumns,GatherDuelMismatchedColumns,
  GatherDuelOversizedTablesCapNoOOB,FindByRoleEmptyAndShortDwords}`.
- `interact_building`, `slice_{church,council,estate,personnel,production,tavern}`,
  `city_info`, `text_recon`, `cutscene_recon2_{movie,tutorial}`, `ui_recon3_widget`:
  reviewed; all entity/slot/field accesses are null-checked or capacity-bounded;
  `city_info`'s `$`-markup parser bounds every `beschr[j]` against the string size;
  `PutPx`/`BlitShape` clip. No OOB found; existing tests still green under ASAN.

## Result
All cluster test targets pass under ASAN+UBSAN (`-fno-sanitize-recover=all`) and in the
normal `build/`. New hardening tests: 26 across the 8 edited test files. Goldens
byte-identical. `build-asan-slice/` removed.
