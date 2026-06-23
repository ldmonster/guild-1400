# Hardening sweep — sim Command codec chunk

Files owned: `src/sim/command_codec.cpp`, `src/sim/command.cpp`,
`src/sim/command_inherit.cpp`, `src/sim/command_leaves.cpp` (+ their `.h` + tests).

MCP IDA Pro (module gilde.exe) used to decompile + disasm every provenance address.
Where Hex-Rays collapsed `__usercall`/`__userpurge` args or x87 stores, the disasm
was used as the reference of record (notably the float-store in 0x58d648).

## command_codec.cpp

| Fn (addr) | Status | Notes |
|---|---|---|
| `DeltaWriter::BeginDeltaPacket` 0x493a94 | VERIFIED-1:1 | id stash, count=0, memset 119 (SetGrayColorThunk), cursor=0. Entity-base resolve (ResolveEntityById) is the documented host boundary — caller passes base directly. |
| `DeltaWriter::AppendDeltaField` 0x493aec | VERIFIED-1:1 | width∈{1,2,4} guard; `v4=width*count`; overflow `v4+cursor+4 >= 0x77`; header `[width][count][offset:LE16]`; cursor `+= v4+4`; per-width (new-old) LE writes. Offset word write `*v6=a4` == hdr[2..3] LE. |
| `DeltaWriter::AppendRawField` 0x493c14 | VERIFIED-1:1 | memcpy abs values; same header/guard. |
| `DeltaWriter::AppendCopiedField` 0x493c90 | VERIFIED-1:1 | element-wise copy; cursor advanced from `v7` (pre-computed) at end — matches. |
| `DeltaWriter::ApplyDelta` (receive side helper) | VERIFIED-1:1 | mirrors Ex* apply add-delta semantics. |
| `AiMethodWriter::Begin` 0x493d64 | VERIFIED-1:1 | person id from *(a1+4) — caller passes id; count=0; memset 119; cursor=0. |
| `AiMethodWriter::AppendEntry` 0x493d9c | VERIFIED-1:1 | `cursor+5 >= 0x77` guard; `[statId:1][delta:LE32]`; cursor+=5; ++count. |
| `QueueRequest16` 0x494630 | VERIFIED-1:1 | a1@0x10,a2@0x14,a4(b)@0x1C,a3@0x1D (stack offsets confirmed from ebp-9Ch frame). |
| `QueueRequest17` 0x49465c | VERIFIED-1:1 | a1@0x10,a2@0x14,a4(w)@0x18,a5(b)@0x1E,a3@0x1F,a6@0x23. |
| `QueueRequestArgs25` 0x494810 | VERIFIED-1:1 | a1@0x10,a2@0x14,a4@0x18,a3@0x1C,a5@0x20. |
| `QueueRequestCoord27` 0x494878 | BOUNDARY | Coordinate packing via `VIBE_Coord_ConvertX` (flt_62EB90/dword_62EB94 globals) is the Rule-3/5 ConvertX boundary. The original emits THREE payload dwords (v4@+0x1C, dword_62EB94@+0x20, (int64)v3@+0x24). The reconstruction's 2-coord signature writes coordX@+0x1C, coordY@+0x24 and leaves +0x20 zero. Documented divergence (the float→packed conversion is host-coupled and out of this chunk's tree). |
| `EnqueueObjectInteraction` 0x4944f0 | VERIFIED-1:1 | a1(b)@0x14,a2@0x15,a4@0x19,a3(w)@0x1D,a5@0x1F,a6@0x23,a7@0x24,a8@0x25. |
| `QueueRequestState22(q,dw)` 0x494750 | **FIXED** | Was writing `[fieldCount]@+0x10, payload@+0x11` — dropped the 4-byte entity id and shifted the payload by 3. Binary qmemcpy's the 124-byte global block `[id:4][count:1][payload:119]` to +0x10. Rebuilt the exact image (now identical to QueueRequestState22FromDelta). **Evidence:** 0x494750 `qmemcpy(v2 /*ebp-98h==+0x10*/, &dword_11AA3E0, 124)`; global layout dword_11AA3E0(id)+0, byte_11AA3E4(count)+4, unk_11AA3E5(payload)+5. Golden fixed too (see e2e below). |

## command.cpp

| Fn (addr) | Status | Notes |
|---|---|---|
| `CommandQueue::Init` 0x4931e0 | VERIFIED-1:1 | pool 0x2000 (8192) threaded prev/next; ACK loop 327680/10 = 32768 entries status=1 ring=-1; free_head=pool[0]; counters 0. Opcode-3 handshake / WaitForPacketType is the net boundary. |
| `ComputePacketSize` 0x493034 | VERIFIED-1:1 | every switch arm byte-checked against decompile (3→20 … 0x5C→26, default 145); variable 0x16/0x17 per-field stride loop and 0x18 (5*cnt+21) and 0x20 (17 if +16==14 else 141) all match. |
| `EnqueuePacket` 0x49388c | VERIFIED-1:1 (modeled) | `v2=((WORD)cnt+1)&0x7FFF` — `&0x7FFF` makes the WORD-trunc irrelevant, equals `(cnt+1)&0x7FFF`. Dup-walk over pending(+149); memcpy 0x99; ++cnt; header set order flag(+3)/extra(+12)/len(+1)/cmdId(+4)/count(+8); sync latch (op32 & byte16==14); ACK seed status0/ring; tail append. The `dword_764CE8` in-flight guard + `GeneratePendingPackets` (dword_11AA4A0) are net boundaries. |
| `FlushSendQueue` 0x4934cc | VERIFIED-1:1 (modeled) | standalone (764CE0==-1) → StoreReceivedPacket loop, unlink-prev fixup; networked → SendPacket. The 764CE8 ACK-gated send loop is simplified to a plain send (net boundary). |
| `StoreReceivedPacket` 0x493f80 | VERIFIED-1:1 | FreePop(+149), memcpy 0x99, ComputePacketSize→len(+1), tail-append to received(11AA498). Disconnect on empty pool is net glue → returns 1. |
| `UnlinkReceivedPacket` 0x494028 | VERIFIED-1:1 | detach from received list, push to free head (11AA49C). |
| `ExecCommands` 0x494088 | VERIFIED-1:1 (modeled) | group framing op5/6/7; per-node unlink+dispatch+recycle; cmdId(+4)==-1 guard; ACK status=2/slot0/seq0; count classification last_req+1==cnt advance / ==last_sync dup / sync(op32,b16==14) / else lost→resync; `op<96` handler table. Reassembly/sprintf-diagnostics and the disconnected-drain pre-pass are boundaries. |
| `GetPacketStatusById` 0x4939d4 | VERIFIED-1:1 | <0x8000 → status(+0) byte else -1. |
| `GetPacketSeqById` 0x4939fc | VERIFIED-1:1 | <0x8000 → seq(+6) else 0 (RetZero). |
| header constants (command.h) | VERIFIED | kPoolSlots 0x2000, kSendRingSlots 0x8000, kAckEntryBytes 10, kMaxPayload 0x77, kNumOpcodes 96, kSeqMask 0x7FFF, ACK layout (+0 status,+2 ring,+6 seq) — all confirmed from Init/Enqueue/Exec/getters. |

## command_inherit.cpp

| Fn (addr) | Status | Notes |
|---|---|---|
| `EnqueueCmd15` 0x494604 | VERIFIED-1:1 | opcode 15; a1@0x10,a2@0x14,a4(b)@0x1C,a3@0x1D. |
| `QueueRequestSlotReset28` 0x4948c8 | **FIXED** | The `do { if(!a1[14]) a1[14]=-1; ++a1; } while(a1!=a1+8)` loop ADVANCES the base each iteration, so `a1[14]` addresses `words[14+k]` for k=0..7 → words[14..21] each forced to -1 when 0. Reconstruction had tested/set only `words[14]` every iteration. **Evidence:** disasm 0x4948d8 `++eax` per iter, `a1[14]` is base-relative. StagePendingBlock uses the captured base (v2). |
| `QueueRequestState22FromDelta` 0x494750 | VERIFIED-1:1 | 124-byte `[id:4][count:1][payload:119]` at +0x10. (Reference layout for the codec-version fix above.) |
| `EmitInheritanceDelta` 0x58d29c..0x58d6fa | **FIXED (2)** | See below. Delta pkt#1 (10 fields), pkt#2 (8 asset dwords@92.. + @36/@44/@40), and the 5 Args26 scalars otherwise VERIFIED-1:1 (field offsets 124/20/28/24/32, RNG draw count=1 in order, sign curve, squaring order `v73*(sign*v73)` confirmed by disasm). |

EmitInheritanceDelta fixes:
1. **field-124 read offset 92 → 124.** `*((float*)v66 + 31)` = byte offset 124; disasm
   0x58d60e `fld dword ptr [eax+7Ch]` (0x7C==124). Was reading offset 92.
2. **skill-reroll v73 double → float.** disasm 0x58d648 `fstp [var_30]` where var_30 is a
   32-bit float: the curve value `(int)draw*A*B+C` is rounded to FLOAT before the sign
   compare and the squaring. Reconstruction kept it `double`; now stored as `float` so the
   compare and both `v73` operands use the rounded value (1:1 with the x87 store).
   (Note: the pre-store accumulation is computed in `double`; true x87-80bit isn't used —
   standard codebase convention; the load-bearing float-store rounding is reproduced.)

## command_leaves.cpp  (`@0xADDR` provenance present — full verify, not just spot-check)

| Fn (addr) | Status | Notes |
|---|---|---|
| `CommandFindOrAllocSlot` 0x485f7c | VERIFIED-1:1 | reuse pass 16× key(+148)==key; alloc pass first key==-1 (full→return 0/orig, -1/model); memset 44, claimed(+152)=0, key(+148). Pointer→index model (consistent existing design). |
| `CommandQueueRequestGuardTarget61` 0x49514c | VERIFIED-1:1 (input-struct model) | offsets v9@0x10,v10@0x14,mode@0x18,v12(squad)@0x19,a4@0x1D,a3@0x1E; consult-enemy `(slot&&!*(slot+0x170))||!slot`; squad=*(slot+4); owner=*(a1+1). Squad-slot/enemy scans are pre-resolved inputs (boundary). |
| `CharActionInsertActionArgs` 0x40c2f0 | VERIFIED-1:1 | type=BYTE4(packed); registry stride 19 dwords, step@+0, argCount@+18 (dword_66FD18=66FCD0+0x48); node[0]step,[12]cnt,[9]type,[20]owner, args@[48+4i], [40]next,[16]state,[4]chained,[8]ready(low byte), RunActionOrFree→[48]=type. (`n>63` clamp is a defensive bound the original lacks; never triggers for valid registry data.) |
| `GameTickHandleTurnControlCommand` 0x5799a8 | VERIFIED-1:1 | tag@+36; 'dsbl'(0x6473626C)→latch 0, 'enbl'(0x656E626C)→latch 1, 'init'(0x696E6974)/'set '(0x73657420)→memcpy 36 from packet+0, else 0. All four magic dwords get_bytes-confirmed. The orig's alignment-split qmemcpy == a flat 36-byte copy. |
| `CityApplyStatsFromAck` 0x5792e0 | VERIFIED-1:1 (count==0 boundary) | head copy; flt_641DA8=*(a1+24); accum loop v20[0..4]+=byte_12CE98F[i+1..i+5], v4+=need ×5/person (v4==5·Σneed); avg `*1/count` then **ConvertX (truncate toward zero)** for all 5 sat + overall, byte stores; order 5 sat then overall. count==0 div-by-zero is the documented guarded divergence. |
| `GfxCrossFadeStep` 0x41e814 | VERIFIED-1:1 (Vulkan/SDL boundary) | alpha(+28)+=8; ≤255→SetFadeParams; else blit rows (count@+20); teardown when alpha>288 → free + active(69FF94)=0. Arithmetic/sequencing 1:1; blit/free/setparams are Rule-3/5 ops hooks. |

## Tests fixed (to the binary)

- `tests/e2e/sim_command_e2e_test.cpp` `DeltaStatePacketRoundtrip`: was reading field-count
  at +0x10 and payload at +0x11 (the old wrong layout). Now: entity id@+0x10 (round-trip
  checked == 0xABCD), field count@+0x14, ApplyDelta from +0x15. Matches the corrected
  QueueRequestState22 124-byte block.
- `tests/unit/sim_command_inherit_test.cpp`:
  - `MakeOldPerson`: wealth column moved 92 → 124 (matches the field-124 read fix).
  - `SlotReset28StagesBlock`: now asserts words[14..21] all == -1 (extended-range loop fix).

## Counts
- Functions with provenance verified: 28 (codec 13, command 9, inherit 4, leaves 6 + struct/const sets).
- VERIFIED-1:1: 24.  FIXED: 4 (QueueRequestState22 codec layout; SlotReset28 loop range;
  EmitInheritanceDelta field-124 offset; EmitInheritanceDelta v73 float precision).
- BOUNDARY/documented divergence: QueueRequestCoord27 (ConvertX coord packing),
  CityApplyStatsFromAck count==0, GfxCrossFadeStep blit/free ops, GuardTarget61 input scans,
  FindOrAllocSlot pointer→index — all genuine tech/data-tree boundaries.
- Tests fixed: 2 files (3 cases).
- Build: all 4 owned .cpp + the 3 affected tests compile clean with cmake flags
  (-Wall -Wextra). Inherit unit+itest (50 checks) and e2e (51 checks) PASS.
  (Full `guild` lib link is blocked only by the unrelated untracked src/gui/widget_layout.cpp.)
