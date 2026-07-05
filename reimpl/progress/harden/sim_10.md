# Harden sweep — sim_10 chunk (person/path/production/recruit cluster)

1:1 verification of every provenance-carrying function/table in the 22 assigned
`src/sim/` files against `gilde.exe` via the IDA MCP (decompile + disasm +
get_bytes). Only proven divergences were changed; evidence addresses cited per
item. All covering test targets rebuilt and green (counts at the bottom).

## Verdict legend
- **VERIFIED-1:1** — decompile/disasm/bytes match the reimpl; no change.
- **FIXED** — proven divergence corrected (binary evidence cited).
- **WIRING-ONLY** — glue file with no reconstructed logic to diff.
- **ADAPTED** — established hook-boundary adaptation, logic verified within it.

---

## src/sim/object_value.cpp
- `VIBE_Object_ComputeMarketValue` @0x594df0 — **VERIFIED-1:1**. Control flow,
  x87 double-modeling, and the six doubles at 0x626B34..0x626B5C
  (0.01, 1/252, 0.25, −0.5, 0.1, 0.3) byte-verified via get_bytes.

## src/sim/path.cpp  (+ path.h)
- `kPathTypeCost` table — **FIXED**. The binary ships **FIVE** 15-float cost
  profiles at 0x62E630..0x62E75B (60 B each; get_bytes verified all 75 floats)
  and `dword_765308 = &unk_62E630 + 60*profile` at 0x43bfe9. Profile 3 is live
  (0x577320 `VIBE_Map_FindNearestDoorCell` calls BuildWaypointList with a7=3 at
  0x57744d). The reimpl carried only profile 0 and ignored the `profile` arg
  ("only 0 is shipped" — refuted). Now: full `kPathCostProfiles[75]` +
  `g_costTable = kPathCostProfiles + 15*profile`; `PathTypeCost` honors profile.
- `VIBE_Path_ExpandNode` @0x43c1c4 — **FIXED (2)**:
  - 0x43c288: `fld flt_62E610[i]; fmul cost; fadd cur.g; fstp` — one x87 chain,
    single narrowing. Reimpl computed float-by-float (rounds the product to 24
    bits first); numerically proven divergent (3718/2.6M sample mismatches).
    Now `(float)((double)stepMul * cost + g)`.
  - 0x43c395: `(_WORD)v28 == 0xFFFF` — the binary tests only the LOW WORD of
    the open-list head; reimpl compared the full int. Now `(u16)v28`.
- `VIBE_Path_ReverseParentChain` @0x43c594 — **VERIFIED-1:1**.
- `VIBE_Path_FindRoute` @0x43be20 — **VERIFIED-1:1** apart from the profile fix
  above (row-offset table {0,−s,0,+s,−s,−s,+s,+s}, generation stamp, flag bits
  5/9, heuristic weight forced to 7.0f (0x43c075 = 0x40E00000), impassable
  endpoint bail, meet-stitching all match). Unmodeled write-only globals noted:
  `word_765312` loop counter, `dword_765304`/`flt_76530C` (=0.25 per profile,
  0x62E75C/0x62E770 byte-verified; consumed outside this cluster).
- `VIBE_Path_BuildWaypointList` @0x43bd70 — **VERIFIED-1:1** (bounds check hits
  exactly goalX/goalY with the permissive `> size` compare; walk emits
  x=node&mask, y=(u16)node>>shift via +22 parent links; count-in-struct
  adaptation established).
- `VIBE_Path_ResamplePolyline` @0x406810 — **VERIFIED-1:1** (12.0/0.5 at
  0x6106E0/0x6106E4 byte-verified; ConvertX truncation; min-1 stride; final
  point append rule).
- Tables byte-verified: dx 0x62E5F0, stepMul 0x62E610 (√2 ≈ 1.41420996…),
  cost profiles 0x62E630.

## src/sim/pathfind_map.cpp
- `VIBE_ObjectSearch_FindByPaletteRange` @0x47abfc, `FindOneByPaletteRange`
  @0x47ae48, `FindPeopleByPalette` @0x47b008, `FindMatchingColors` @0x47a8dc,
  `FindMatchingColor` @0x47aad4 — **VERIFIED-1:1 (ADAPTED)**. Gates
  (`count>3`, `(min>0 || max<100) && min<max` with 100.0 at
  0x61AC00/0x61AC08/0x61AC10 byte-verified), stride table dword_478410
  (16 ints, byte-verified), 768-probe `%768` walk, person-type skip
  {8,7,5,9} at stride 536, rank |Δ|<5 gate, pinned dword_62EB8C fast path all
  match. Adaptations (established): RNG stride/start injected as params; the
  40-byte local filter record (zero-fill + 0xFF ref-chain + tag 2 + bit-27
  clear) is behind the eligibility hook. NOTE: the original's
  "SetGrayColorThunk" (0x5c6af0) is really a byte-broadcast memset onto that
  local record — the reimpl's `setGrayColor(0,40)/(255,8)` hook calls are inert
  stand-ins for those fills and must never be wired to a real light routine.
- `VIBE_Map_ComputeBuildingChainDepth` @0x592c7c — **FIXED**: 0x592caa jumps to
  `return 0` (0x592d6e) when the FIRST parent link is 0; the reimpl fell
  through to the second link. (The full 1:1 twin in world/road_network.cpp
  already had this right.)
- `VIBE_Map_ComputeRoadNetworkLayout` @0x592d98 — **PARTIAL (documented
  sketch)**. This file's version is the simplified model; the byte-exact
  reconstruction lives in `src/world/road_network.cpp` (verified there by an
  earlier wave). The sketch's shared steps (depth pass 0x592f4e, bubble sort
  0x592f96, level starts 0x593052, level-0 X 0x5930c1, per-level averaging
  0x593103/0x5931d3, Y rows 0x5935ab) verified; its missing passes (intra-level
  cost sort 0x59320f, same-cost spreading 0x593269, 80-px separation 0x5933b3,
  ConvertX normalization 0x593455) are owned by road_network.cpp — not
  duplicated here.
- `VIBE_Map_RasterizeBauplatzEdge` @0x5776d8 — **FIXED (2)**:
  - 0x577703/0x577739: the outer edge length squares+sum stay on the x87 stack
    and narrow ONCE — modeled with double intermediates.
  - 0x5777da..0x5777f8: the inner cross-segment length is fed to `fsqrt`
    WITHOUT a float spill — inner step count now computed on the unnarrowed
    double chain (2·trunc(sqrt(...) + 0.9)); bias 0.9 at 0x62562C
    byte-verified.
- `VIBE_Map_LoadCityFile` @0x528bd0 — **VERIFIED-1:1 (ADAPTED)**. Strings
  ("gamedata/cities", "%s/%s.NET"/".CTY", "scenes/*ChooseCity.ed3") verified in
  the binary; call order matches. Unmodeled: the qword_13CE852 date-header
  stamps (=0 /6 /0) between sprintf and WriteGameFile (global lives outside
  this slice; noted for its owner).
- `VIBE_Map_SpawnCityTowerMarker` @0x52e194 — **VERIFIED-1:1 (ADAPTED)**.
  Scales at 0x6231A0/0x6231A4 (≈1/700, 1/750) byte-verified; span/bias math
  matches; node flag stamps (+529/+530/+535/+536) behind the attach hook.
- `VIBE_Map_SpawnCityPointMarker` @0x52e2d0 — **FIXED**: FindByHandle receives
  kind **320** (0x52e327 `mov edx,140h`; the sprintf thunk 0x5cba00 preserves
  edx via push/pop at 0x5cba01/0x5cba1e); the reimpl passed 0.
- `MapBuildDummyName` (0x52e2eb..0x52e32c inline) — **VERIFIED-1:1**
  (uppercase + `"dummy_%s"`).
- `VIBE_Path_BuildMarkerPoints` @0x4074c0 — **DIVERGENT SKETCH — flagged, not
  fixed**. The binary has: a leading tag-8 anchor from rec+292, gates on
  rec+296 (+9==45, +244 buffer, +240 count, +248 start, +340==−1), tag-1/tag-2
  points, and a whole "elbow insertion" post-pass (0x4075e7..0x40764d) that
  appends two tag|4 points per L-corner. The reimpl models only a skeleton
  (wrong count field rec[61], no gates, no post-pass). It has NO in-tree
  callers and NO tests; a faithful re-reconstruction needs the route-record
  pointer model resolved (records hold raw 32-bit pointers) — left as a named
  gap per rule 8 rather than papered over.

## src/sim/person.cpp
- `VIBE_Person_IsValidActiveRecord` @0x4f8e60, `GetCashAmount` @0x58bc9c,
  `ComputePriceMultiplier` @0x58f71c (42.0f/0.25/1-252 verified),
  `ComputeWealthRank` @0x592ae8, `ComputeOfficeRank` @0x58bccc (incl. the
  double recursive call), `IsFamilyMemberEligible` @0x58c2f8 — all
  **VERIFIED-1:1**; every kPf offset checked against the symbol deltas.

## src/sim/person_create.cpp  — `VIBE_Person_CreateAndSpawn` @0x58da70
Deep re-verification of the no-parents (new-game) path. **FIXED (9)**:
1. **Player-mode stamp keys on KIND, not ownerWord** — 0x58dc42
   `mov bh,[ebp+2]` / 0x58dc69 `cmp bh,3` (then 16/19): `+0x1E4=1, +13=2` when
   the KIND is 3/16/19. Reimpl tested args.ownerWord. Test pin updated
   (person_create_w17_test: kind-6 create now expects +0x1E4==0).
2. **Gender-override leaves** — 0x58dd38 calls `VIBE_Building_IsTypeInGroup`
   (0x5898cc) for a7; 0x58e893 calls `VIBE_Building_ClassifyTypeFlag`
   (0x589818) for a6. Reimpl routed both through GroupFromCode. Both correct
   leaves already existed in building_type.cpp; now wired.
3. **Stat seed table is 8 groups × 28 floats** — 0x58ea91
   `movsx eax,bl; imul eax,0x70`: row = 28·GroupFromCode(...). The old 40-float
   window assumed the other groups were zero-filled; get_bytes @0x582900
   (896 B) shows all 8 groups populated. Full 224-float table transcribed and
   bit-verified (0 mismatches).
4. **Third stat write lands at +0x8C+12t** — 0x58eb1f `add esi,0Ch` precedes
   the `fstp [esi+80h]` (0x58eb25). Old code wrote +0x80+12t.
5. **Fitness +0x128 uses the unnarrowed product** — 0x58eb59 is `fst` (keeps
   st0), then ×0.001 → +0x128. Old code re-multiplied the narrowed float.
6. **Dynasty talent row = (i8)rec[0x165] (a7)** — 0x58ef39
   `mov eax,[ebp+162h]; sar eax,18h`. Old code used the a6 byte, which is 0 on
   every path that reaches the byte_649AD8 branch.
7. **Wappen dedup stores only when an unused value exists** — 0x58ed68
   `++v128 >= 1350 -> LABEL_188` skips the +0x54 store; old code wrote 1350.
8. **Newton refinement of +0x1C implemented** — 0x58e377..0x58e41f: when
   (u16)ownerWord(+0x0A) > +0x20, iterate `x = x − v152 + v152/x`
   trunc(target−12) times from +0x1C (all on the FPU; modeled in double,
   narrowed once), clamp via SIGNED bit-compare vs 0x3F800000. Old code was an
   empty stub. Test pin updated to the reference computation.
9. **+130 stat boost keys on the RECORD kind byte** — 0x58e442 reads rec[+2]
   (which the kind-16 avatar remap rewrote to 3), not the spawn-arg kind.
Verified byte-exact: kStat582900 (896 B), kByte649910/kByte649AD8 (48 B each),
all float/double constants 0x62687C..0x6268D4, %8/%2/%10/1536-draw relation
loop/%42+21/%149/%112/%191 draw order (RNG stream shape unchanged by fixes 1-9
except the +0x1C block which makes NO draws… note: fix 8 adds no draws; fix 9
changes WHEN the final %3 draw occurs only for kind-16 spawns).
Documented deferrals unchanged: two-parent path, avatar record copy (hook),
family table leaf, head-bone resolve.

## src/sim/person_lifecycle.cpp
- `VIBE_Util_StrCmpNoCase` @0x5cb8f0, `Person_FindByObjectRef` @0x586a40,
  `Person_CountActiveSlots` @0x587b60, `Person_CollectByType` @0x587b9c
  (signed-kind compare verified), `BuildingType_GetGuildRankPair` @0x589b40,
  `Person_ComputeBirthDate` @0x58be24, `Person_ComputeBirthDateFromRecord`
  @0x58bd84 — all **VERIFIED-1:1** (unsigned %12 div at 0x58be88 confirmed in
  disasm; PackToRecord @0x583304 model confirmed).

## src/sim/personnel.cpp
- `VIBE_Personnel_ComputeWageByCategory` @0x594d70 — **VERIFIED-1:1**
  (3.0/9.0 at 0x626B2C/0x626B30 byte-verified; cat 10/11/12 split; signed
  action byte; float narrowing).

## src/sim/personnel_recruit2.cpp
- `VIBE_Avatar_AllocSlot` @0x484598 — **VERIFIED-1:1** (+44/+48 overlapped
  float columns, 14×12-byte walk, 1/1000 at 0x61B0A8 byte-verified, wrap
  `(slot+1)&0x1F`).
- `VIBE_Avatar_Save` @0x484658 / `VIBE_Avatar_Load` @0x4846ec —
  **VERIFIED-1:1** (the 0x484735 pre-mark loop's `byte_62EF4A[i]` with the
  post-body update covers exactly the 32 flag bytes at +246 — checked
  address-by-address).
- `VIBE_Animal_SpawnDog` @0x483b58 (katze/kind 1!), `SpawnCat` @0x483bc4
  (hund/kind 0!), `SpawnSheep` @0x483c30, `SpawnCow` @0x483ca8 (1.5f =
  0x3FC00000 verified), `SpawnLivestock` @0x483d20 (kuh/pferd/schwein switch) —
  **VERIFIED-1:1 (ADAPTED)** placement/scene behind hooks.

## src/sim/person_personnel2.cpp
- `VIBE_Person_FindActiveByEntity` @0x5920b0 — **VERIFIED-1:1**.
- `VIBE_Person_FindNearestByDistance` @0x5949fc — **VERIFIED-1:1** (float
  running-min narrowing is value-neutral for the signed-byte distances).
- `VIBE_Person_FindEmploymentRelation` @0x58d95c — **FIXED**: the exhausted-
  scan guard at 0x58d9d6 is `768 − dword_647724 < 32 → return 1` (near-full →
  1, NO book scan); the book scan runs only with ≥32 free slots. The reimpl had
  the condition inverted. Also now reads the real `g_personLiveCount`
  (dword_647724 mirror) instead of counting markers.
- `VIBE_Person_ComputeAssetWorth` @0x591328 — **VERIFIED-1:1 (ADAPTED)**; noted
  caveat: the binary truncates `(int)(price + v9)` per item; the hook returns a
  pre-summed worth, so a hook backend must reproduce per-item truncation.
- `VIBE_Person_CheckDebtRatioCritical` @0x591ff0 — **VERIFIED-1:1**
  (0.2/0.07 at 0x626A64/0x626A6C byte-verified; float narrowings match).
- `VIBE_Person_SyncMasterShopObjects` @0x594c94 — **FIXED**: the binary
  iterates EVERY {op 0, 30} match (0x594ca7 for-loop with IterNext); the
  adaptation synced only the first. Now walks the real
  PersonQueryBegin/PersonIterNext pair; unit test rewritten over g_objects.
- `VIBE_Person_BeginQueryThenSetField270` @0x59630c — **VERIFIED-1:1**.
- `VIBE_Person_BeginQueryThenOpenBuilding` @0x59647c (+ byte-identical
  0x596514) — **FIXED**: 0x596487 `LODWORD(a1) = QueryBegin(...)` replaces the
  low byte, so a null query returns 0 — the reimpl returned the incoming 1.
  Test pin updated.
- `VIBE_Personnel_BuildBookRow` @0x53b5d0 / `DestroyBookRowWidgets` @0x53ba4c —
  **VERIFIED (ADAPTED)**: field stamps/coordinates (x−54, y−17 / y+68) match;
  the 12-dword sentinel region (+36..+80) is modeled by the struct's
  extraStart/extraEnd pair (documented adaptation); widget flag words
  (740-stride) behind the AddWidget hook.

## src/sim/person_query.cpp
- `VIBE_Person_QueryByGoodType` @0x5929f0 — **VERIFIED-1:1** (keys 15/23/24/
  25/26 at 0x592a0f..0x592a4f; signed `a1−1` switch semantics equal for u8).

## src/sim/person_record.cpp
- Eco-inputs adapter (0x578438 / 0x578634 reads) — **VERIFIED-1:1** (need byte
  at typeRec+583, employment word +39, 0xFFFF sentinel, 589 stride — all
  present in both consumers' decompiles).

## src/sim/person_relations.cpp
- `VIBE_Person_ResolveStatusFlags` @0x553ce8 — **VERIFIED-1:1** (flag values
  1596..1605; filters 111/24/69; +172/+176/+8/+196 matchers; output buckets
  100/350/650 at 0x6246EC byte-verified).
- `VIBE_Office_CollectFamilyHeirCandidates` @0x5555c8 — **VERIFIED-1:1**.
- `VIBE_Person_CollectRelatedNpcs` @0x554e34 — **FIXED**: 0x55504a/0x554ebf
  compare the kind byte as SIGNED char (`(char)+2 < 10`); the reimpl used u8
  (kinds ≥0x80 wrongly excluded). Relation-class mapping, household fallback,
  filter-65 extra, count-only pass otherwise verified. (The original's
  uninitialized-RecordById read when no filter-65 handler exists is UB; the
  null-default kept, documented.)
- `VIBE_Person_CollectTopByScore` @0x555150 — **VERIFIED-1:1** (bit-compare
  ≥0x42960000 ≡ float ≥75.0 for all inputs incl. NaN; insertion pass proven
  equivalent under the maintained descending invariant; ConvertX trunc of the
  kept score).

## src/sim/plant.cpp
- `VIBE_Plant_AdvanceGrowthStage` @0x56eba4, `EnsureModelsLoaded` @0x56ef2c,
  `HideAllModels` @0x56ef78 — **VERIFIED-1:1 (ADAPTED)** (24-byte records ×64;
  alive = (int)(rec+10)>>24 != −1; typeId = HIWORD(+8); model +20; max stage
  from typeRec+68 behind the hook; the retry-call artifact noted).

## src/sim/production_slots.cpp
- `VIBE_Inventory_IsWeaponSlotCompatible` @0x550298 — **VERIFIED-1:1**.
- `VIBE_Inventory_IsObjectSlotActive` @0x54f0ec / `IsProductionSlotMatch`
  @0x54f124 — **VERIFIED-1:1 (ADAPTED)**.
- `VIBE_Inventory_CollectProductionSlots` @0x5922d4 — **FIXED (3)**:
  - BOTH capacity branches read the ROOT node's level `[ebx+0Eh]`
    (0x59234d / 0x5923e1) — capacity is constant across slots; the reimpl used
    each slot's own level.
  - The iterated slots are the root's CHILDREN (QueryFind(v4[5],1,5) at
    0x59231d); the reimpl also counted the root as a slot. Adapter contract now:
    nodes[0] = root, nodes[1..] = slots.
  - dst+176 worth is an INT accumulated with a per-iteration ConvertX
    truncation (0x5923b1..0x5923b8), the level narrowed to float first
    (0x59239e fstp); the reimpl kept untruncated double.
  Unit/itest/e2e pins updated with the addresses. Unmodeled: dst[0]
  (root+28 byte) and dst+180 (root ptr) — noted for the future UI consumer.
- `VIBE_Inventory_GetSlotCapacity` @0x592474 — **VERIFIED-1:1** (kept table).
- `VIBE_Inventory_TickProductionTimers` @0x54f168 — **VERIFIED (ADAPTED)**
  per-order step (timer debit via DiffMinutes, restamp, active-clear, kind-6
  message, owner-turn gate with kind-7 exclusion) matches; the 32-slot window
  scan + slot-qty gating (+4 > 0) is the caller wrapper (documented).

## src/sim/projectile.cpp
- `ResolveBlastOnTarget` (inner body of 0x4866b8/0x486ce4) — **VERIFIED-1:1**;
  constants byte-verified: worth scale 0.01 (0x61B164/0x61B1AC), falloffs
  1/170 (0x61B16C) and 0.01f (0x61B1B4), radii 170.0f/100.0f (bit compares),
  fuse 350. **Comment corrected**: the spawned damage number is the
  distance-attenuated dmg — v29 is overwritten with (int)v17 at 0x487032
  before SpawnDamageNumber at 0x48705c (the old note claimed the unattenuated
  base; the CODE was already right).
- `UpdateBombExplosions` @0x4866b8 / `UpdateThrownBombs` @0x486ce4 /
  `RollProjectileDamage` (0x487760 excerpt) — **VERIFIED-1:1 (ADAPTED)**
  (audio/particle/terrain-crater side effects behind hooks, documented).

## src/sim/recruit_cost.cpp
- `VIBE_Recruit_ComputeRecruitmentCost` @0x55d674 — **FIXED**: the fee store is
  TRUNCATED — 0x55d84d calls ConvertX (0x5c6b08: `fldcw` with CW-hi 0x1F →
  RC=11, `frndint`, restore) right before the fistp at 0x55d852. The reimpl
  used std::lrint (round-to-nearest). Golden pin updated 7→6 (2.85 → 2, +4
  rep) with the addresses. Constants 5.0/100.0/0.1f/24.0/210.0/168.0 at
  0x624A44..0x624A64 byte-verified; peak/max branch order, rep-byte loop,
  debug-halve, clamps verified.
- `VIBE_Person_EvaluateCandidateEligibility` @0x5596f8 — **FIXED (2)**:
  - a3+5 bit1 (0x559c07/0x559739): when set and IsTargetUnderfull returns 0
    the original `return result` returns that **0** — the reimpl returned 1.
  - 0x559ac5: `*(char*)(a3+4) < 0` tests the sign of the LOW BYTE of flagsA
    (fA & 0x80) — the reimpl tested bit 31 of the dword.
  All other gates (exclude list, gender, unemployment, household, kind-1,
  profession, citizen/office, jail, class-set, favor window, rank-span
  `(fA & 0x7C00000)>>22`, citizen fallback) **VERIFIED-1:1**.

## src/sim/real_hooks.cpp / real_hooks2.cpp / real_hooks3.cpp /
## real_hooks3_apply2.cpp / real_hooks4.cpp / real_reaper_wiring.cpp
- **WIRING-ONLY** — no reconstructed logic bodies; adapters delegate to
  already-reconstructed builders/pools/arrays. Reviewed for drift; none. (The
  FNV tag-hash in real_hooks2 is a documented adapter shim, not a claimed 1:1
  function.)

---

## Test targets built & run (all green)
| target | checks |
|---|---|
| sim_path_test | 88 |
| sim_path_e2e_test | 119 |
| sim_path_query_test / _itest / _e2e | 49 / 20 / 22 |
| wire_npc_movement_e2e_test | 14 |
| pathfind_map_test / _e2e | 72 / 25 |
| sim_person_test | 61 |
| person_create_w17_test | 47 |
| person_create_harden_test | 1546 |
| newgame_builders_w13_test / newgame_apply_e2e_test | 48 / 23 |
| sim_command_apply5_test / _e2e | 92 / 132 |
| session_persons3d_test / _e2e / session_persons_render_e2e | 70 / 69 / 67 |
| app_wiring3_e2e_test / app_wiring4_e2e_test | 26 / 23 |
| sim_person_lifecycle_test / _e2e | 75 / 25 |
| sim_personnel_test / _e2e | 98 / 24 |
| personnel_recruit2_test / _itest / _e2e | 112 / 51 / 62 |
| person_personnel2_test / _itest / _e2e | 54 / 8 / 15 |
| wire_production_test | 20 |
| sim_person_relations_test / _itest / _e2e | 33 / 9 / 21 |
| court_council2_test / _e2e | 163 / 12 |
| vegetation_test | 27 |
| sim_production_slots_test / _itest / _e2e | 60 / 14 / 18 |
| sim_combat_battle_test / _e2e | 99 / 24 |
| person_reconcile_test / _e2e | 63 / 15 |
| object_value_test | 16 |
| sim_real_hooks_e2e_test / sim_real_hooks3_e2e_test | 17 / 22 |
| turn_driver_e2e_test | 21 |
| playable_flow_e2e_test | 154 |

## Updated golden pins (all proven against the binary, addresses above)
- person_create_w17_test: +0x1E4 keying (kind vs ownerWord, 0x58dc42/0x58dc69);
  +0x1C Newton refinement (0x58e377..0x58e41f).
- person_personnel2_test: OpenBuilding null-query returns 0 (0x596487);
  SyncMasterShopObjects iterates all matches (0x594ca7).
- sim_personnel_test GoldenFee: 7 → 6 (ConvertX truncation, 0x55d84d/0x5c6b08).
- sim_production_slots_{test,itest,e2e}: root-level capacities, children-only
  slot set, int-truncated worth (0x59234d/0x5923e1/0x59231d/0x5923b8).

## Open items flagged for the user / future waves
- `VIBE_Path_BuildMarkerPoints` @0x4074c0: existing sketch is structurally
  divergent (details above); needs a proper re-reconstruction with a pointer-
  bearing route-record model. No callers/tests today.
- pathfind_map's `setGrayColor` hook must never be wired to a real light
  routine (it models a local-record memset, 0x5c6af0).
- MapLoadCityFile's qword_13CE852 date-header stamps (0x528c1a..0x528c33) are
  unmodeled here (global owned elsewhere).
