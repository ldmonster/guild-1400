# Harden sweep — sim command builders (chunk sim_04)

Files owned:
- `src/sim/command_builders.cpp` (+ `.h`)
- `src/sim/command_builders2.cpp` (+ `.h`)
- `src/sim/command_builders3.cpp` (+ `.h`)

Method: every function carries a `gilde.exe 0xADDR` provenance comment. Each was
decompiled via IDA MCP and the reconstruction diffed line-for-line against the
pseudocode, with stack-slot offsets resolved from `ebp-` deltas (packet base
`v[0]` sits at the most-negative ebp slot; payload locals decode to packet
offsets +0x10, +0x14, …). All builders share the same skeleton: stage a 153-byte
temp, write opcode at +0, payload fields, `EnqueuePacket`. No float→int, RNG,
fixed-point, or x87/SSE sites exist in this chunk (pure integer/byte packing),
so those divergence classes are N/A here.

## command_builders.cpp

| Fn | Addr | Status | Notes |
|----|------|--------|-------|
| QueueRequestEntity29 | 0x4949c4 | VERIFIED-1:1 (payload) / BOUNDARY (side-channel) | All 13 record-slice reads + a1@+0x3E + HE_NULL terminator@+0x3F match byte offsets (4,68,72,76,80,82,86,90,94,96,100,104,108). The original ALSO calls `StagePendingBlock(160, h+172)` (the +172/0xAC region) — a pending-fragment side-channel owned by command.cpp; documented-omitted in the recon (same rationale as Buffer28's staging, which is in-tree). NOTE/HANDOFF: if a caller needs the opcode-29 fragment chain, the 160-byte StagePendingBlock(h+172) must be wired (currently absent). |
| QueueRequestPair33 | 0x494b04 | VERIFIED-1:1 | a1@+0x10, a2@+0x14. |
| QueueRequestSingle49 | 0x494e4c | VERIFIED-1:1 | a1@+0x10. |
| QueueRequestQuad56 | 0x495098 | VERIFIED-1:1 | a1@+0x10, a2@+0x14, a4@+0x18; a3 -> ebp-4 off-wire. |
| QueueRequestPair57 | 0x495100 | VERIFIED-1:1 | SWAP: v4=a2@+0x10, v5=a1@+0x14. |
| RequestBuildOp88 | 0x495ae0 | VERIFIED-1:1 | a1@+0x10. |
| RequestBuildOp91 | 0x495b7c | VERIFIED-1:1 | a1/a2/a4 on wire (+0x10/+0x14/+0x18); a3 off-wire. |
| RequestBuildOp93 | 0x495bd0 | VERIFIED-1:1 | a1/a2/a4 on wire; a3 off-wire. |
| RequestBuildOp73Str | 0x495554 | VERIFIED-1:1 (code) / FIXED (.h doc) | Disasm: v15=a3@+0x1F, v16=a5@+0x20. The `.cpp` writes are correct (a3@0x1F, a5@0x20); the `.h` comment had a3/a5 SWAPPED — fixed to match disasm. a1@+0x14, a2@+0x17, a4@+0x1B, a6@+0x21, name@+0x27 (HE_NULL 2-byte-stride). |
| QueueRequestNamedObject53 | 0x494f0c | VERIFIED-1:1 (emitted payload) / BOUNDARY (a4/a5 resolution) | a1@+0x10, a2@+0x14, a4@+0x18, a5@+0x1C, obj@+0x1D (stride copy), name@+0x3D (StrNCopyPad,31). The `RecordById && !rec[8] => -1` early-out is modelled via `personStamped`. The `if(!a5 && a4==-1){…}` block (Building_FindById / IsProductionType / GameObject_QueryFind / FindStorableObject → may set a5=1,a4=-1) consults cross-cluster Building/GameObject leaves and is documented as caller-resolved (a4/a5 supplied resolved). |

## command_builders2.cpp

| Fn | Addr | Status | Notes |
|----|------|--------|-------|
| QueueRequestArgs26 | 0x494848 | VERIFIED-1:1 | a1/a2/a3 @+0x10/+0x14/+0x18. |
| QueueRequestPair35 | 0x494b74 | VERIFIED-1:1 | a1@+0x10, a2@+0x14. |
| QueueRequestPair41 | 0x494c80 | VERIFIED-1:1 | a1@+0x10, a2@+0x14. |
| QueueRequestPair42 | 0x494ca4 | VERIFIED-1:1 | a1@+0x10, a2@+0x14. |
| QueueRequestPair51 | 0x494ec0 | VERIFIED-1:1 | a1@+0x10, a2@+0x14. |
| QueueRequestQuad37 | 0x494bbc | VERIFIED-1:1 | a1/a2/a4 on wire; a3 off-wire (ebp-4). |
| QueueRequestQuad43 | 0x494cc8 | VERIFIED-1:1 | a1/a2/a4 on wire; a3 off-wire. |
| QueueRequestQuad52 | 0x494ee4 | VERIFIED-1:1 | a1/a2/a4 on wire; a3 off-wire. |
| QueueRequestQuad54 | 0x495070 | VERIFIED-1:1 | a1/a2/a4 on wire; a3 off-wire. |
| QueueRequestQuad46 | 0x494d68 | VERIFIED-1:1 | a1@+0x10, a2@+0x14, a4@+0x18, a3@+0x1C (all four). |
| QueueRequestQuad60 | 0x495124 | VERIFIED-1:1 | a1@+0x10, a2@+0x14, a4@+0x18, a3@+0x1C (all four). |
| QueueRequestSingle58 | 0x4950c0 | VERIFIED-1:1 | a1@+0x10. |
| QueueRequestSingle59 | 0x4950e0 | VERIFIED-1:1 | a1@+0x10. |
| QueueRequestFlag55 | 0x495040 | VERIFIED-1:1 | a1@+0x10, StrNCopyPad(name,47)@+0x14, flag a2@+0x44. Original sets flag BEFORE StrNCopyPad; recon sets after — no overlap (StrNCopyPad zeros [0x14..0x43] only), observably identical. |
| QueueRequestPerm30 | 0x494a50 | VERIFIED-1:1 | GameTime_Compare(appt, qword_13CE852)==0 => -1 (modelled via apptValid). appt[0]@+0x10, [1]@+0x14, [2]@+0x18, appt[3].lo word@+0x1C (pointer-walk decoded: v4=a1+1, v7=*v4++, v8=*v4, v9=*((WORD*)v4+2)). |
| QueueRequestBuffer28 | 0x494910 | VERIFIED-1:1 | Person_FindRecordById(header->id @+8) guard (modelled via personFound). Stages 248-byte header || bodyLen body via StagePendingBlock(bodyLen+248). EmitWithPendingBlock links header+12 to the fragment chain. |

## command_builders3.cpp

| Fn | Addr | Status | Notes |
|----|------|--------|-------|
| RequestBuildOp65Blob | 0x4953d8 | VERIFIED-1:1 | 56B@+0x10, 2B@+0x48. (2nd reg arg a2 off-wire.) |
| RequestBuildOp66 | 0x495414 | VERIFIED-1:1 | a1@+0x10. |
| RequestBuildOp67 | 0x495434 | VERIFIED-1:1 | a1@+0x10. |
| RequestBuildOp68Blob | 0x495454 | VERIFIED-1:1 | 4B@+0x10, 3B@+0x14. |
| RequestBuildOp69Blob | 0x495490 | VERIFIED-1:1 | 12B@+0x10, 3B@+0x1C. |
| RequestBuildOp70Blob | 0x4954cc | VERIFIED-1:1 | 8B@+0x10, 1B@+0x18. |
| RequestBuildOp71 | 0x495508 | VERIFIED-1:1 | a1/a2/a4 on wire; a3 off-wire. |
| RequestBuildOp72 | 0x495530 | VERIFIED-1:1 | a1@+0x10, a2 byte@+0x14. |
| RequestBuildOp74 | 0x4955c0 | VERIFIED-1:1 | a1@+0x10. |
| RequestBuildOp75Blob | 0x4955e0 | VERIFIED-1:1 | 128B@+0x10. |
| RequestBuildOp83 | 0x495954 | VERIFIED-1:1 | 5 dwords @+0x10..+0x20. |
| RequestBuildOp84 | 0x495980 | VERIFIED-1:1 | 3 dwords @+0x10..+0x18. |
| RequestBuildOp86Blob | 0x495a90 | VERIFIED-1:1 | 40B@+0x10. |
| RequestBuildOp87 | 0x495ac0 | VERIFIED-1:1 | a1@+0x10. |
| RequestBuildOp92 | 0x495ba4 | VERIFIED-1:1 | *a1@+0x10, a1[1]@+0x14, *((WORD*)a1+4)=byte8 word @+0x18 (recon `rd32(src,8)&0xFFFF` — correct). |
| RequestBuildOp94 | 0x495bf8 | VERIFIED-1:1 | *(base+1) (unaligned dword) @+0x14, 28B blob @+0x18. +0x10 stays 0. |
| RequestBuildOp95 | 0x495c3c | VERIFIED-1:1 | *src@+0x10, *(rec+4)@+0x14. |
| RequestCreateGebaeude | 0x49561c | VERIFIED-1:1 | a1@+0x10, a2 byte@+0x18, pos 16B@+0x19, meta 12B@+0x29. Debug sprintf "cm_RequestCreateGeb(): pos %f   %f   %f" (3 spaces) is out-of-packet/discarded; format matches. The stray `v10[115]=a2` store at byte 156 is OUTSIDE the 153B packet (a2 already at +0x18) — not on wire, correctly omitted. |
| RequestSendCutInfo | 0x494be4 | VERIFIED-1:1 | a1@+0x10, a2@+0x14. Debug sprintf uses GLOBAL dword_62EB38 for the time field (recon takes a `gameTick` param) — sprintf output is out-of-packet/discarded, so this is cosmetic only. Format "cm_RequestSendCutInfo(): PlayerId:%i Time:%i" matches. |
| RequestCutsceneReady | 0x495b00 | VERIFIED-1:1 | 68B@+0x10. Debug sprintf "cm_RequestCutsceneReady(): requesting for id %i" out-of-packet; matches. |

## Counts
- Functions audited: 44 (10 builders.cpp + 16 builders2.cpp + 18 builders3.cpp).
- VERIFIED-1:1: 44 (all packet/wire payloads byte-exact vs disasm).
- FIXED: 1 (Op73Str `.h` doc comment — a3/a5 offsets were swapped vs disasm; code was already correct).
- BOUNDARY/documented-omit: 2 (Entity29 StagePendingBlock side-channel; NamedObject53 a4/a5 cross-cluster resolution) — both pre-existing documented hooks, consistent with Rule 8.
- No control-flow, constant/table, float→int, fixed-point, RNG, or struct-offset divergences found.

## Tests
- Added `tests/unit/sim_command_builders_test.cpp` (8 TESTs) — there was no golden
  unit test for command_builders.cpp; covers Entity29 record-slice layout, Pair33/
  Single49, Quad56 (a3 drop), Pair57 (swap), Op73Str layout, NamedObject53 layout
  + not-stamped early-out, and Op88/91/93.
- Existing `sim_command_builders2_test.cpp` / `sim_command_builders3_test.cpp`
  reviewed against the binary — both encode the correct wire layouts; no fixes.

## Build
- All three owned `.cpp` compile clean (`g++ -std=c++17 -fsyntax-only` and the
  CMake `guild` target produced their `.o` files: command_builders.cpp.o,
  command_builders2.cpp.o, command_builders3.cpp.o).
- New test compiles clean (`-fsyntax-only`).
- The full `guild` library link is broken ONLY by the unrelated untracked file
  `src/gui/widget_layout.cpp` (Widget has no `ld<>()` member) — NOT in this chunk;
  ignored per brief.
