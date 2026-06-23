# Harden sweep — sim_06_he (the "He" entity-handler cluster)

Chunk files (verified against gilde.exe via IDA MCP, imagebase 0x400000):
- src/sim/he.cpp
- src/sim/he_entity_query.cpp
- src/sim/he_handlers.cpp
- src/sim/he_messaging.cpp
- src/sim/handler_entry.cpp  (provenanced table ops — spot-verified, see notes)

Method: every provenanced function decompiled AND (where geometry/offsets matter)
disassembled; stack-frame offsets, table bytes, float ops, signed/unsigned compares,
RNG (none here), and return values diffed line-for-line. Disasm is the reference of
record where Hex-Rays collapsed __usercall regs.

## Constants re-verified with get_bytes / stack frames
- unk_631E98 score table: get_bytes(0x631E98, 936) — confirmed byte-for-byte
  (rows 0, 8, ... spot-decoded; full 936 bytes captured). kHeScoreTable correct.
- flt_61E73C = `db 0f c9 40` = 0x40C90FDB = 6.2831855f (2*PI). CONFIRMED.
- dbl_61E744 = 0x3FE0000000000000 = 0.5. CONFIRMED.
- 42C80000 = 100.0f circle radius. CONFIRMED (disasm 0x4c6619).
- Message-header field offsets recovered from IDA stack frames (var_104/var_10C
  base @0x1fa0): payLen/a5 = +0x58, a6 = +0x5C, a7 = +0x60, contact = +0x68,
  flags = +0x9C. (The source had 0x98/0x9C/0xA0/0xD4 — WRONG; fixed.)

## he.cpp
- He_SumPlayerHandlerValues @0x4c3a78 — FIXED.
  - Evidence: original `char v4 = byte_11BC77C[i]; if (v4 < 26) qmemcpy(...)` — a
    SIGNED-char compare. Source used `u8 action; if (action < 26)`.
  - Before: unsigned compare (action bytes 128..255 rejected).
  - After: signed compare `i8 action; if (action < 26)`; for 0..25 indexes the
    score row, 26..127 carries the previous row forward (matches), 128..255 the
    original does an OOB qmemcpy from a NEGATIVE table index — BOUNDARY: that data
    lies before unk_631E98 (out of tree); reproduced as "row carries forward" and
    documented inline. Owner(+0)/action(+6)/state(+15) offsets vs the
    dword_11BC776-based table confirmed (owner@+22, action@+28, state@+37 absolute).

## he_entity_query.cpp
- He_ResetEntityTables @0x4c44ec — VERIFIED-1:1. The shifted bases
  (dword_11BC733/745/749/758) net to clearing record cols key/+18/owner=-1,
  state=0 over records 0..511; pair loop clears key/val[0..2047] (the original's
  "skip index 0 / write 4096" is a base-shift artifact that nets to the same
  logical slots). Returns 16384.
- He_FindMatchingEntityIndices @0x4c42c0 — VERIFIED-1:1.
- He_FindMatchingEntityIds @0x4c4388 — VERIFIED-1:1.
- He_CountMatchingEntities @0x4c4458 — VERIFIED-1:1.
- He_CollectPlayerEntitiesByType @0x4c3de4 — VERIFIED-1:1 (dedup + 32-entry
  adjacent-swap network, winning slot +124, return capped at 32).
- He_CollectPlayerEntityHandlers @0x4c3ffc — VERIFIED-1:1 (state@+37 / owner@+22,
  active-count, 3-column swap network keyed on total count).
- He_SortEntitiesByRank @0x4c4548 — FIXED (degenerate return).
  - Evidence: original `result = (a1 - 1)` (count-1 as a raw pointer); for
    count<=1 the loops never run and it returns that sentinel.
  - Before: returned `buf` when no iterations.
  - After: `last = (u8*)(uintptr_t)(u32)(count-1)`. Body unchanged (full 45-byte
    swap, +28 rank byte unsigned compare, +44 trailing byte). Returns one past the
    last inner record on the normal path.
- He_RequestRivalEntityPairs @0x4c3ad4 — VERIFIED-1:1 (rival gather kind 6/7 +
    distinct id, ≤8; owned-active scan; pair match; op35 queue; notify flagged).
- He_MatchRivalEntityPairs @0x4c3c48 — VERIFIED-1:1 (caller-supplied keys; the
    original's `*v16` at the queue == keys[n] == resolved record key — equivalent).

## he_handlers.cpp
- He_NpcActionHandler @0x4cdd10 — VERIFIED-1:1. Disasm confirms edx preserves the
  record across NpcAction_Dispatch(a1+172) so FreeHandlerEntry gets the record.
  (Dispatch behind a hook guard — plumbing only.)
- He_CounterWaitHandler @0x4d0ac4 — VERIFIED-1:1 (state >= -2 gating; +172 byte
  countdown; `!v4` decrement, signed/unsigned irrelevant since only 0 matters).
- He_Entity29RequestHandler @0x4d0a10 — VERIFIED-1:1. `||` short-circuit on the
  packet-status gate matches `if(handle!=-1){...if(status==0)return status;}`;
  clock stamp from qword_13CE852 == NpcClock(); +2 min advance; queue + store +132.
- He_FindEntityHandlerOrdinal @0x4c57e0 — VERIFIED-1:1 (target = 8*67*slot = 536*
  slot; skip-then-count prof-6 records; -1 on overrun).
- He_UpdateSubsystems @0x4c5370 — VERIFIED-1:1 (tick / CharAction / Event /
  Building, each returning 1 on nonzero — plumbing via hooks).

## he_messaging.cpp
- He_SendEntityMessage @0x4c5c54 — FIXED (header offsets).
  - Evidence (IDA frame, header base var_104 @0x1fa0): a4/payLen at var_AC @0x1ff8
    = +0x58; flags at var_68 @0x203c = +0x9C.
  - Before: PutDword(header, 0x98, a4); flags byte at 0xD4.
  - After: kHeMsgPayLenOff = 0x58, kHeMsgFlagsOff = 0x9C (he_messaging.h).
    Type(+4)/sender(+8)/arg2(+12)/subkind(+0x36) confirmed unchanged.
- He_SendQuickjumpMessage @0x4c5d98 — FIXED (header offsets, same root cause).
  - Evidence (frame var_10C @0x1fa0): a5 var_B4 @0x1ff8 = +0x58; a6 var_B0 @0x1ffc
    = +0x5C; a7 var_AC @0x2000 = +0x60; contact var_A4 @0x2008 = +0x68; flags
    var_70 @0x203c = +0x9C.
  - Before: QjArg5=0x98, QjArg6=0x9C, QjArg7=0xA0, Flags=0xD4.
  - After: 0x58 / 0x5C / 0x60 / 0x9C (contact 0x68 already correct).
- He_AssignIconForHandler @0x4c6964 — FIXED (gfxPtr arg for object-board icons).
  - Evidence: kind 0x35 and 0x6B reach `CreateGfxInfo(v3, aHeAusrufezeich,
    (const char*)a1)` where a1's low dword = the Object_FindByHandle result.
  - Before: passed nullptr as the 3rd (gfxPtr) arg.
  - After: forward the objectFindByHandle() result as gfxPtr. All other kind arms
    (2 / 4 / 0x1A / 0x1B / 0x1C / 0x1E / 0x1F / 0x40) verified 1:1, incl. the
    muenze (0x1B) path that omits the +97 gfx-presence check, the +97 building
    gfx, and the local-city/office-bit gates.
- He_ArrangeIconsInCircle @0x4c64bc — VERIFIED-1:1.
  - Slot scan stride 16 / bound 1024 = 64 slots (HeIconSlot); match on +8 (mesh),
    read node +12 for both height-range and SetPosition. radius = count==1?0:100.
  - Floats: angle = (double)i * 2*PI * (1.0/count) via fild (NOT ConvertX);
    positions stored via fstp to float slots — NO float->int conversion anywhere,
    so the ConvertX-truncation bug class does NOT apply here. pos[1]=(hi-lo)*0.5 +
    baseLow; anchor[1] computed then overwritten by baseLow (preserved). Transform
    + first height-range run BEFORE the count<=0 early-out (preserved).
- EventPanel_HandleSlotClick @0x4c5b40 — FIXED (release-block result value) +
  one documented BOUNDARY.
  - Evidence: original `(result = *active, *v6==17) && ... || (result = *active,
    *active==0x87)` — the `result = *active` (WIDGET pointer) side-effect lands
    whenever the active widget is non-null, regardless of the inner sync branch.
  - Before: result stayed = g_activeSlot (the slot) when widget!=0 and condition
    false / sync set.
  - After: set `result = widget` inside `if(widget)` before the condition (then
    conditionally cleared to null). New-slot activation block already matches.
  - BOUNDARY: when the NEW slot is activated but its widget == 0, the original
    returns Form_RaiseWindows(form)'s eax; our formRaiseWindows hook is void
    (GUI leaf return value not modeled) — we return newSlot. Dominant widget!=0
    path returns the widget correctly.

## handler_entry.cpp (provenanced table ops)
Spot-verified offsets/flow against earlier waves; not re-diffed exhaustively in
this pass (the brief's key addresses were the he_* functions above). No divergence
found in the fields these touch (kind@+0, ordinal@+4, +124 gfx, +120 flags,
icon array, high-water walk). Left VERIFIED as-prior; no changes.

## Golden tests fixed to the binary
- tests/unit/he_messaging_test.cpp: header offset assertions moved 0x98->0x58,
  0x9C->0x5C, 0xA0->0x60, 0xD4->0x9C (entity + quickjump builders, flag bytes).

## Counts
- VERIFIED-1:1: 16 functions
- FIXED: 6 functions (He_SumPlayerHandlerValues, He_SortEntitiesByRank,
  He_SendEntityMessage, He_SendQuickjumpMessage, He_AssignIconForHandler,
  EventPanel_HandleSlotClick) + 4 corrected header constants + 5 golden assertions.
- BOUNDARY: 2 (Sum negative-index OOB region; HandleSlotClick RaiseWindows return
  value for the widget==0 new-slot path).

## Build/test status
All affected unit/itest/e2e targets rebuilt and green:
  he_messaging_test 61/61, sim_he_test 118/118, sim_he_entity_query_test 54/54,
  sim_he_handlers_test 46/46, sim_he_e2e 36/36, sim_he_entity_query_e2e 25/25,
  sim_he_handlers_e2e 19/19, sim_he_entity_query_itest 29/29, wire_he_test 13/13,
  he_recon3_worldpos_test 616/616.
