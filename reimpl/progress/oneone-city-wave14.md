# Wave-14 1:1 FIDELITY AUDIT — W14-CITY (world family / city / relation / setup)

MCP was DOWN this wave → IN-TREE audit only (no live binary diff). This pass extends
the wave-12 hardening of this cluster with a 1:1 VALUE-PINNING + CONFIDENCE MAP and a
precise NEEDS-LIVE-MCP queue. No valid-input behaviour changed; goldens stay
byte-identical. The only source-tree edits are TEST additions (relation + census
golden pins); no cluster source was edited (no evidence-backed drift was found).

## Cluster owned (source + provenance address)

| Function | gilde.exe | File |
|---|---|---|
| `FamilyTree::Find` | (VIBE_Person_FindRecordById, recoverable rule) | stammbaum.cpp |
| `FamilyGetFather/Mother/Spouse` | 0x55ab84 record reads (+0x5C/+0x60/+0x64) | stammbaum.cpp |
| `FamilyGetChildren` | 0x55ab84 child-slot walk (+0x68, skip -1) | stammbaum.cpp |
| `FamilyIsParentOf` | derived from the +0x5C/+0x64 links | stammbaum.cpp |
| `FamilyCollectHeirs` | 0x5555c8 (VIBE_Office_CollectFamilyHeirCandidates, v7<4 cap) | stammbaum.cpp |
| `FamilyIsAncestorOf` / `FamilyGetSiblings` | derived from the link reads | stammbaum.cpp |
| `FamilyGenerationDistance` / `*CollectAncestorsAtGen` / `*DescendantsAtGen` / `FamilyResolveInheritance` / `FamilyAreBloodRelated` | derived multi-gen walks (built on 0x55ab84 links) | stammbaum_query.cpp |
| `FamilyIsDescendantOf` / `FamilyCollectDescendants` / `FamilyHeirLine` | derived from 0x5555c8 + the link walks | family_query.cpp |
| `StammWorld::Find` | VIBE_Person_FindRecordById linear scan | stammbaum_tree.cpp |
| `StammbaumGatherTree` | **0x55ab84** (the gather kernel; flag-4 hide, spouse-children cap 4, own-children category<10 cap 5) | stammbaum_tree.cpp |
| `PersonGetFamilyRecordIndex` / `PersonGetFamilyRecord` | **0x58c408** (category∈{5,6,7} && signByte<0; idx = houseWord & 0x0F; &word_13C3110[82*idx]) | stammbaum_tree.cpp |
| `UtilParseInt` | **0x5dc070** (VIBE_Util_ParseInt) | city.cpp |
| `CityParseCsvFieldList` | **0x50704c** | city.cpp |
| `CityInitParameterTable` | **0x577a9c** (28-good drift/contrib/cap tables) | city.cpp |
| `IniParse/IniGet/IniFree` / `CityLoadFromIni` | non-original INI mapper + the loader's field placement (byte_13CD6A0) | city.cpp |
| `CityGetDistrictCoord` | **0x5783c4** (a1>=72 → 0; word_641DB0[4*a1]) | city_load.cpp |
| `CityLoadDefinitionIni` / `…FromVfs` | **0x507144** (path build + field placement + slot==0 NachbarStadt recursion, cap 8) | city_load.cpp |
| `CityAggregateDistrictStats` | **0x578abc** | city_population_census.cpp |
| `CityComputeWealthGrid` | **0x577e74** | city_population_census.cpp |
| `CitySnapshotStats` | **0x5783e4** | city_population_census.cpp |
| satisfaction grid (`GridSat*` / `BuildSatisfactionGrid` / crime) | 0x578240 + grid kernel | city_satisfaction_grid.{h,cpp} |
| `RelationReset/Get/Set` | **0x5942fc** (VIBE_Relation_LookupMatrixEntry; mutated by 0x49818C) | relation.cpp |
| `WorldCountActiveObjects` | **0x5839f0** (65-byte stride, 731 entries, 47515 bound) | world_setup.cpp |
| `StrCmpNoCase` (leaf) | **0x5cb8f0** | world_setup.cpp |
| `WorldSetupInit` | world bootstrap (resets slot + CityInitParameterTable + globals) | world_setup.cpp |
| `WrObject` | **0x5e5ab4** (VIBE_WorldIo_WriteObject) | world_io_save_recon.cpp |
| `WrBuildingData` | **0x5e5f74** | world_io_save_recon.cpp |
| `WrObjectCallback` | **0x5e61ec** | world_io_save_recon.cpp |
| Bio_* leaves | 0x5dc8cc / 0x5dc918 / 0x5dcac0 / 0x5dc8ec / 0x5dc9dc / 0x5dca40 / 0x5dcbb0 | world_io_save_recon.cpp |
| `UtilStrChr` leaf | 0x5d3ef0 | world_io_save_recon.cpp |
| `Groundplan_GetBuildingState` | **0x4af464** | groundplan_recon.cpp |
| `Groundplan_GetWappenLabelId` / `…WappenLabelForGroup` | **0x4ae59c** | groundplan_recon.cpp |
| `Groundplan_RetZero` | **0x4ae824** | groundplan_recon.cpp |
| `MoneyFormatWithSeparators` / `MoneyGroupThousands` | **0x58f798** | money_format.cpp |
| `WorldInitBuildingTypeTable` | **0x5833b4** | data_load.cpp |
| `WorldLoadBuildingAndObjectData` | **0x5835f8** | data_load.cpp |

### Address-less functions reached by the cluster (RED FLAGS)

All addressed except where noted as a **recoverable rule** (a sub-rule extracted
from an addressed parent, not a standalone binary function):
- The multi-generation walks in `stammbaum_query.cpp` + the descendant/heir-line
  walks in `family_query.cpp` are derived helpers built on the 0x55ab84 link reads
  and the 0x5555c8 heir cap; they have no standalone address (modelled traversals).
  NOT a red flag — they are documented as derived. The deeper-generation ordering
  (BFS frontier dedup, the 64-wide cap) is a reconstruction choice, not a recovered
  binary order → flagged UNDER-VERIFIED below.
- `WorldSetupInit` / `WorldSetupInit`'s reset sequence is a composed bootstrap (no
  single address); its callees are addressed.

## 1:1 VALUE PINNING — golden tests added this wave

1. **`world_relation_harden_test.cpp`** (+21 checks → 40 total):
   - `GridGeometryConstants` — pins `kRelationRowStride==768`, `kRelationDim==768`,
     `kRelationSelf==127`, `kRelationBytes==768*768==589824`.
   - `SetTargetsByteOffset768aPlusB` — for 7 off-diagonal cells (incl. corners
     (766,767)=589055, (767,0)=589056, (767,766)=589822) asserts the value lands at
     the EXACT raw `g_relationMatrix[768*a+b]` byte AND reads back signed via
     `RelationGet`. Proves the recovered `768*a + b` arithmetic (fixups-wave2 #2 /
     0x5942fc derivation), not just a round-trip. Also pins (767,767) self never
     stored (byte 589823 stays 0).

2. **`world_city_census_harden_test.cpp`** (+7 checks → 88 total):
   - `GridStrideConstantsMatchReductionLiterals` — pins the step-5 reduction's
     literals against the named grid geometry: `kCityGridRowBytes==192`,
     `kCityGridCellBytes==24`, `192*8==1536` (the `v20 != 1536` row-loop bound),
     `24*8==192` (8 cells/row), and the SatWeight/SatDenom/SatCount offsets
     `+36/+40/+44`. Catches drift on either the loop literals or the header.

### Already-pinned 1:1 values verified intact (no change)

- **money_format @0x58f798**: glyph 0x11, round-bias 0.5, `flt_6269C4` 1/3 grouping,
  thousands cadence — `world_money_format_test` (32 checks incl. INT_MAX/INT_MIN/u32
  range). Verified the source's `kCurrencyGlyph`, `kMoneyFormatRoundBias`, the
  `0.333333343f` third and the `(v8+1)%3` cadence match the provenance comment.
- **groundplan wappen @0x4ae59c**: label table {1241,1245,1249,1253} by group,
  v0-default 1241, 536-byte marker stride, 134-dword type-word stride, 411648 cap —
  `groundplan_recon_test` (37 checks, incl. the wave-12 fall-through OOB pins).
- **stammbaum gather @0x55ab84 / family-record @0x58c408**: portrait reads, flag-4
  hide on father, spouse-children {0→1,1→4,4→5} cap-4 stepping, own-children
  category<10 cap-5, the `houseWord & 0x0F` family-record index, the 82-slot
  HouseRecord stride — `world_stammbaum_tree_test` (33 checks).
- **city.cpp @0x5dc070/0x50704c/0x577a9c**: ParseInt edges + wrap, CSV bounds, the
  28-entry drift/contrib/cap tables — `world_economy_test` (151) +
  `world_city_load_harden_test` (40).
- **WorldCountActiveObjects @0x5839f0**: 65-stride / 731-count / 47515-bound,
  A-Z-only nocase compare — `world_bootstrap_test` (79).
- **world_io_save WrObject @0x5e5ab4**: case-0 object byte-stream + the callback
  sibling-null save/restore + the two BioWriteArray overflow guards —
  `world_io_save_recon_test` (13).
- **city_population_census wealth/snapshot/aggregate**: corner tiles, out-of-range
  tile rejection, snapshot 300-byte block + 4 scalars + cap-divisor trunc, the
  aggregate integer outputs (popClamped8/v47sum7/residents4) — `world_city_census_harden_test` (88).

## INTERNAL CONSISTENCY — verified, no drift fixes needed

- relation.cpp/.h: `768*a+b` matches the header derivation and fixups-wave2 #2; the
  `i8` cast == the binary's arithmetic `>>24`. ✔
- census reduction literals (192/24/1536/+36/+40/+44) == the named constants in
  `city_satisfaction_grid.h`. ✔ (now pinned)
- money_format constants == provenance comment. ✔
- groundplan group→label switch == header table + test. ✔
- world_io_save record strides (object node offsets, room 344 / part 56 / 8×64
  fixed strings) == the provenance comments and the `WriteObject`/`WriteBuildingData`
  decompile transcription. ✔ (object path byte-pinned; building path stride-checked
  by code review, see UNDER-VERIFIED).

## CONFIDENCE MAP

GOLDEN-PINNED (recovered values asserted by tests):
- relation get/set + grid geometry; money_format (incl. grouping core + extremes);
  groundplan wappen label table + grid walk (incl. fall-through OOB); stammbaum
  gather kernel + family-record index/stride; city ParseInt/CSV/param-tables;
  WorldCountActiveObjects; WrObject case-0 + callback; census wealth-grid /
  snapshot / aggregate integer outputs; CityGetDistrictCoord bound (a1>=72).

UNDER-VERIFIED (reconstruction faithful but a recovered ORDER/float value not
golden-asserted — needs a deeper vector or live diff):
- `stammbaum_query.cpp` multi-gen walks: the BFS frontier dedup + the 64-wide
  frontier cap + the inheritance-fallback-to-siblings order are reconstruction
  choices; only structural behaviour is tested, not a binary-recovered traversal
  order. (No single binary address — derived helpers.)
- `CityAggregateDistrictStats` FLOAT outputs (popDensity5, satScore2, lawFold9,
  density0, ratioB1/D3): the arithmetic chain is transcribed 1:1 but only the
  integer outputs are golden; the float results depend on live law-table state and
  are not pinned to a recovered numeric vector.
- `WrBuildingData @0x5e5f74` POSITIVE path: the room (344) / part (56) / 8×64-string
  strides are transcribed and the overflow guards are pinned, but no populated-graph
  golden byte-stream exists (needs a LinkResolver fixture). Empty-string/no-room
  path is covered.

NEEDS-LIVE-MCP (exact decompile target for the binary diff when MCP returns):
- **0x5833b4 VIBE_World_InitBuildingTypeTable** — carry-forward (per brief): the
  `kindWorth`/`byte_13CEB3C[25]` OOB the orchestrator bounded. Confirm whether the
  binary's table is wider, the +33 subtype index is different, or the subtype is
  masked (the in-range path subtype<24 is byte-identical; out-of-range → 0 default).
- **0x5833b4 + 0x5835f8 — `objType` (room & 0x7FFF) reads of `g_sceneTypes`.** NEW
  this wave (same OOB CLASS as the kindWorth one, distinct site): in
  `WorldInitBuildingTypeTable` `ObjByte(objType,33)` (line 131) and in the
  `WorldLoadBuildingAndObjectData` room-count fixup `ObjByte(objType,0)` (line 234),
  `objType = room & 0x7FFF` can reach 32767 while `g_sceneTypes` holds 1024 elements
  → an OOB read for `objType >= 1024`. In the binary the object-type table base
  (dword_13CE27C) is followed by adjacent BSS that absorbs the read; the in-tree
  fixed-capacity array does not. NOT modified this wave (audit-only; whether the
  original masks objType, the table is larger, or such room words never occur on
  real A_Geb.dat is a behavioural question for the live decompile). Real assets
  (A_Geb.dat room words) keep objType well under 1024, so existing goldens are safe.
  Flagged for the same MCP pass as the kindWorth OOB.
- **0x50704c CityParseCsvFieldList trailing empty token** (carry-forward from
  wave-12): with fewer commas than maxFields the loop parses ONE extra empty token
  (`ParseInt("")==0`). Cross-check the v16/"last field" handling against the binary.
- **0x5942fc / 0x49818C relation** — no range guard on (a,b) is the engine's own
  envelope (faulting in both); confirm no bound exists in the original (no guard
  added). Carry-forward.
- **0x578abc degenerate divisors** — zero live-slot / zero building sums divide by
  zero in DOUBLE (inf/nan), matching the FPU path; confirm no integer div exists.
  Carry-forward.

## Build / test status

`cmake --build build` green for the cluster. Cluster suite run after the test
additions:
```
world_relation_harden_test       40  checks, 0 failures   (+21)
world_city_census_harden_test    88  checks, 0 failures   (+7)
world_money_format_test          32  checks, 0 failures
world_stammbaum_tree_test        33  checks, 0 failures
world_io_save_recon_test         13  checks, 0 failures
groundplan_recon_test            37  checks, 0 failures
world_data_load_test             19  checks, 0 failures
world_family_harden_test         49  checks, 0 failures
world_bootstrap_test             79  checks, 0 failures
world_city_load_harden_test      40  checks, 0 failures
world_economy_test              151  checks, 0 failures
city_satisfaction_grid_test     203  checks, 0 failures
city_satisfaction_grid_itest    105  checks, 0 failures
```
Goldens byte-identical; only additive pins. No cluster source edited; `progress/INDEX.md`
left untouched per the brief.
