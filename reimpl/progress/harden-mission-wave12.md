# Wave-12 hardening — world mission/event/location/tutorial cluster

MCP DOWN: hardening only (no new 1:1 reconstruction). ASAN+UBSAN build of the
cluster's test targets; added boundary / malformed / truncated tests; fixed one
genuine OOB read. Goldens kept byte-identical; valid-input paths unchanged.

## Build / sanitizer setup
- Build dir: `build-asan-mission` (`-DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"`).
- Cleaned up at the end of the wave.
- Normal `build/` kept green for every owned target.

## Owned cluster
`src/world/`: mission*.{h,cpp}, event*.{h,cpp} (event, event2..5, event_bindings,
event_effects, event_fire), location*.{h,cpp}, tutorial*.{h,cpp}, mood_religion.{h,cpp}.

## FIX — genuine OOB read (faithful)
### `src/world/mood_religion.cpp` — `ParseSetReligion` (VIBE_Cheat_ParseSetReligion 0x4fbf70)
The religion-name match used `std::memcmp(p, kReligionNames[i], len)` where
`len` is `strlen("KATHOLISCH")`=10 / `strlen("EVANGELISCH")`=11 and `p` points
into the (NUL-terminated) cheat command. A command shorter than the name (e.g.
`"-KAT"`) makes `memcmp` read a fixed `len` bytes **past the command's NUL** —
an out-of-bounds read on a malformed/truncated command.
- **Fix:** `std::strncmp(p, kReligionNames[i], len)`. `strncmp` stops at a NUL in
  either string, so it never over-reads. For valid input a match still requires
  the first `len` bytes to be identical and the command to continue past them —
  **byte-identical observable behavior**; only the degenerate over-read is removed.
  (This is the prefix-compare the original performs against a NUL-terminated arg.)
- **Pinned by:** `tests/unit/mood_religion_test.cpp ::
  MoodReligionUnit.ParseSetReligionTruncatedNameNoOverread` (prefixes of every
  name, an empty string, and a heap-allocated 5-byte buffer that ASAN would flag
  on a fixed-size memcmp).

## Tests added (boundary / malformed / truncated / empty)
- **`tests/unit/mood_religion_test.cpp`** — `ParseSetReligionTruncatedNameNoOverread`
  (short/empty/heap-short commands; reject behavior unchanged).
- **`tests/unit/world_mission_save_test.cpp`**
  - `MissionSave.LoadFailsOnNullStream` — save/load on a `{nullptr,0}` stream.
  - `MissionSave.LoadFailsOnTruncatedMidSlot` — buffer cut mid-slot aborts cleanly.
  - `MissionSave.LoadExactSizeSucceeds` — exact `1 + 128*28` consumes the whole stream.
  - `MissionRequirement.AdvanceRejectsOutOfRangeSlot` — `MissionRequirementAdvance`
    rejects negative / `==count` / far-past indices before any slot access.
  - `MissionSlot.RegisterFullTableReturnsMinusOne` — fill all 128 slots, next
    register returns -1 (no write past the table).
- **`tests/unit/world_event_bindings_test.cpp`**
  - `RegisterIdPastTableRangeIsNoOp` — event id `>= kEventSlotCount` (7) is a no-op
    (the guard the reconstruction added; the original would index past 7 slots).
  - `ByteReaderPastEndIsSafe` — `ReadDword`/`ReadString` past `size` zero/empty-fill
    without OOB (incl. a `{nullptr,0}` reader).
  - `LoadTruncatedAfterCountRegistersNothing` / `LoadCountLargerThanPairs` — a
    count larger than the payload reads empty pairs (id 0 = NONE) and installs only
    real pairs; never reads past the buffer.
- **`tests/unit/location_test.cpp`**
  - `LocationFsmBoundary.DispatchEmptyAndOutOfRange` — `ContactDispatch` over an
    empty menu and with clicked slot 0 / past-range / negative / huge -> -1.
  - `LocationFsmBoundary.DispatchShortRegistration` — registration shorter than the
    item list dispatches only within both bounds.
  - `LocationFsmBoundary.ClassifyUnknownTypesDefaultIdle` — `ClassifyLocationKind`
    on 0 / 255 / -1 / huge object-type bytes -> Idle (switch default; no table OOB).
- **`tests/unit/world_tutorial_core_test.cpp` (new file)** — first dedicated unit
  test for `src/world/tutorial.cpp`:
  - `FormResourceBoundsAllPositions` / `FormResourceEndSentinelAndOutOfRange` —
    `TutorialFormResource` resolves 0..3 and returns null for the `==4` end sentinel
    and any out-of-range byte (would index past `kFormResource[4]`).
  - `ClassifyStepIndexOutOfRangeEndsChapter` — `stepIndex == count` and far past
    take the end path without reading the step array.
  - `ClassifyNullStepsArrayEnds` — non-zero `stepCount` with a null `steps` array
    still ends (no deref).
  - plus null-chapter / zero-steps / reuse-rebuild-end / commit / chain-length.

## BEHAVIORAL — needs MCP (NOT changed; envelope or cross-cluster)
1. **`event3.cpp` / `event4.cpp` — `season = day % 4`** (VIBE_GameTime_GetSeasonFromDay
   0x58339c) indexing `kSeasonAnimBase[4]` / `kMorningHour[4]` / `kEveningHour[4]`.
   `i32 day` is signed; a negative day yields a negative season -> OOB index. The
   original record's `day` is also signed and the original does the same modulo, so
   for valid (non-negative) days this is byte-identical and never OOB. Whether the
   original masks (`& 3`) or uses signed `idiv` (negative remainder) is an
   observable difference on a corrupt/negative day that needs the disasm at
   0x58339c to settle — **do not "fix" `% 4` -> `& 3` without MCP** (it would change
   output). In-engine `day` is always >= 0, so this is the engine's envelope.

2. **He-record misaligned access (UBSAN alignment) — ROOTS IN `sim/he.h` (SIM CLUSTER,
   NOT OWNED).** Under `-fsanitize=alignment`, `event3_test` / `event4_test` /
   `event5_test` abort with *"reference binding to misaligned address ... requires 4
   byte alignment"*. `HeRecord` is `GUILD_PACKED` (alignment 1) and the He accessors
   return references to unaligned dwords/words at byte offsets (e.g. `He_ApptTime`
   @+82, dword reads @+82/+170/+178). This is UB per the C++ standard but harmless on
   x86 and faithful to the original's byte-addressed record reads. The accessor
   infrastructure (`HeBytes`, `He_*` inline reference-returning getters, the packed
   `HeRecord` layout) lives in **`src/sim/he.h`**, owned by the sim cluster; the
   event3/4/5 file-local `Dword/Word/Time` helpers mirror that same sim pattern and
   also flow through `He_ApptTime`/`He_*`. A correct fix is alignment-safe
   memcpy-based accessors (as `mission_requirement*.cpp` already use via
   `ReadDwordAt`/`ReadWordAt`), but it must be applied at the `sim/he.h` source of
   truth to be consistent and ODR-clean. **Documented for the SIM cluster owner**;
   not changed here per ownership rules (no ASAN heap/stack OOB — alignment-only).

## Audited and found already safe (no change)
- `mission.cpp` — `MissionSlotRegister` / `MissionFindBySource` / `MissionRequirementAdvance`
  all bound `[0, kMissionSlotCount)`; full-table -> -1.
- `mission_save.cpp` — `StreamWrite`/`StreamRead` check `data` and `pos+n > size`.
- `event.cpp` — `EventTableCountByCategory` rejects category > 5; `EventPickRandomByCategory`
  / `EventTableFindByCategory` bound on `g_eventTableCount`; `EventTableLoadDefault`
  memcpy is 1152 bytes into a 64-record (1536-byte) capacity.
- `event_bindings.cpp` — `RegisterEvent` bounds the id `< kEventSlotCount`; `StrNCopyPad`
  writes exactly `max <= 127` into a 128-byte field; `ByteReader` clamps on `pos < size`.
- `event_fire.cpp` — spawn writes bound on `spawned < kFireMaxSpawns`.
- `event2.cpp` — `ReadHelpStep` / `ReadAdviceId` bound-check before indexing the
  host-supplied tables.
- `event3.cpp` `GatherTargetsInit` — `candidateIds[pick]`/`[cur]` bound by
  `candidateCount`; season tables indexed by `day % 4` (see BEHAVIORAL #1).
- `location.cpp` / `location3.cpp` — vector/array accesses bound by `.size()` /
  `kMaxSlots` / `kSelectionSlots`.
- `mission_requirement*.cpp`, `mission_recon3_evaluate.cpp`, `mission_dialog.cpp`,
  `mission_rules.cpp`, `mission_reward.cpp`, `mission_member.cpp` — hook-gated or
  count-bounded; the 49-case `MissionReqEvaluate` switch falls to `default -> false`
  for any out-of-range type. The `byte_12CE912[536*owner]` read in
  `MissionReqCountGuildMembers` (only `0xFFFF` excluded) is the original's own
  unbounded person-index read — engine envelope, matches the binary; documented.
- `tutorial*.cpp`, `mood_religion.cpp` (post-fix) — bounds/default-safe.

## Sanitizer status (cluster test targets)
PASS under ASAN+UBSAN: world_mission_save_test, mission_requirement_test,
mission_recon3_evaluate_test, mission_requirement_event_recon_test,
world_location_test, location_test, location3_test, location4_test,
world_event_bindings_test, event2_test, mood_religion_test, tutorial_mission_test,
world_tutorial_chapters345_test, world_tutorial_core_test (new).

FAIL under UBSAN alignment only (sim/he.h-rooted, documented above; PASS in the
normal build): event3_test, event4_test, event5_test.
