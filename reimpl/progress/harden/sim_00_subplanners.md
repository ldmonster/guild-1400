# Harden pass — src/sim/ai_meister_subplanners.cpp

Module: 8 MeisterAi sub-planner loop drivers. Each function decompiled AND
disassembled via IDA MCP (gilde.exe, imagebase 0x400000) and diffed line-for-line
against the reimplementation. Constants re-verified with get_bytes.

Files touched: `src/sim/ai_meister_subplanners.cpp`,
`tests/unit/ai_meister_subplanners_test.cpp`. Nothing else edited.

Both edited TUs pass `g++ -std=c++17 -fsyntax-only`. The full test target cannot be
linked in this tree because of an unrelated broken WIP file (`src/gui/widget_layout.cpp`,
not owned here) plus missing `Calc*`/`g_buildingTypes` defs from other in-flight TUs.

## Per-function verdict

### 0x45c670 MeisterHireStaff (eax=mr) — VERIFIED-1:1
Owner-kind gate (skip only kind∈{6,7} && bit0x02 clear), SyncProfessionState sweep
(+560 profType, field174>=4), worker count, He handler scan (handler+176..+188),
cap561+cap562, wage via ComputeWageByCategory + ConvertX/fistp truncation `(int)`,
RNG gate `RandomModulo(0x48) >= 36-2*v6` with the `!v6` short-circuit and `!v26`
handler check, actorId = g_personIds[ownerWord], budget -= v27. All offsets and the
RNG draw order match. No changes.

### 0x45d2ac MeisterTrainStaff (eax=bldgNode, edx=mr, ebx=cap) — FIXED (2)
1. **0x45d2ff** `cmp [ecx+0Eh],3 / jge return`. ecx == entry eax (bldgNode):
   RandomModulo and He_FindFirstHandlerByFilter both push/pop ecx (callee-saved),
   so ecx still holds bldgNode. Added the missing real gate
   `*(i32*)(bldgNode+14) < 3`. The reimpl had dropped it (mislabeled as a stale He
   scratch).
2. **0x45d36c** `extra0 = *(_DWORD*)(bldgNode+2)` is a DWORD, not a word. The reimpl
   read only 2 bytes — widened to a 4-byte read.
byte_6477A1 re-verified = 0x00.

### 0x45df7c MeisterFlagIdleStaff (eax=mr) — FIXED (1)
First loop, RNG gate, second flag sweep all matched. **Fix:** the `!v2` branch
(0x45e119). When no idle worker is found the first loop runs all 768 iters so
v3 == kPersonCapacity (768); the original then reads byte_12CE993/992[536*768] —
a one-past-end OOB read of the zero-adjacent free slot — and still draws
`RandomModulo(0x80)`. The reimpl's `if (v3 < kPersonCapacity)` guard SKIPPED that
draw, desyncing the global RNG. Now the one-past slot is modeled with gauges = 0
(so the `<252` test is true and the RNG IS drawn), preserving RNG draw count/order.

### 0x45e71c MeisterCollectTransporters (eax=mr) — FIXED (2) + 1 handoff
1. **0x45e7eb** scene-node owner check is `*((_DWORD*)v3 + 7)` = byte offset **28**
   (`mov edx,[ebp+1Ch]`), not 14. The reimpl read at +14. Fixed.
2. **0x45ead5/0x45eae3** lost-transporter command: cl=0x0F set before
   SetGrayColorThunk (push/pops ecx) and stored to var_130 → **cmdType = 15**, not 2.
   Fixed. (mode byte = 2 from `mov ch,2` is unchanged.)
Verified: nodeId at +2, He target at +0xAC (43*4), next-chain at +6, counters
init 0, horse(310)/non-horse branches, cap +583, RandomModulo(0x2EE)<2, item
selection RandomModulo(8)>=4 ? 310:309, `2*price < budget`, ConvertX truncation.
**HANDOFF (leaf):** ComputeMarketPrice @0x58f3d0 caches `*(v3+56)` and the original
calls it TWICE per buy (the 2nd call returns the cached value). The reimpl calls
the `marketPrice` leaf once. ComputeMarketPrice draws NO RNG, so the RNG stream is
unaffected, but to be byte-exact the `marketPrice` leaf must model the
first-call-computes/second-call-returns-cache behavior and be invoked twice.

### 0x45f1e4 MeisterTradeManageStorage (eax=mr, edx=a2) — DEFERRED (unchanged)
Outer "Morsches Holz" gate + stock-row scan + type-22 half-capacity adjust are
reproduced. The inner restock dispatch needs Inventory leaves
(GetSlotCapacity/CollectWorkstationSlots, QueueRequestState22/BeginDelta/AppendDelta)
not in MeisterAiLeaves — left deferred per rule 8 (already documented in-file). The
edx=a2 `*(u16*)a2 == 0x116` gate is not threadable through the current signature.
No regressions introduced; not re-hardened beyond confirming the deferral notes.

### 0x45cfac MeisterFillAiSlots (eax=bldgNode, edx=mr, ebx=cap) — FIXED (3)
1/2. **0x45cff8/0x45cffb** accumulator seeds read **bldgNode** (entry eax / v31),
   not mr: usedWorkers seed = `bldgNode[0x1C]`, usedGuards seed = `bldgNode[0x1D]`.
   The reimpl seeded usedWorkers from `mr[29]` and usedGuards from 0. Fixed.
3. **0x45d129** `extra0 = *(_DWORD*)(bldgNode+2)` (DWORD off bldgNode), not a word
   off mr. Fixed. Documented that var_B4/var_B0 (v29/v30 shortage flags) are two
   separate dwords; MeisterCommand exposes one aux here (carries v29).
Verified: handler walk (+176/+180), AiSlotDeficit (caps +576/+577, fire, cash>=24000),
spend 8000*slotCount.

### 0x45c9ac MeisterRenovateBuilding (eax=mr) — FIXED (5)
1. **0x45c9c1/0x45c9cf** entry gate is bit **0x08 of the BYTE at +456**
   (BYTE1(result)=*(mr+456), mask 0x800), i.e. `rd8(mr,kM_flags2)&0x08` — the same
   "done" bit the siblings use. The reimpl tested the dword & 0x800 (wrong offset).
2. **0x45ca2b/0x45ca31/0x45caf5** the ENTIRE body (room-worth block AND the item
   scan) is gated on `(kind∈{6,7}) && (dayFlags & 0x08)`; otherwise the function
   returns. The reimpl ran the item scan unconditionally. Now gated; returns early.
3. **v3 tracking** (esi init 253). Loop 1 now updates v3 on craft items (type 2/6)
   and applies the real `if (v3==253 || QueryFind) skip` guard. Loop 2 reproduced as
   the exact no-op it is (resets v3=0, so its `v3==253 && …` record gate never fires
   and QueryFind is never called; only recomputes v3; v59[0] stays 0 → return).
4. **Buy block:** keep the QueryFind RESULT pointer. If null → `flags2|=8; return`
   (0x45ce45). extra0 = `*(_DWORD*)(qfResult+2)` (0x45ceea, off the QueryFind result,
   NOT bldgRec). actorId owner word at bldgRec+0x25 (37). QueryFind filter = {1,0,v3}.
5. RandomModulo(0x14) draw kept for RNG fidelity; the GetUpgradeLevel/ComputeRoomWorth
   cmd-28 sub-block stays deferred (leaves absent) — documented.

### 0x45e12c MeisterFindFreeStaffSlot (eax=mr) — FIXED (3)
1. **0x45e23d** targetId = `*(_DWORD*)(v7+2)` (offset +2), not +1; -1 when v7 null.
2. **No standalone cmdType=22 emit.** The cmdType=22 buffer is a TEMPLATE; the only
   QueueRequestSlotReset28 calls (0x45e31a/0x45e33d) are inside the worker sweep,
   one per qualifying worker, reusing the template with the worker's sub-type
   (byte @0x58 = 2/3) and actor id (dword @0x5c). The reimpl emitted the bare
   template once unconditionally PLUS a separate worker cmd. Now: no standalone
   emit; one template-copy per qualifying worker.
3. **v2 decrement + early exit.** The sweep loops while v2>0 and `dec ecx` (v2--) on
   each emit, re-checking `v2<=0` (0x45e296). The reimpl never decremented/checked
   v2. Added.
Verified: first sweep dereferences the busy column as a pointer (`*ptr==22 → --v2`),
sub-type `gaugeB>=gaugeA ? 2:3`, turnBits |= 4, QueryFind {2,6,0,96} (type 4) /
{2,6,0,325} (type 19).

## Golden (test) corrections
- **TEST 16** renamed `RenovateBuilding_SkipsWhenFlags2DoneBitSet`: was setting the
  flags2 DWORD to 0x800 (which leaves byte+456 == 0 → does NOT skip). Now sets
  byte+456 bit 0x08, matching the real entry gate.
- **TEST 17** `RenovateBuilding_EmitsUpgradeCmd…`: added owner kind=6 + dayFlags&8
  (required by the corrected body gate), a craft item[0] (type 2) before the missing
  non-craft item[1] (so v3≠253 and the missing-item path fires), and a >=6-byte
  QueryFind result (for the `*(qfRes+2)` read).
- **TEST 18** split: `FindFreeStaffSlot_NoEmit_WhenNoQualifyingWorker` (the old test
  expected a spurious standalone cmdType=22 with zero workers — corrected to expect
  an empty sink) and new `FindFreeStaffSlot_EmitsPerWorker_WhenWorkerQualifies`
  (worker with flagWord 0x10, action-obj target == bldg id → one emit, turnBits 0x04).
- TESTs 6 and 13 still pass unchanged (their dummy nodes are zeroed, so the new
  bldgNode+14 / bldgNode+0x1C/0x1D reads evaluate as before).

## Counts
- Functions hardened: 8 (7 with diffs; TradeManageStorage deferral re-confirmed).
- VERIFIED-1:1 with no change: 1 (HireStaff).
- Source fixes: 16 distinct divergences across 6 functions
  (TrainStaff 2, FlagIdleStaff 1, CollectTransporters 2, FillAiSlots 3,
   RenovateBuilding 5, FindFreeStaffSlot 3).
- Golden corrections: 3 tests rewritten (16, 17, 18→18+18b).
- Cross-file handoffs: 2 (marketPrice cache/double-call; MeisterCommand could
  use a 2nd aux dword for FillAiSlots v30 and for FindFreeStaffSlot's
  cmdType-22-template sub-type vs worker actor split).
