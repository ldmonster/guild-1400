# Wave-13 1:1 audit — the NEW-GAME COMMIT segment

MCP DOWN: this is the in-tree (MCP-free) half of the 1:1 comparison —
cross-check each reconstructed function on the new-game flow against its
provenance comments + `progress/newgame-apply.md`, ensure every recovered 1:1
value is GOLDEN-PINNED, flag drift, and produce a confidence map.

Owner segment: `src/play/newgame_apply.{h,cpp}`, `src/sim/person_create.{h,cpp}`,
the packet builders the commit uses, and the apply handlers ExCreatePersonA/B.

## 1. Inventory (function -> provenance address -> confidence)

| function | gilde.exe | file | confidence |
|---|---|---|---|
| `play::ApplyNewGameParams` | `0x5336f0` (+ caller block `0x533b9d..0x533f8f`) | newgame_apply.cpp | GOLDEN-PINNED |
| `play::NewGameProfessionTalents` | `0x52d9ef..0x52da0e` | newgame_apply.cpp | GOLDEN-PINNED |
| `sim::EnqueueTradeRequest` (opcode 12) | `0x494548` | command_apply7.cpp | GOLDEN-PINNED |
| `sim::EnqueueObjectInteraction` (opcode 11) | `0x4944f0` | command_codec.cpp | GOLDEN-PINNED (**added W13**) |
| `sim::QueueRequestCoord27` (opcode 27) | `0x494878` | command_codec.cpp | GOLDEN-PINNED (**added W13**) |
| `sim::EnqueueCmd15` (opcode 15) | `0x494604` | command_inherit.cpp | GOLDEN-PINNED |
| `sim::QueueRequestFlagBlob32` (opcode 32) | `0x494ab4` | command_apply7.cpp | GOLDEN-PINNED |
| `sim::ExCreatePersonA` (opcode 0x0B) | `0x496614` | command_apply5.cpp | GOLDEN-PINNED |
| `sim::ExCreatePersonB` (opcode 0x0C) | `0x496714` | command_apply5.cpp | GOLDEN-PINNED |
| `sim::Person_CreateAndSpawn` (default backend) | `0x58da70` | person_create.cpp | GOLDEN-PINNED (**stamps added W13**) |

No function in the segment is missing a provenance header address — **no red flags.**

## 2. 1:1 values pinned (all trace to source/`newgame-apply.md`, none invented)

Pre-existing (verified intact, byte-identical):
- **Talent bytes** (`tests/unit/newgame_apply_test.cpp`): TypeRecordA variant 1 ->
  `69 69 BD 93 69`, variant 2 -> `3F 3F 93 69 3F`, variant 0 / out-of-range ->
  all-zero (record-0 fallback). Matches `get_bytes @0x649916`.
- **Start-gold formula**: `1250 - 250*difficulty` (difficulty 2 -> 750), cheat ->
  75000, network -> skipped. Scan filter alive +8 != 0 AND kind in {6,7}.
- **Six Coord27 relation pairs** (P,F)(F,P)(P,M)(M,P)(F,M)(M,F), delta 127, mode 0;
  saturate grid A to 127, grid B untouched (apply through batch 6).
- **Record offsets**: name +0x30, gender +9, faith +12, wappen +0x54 (id 1342+i),
  variant +356, family name (father) +0x40, family word +0x50, family head +0x68,
  spouse +0x5C, parent links +0x60/+0x64, portrait +0x18C (1555), talents +0x80..+0x84.
- **Parent purses**: `32*RandomModulo(0x200) + 16000`.
- **Packet builders** opcode 12 (EnqueueTradeRequest: +0x1C word, +0x1E a3, +0x1F a5,
  +0x23 a6, +0x24 a7, name1 +0x25, name2 +0x35), opcode 15 (+0x10/+0x14/+0x1C/+0x1D),
  opcode 32 (+0x10 flag, +0x11 124-byte blob, len 141 / short 17 for flag 14).

Added this wave (`tests/unit/newgame_builders_w13_test.cpp`, 7 tests / 48 checks):
- **opcode 11 (EnqueueObjectInteraction)** wire image: kind +0x14, a2 +0x15,
  a4 +0x19, a3 word +0x1D, a5 +0x1F, a6 +0x23, a7 +0x24, a8 +0x25 — plus the exact
  mother(gender 1)/father(gender 0) parent calls. Previously only the *apply* side
  (ExComputeObjectCoords reads) was pinned, never the builder's byte layout.
- **opcode 27 (QueueRequestCoord27)** wire image: a1 +0x10, a2 +0x14, delta +0x18,
  coordX +0x1C, coordY +0x24, with +0x20 left zero; plus the new-game delta-127/
  mode-0 call. Previously unpinned as a builder.
- **Person_CreateAndSpawn default backend** stamps: kind +2, id +4, **alive +8 == 100**
  (`0x58da70: *(v14+8)=100`), gender +9, owner word +10, a6 +356, a7 +357, id-column
  lockstep + allocator advance. The "always alive" invariant the start-gold scan
  depends on is now pinned across kinds {2,6,7,9}.

## 3. Internal consistency check — NO drift found

Cross-checked the packet field arithmetic end to end:
- opcode-12 a5 (wappen) @+0x1F -> ExCreatePersonB reads `pkt.bytes+31` -> rec+0x54. OK.
- opcode-12 a7 (faith) @+0x24 -> ExCreatePersonB reads `pkt.bytes[36]` -> rec[12]. OK.
- opcode-12 a3 (variant) @+0x1E -> `HiByte(pkt,27)` = bytes[30] -> a6 -> rec[356]. OK.
- opcode-12 a6 (gender) @+0x23 -> `HiByte(pkt,32)` = bytes[35] -> a8 -> rec[9]. OK.
- Six Coord27 packets: builder delta lands at +0x18, apply handler reads subject/
  object at +16/+20, delta +24... NOTE the apply handler `ExComputeObjectCoords`
  reads delta at packet **+24** and mode at **+28**, while the builder writes a3 at
  **+0x18 (=24)** and leaves +0x1C (=28) zero — i.e. the builder's `a3` argument is
  the apply handler's "+24 delta" and the builder's `coordX` (+0x1C) is the handler's
  "+28 mode". The commit passes a3=127, coordX=coordY=0, so delta=127/mode=0. This is
  internally consistent (verified in both newgame_apply_test and sim_command_apply6_test);
  documented here because the argument *names* differ between the two layers but the
  byte offsets agree.

All constants/offsets in the source match their provenance comments and
`progress/newgame-apply.md`. No source edits were required.

## 4. Confidence map

GOLDEN-PINNED (high 1:1 confidence — constants + control flow tested):
- the whole segment (every function in the inventory table). The commit's full
  record image, RNG stream, start-gold scan/formula, mission gate, six relation
  packets, all five packet builders, and the person-create stamps are now asserted.

UNDER-VERIFIED: none remaining in this segment after the W13 additions.

NEEDS-LIVE-MCP (in-tree evidence cannot confirm exact 1:1; decompile targets to
binary-diff when MCP returns):
- `VIBE_Office_ResolveStaffModel @0x57c1e8` — named gap, inert no-op hook; the
  staff-model/avatar resolve body is unreconstructed.
- First-name string tables `dword_8C4320` (female, 112) / `dword_8C400C` (male, 191)
  loaded by `0x530e50` — named gap; the RandomModulo draws (0x70 / 0xBF) are made so
  the RNG stream matches, but the table contents are not reconstructed.
- `VIBE_Person_CreateAndSpawn @0x58da70` full body (~150 field stat/avatar/family RNG
  init) — only the observable allocation + the verified `+8=100` / building-column
  writes are reconstructed; the family-record allocation (`word_13C3110`) keeping the
  `Person_GetFamilyRecord` gate false is a deliberate deferral.
- `dword_122F4EC` (mission id) / `byte_63C8F4` (mission mode, cold image 0xFE) arrive
  as NewGameApplyInputs until the mission-dialog flow is wired.

## 5. Build / test status

- New file: `tests/unit/newgame_builders_w13_test.cpp` (7 tests, 48 checks, all pass).
- Re-ran segment suites green: `newgame_apply_test` (142), `command_apply7_test`
  (523), `sim_command_inherit_test` (23), `person_create_harden_test` (1546),
  `sim_command_apply6_test` (113), `newgame_result_itest` (23).
- No source files changed; portable Debug build (GUILD_BACKEND=OFF) stays green.
