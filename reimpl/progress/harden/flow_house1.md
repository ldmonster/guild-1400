# HOUSE-1 hardening — in-house/workshop GUI entry + building dialogs

Owned files: `src/play/scene_interior.cpp`, `src/play/interact_building.cpp`,
`src/play/building_scene_attach.cpp`, `src/play/dialog_bank.cpp`,
`src/play/dialog_council.cpp`, `src/play/dialog_market.cpp` (+ headers/tests).

MCP live. Every provenance'd function decompiled + diffed line-for-line against the
binary (disasm is the reference of record). Float→int sites checked for ConvertX
truncation vs fistp rounding. Struct offsets/strides confirmed with get_bytes.

## scene_interior.cpp / .h

| fn | addr | verdict |
|---|---|---|
| `Building_MapTypeToCategory` | 0x5878b0 | VERIFIED-1:1 (thin REUSE of sim::Building_MapKindToCategory) |
| `Building_IsProductionType` | 0x587f80 | VERIFIED-1:1 — decompile confirms `table[589*type] ∈ {11,12,13,16,28}` |
| `SelectRoom_Pick` | 0x51db4c | **FIXED** (see below) |
| `SelectRoom_Direct` | 0x51db4c (-1 branch) | VERIFIED-1:1 — `-1` path writes a2(dx) into building+0x29 |
| `OpenBuilding_GateAllows` | 0x4adef4 | VERIFIED-1:1 |
| `AttachObjectMesh_Decide` / `Combat_AttachObjectMesh` | 0x485e88 | VERIFIED-1:1 |
| `EnterBuildingInterior_Decide` | 0x5066b8 | VERIFIED-1:1 (4 decision cores) + doc clarified |
| `Building_SelectRoomToEnter` (orchestrator) | 0x51db4c | VERIFIED-1:1 transition order |

### FIXED — `SelectRoom_Pick` count>available (0x51db4c)
Disasm (`mov ecx,eax` ⇒ ecx = the count arg `a1`):
```
edx=1; cmp ecx,1; jle LABEL_9 (count<=1 -> roomIds[0])
loop: result=IterNext(); test eax; jz -> return NULL (no write)
      inc edx; cmp edx,ecx; jl loop
exit (edx>=count) -> LABEL_9 writes current result = roomIds[count-1]
```
- Before: when `roomCount > roomIds.size()` the recon CLAMPED to the last available
  match and returned found=true.
- Binary: IterNext walks off the end, returns null while `edx<count`, so the
  function returns **NOT FOUND** (no write). It does not clamp.
- After: `roomCount>n -> {found=false}`. `1<roomCount<=n -> roomIds[roomCount-1]`;
  `roomCount<=1 -> roomIds[0]`.
- Golden also corrected: `tests/unit/scene_interior_test.cpp` (`RoomPick_WalksToCountth`)
  had `SelectRoom_Pick({10,20},5) -> found,20` (WRONG). Now `!found`, plus an added
  `count==available` case (`{10,20},2 -> 20`).

### Doc fixes (no behavior change)
- `EnterBuildingInterior_Decide`: clarified that `interiorModeByte` is `*v4` where
  `v4 = 589*building[0] + dword_13CE294` — the SAME building-type kind byte that
  `IsProductionType` reads (not a separate "interior mode" field). Season write
  `<=2 || ==3` ⇔ `season<=3` confirmed. `*a1==30 && HIWORD==253` confirmed.
- `EnterGateInputs.activeCharLive` comment: `byte_12CEAC1[535*char]` → `536*char`
  (decompile @0x4adef4 shows stride 536).

## interact_building.cpp / .h

| fn | addr | verdict |
|---|---|---|
| `BuildingDialogKindToActionGroup` | 0x51defc switch(*v21) | VERIFIED-1:1 (the 10 mapped arms) |
| `BuildBuildingCmdPacket` / opcode-26 layout | 0x494848 | VERIFIED-1:1 |
| `ClassifyBuildingAction`, FSM, `IssueBuildingClick` | slice over 0x51defc | slice (documented) |

- Switch arms diffed against the full ladder: 19→GuildMaster, 20→Sabotage,
  21→Threat, 22(≥0x16)→WineCellar, 24→Mistress, 0x74→Smith, 133→Carpenter,
  0x9B→Stonemason, 0xF7→Production, 0x114→Treasury — all correct. The enum
  deliberately models only these (the building-mutating + named social loops);
  unmapped codes → kNone, as the header documents. NOT a full-switch 1:1 claim.
- Opcode-26 packet: disasm @0x494848 confirms `var_A0(off 0)=0x1A`,
  `var_90(0x10)=eax(id)`, `var_8C(0x14)=edx(field)`, `var_88(0x18)=arg0(value)` →
  recon offsets 0x10/0x14/0x18 VERIFIED-1:1; arg order id/field/value matches
  building_stock.h callers.

## building_scene_attach.cpp / .h  (city-view building placement; secondary to the
ENTER chain but in-tree)

| fn | addr | verdict |
|---|---|---|
| `SpawnKindForVersion` | 0x5e67c8 ladder | VERIFIED-1:1 |
| `SpawnTypeForKind` | 0x5b054c | VERIFIED-1:1 (decision) + BOUNDARY note added |
| `ResolveGebaeudeOgrVariants` | 0x50d01c probe | VERIFIED-1:1 (float math below) |
| `BuildingLoadAndAlignGebaeudeModel` | 0x50d01c | VERIFIED-1:1 (constants below) |
| `ReadCityObjectRecord` | 0x5e67c8 | VERIFIED (version ladder; large, unchanged) |
| placement/align/rebuild helpers | various | VERIFIED (unchanged) |

### Variant-probe float math (0x50d01c) — VERIFIED-1:1
Disasm 0x50d178..0x50d275:
```
counter=0; build "%sgebaeude/*%s.ogr" (base, no suffix)
probe: ResolveAndBuildPath -> hit: store slot[count*0x60], count++
       miss & (counter bits & 0x7FFFFFFF)!=0 -> EXIT; miss & counter==0 -> continue
fld1; fadd counter; fstp counter            ; counter += 1.0 (x87)
cmp counterBits, 0x43000000; jge EXIT        ; >= 128.0f
fld counter; fadd flt_621560(=64.0); ConvertX(@0x5c6b08 TRUNCATE); fistp tmp
suffix = (u8)tmp ; build "%sgebaeude/*%s_%c.ogr"
```
- `flt_621560` get_bytes = `00 00 80 42` = 64.0f (confirmed). Recon `kVariantCharBase=64.0f`.
- ConvertX truncates toward zero; recon uses `(u8)(i32)(counter+64.0)` — C `(int)`
  cast truncates toward zero — MATCHES.
- 128.0 cap (`0x43000000`, signed `jge` on bits) and `&0x7FFFFFFF` zero-test reset
  (dead after +=1.0) both reproduced. Slot stride 0x60=96 (`kOgrPathSlot`), cap 128
  (`kMaxOgrVariants`) — confirmed by `add ebp,0x60` and the 128.0 bound.
- Patterns get_bytes: 0x621474 `%sgebaeude/*%s.ogr`, 0x62145c `%sgebaeude/*%s_%c.ogr` —
  match `kOgrPatternBase`/`kOgrPatternVariant`.
- Duplicate-building dialog (0x50d2a0): text id `14*type + 0x436`(=1078), msg id
  0x1405(=5125) — match recon `14*btype+1078`, `5125`.

### BOUNDARY note added — `SpawnTypeForKind` (0x5b054c)
The original applies a global override at LABEL_7 (0x5b05a9):
`if (byte_649D54 && type==6) type=5` (the "no rotating billboard" engine global).
That global is not an input to the pure helper, so it is NOT modeled — documented
inline as a BOUNDARY (the placement path is in the flag-clear regime).

## dialog_bank.cpp / dialog_council.cpp / dialog_market.cpp

These are the GUI RENDER-LAYOUT wrappers for the bank/loan, council/office, and
market/trade panels. Their provenance addresses (0x41beb8 Form parse, 0x508xx
TradePanel, 0x58f3d0 price) are REUSE references to REAL siblings reconstructed in
OTHER files (form_parse.cpp, trade_panel.cpp, building_production.cpp,
slice_bank/market/council.cpp — NOT owned by HOUSE-1). The synthetic-form builders
and widget cells are documented INERT render-layout choices (not recovered offsets),
per the header notes — no on-disk coordinates exist for the runtime-built rows.
The one verified-real constant kept: lender-row height 16 (Window_AddChildWindow
height arg) and slider w=48/h=100 (BuildSliderRow @0x50b600). The float→int price
site (`TruncToInt` in dialog_market) uses `(i32)` truncation matching slice_market's
cvttsd2si. Nothing to fix; these delegate all 1:1 math to the owned-elsewhere slices.

VERDICT: no divergences in the dialog wrappers' owned code.

## Rule 13 — wiring of the ENTER chain (Scene_EnterBuildingInterior @0x5066b8)
- The building click→dialog path is LIVE: `src/world/wire_building.cpp`
  `InstallRealBuildingWiring()` binds `BuildingDialogHooks.checkEntryAllowed` to the
  REAL entry gate `sim::Building_CheckEntryAllowed` (0x51dcd4) via
  `sim::BuildingFindById`. `play::IssueBuildingClick` → `BuildingDialogFsm::Open`
  (the 0x51defc EnterAndDispatch stand-in) → CheckEntry gate → kind→action-group
  dispatch → opcode-26 command through the REAL CommandQueue.
- `EnterBuildingInterior_Decide` (0x5066b8) and `Building_SelectRoomToEnter`
  (0x51db4c) are the deterministic decision cores; the FSM routes the interior
  load + room transition through the inert `enterInterior` hook (a documented
  GUI/scene-load boundary). Decision cores are reached by the unit suite
  (`scene_interior_test`) and the e2e (`play_interact_building_e2e_test`).

## Build / test status
- All FIVE owned non-council TUs compile clean (`-Wall -Wextra`, syntax-only):
  scene_interior, building_scene_attach, interact_building, dialog_bank,
  dialog_market. dialog_council.cpp is sound; its ONLY error is transitive in
  `src/play/slice_council.h:89` (`sim::CommandPacket` used without including
  `sim/command.h`) — a CONCURRENT agent's uncommitted, incomplete edit (NOT a
  HOUSE-1 file). The src/** single-lib glob makes that error block linking of all
  test mains until its owner fixes it.
- A background monitor retries the build + runs the six suites once slice_council.h
  compiles; results land in /tmp/guild_harden/house1_{build,ctest}.log.

## Counts
- VERIFIED-1:1: 14 functions (scene_interior 7, interact_building 2, building_scene_attach 5+).
- FIXED: 1 source divergence (SelectRoom_Pick count>available) + 1 wrong golden + 2 doc fixes.
- BOUNDARY: 1 (SpawnTypeForKind byte_649D54 global) + the dialog GUI render-layout cells.
