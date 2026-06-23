# Wave-12 hardening — cutscene + building-economy + console/cheat (W12-CUT)

MCP was DOWN: hardening only, no new 1:1 reconstruction. ASAN+UBSAN build of the
cluster's test targets (`build-asan-cut`, `-fsanitize=address,undefined
-fno-sanitize-recover=all`), added malformed/boundary tests, fixed every
OOB/UB found. Goldens kept byte-identical; valid-input paths unchanged.

## Cluster (owned source + tests)
- `src/sim/cutscene.{h,cpp}`, `cutscene_auction.*`, `cutscene_duel.*`,
  `cutscene_misc.*`, `cutscene_process.*`, `cutscene_wedding.*`
- `src/sim/building_create.*`, `building_lifecycle.*`, `building_stock.*`,
  `building_storage.*`, `building_upgrade.*`, `building_value.*`
- `src/sim/console_recon.*`, `cheat_recon.*`
- tests: `cheat_recon_test`, `console_recon_test`, `sim_building_test`,
  `sim_remaining_test`, `sim_building_lifecycle_test`, `sim_building_upgrade_test`,
  `sim_cutscene_process_test`, `sim_cutscene_types_test`, `cutscene_misc_test`,
  `cutscene_recon2_test`

NOT edited (other waves): building_type/buildingtype_recon/building_production,
buildingtype_callers, cutscene_misc2/3/4/5 (wave-2 fixups), building_create2.

## Bugs fixed (real OOB / UB — faithful, the original did not corrupt memory)

### 1. cheat_recon.cpp — global-buffer-overflow in the cheat-token scan
`Cheat_MatchToken` / `Cheat_ParseSetWeapons` compared `strlen(cheatString)` bytes
of the candidate token with `memcmp`. On a SHORT / NUL-less / overlong console
arg that reads past the token's storage. **Caught by ASAN on the EXISTING test**
(`Cheat_MatchToken("AUFRUHR")` read 21 bytes comparing against the earlier 21-char
entry "FERNHANDEL_RAUBRITTER" — read-of-size-16 over-read of an 8-byte token).
Fix: added `TokenHasPrefix(token, s)` — a byte-for-byte prefix walk that stops at
the token's NUL. Result is identical to `memcmp(token,s,strlen(s))==0` for valid
input (a token NUL meeting a non-NUL cheat byte is a mismatch, exactly as memcmp
computes) but never runs off the token. Applied to `Cheat_MatchToken` (also
null-guarded) and both `memcmp` sites in `Cheat_ParseSetWeapons` (sign + category).
Pinned by `CheatReconHarden.*` tests (tight heap buffers so ASAN red-zones any
over-read).

### 2. cutscene_process.cpp — out-of-range per-type table index
`CutsceneExecMainFunc` and `CutsceneProcessActive` (×2) indexed the 12-entry
`CutsceneTypeTable` with `slot->type` (a u8, 0..255). A malformed slot with
type ≥ 12 indexes past `std::array<…,12>` (OOB read of a stale function pointer,
then a call through it). Fix: `SlotTypeEntry(types, type)` returns null for
`type >= kCutsceneTypeCount`; an out-of-range type dispatches no main/step fn.
Valid types 0..11 are byte-identical. Pinned by
`CutsceneHarden.ExecMainFuncOutOfRangeType`.

### 3. cutscene.cpp / cutscene_process.cpp — participant-list over-read
`partCount` (+48) is a u8 (0..255); the participant id array `partIds[]` holds
exactly `kMaxParticipants` (16). `AddParticipant` enforces the bound on the happy
path, but a malformed slot (e.g. `AllocSlot` from a bad 276-byte template) could
carry `partCount > 16`, making the dedup/scan/remove loops read past `partIds[]`.
Fix: clamp the loop bound to `kMaxParticipants` in `CutsceneActorHasParticipant`,
`CutsceneTable::RemoveParticipant`, and the dedup scan of
`CutsceneTable::AddParticipant`. (`CutsceneParticipants::Init` already bounds its
outer loop to 16, so it was safe.) Pinned by `CutsceneHarden.*`.

### 4. building_value.cpp — out-of-range factor index + negative stat
- `Building_ComputeItemBaseValue` read `outputFactor[outIdx]`/`inputFactor[inIdx]`
  from the type table; those arrays are **2-element**. The big worth aggregator
  `BuildingValue_ComputeProductionWorth` (building_stock.cpp) sweeps output slots
  **0..5** / input slots 0..1, so out-of-range slots read past the 2-byte factor
  array (OOB). Fix: bound the index to `kFactorCount` (2); an out-of-range slot
  contributes 0. Valid 0..1 indices stay byte-identical. Pinned by
  `SimBuildingHarden.ItemBaseValueIndexBounds` /
  `SimRemainingHarden.ProductionWorthBadAndGoodType`.
- `Building_EvalProductionRating` only guarded `stat >= 5`; a negative stat would
  index `statLevel[-1]`. Added the lower bound (`stat < 0`); valid 0..4 unchanged.
  Pinned by `SimBuildingHarden.EvalProductionRatingStatBounds`.

  NOTE (BEHAVIORAL — needs MCP): the **0..5 output-slot loop bound** in
  `BuildingValue_ComputeProductionWorth` (gilde.exe 0x58fe68) vs the 2-wide factor
  table is a 1:1 question. The original may walk a different per-worker slot table,
  or the factor array may be wider than the 2 bytes the struct currently models.
  The memory-safety fix here only bounds the access; it does **not** decide the
  correct loop count. Re-examine 0x58fe68 / 0x58f328 + the BuildingTypeDef
  +553/+563 layout when MCP is back.

## Boundary / malformed tests added (all pass, ASAN+UBSAN clean)
- cheat_recon_test: NUL-less / empty / null / strict-prefix tokens; tight-buffer
  exact match for every cheat entry; short `-MINUS`/`-PLUS`/spawn/rename args.
- sim_cutscene_process_test: out-of-range slot type; oversized (255) partCount on
  actor scan / remove / add; 0- and full-16-participant slots; full/absent actor
  list (8-entry) add/remove bounds.
- sim_cutscene_types_test: 0 / >cap (50) bidders; empty auction; owner-unresolved
  abort; many-others marriage; unresolved-spouse wedding abort.
- sim_building_test: out-of-range output/input factor slots → 0; valid 0..1
  byte-identical; negative/over-large/null stat; unloaded type.
- sim_remaining_test: ComputeProductionWorth output sweep (slots 2..5 contribute
  0); null record; flagged-slots / sale-price / workstation count on unloaded type.
- sim_building_lifecycle_test: full unterminated 64-room list (i+1<64 boundary);
  unloaded type in RoomWorth; out-of-range `BuildingPersonAt` / RemoveAndCleanup /
  FindNearestSameType slot indices.
- sim_building_upgrade_test: unloaded/out-of-range type → at-max guard; upgrade
  cost on unloaded type.

## Documented for other owners (NOT fixed — out of cluster)

### cutscene_misc5_test.cpp — stack-use-after-return (TEST bug, wave-2 owner)
`build-asan-cut/cutscene_misc5_test` ABORTs under ASAN:
`stack-use-after-return` at `cutscene_misc5_test.cpp:233` (strcmp on
`g_rec.lastFile`). The `RecLoad` hook (line 215) stashes the `const char* f` it
receives, which points to `SceneLoadStadtScene`'s local `char buf[264]`
(cutscene_misc5.cpp:179); the test reads it AFTER the function returns. The source
function is correct (it only passes `buf` to the hook synchronously). FIX (for the
cutscene_misc5/wave-2 owner): in the test, `memcpy`/`std::string`-copy the filename
inside `RecLoad` instead of saving the raw pointer. Not previously seen because
misc5 was only built without sanitizers. Not in this wave's edit set.

## Build/run
- ASAN+UBSAN dir `build-asan-cut` (cleaned at end). All 10 owned test targets:
  0 failures, 0 sanitizer hits.
- Normal `build/`: all 10 owned targets rebuilt and pass (0 failures).
