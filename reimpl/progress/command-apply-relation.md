# Opcode-27 relation apply — the new-game relationship packets land

Closes the named gap from `progress/newgame-apply.md`: the six
`QueueRequestCoord27(a, b, 127)` packets the new-game commit
(`VIBE_Command_EnqueueInheritanceTransfer @0x5336f0`, `0x533930..0x5339ae`)
enqueues now genuinely apply. Landed 2026-06-11.

## The original

- **Dispatch:** jump table `funcs_4941F4 @0x631298`, slot 27 (verified by
  `get_int @0x631304` → `0x49818C`; sanity-checked against slot 25 →
  `0x498024` ExPatchObjectBitfield and slot 26 → `0x498104`
  ExAddObjectFloatField).
- **Handler:** `0x49818C` — IDA auto-name `VIBE_Command_ExComputeObjectCoords`
  (a misnomer: it is the relation-matrix mutation). `__usercall`, packet base
  in EAX, ack byte ptr in EDX.
- **Builder:** `VIBE_Command_QueueRequestCoord27 @0x494878` — a1@+0x10,
  a2@+0x14, a3(delta)@+0x18, **mode = ECX (4th register arg) @+0x1C**, the
  staged float `dword_62EB94` @+0x20, `trunc(flt_62EB90)` (band) @+0x24. All
  six new-game call sites do `xor ecx,ecx` + `mov ebx,7Fh` → **mode 0,
  delta 127**.

### Recovered semantics (payload offsets from packet base)

| field | offset | meaning |
|---|---|---|
| idA | +0x10 | row person id (modes 0/1/2); -2/-3/-4 remap, written back |
| idB | +0x14 | column person id (all targeted modes); remap, written back |
| delta | +0x18 | signed dword added to grid A |
| mode | +0x1C | 0/1/2/3/4 (others: resolve + ack, NO mutation — `0x498640`) |
| float | +0x20 | mode-3 scale factor |
| band | +0x24 | mode-4 decay band 0..4 (unsigned >=4 == band 4) |

Two 768x768 signed-byte grids, row stride 768 **bytes**, cell read as the
signed high byte of the dword at base-3 + 768*i + j:

- grid A @`0x123D6D0` (decompiles as `dword_123D6CD >> 24`) — primary attitude;
- grid B @`byte_1333110` (alias `unk_133310D >> 24`) — secondary/mood.

Person columns are the **live Person array itself** (stride 536):
id `dword_12CE914[134*i]` == record+4, marker `word_12CE910[268*i]` ==
record+0 (-1 free), mode-3 exclusion key `dword_12CEB1C[134*i]` ==
record+0x20C. Lookup = first id match (markers NOT consulted during the scan),
then the marker gates (a dead slot earlier in the table shadows a live one).

Modes: **0** A[a][b] += delta, clamp [-127,127]. **1** same, then
B[a][b] += (delta < 0 ? clampedA : clampedA/2), clamp. **2** B[a][b] = 0,
then the mode-0 add. **3** for every row k whose person id != target's +0x20C
key: B[k][b] = (i8)trunc(B[k][b] * float) — `VIBE_Coord_ConvertX @0x5c6b08`
is frndint under control word high byte 0x1F → RC=3 (chop), i.e. trunc toward
zero. **4** global decay over grid A: per-band (threshold, lowBound,
highDelta, lowDelta) = band 0 {100,-75,-4,+11}, 1 {85,-85,-8,+9},
2 {70,-95,-12,+7}, 3 {55,-105,-16,+5}, >=4 {40,-115,-20,+3}; every
off-diagonal cell with both persons live: v > threshold → +highDelta, else
v < lowBound → +lowDelta; clamp + store unconditionally.

## What changed in the reimpl

The handler already existed (`sim::ExComputeObjectCoords`,
`src/sim/command_apply6.{h,cpp}`, registered at opcode 0x1B by
`RegisterApplyHandlers6`) but had two divergences, both fixed:

1. **Live person table.** It resolved persons in modeled `RelationState`
   columns (`personId`/`aliveMarker`/`slotId`) that nothing populated. Those
   columns are now removed; the handler scans `sim::g_persons` directly
   (id @+4, marker @+0, slot key @+0x20C — `kRelPersonSlotIdOff`), the same
   records the batch-5 create-person handlers fill. `RelationState` keeps only
   the two grids (their backing globals are separate from the person array).
2. **Unknown-mode no-op.** Modes outside 0..4 used to fall into the mode-0
   mutation; the original (`0x498640 test ebp,ebp; jnz`) resolves both
   persons, stamps the ack and returns 0 **without touching either grid**.

### Wiring (rule 13)

- `src/app/wiring.cpp` (command-system init): `RegisterApplyHandlers6` is now
  composed onto the owned queue next to `RegisterApplyHandlers3` — in the
  binary this is one static 96-entry table, and the batch registries are
  disjoint.
- `ApplyNewGameParams` (src/play, other owner) registers batches 2/5 itself on
  whatever queue the host passes; with the host queue carrying batch 6 the six
  relation packets dispatch to the real handler. Both newgame test harnesses
  mirror the host-side registration.

## Tests

- `tests/unit/sim_command_apply6_test.cpp` — relation tests reseeded onto the
  live `g_persons` table; **4 new tests**: `RelationFirstIdMatchWinsEvenWhenDead`
  (the marker-after-scan gate), `RelationMode3ScalesSecondaryColumn` (golden
  trunc-toward-zero vectors -7*0.5→-3, 9*0.5→4 + the +0x20C exclusion),
  `RelationUnknownModeAcksWithoutMutating` (the 0x498640 fall-through),
  `RelationNewGameSixPairImage` (six mode-0/127 packets → all six directed
  cells 127, grid B and diagonal untouched). Suite: 110 checks, 0 failures.
- `tests/unit/newgame_apply_test.cpp` — `RelationPacketsApplyThroughBatch6`
  (full commit through the real queue + batch 6 → the six A-cells at the
  records' slot indices == 127) and `RelationPacketsNoOpWithoutBatch6` (the
  unset slot stays a safe no-op). Suite: 118 checks, 0 failures.
- `tests/e2e/sim_command_apply6_e2e_test.cpp` — mixed-stream determinism
  reseeded onto `g_persons`. 115 checks, 0 failures.
- `tests/e2e/newgame_apply_e2e_test.cpp` (guarded on the real install) — after
  the real `AUGSBURG.cty` load + commit, the six relation cells over the live
  populated array are 127, grid B clean. 23 checks (+7), 0 failures.

Full suite after the change: 1392/1394 — the two failures
(`session_camera_test`, `shape_convert16_test`) are other agents' in-flight
modules (camera_recon modified / shape_convert16 untracked-new in the same
working tree), unrelated to this change.

## Named gaps / notes

- ~~`world/relation.h` (`RelationGet @0x5942fc`) models the SAME grid-A global
  `dword_123D6CD` with a 192-**byte** row stride and a separate 9 KB backing
  store~~ — **CLOSED in fixups wave 2** (see `progress/fixups-wave2.md`):
  `world/relation.{h,cpp}` now uses the correct 768-bytes/row x 768-row
  geometry (`dword_123D6CD[192*a]` is a DWORD index = 768 bytes/row; the
  `>> 24` high byte is the signed byte at `0x123D6D0 + 768*a + b`), and
  `sim::RelationState::matrixA` ALIASES `world::g_relationMatrix` — one
  backing store for the one original global.
- Grid-A/grid-B contents are process-state only (the cold IDB images are
  zero); save/load serialization of the grids is owned by the save cluster.
