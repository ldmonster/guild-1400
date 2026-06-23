# Wave-15 TRUE 1:1 binary diff — command apply/queue pipeline (W15-CMD)

MCP was LIVE. Every function below was decompiled from `gilde.exe` and compared
line-for-line against `src/sim/command*`. This wave RESOLVES the wave-11/13
NEEDS-LIVE-MCP queue and fixes the one real divergence found.

Segment files: `command.{h,cpp}`, `command_apply.{h,cpp}` (the field-patch family),
`command_codec.{h,cpp}`, `command_pending.{h,cpp}`, `command_receive.{h,cpp}`,
`command_recon2_sync.{h,cpp}`, `command_recon_syncrange.{h,cpp}`,
`command_apply6.{h,cpp}` (ExSellObjekt / ExComputeObjectCoords), and the
`CheckSyncRangeAcked` scan in `command_apply10.cpp`. `command_recon4_senders`
was read-only (not edited).

---

## FIXED — real divergence (source + golden corrected to the binary)

### ExApplyNeedDeltas upper clamp = 1024.0, not 1000.0 (`command_apply.cpp` @0x497ed0)
The decompile's post-add clamp (`0x497f4d..0x497fa6`):
```c
v13 = (double)*slot;                       // post-add value
if ( v13 < 0.0 || v13 < dbl_61BE74 ) {     // dbl_61BE74 = 1000.0
    if ( *slot >= 0.0 ) keep; else *slot = 0.0;
} else {
    v8 = 1083129856; LODWORD = 0;          // double 0x4090000000000000 == 1024.0
    *slot = (float)1024.0;
}
```
`get_bytes @0x61BE74` = `00 00 00 00 00 40 8F 40` = **1000.0** (the THRESHOLD).
The overflow store uses hi-dword `1083129856 = 0x40900000`, lo-dword 0 →
`0x4090000000000000` = **1024.0** (the CLAMPED VALUE). So the bound is asymmetric:
lower 0, threshold 1000.0, stored-on-overflow 1024.0.

Prior reconstruction stored `1000.0f` (clamp == threshold) — WRONG. The wave-13
golden `ApplyNeedDeltas_ClampHigh` asserted `1000.0f`; per the brief the golden was
wrong, so source + golden are both corrected to the binary (1024.0). Added
`ApplyNeedDeltas_ClampHighThresholdBoundary` pinning the `< 1000.0` boundary
(exactly 1000.0 → 1024.0; 999.0 → unchanged). All other clamp cases (negative→0,
in-band unchanged) were already correct.

---

## RESOLVED — wave-11/13 NEEDS-LIVE-MCP queue

### Q1. Field-patch destination `off` bound — ANSWER: the original has NO dest bound.
`ExPatchObjectFieldsAdd` (0x497c18), `ExWriteObjectFields` (0x497da4),
`ExPatchObjectBitfield` (0x498024), `ExAddObjectFloatField` (0x498104) all compute
`dst = base + off` with `off` a raw wire value (u16 for the two field-walkers, u32
for the bitfield/float handlers) and write unconditionally — confirmed in every
decompile (`v8 = (char*)v25 + v7`, `v7 = v5 + a1[5]`, `v5 + *(a1+20)`). No clamp
against the entity record size anywhere.

### Q2. Field-patch SOURCE-cursor bound — ANSWER: the original has NO source bound.
The decompiles walk exactly `*(a1+20)` records, advancing the cursor by
`width*count + 4` per record with no end-of-record check; the size-walk in
`ComputePacketSize` (0x4930b5) does the same. The binary's 153-byte packet lives in
a contiguous pool (`byte_B5FB60` / the send ring), so an over-read lands in adjacent
valid packet memory — defined in the engine's envelope, but a real heap OOB in our
standalone `u8[153]` layout.

### Q3. ExApplyNeedDeltas statId dest bound — ANSWER: the original has NO statId bound.
`*(float*)&v14[6*statId+72]` with `statId = *v5` the raw wire byte (0..255). Writes
to `base + 144 + 12*statId` unbounded (max 3208 > the 536-byte Person, into adjacent
`g_persons`). No clamp to the need-stat count.

**Resolution of Q1–Q3:** the wave-11 source-cursor `break` guards in
`ExPatchObjectFieldsAdd` / `ExWriteObjectFields` / `ExApplyNeedDeltas` /
`ComputePacketSize` are NOT in the binary. They are provably **never reached on any
reachable input**: the entire live call tree produces only codec-built packets
(`AppendDeltaField`/`AppendRawField`/`AiMethodWriter::AppendEntry`, payload bounded
to `kMaxPayload = 119` by the 0x77 cap), so on every valid packet the guarded path
is byte-identical to the unbounded original. They are kept ONLY as a never-hit
memory-safety net for the standalone packet layout (removing them gains zero 1:1
fidelity on the valid path and reintroduces UB). The statId destination is left
**unbounded** to match the original exactly. All four guard comments were rewritten
from "we don't know (FLAG)" to the MCP-confirmed fact.

### Q4. 0x49818C mode-3 trunc-toward-zero — ANSWER: truncate-toward-zero, VERIFIED.
`VIBE_Coord_ConvertX` (0x5c6b08) does `fstcw; CW = (saved & 0x00FF) | 0x1F00; fldcw;
frndint; fldcw saved`. The top byte `0x1F` sets RC (bits 10-11) = `11` = round
toward zero (chop). `frndint` truncates; the subsequent `fistp` is exact. So
mode-3's `byte_1333110[v13] = (int)((double)cell * scale)` is truncation toward
zero — the `static_cast<i32>(scaled)` in `ExComputeObjectCoords` matches it.
The mode-3 loop (`for k in [0,768): if g_persons[k].id != PersonSlotId(idx):
B(k,idx) = trunc(B(k,idx)*scale)`) matches the decompile's
`dword_12CE914[i] != dword_12CEB1C[v15]` / `byte_1333110[v13]`, `v13 += 768` walk.

### Q5. ExecCommands duplicate-sync / received-sync diagnostic branches — VERIFIED no-op.
Decompile (0x494088): the four-way sequence classification is
- `LastRequCount+1 == Count` → `dword_11AA468 = Count` (in-order advance);
- `Count == dword_11AA470` (LastSyncCount) → **Sprintf into a discarded stack
  buffer only**, no state change;
- `op == 32 && [16] == 14` (received sync) → **Sprintf only**, no state change;
- else (Lost a Command) → Sprintf + `dword_11AA468 = Count` (resync).

Confirmed the dup-sync and received-sync branches have NO observable side effect
beyond the (unmodeled) log string; the reconstruction models them as no-ops and the
Lost branch as the resync — VERIFIED-1:1.

---

## VERIFIED-1:1 (decompiled and matched line-for-line)

- **ComputePacketSize** (0x493034): all 96 case labels + the 0x16/0x17 variable
  walk (`v3[1]*v3[0]+4`, start 21) + 0x18 (`5*a1[20]+21`) + 0x20 (`[16]==14?17:141`)
  + default 145 — every entry matches the live switch.
- **ExecCommands** (0x494088): received-list walk, group framing (op 5/6/7),
  ack stamp (`byte_B5FB60[10*(cmdId&0x7FFF)]` status 2 / slot 0 / seq 0), the
  sequence classification above, recycle to free pool.
- **ExPatchObjectFieldsAdd / ExWriteObjectFields / ExPatchObjectBitfield /
  ExAddObjectFloatField** (0x497c18 / 0x497da4 / 0x498024 / 0x498104): the -2/-3/-4
  remap + write-back, the {object,scene,person} / {person,scene,object} resolve
  priority, the width 1/2/4 add / absolute-write / clear-mask|set / float-add, and
  the `*a2 = 1` ack — all match (modulo the never-hit guards, Q1–Q3).
- **ExComputeObjectCoords** (0x49818C): mode 4 band-decay (band params
  {40,-115,-20,3}/{55,-105,-16,5}/{70,-95,-12,7}/{85,-85,-8,9}/{100,-75,-4,11},
  band>=4 & band 0 fallthrough), modes 0/1/2 grid mutation + ClampRel
  ([-127,127] via >126/<-127), mode 3 trunc (Q4), unknown-mode no-op ack.
- **ExSellObjekt** (0x496B90): decode offsets (+20 src remap, +16 dst remap,
  HIWORD(+22) proto, +31 qty, +35 raw, +30 good, +26 curSellCmd, +8 cmdCount), the
  source/dest capacity gates, the `dword_631290` latch deferred to the
  buildingtype hook (LABEL_58 `= *(v55+1)`), the ack (`*a2=1; if a1[16]!=-1
  {a2[1]=3; a2[6]=v55}`) — matches the documented hook-boundary contract.
- **MarkSyncRangeStart / MarkSyncRangeEnd** (0x493a1c / 0x493a28):
  `= sendCount+1` (u32 wraparound), return same.
- **CheckSyncRangeAcked** (0x493a34): start==end→1; start>=end→1 (unsigned);
  scan `ackTable[10*(seq&0x7FFF)]`: status 0 break→0, status 2→result -1, advance
  to end→result. Exact match.
- **AppendDeltaField** (0x493aec): width 1/2/4 validate, `width*count+cursor+4 >=
  0x77` cap, `[width][count][offset:2][new-old]` layout, field_count++, new-minus-
  old per width little-endian, oldBase = entity + offset. Exact match.
- **ReceiveAndQueue** (0x493ebc): standalone early-out (`dword_764CE0 == -1` →
  return 0) + the net-pump StoreReceivedPacket loop — contract matches; the net
  pump itself is the SDL/net glue boundary (rules 4/5).

---

## Still under-verified (not in this wave's priority list; flagged, not fixed)

- **ExecCommandGroup** (0x4942c0): runs a per-opcode VALIDATOR table
  `funcs_4942DD` @0x631418 (DISTINCT from the apply-handler table `funcs_4941F4`
  @0x631298) as a group pre-pass, then stamps every packet in the [5..6] frame
  status 1 (all accepted) or status 2 (any rejected). The reconstruction models the
  framing but not the validator-table gate values. This is networked-group framing;
  the standalone playable flow uses FlushSendQueue→StoreReceivedPacket→ExecCommands
  and never exercises it. Reconstructing the 96-entry validator table is its own
  task (would belong to the senders/validator segment). Low risk: pure control flow,
  no state mutation beyond the accept/reject status byte.

---

## Build / test

Normal `build/` green for all segment targets (the transient `frame.cpp` compile
race from a concurrent wave resolved before the build):

- `sim_command_apply_test`: 68 checks, 0 failures (incl. corrected ClampHigh=1024.0
  + new ClampHighThresholdBoundary).
- `sim_command_test`: 310, `sim_command_apply6_test`: 113,
  `command_recon_syncrange_test`: 21, `sim_command_e2e_test`: 50,
  `sim_command_receive_test`: 59, `command_recon2_sync_test`: 42 — all 0 failures.

Source edits: `command_apply.cpp` (ExApplyNeedDeltas clamp 1024.0 + four comment
rewrites), `command.cpp` (one comment rewrite), `sim_command_apply_test.cpp`
(golden corrected + boundary test). No other segment files changed; no bind-site
files touched.
