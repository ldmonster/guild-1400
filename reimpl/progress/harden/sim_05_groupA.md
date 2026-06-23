# Hardening sweep — sim group A (command pending / receive / sync / unit-orders)

Scope (owned files):
- src/sim/command_pending.cpp
- src/sim/command_receive.cpp
- src/sim/command_recon2_sync.cpp
- src/sim/command_recon_syncrange.cpp
- src/sim/command_unit_orders.cpp

MCP module gilde.exe, imagebase 0x400000. Every provenance-tagged function was
decompiled AND disassembled and diffed line-for-line. DISASM is the reference of
record where Hex-Rays collapsed `__usercall` register args / mislabeled uninit.

Counts: VERIFIED-1:1 = 9, FIXED = 4 (across 3 functions), BOUNDARY/modeled = 5.

---

## command_recon_syncrange.cpp

### MarkSyncRangeStart @0x493a1c — VERIFIED-1:1
`s.start = sendCount+1; return sendCount+1` exactly matches
`dword_11AA484 = dword_11AA494 + 1`. u32 wraparound preserved.

### MarkSyncRangeEnd @0x493a28 — VERIFIED-1:1
`dword_11AA47C = dword_11AA494 + 1`. Identical.

---

## command_pending.cpp

### StagePendingBlock @0x49436c — VERIFIED-1:1
Reject `staged!=0 || (u16)len > 0x3FE`; write len word; qmemcpy(len); staged=len+2;
return 1. Cap 0x3FE confirmed in disasm (`cmp ...,3FEh; ja`). Matches.

### GeneratePendingPackets @0x493584 — VERIFIED-1:1 (documented abstraction)
Chunk copy size confirmed `mov ecx,80h` (128 bytes) @0x4935e1. Loop walks
`var_14 < dword_11AA4A0` advancing block ptr & cursor by 0x80. firstCount =
`dword_11AA494 + 1`. The recon reuses EnqueuePacket (ODR-owned by command.cpp)
to assign cmdId/Count/size and link the ring/pending list, then patches the +12
Count chain — observably identical to the binary's inline ring writes.

### ReassembleReceived @0x49377c — FIXED (return value + side effects)
Evidence: disasm 0x4937dc..0x49386f / LABEL_19 path.
- BEFORE: returned a fabricated `consumed` fragment COUNT; did not clear each
  consumed fragment's +12 link; did not clear the last fragment's +12.
- AFTER: returns `dword_11AA478` (reasm_len) on full reassembly and 0 on the
  not-found paths — exactly the `eax`/`result` register the binary returns
  (`result = dword_11AA478` at LABEL_19; result==0/NULL on missing-fragment).
  Also now clears EACH consumed fragment's +12 (`*(v1+12)=0` @0x493826) and the
  final fragment's +12 at LABEL_19 (`*(v1+12)=0` @0x49386f), matching the binary.
- Verified via a standalone harness: round-trip of a 400-byte block now returns
  400 (was 4). The sole live caller (ExecReceivedCommands @0x4941d0) discards the
  value, so dispatch behavior is unchanged; the golden test encoded the wrong
  return and was corrected.
- Golden updated: tests/unit/sim_command_receive_test.cpp ReassembleRoundTrip now
  CHECK_EQ(ret, 400).

### CheckReassemblyComplete @0x4936e4 — VERIFIED-1:1
Skip(op7)/no-block(+12==0) -> 1. First-fragment search by (+3 flag, +8 Count).
`v4 = *(u16*)(v1+16)`, unsigned compare `> 0x7E`; chunk walk `v3 += 128` until
`v3 >= v4` -> 1; missing fragment -> 0. Read-only (no +12/opcode clears, matches).

---

## command_receive.cpp

### StoreReceivedPacket @0x493f80 — BOUNDARY (node-pool model)
Copies 153 bytes, recomputes len via ComputePacketSize, appends to received list
(tail walk). The binary pulls a node from a fixed FREE list (dword_11AA49C) and,
on exhaustion, disconnects + returns 1; the recon uses a heap node pool that never
exhausts (returns 0). Documented model difference (the free list / disconnect-on-
exhaustion is host net glue).

### ReceiveAndQueue @0x493ebc — BOUNDARY (rule-4 transport model)
Standalone early-out (dword_764CE0==-1), pump, store-on-completion loop modeled
over NetTransport's SetRecvBuffer/ReceivePacket/CompletedThisCall contract
(dword_764CE4 = "still assembling"). Note: the binary's `++dword_631278` per-loop
diagnostic recv counter is not modeled (internal, unobserved). Transport is the
Win32->SDL boundary.

### ExecCommandGroup @0x4942c0 — VERIFIED-1:1 (defensive null-guard)
Gate pre-pass over frames after begin until op6; accept -> stamp begin+end op1;
reject -> stamp whole [begin..end] span op2; returns 1. The recon adds a `p != null`
guard the binary lacks (binary assumes a well-formed list).

### ExecReceivedCommands @0x494088 — FIXED (group-close dispatch gate)
Evidence: edx-gate disasm 0x4940eb..0x494176, esp. 0x494166.
- BEFORE: on group close the recon forced `dispatchGate = 0`, suppressing dispatch
  of the reprocessed BEGIN frame.
- ROOT CAUSE: at loc_494156 the binary does `call ExecCommandGroup; test eax,eax;
  jnz loc_49416C` — it only `xor edx,edx` (clears the gate) when ExecCommandGroup
  returns 0. ExecCommandGroup ALWAYS returns 1, so edx STAYS 1 and the begin frame
  (now opcode 1) IS dispatched in the same pass (then the framed command, then the
  end frame which is also opcode 1).
- AFTER: `int r = ExecCommandGroup(groupStart); if (r == 0) dispatchGate = 0;` and
  the group-close branch leaves groupStart==0 so the trailing
  `if (groupStart) dispatchGate=0` no longer fires -> gate stays 1.
- Golden updated: tests/e2e/sim_command_receive_e2e_test.cpp — a begin/cmd/end
  group now dispatches the begin (op1) AND end (op1) frames (same handler) plus the
  framed command, so group_begin_seen == 2 (was 1). e2e net-vs-direct round-trip
  passes.
- The ack/seq classification arms (advance / ==LastSyncCount / Sync(op32,[16]==14)
  / Lost) verified in order against 0x49422b..0x4942b4; cmdId mask `& 0x7FFF`
  (kSeqMask), opcode gate `< 0x60` (kNumOpcodes=96) confirmed. The dword_764CF0
  disconnect-cleanup branch is documented-omitted host teardown.

---

## command_recon2_sync.cpp

### SyncSceneObjectStates @0x500c38 — VERIFIED-1:1 (one documented uninit)
RandNext once up front; entity loop signed `*(char*)(rec+2) < 5`, skip 0xFFFF;
RandomModulo(0x122) then MoneyMultiplyByRate(+10); AppendRawField(4,1,&v,428);
4 barrier passes; pass-3 RNG order rand(3)+4 THEN rand(8) (for-increment quirk
verified — 8 iters); pass-4 rand(8), op11, QueueRequest17(-2,-1,1,340,cur,0).
NOTE: pass-1 first EnqObj arg5 is an UNINITIALIZED incoming `ecx` in the binary
(`push ecx` @0x500cfc, no prior load) — genuinely non-deterministic; recon uses 0
(documented best-effort, cannot be 1:1 on undefined source).

### SyncSceneEntryExit @0x500f0c — FIXED (first EnqObj args)
Evidence: disasm 0x500f10 + 0x500f30..0x500f3d.
- BEFORE: first EnqObj used entryEntityId for v2 args:
  `EnqObj(12, entryEntityId, 18, entryEntityId, entryEntityId, 0,0,0)`.
- ROOT CAUSE: Hex-Rays labeled v2 as `this`/uninit. The disasm shows edx = -1
  (`mov edx, 0FFFFFFFFh` @0x500f10, never reloaded), and the call uses `ebx=edx=-1,
  push edx(-1)`. `this`(entryEntityId) is stored only into the dead ack-tracker
  slot v7[0] and never read.
- AFTER: both EnqObj calls are identical `EnqObj(12, -1, 18, -1, -1, 0,0,0)`.
- Golden updated: tests/unit/command_recon2_sync_test.cpp asserts
  `obj op=12 a2=-1 a3=18 a4=-1 a5=-1 ...`.

### SyncCharSlotAssignments @0x501064 — FIXED (slotByte persistence)
Evidence: disasm 0x501099/0x5010a2/0x5010ad + stack var v20 @[esp+Ch].
- Loop bound recovered: ecx=a1(count), `test;jle` -> if(count>0); counter v19 is
  `xor edx,edx`=0 -> count iters. (Confirmed; recon already did this.)
- BEFORE: `slotByte` (v20) was reset to 0 INSIDE the loop each iteration.
- ROOT CAUSE: v20 is one stack slot declared OUTSIDE the loop; when the inner probe
  exhausts all slots (`--v9`==0 -> LABEL_7) v20 KEEPS the prior iteration's value.
- AFTER: `slotByte` hoisted above the for-loop so it persists across iterations.
- RNG order rand(slotCount) then rand(2) for stride (1|3), %slotCount probe walk,
  MoneyRate(750), guildBank bonus rand(0x1388)+5000 — all verified. Tests pass.

---

## command_unit_orders.cpp (all hook-based models; constants verified)

### ExecPickupGroundItem @0x491054 — VERIFIED-1:1 (modeled)
Gate `*(BYTE)(a1+4) && *a1 != -1`; FindUnitById; StandUp(unit[97]); carried-object
release gated on unit[107] then unit[107]=0; InsertActionVararg tag 46 + copy action
name. Tag 46 and the action-name string @0x61bb1c ="spezial/einhaendig_vom_boden_
nehmen" confirmed via get_string. Deep object/transform mechanics are leaves (hooks).

### ExecUnitSelectSound @0x49110C — VERIFIED-1:1 (modeled)
FindRecordById latched in dword_6311F0; RenderFormattedMessage(.,3610,*u16);
SetStatusBannerText. Text id 3610 confirmed. Recon adds a null-record guard the
binary lacks (binary derefs unconditionally).

### ExecConquerFlag @0x491148 — VERIFIED-1:1 (modeled)
Gate `*(BYTE)(a1+4)`; banner text id 3611 confirmed; flag-mesh detach/attach &
holder-change branch modeled via flagAttach/flagDetach hooks. Texture-set select
(`*(v8+84)-61`) and the universe-node attach are leaves.

### CharacterAttachToScene @0x49CD10 — VERIFIED-1:1 (float 8.0f confirmed)
Position check uses targetPos (a3, +76 field), translation check uses targetDir
(a2, +132 field); tolerance constant is `push 41000000h` = FLOAT 8.0f (NOT a
double — Hex-Rays showed 8.0; disasm confirms 32-bit float). Recon passes 8.0f. ✓

---

## Build / test status

The shared `guild` CMake target is currently broken by an out-of-tree, untracked
file `src/gui/widget_layout.cpp` (another agent's WIP — `w.ld<...>` member errors).
That file is OUTSIDE this chunk; NOT edited. HANDOFF: owner of src/gui/widget_layout.cpp
must fix the `Widget::ld` template usage so libguild links again.

All five owned sources compile clean in isolation (g++ -std=c++17 -fsyntax-only).
Owned tests were built by linking the freshly compiled owned objects ahead of the
pre-break libguild.a and all pass:
- sim_command_receive_test            53 checks, 0 failures
- sim_command_receive_e2e_test        11 checks, 0 failures
- command_recon2_sync_test            42 checks, 0 failures
- scene_recon2_orchestrator_test     159 checks, 0 failures
- command_recon_syncrange_test        21 checks, 0 failures
- sim_command_unit_orders_test        43 checks, 0 failures
- sim_command_unit_orders_itest       22 checks, 0 failures
- sim_command_unit_orders_e2e_test    23 checks, 0 failures
