# Wave-12 hardening — character + charaction cluster (W12-CHAR)

MCP was DOWN for this wave: no new 1:1 reconstruction. Scope was ASAN+UBSAN
hardening of the `src/sim/` character / charaction cluster — find & fix memory-safety
/ UB bugs, add boundary / malformed / degenerate-input tests, keep every golden value
byte-identical, keep the normal `build/` green.

Sanitizer build dir: `build-asan-char` (configured `-fsanitize=address,undefined
-fno-sanitize-recover=all`). Cleaned up at end of wave.

## Owned cluster

character_mesh, character_move, character_path, character_query, character_recon2_cmds,
character_recon4_avatar, character_recon4_flags, character_recon5_morph,
character_recon5_spawn, character_recon5_transport, character_recon_tavern,
character_render(*see note), character_social, character_state, character_universe,
avatar, charaction, charaction_brawl/misc/motion/steps(2-8)/walk + their tests.
(Excluded: character_factory = wave-11; charaction_npcaction_recon* = W12-AI;
character_render2-5 = render owner.)

## Bugs found & fixed

### 1. Stack/heap buffer over-read in the walk-on-path waypoint gap-skip
`src/sim/charaction_motion.cpp` — `WalkOnPathStep` (gilde.exe 0x40a4d4), the waypoint
gap-skip gate. The probe of the *next* waypoint pair dereferenced
`waypoints[2*next]` / `[2*next+1]` BEFORE the bound `next < waypointCap` was tested:

```cpp
u8* w = ch->waypoints + 2 * next;
if (ch->waypoints && w[0] == 0 && w[1] == 0 && next < ch->waypointCap) {  // BUG
```

C++ `&&` short-circuits left-to-right, so `w[0]`/`w[1]` were read even when
`next == waypointCap`. With `waypointIdx == 255` (the last of the 256 slots in the
512-byte buffer), `next == 256` → read at byte offset 512/513, one (col,row) pair past
the buffer. **Confirmed** with ASAN: pre-fix, `stack-buffer-overflow ... at offset 1024
overflows this variable 'ch'` at `charaction_motion.cpp:565`; post-fix clean.

FIX: reorder the condition so the bound is checked first (behaviour-identical on valid
in-range input, where every term is evaluated either way):
```cpp
if (ch->waypoints && next < ch->waypointCap && w[0] == 0 && w[1] == 0) {
```
Pinned by `tests/e2e/sim_charaction_motion_e2e_test.cpp` —
`SimMotionE2E.WaypointAdvanceAtCapacityNoOverread` (waypointIdx at slot 255).

### 2. Misaligned-reference UB on unaligned He-record / person-record fields
The original x86 binary reads He / person record fields with unaligned
`*(int*)(rec+N)` / `*(WORD*)(rec+N)` / `*(void**)(rec+N)` loads at byte offsets that
are NOT naturally aligned (e.g. +1, +2, +37, +39, +59, +82, +93, +169, +170, +380).
The C++ port mirrored these as `*reinterpret_cast<i32*>(HeBytes(x)+N)` etc. Binding an
`i32&` / `u16&` / `HeRecord**` reference to a misaligned address is UB; `-fsanitize=
undefined` traps with "reference binding / load / store to misaligned address".

FIX (everywhere): replace the misaligned reference forms with byte-exact, alignment-safe
`std::memcpy` load/store helpers (`LoadI32At` / `LoadU16At` / `StoreI32At` etc.). The
memcpy reads/writes the *identical* little-endian bytes the original did — **no value,
offset, or logic change**, golden outputs unchanged. Naturally-aligned accesses were
left untouched. Files fixed:

- `src/sim/charaction_misc.{h,cpp}` — `Gi_DwellCounter` (+82, i32) → `Gi_GetDwellCounter`
  / `Gi_SetDwellCounter` getter/setter pair; 3 call sites updated.
- `src/sim/charaction_steps3.cpp` — i32 reads @ +1, +2, +170. + test
  `tests/unit/charaction_steps3_test.cpp` (+1/+2/+170 pokes) and
  `tests/e2e/charaction_steps3_e2e_test.cpp` (+1 poke).
- `src/sim/charaction_steps5.cpp` — i32 @ +93, u16 @ +39, pointer @ +59 / +380. + test
  `tests/unit/charaction_steps5_test.cpp` (pointer @ +59/+380 stores).
- `src/sim/charaction_steps6.cpp` — u16 @ +39, i32 @ +93 / +39. + test (u16 @ +39 store).
- `src/sim/charaction_steps7.cpp` — many i32 @ +1/+2/+93, pointer @ +36/+59/+97. + test
  + itest (pointer @ +59 stores) + e2e (pointer @ +36/+59).
- `src/sim/charaction_steps8.{h,cpp}` — i32 @ +1/+2/+93, u16 @ +37, +82/+86/+90 clock
  triplet, pointer slots at 4-byte stride (32-bit layout) traps on a 64-bit host. The
  +169 header getter was already value-returning (memcpy body only). + test + itest +
  e2e (u16 @ +37 etc.).

All fixed test/itest/e2e targets run clean under ASAN+UBSAN with zero runtime errors.

## Boundary / degenerate-input tests added

- **motion** (`sim_charaction_motion_e2e_test`): waypoint advance at the 256-slot
  capacity (regression for bug #1).
- **avatar** (`character_recon4_avatar_test`): max in-range object/building type indices
  stay inside the reconstructed slot/name tables; registry lookup + find-or-alloc with a
  full table and an empty owned-id list (no over-read, returns null at the bound).
- **path** (`character_path_test`): `AllocSlotAtIndex` at the top of the 512-slot live
  table; `WaitSlotCallback` all-zero-template (no append, no queue overrun) + append at
  count; `PickWaitAnimation` empty collection (0) and max-count (32) index in bounds.
- **morph** (`character_recon5_morph_test`): `FadeOutSlots` with count==0 (no-op) and a
  live slot with a null character record (degenerate fade record handled, slot still
  cleared, no null deref).
- **transport** (`character_recon5_transport_test`): `MoveToUniverse` with an attached
  transport that has a bad/zero vehicle scene-object pointer (passes the null through the
  hook, completes without dereferencing it). (Null-char + zero-universe early returns were
  already covered.)
- **state/social scan** (`sim_character_core_test`): `ProcessFlaggedLocal` /
  `RefreshFlaggedLocal` reach an actor placed in the LAST live slot (511) without reading
  past the table. (`IsInWorkSeason` out-of-range season class was already covered.)

## Examined and found SAFE (no change)

- `character_query.cpp` — all entity scans are bounded by `kLiveCapacity` /
  `kUniverseSlotCount`; `CountByOwner*` guard the universe index; `IndexFromUniverse` /
  `FindFreeSlot` are bounded. Caller-supplied `poolStep0[0..nodeCount)` is by contract.
- `character_social.cpp` / `character_state.cpp` — loops use `kCharacterCapacity` /
  `kLiveCapacity`; `IsInWorkSeason` already guards [0,16). The endpoint null-derefs in
  `FadeOutSlots` are guarded by `obj`-non-null (and `obj` non-null ⟹ `ch` non-null).
- `character_recon_tavern.cpp` — `FindTavernTargetSlot`: `entry` is guaranteed non-null on
  every iteration by the pre-loop guard (line 42) and the loop-bottom guard (66-67); the
  scan is capped at 16. NOT a null-deref (an earlier audit pass mis-flagged it).
- `character_recon5_transport.cpp` / `character_recon5_spawn.cpp` — bounded; the
  spawn name-copy loops are byte-faithful into fixed struct fields.
- `charaction_steps2.h` `Cas2_ApptTime`(+82) / `he.h` `He_ApptTime`(+82) — these return
  `GameTime&`, but `GameTime` is `GUILD_PACKED` (alignment 1), so a reference at any byte
  offset is well-defined. NOT a UB site.

## BEHAVIORAL — needs MCP (documented, NOT changed)

These match the original binary's own envelope on degenerate input; "fixing" them would
change observable behaviour on valid input or requires the binary to confirm a bound:

- `character_path.cpp` `AllocSlotAtIndex(index)` indexes `g_live[index]`
  (gilde.exe 0x4022c8 `dword_66F0D4[index]`) with NO bound — identical to the original.
  Whether the save-file slot id is validated upstream needs MCP. Tested only at the valid
  top-of-table boundary (511).
- `character_recon4_avatar.cpp` `EnsureObjectAvatar`/`EnsureBuildingAvatar` index the
  type tables / name rows by the raw `objType`/`bldType` (gilde.exe 0x505074/0x505134)
  with no bound. The reconstruction sizes the tables for 256 types; the original's real
  table extents (dword_122DDAC / dword_13CE27C / dword_13CE294) need MCP to confirm
  whether a larger or guarded range applies. Tested at the max in-range type.
- `character_path.cpp` `PickWaitAnimation` / `WaitSlotCallback`
  (gilde.exe 0x406344 / 0x4062c0) index a fixed 32-int / 33-int buffer by a
  caller-supplied count; the original (`v5[32]` / `*(4*v6+a2)`) has the same fixed buffer
  and trusts count < cap. Degenerate counts over the cap are the engine's envelope.

## Test status (ASAN+UBSAN, build-asan-char — all 0 failures, 0 sanitizer hits)

motion(38/e2e 26) misc(60/e2e 23) morph(45) transport(33) spawn(20) avatar(39)
flags(27) tavern(30) cmds(111) path(555→567/e2e 11/itest 6) char(64) core(80→85)
move(47/e2e 33) brawl(61/e2e 21/itest 20) steps2(102/e2e 25) steps3(118/e2e 21)
steps4(86/e2e 12/itest 16) steps5(65→/e2e 21/itest 17) steps6(72/e2e 19/itest 8)
steps7(52/e2e 27/itest 12) steps8(77/e2e 20/itest 9) morph_blend_walk(20)
sim_character_e2e(26) sim_charaction_misc_e2e(23). Normal `build/` re-verified green.
