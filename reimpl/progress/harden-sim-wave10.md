# Wave-10 hardening — W10-SIM cluster (NPC daily / animals / persons3d / building smoke)

MCP was DOWN for this wave: NO new 1:1 reconstruction. This is a memory-safety +
degenerate-edge-case pass over the waves 6–9 reconstructions in the SIM cluster.
All fixes are FAITHFUL (the original did not corrupt memory either); the in-bounds
/ real-input path stays byte-identical and all prior golden values are unchanged.

## Cluster (owned source + tests)

Source:
- `src/sim/npc_clip_select.{h,cpp}` — clip selector (VIBE_Character_Update core)
- `src/sim/animal.{h,cpp}` — ambient-animal pool + spawn/despawn (0x4835c0..0x48364c)
- `src/sim/animal_wander.{h,cpp}` — wander/herd/door/AI + model table (0x4839f0..0x484468)
- `src/sim/npc_daily.{h,cpp}` — daily-routine director (VIBE_NpcAction_DailyRoutineStep 0x4e7e88)
- `src/play/session_npc_daily.{h,cpp}` — session director bridge (SessionNpcDailyAssign)
- `src/play/session_persons3d.{h,cpp}` — person bind/update/rebind bridge
- `src/render/building_fx.{h,cpp}` — chimney smoke (0x4b60a0 / 0x504910)

Tests:
- `tests/unit/npc_clip_select_test.cpp`
- `tests/unit/sim_animal_wander_test.cpp`
- `tests/integration/sim_animal_wander_itest.cpp`
- `tests/unit/sim_personnel_test.cpp` (animal pool/wander parts)
- `tests/unit/sim_npc_daily_test.cpp` (npc_daily rule + director)
- `tests/unit/session_living_city_test.cpp` (SessionNpcDailyAssign)
- `tests/unit/session_persons3d_test.cpp`
- `tests/unit/building_fx_test.cpp`

(`src/play/sdl_session.*` is a bind site — NOT edited. `src/render/building_fx`
is render but is in this cluster per the brief.)

## Memory-safety / UB bugs found and FIXED

### 1. building_fx season index OOB on a negative `day` — `src/render/building_fx.cpp`
`SpawnChimneySmoke` computed `season = (u8)(H->gameTimeDay() % 4)` and indexed the
4-entry season tables `kSmokeStartHour[season]` / `kSmokeEndHour[season]`. C++ `%`
yields a NEGATIVE remainder for a negative `day`; `(u8)(-1 % 4) == (u8)(-1) == 255`,
so the index became 255 → global-buffer-overflow read (caught by ASAN).
FIX: `const u8 season = (u8)(H->gameTimeDay() % 4) & 3u;` — for the engine's real
`day >= 0` the value is already in {0,1,2,3} and `& 3` is a no-op (in-bounds path
byte-identical). Pinned by `BuildingFx.NegativeDaySeasonNoOOB`.

### 2. npc_daily season index OOB — `src/sim/npc_daily.cpp`
Same class of bug in two places:
- `NpcDaily_DailyRoutineStep`: `season = SeasonFromDay(clk.day)` (== `day % 4`)
  then indexes `kWorkStartHour[season]` / `kWorkEndHour[season]`. Masked the LOCAL
  index: `const int season = SeasonFromDay(clk.day) & 3;`. (`SeasonFromDay` itself
  is unchanged — its `day % 4` contract is shared by other modules.)
- `SelectDailyActivity(int state, int season, ...)`: the `season` parameter is
  documented as GetSeasonFromDay output (0..3) but indexes the 4-entry tables
  directly. Added `const int s = season & 3;` and use `s` for the two table reads.
  For valid 0..3 inputs this is a no-op. Pinned by `NpcDaily.RuleSeasonWraparoundNoOOB`.

### 3. Animal_CollectSpawnBuilding cap write OOB — `src/sim/animal_wander.cpp`
`records[]` holds 32 slots; the original appends then returns `count < 32`, so the
WalkAndInvoke caller stops the moment count hits 32 and the in-bounds path never
writes past index 31. A DIRECT call with `count` already at 32 (a degenerate / test
input) wrote `records[32]` → OOB. FIX: guard the write+increment with
`if (n >= 0 && n < 32)`. The reachable path (count 0..31) is unchanged. Pinned by
`SimAnimal.CollectSpawnBuildingCapNoOverflow`.

### 4. Animal_HerdGroupFrom out-buffer size mismatch — itest + header doc
`HerdGroupFrom` stores a 3-float triple while `(group < 16)`, so the final triple
can land at `group == 15` and write indices 15,16,17 — i.e. it needs an 18-float
out-buffer, not 16. The engine path passes `nullptr` (no write) or the large
AnimalRec scratch, so the real code never overflowed; only the integration test's
`float pts[16]` was undersized (a latent OOB if a full 6-candidate group ever filled
it). FIX: `tests/integration/sim_animal_wander_itest.cpp` buffer enlarged to
`pts[18]`; the requirement is now documented in `animal_wander.h` above
`Animal_HerdGroupFrom`. No source-logic change (the returned `group` count is
byte-identical).

## Things checked and found SAFE (no change needed)

- `Animal_Update` lifetime decay: `denom = g_gameTick - threshold` is only reached
  under `threshold < gameTick`, so `denom >= 1` — no divide-by-zero.
- Pool entries (`Animal_AllocPool/AllocSlot/FreeSlot/Update`) guard `!g_animalPool`
  and null records; the round-robin cursor wraps with `% kAnimalPoolCapacity`.
- `Animal_LoadModels` guards `g_animalModelCount < kAnimalModelCapacity` (16 cap,
  7 loaded).
- `npc_clip_select`: pure switch selector, no array indexing; `ActionClipName`
  always returns a valid C-string (default `""`), so `name[0]` is never a bad deref.
- `session_npc_daily` providers all bounds-check `i` against `kPersonCapacity` and
  `ownerSlot`/`rec->alive` against their capacities before indexing.
- `session_persons3d` routes entirely through the public CityView3D API (no raw
  indexing); `SessionNpcDailyAssign` guards `!hm || hm->size <= 0`.

## Degenerate / edge tests ADDED (all green under ASAN+UBSAN)

- npc_clip_select: invalid/negative/huge action types (state moving/idle/sit/action
  + invalid) → None, never null deref (`InvalidActionTypeSelectsNothing`,
  `InvalidActionTypeWithMotionStaysNone`).
- animals (sim_personnel_test): null-pool ops safe; Animal_Update over an empty pool
  (cursor walk + wrap); extreme/negative season (no OOB, winter-vs-non-winter);
  despawn-during-iterate clears the slot; CollectSpawnBuilding at the 32-cap.
- npc_daily (sim_npc_daily_test): director with 0 persons (and null personCount);
  persons missing home/work/dest columns (data-absent path); rule hour boundaries
  across all 4 seasons; season wraparound / out-of-range index.
- building_fx: no matched effect slot → no spawn; negative-day season → no OOB.
- session_persons3d: zero persons wire/update/rebind/unwire; max persons (full
  kPersonCapacity bind + cap); a no-anchor person stays unplaced.
- session_living_city: SessionNpcDailyAssign with a null and a zero-size heightmap.

## Build / test status

- ASAN+UBSAN build (`-fsanitize=address,undefined -fno-sanitize-recover=all`):
  every owned unit + integration target builds clean and PASSES with zero ASAN/UBSAN
  reports:
  npc_clip_select_test (96), sim_animal_wander_test (57), sim_personnel_test (98),
  session_living_city_test (75), session_persons3d_test (70), building_fx_test (56),
  sim_npc_daily_test (88), sim_animal_wander_itest. Asset-free e2e
  (sim_animal_wander_e2e_test, sim_npc_daily_e2e_test) also pass clean.
- Normal (non-ASAN) `build/`: all owned targets build green and pass.

## Out-of-scope leaks observed (NOT this cluster — left for the owning clusters)

Two asset-driven e2e tests report LeakSanitizer leaks, but every leak frame is in
NON-owned io/compress/render-binding modules, never in this cluster's source:
- `session_persons3d_e2e_test`: leaks in `io/zip_archive.cpp:437`
  (`ExtractCurrentFile`), `compress/inflate.cpp` (`Inflater::NewBlocks`/`InflateRaw`),
  `render/texture_bin.cpp:141` (`TextureBin::Decode`),
  `play/real_texture_source.cpp` (`DecodeAndPalettize`/`BuildTableFor`) — reached via
  CityView3D::bindFor scene-walk texture decode.
- `session_persons_render_e2e_test`: leaks in `compress/inflate.cpp:935`
  (`InflateBlocks`) via `io/vfs.cpp` / `io/save_world_load.cpp` (`LoadWorld`).

These predate this wave and are owned by the io/compress/texture clusters. Flagged
here for the owning agents; not fixed (ownership rule).

## BEHAVIORAL ambiguities to confirm with MCP (none blocking)

None. All fixes are pure bounds/index guards on degenerate inputs the engine's real
(day >= 0, in-window, count < cap) path never produced; the observable output for
every real input is unchanged. No 1:1 question was deferred.
