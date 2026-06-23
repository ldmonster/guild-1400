# Harden — src/sim/ai_meister_passes.cpp (work-order pass DRIVERS)

Module: the three MeisterAi work-order pass drivers around the pure cores in
`meister_mgmt_recon.{h,cpp}`.

| addr | function | verdict |
|------|----------|---------|
| 0x45379c | VIBE_Ai_AssignIdleWorkers → MeisterAssignIdleWorkers | VERIFIED-1:1 |
| 0x45d4c4 | VIBE_MeisterAi_CancelMatchingTasks → MeisterCancelMatchingTasks | VERIFIED-1:1 |
| 0x45d618 | VIBE_MeisterAi_DispatchOrders → MeisterDispatchOrders | FIXED (modes 21 + 40) |

## Per-function detail

### 0x45379c MeisterAssignIdleWorkers — VERIFIED-1:1
Diffed decompile + disasm. Confirmed:
- WO stride: disasm `add ecx,58h` (88) — pseudocode's `v3+=44` is word-stride
  bookkeeping; reimpl indexes `kWorkOrderStride*i` (88). Correct.
- effStock read: disasm `cmp ds:dword_B564A4[ecx],0` with ecx byte-stepping 88;
  B564A4 = B56464+0x40 → reimpl reads `+kWO_effStock(0x40)`. Correct.
- flag test: `test byte ptr word_B564BC[ecx],8` → `IdleWorkerShouldQueue` (busy==0 &&
  (flags&8)==0). Correct.
- product hi: `mov edx,(dword_B56464+2)[ecx]; sar edx,10h` → `woItemHi` then `(i16)`;
  QueueRequest20's arg1 is `__int16`, so the sar/logical-shift difference is erased by
  the i16 truncation. Correct.
- building id: `mov eax,[esi+2]` → `*(wsNodeBldgId)` from g_aiIdleWorkstationNode+2.
  Correct.

### 0x45d4c4 MeisterCancelMatchingTasks — VERIFIED-1:1
Diffed decompile + full disasm. Confirmed:
- bldgId from `*(bldgRec+1)` (disasm `mov edx,[eax+1]`). Correct.
- Three filter arms (a2==4→mask8, a2==8→mask0x10, a2==3→mask8); other filter types
  leave v5=0 and skip the person walk. Reimpl's `CancelTaskFlagMask` returns 0 for
  unhandled types and the scan is gated on `flagMask!=0`, exactly matching. Correct.
- order/handler key compare uses arithmetic `sar ...,10h` both sides → `TaskMatchesOrder`
  uses signed `>>16`. Correct.
- handler key at `[edi+0AAh]` = handler+170. Correct.
- person walk: stride 0x218 (536) × 768 = 0x64800 (411648); marker!=-1 &&
  `edi(handler) == dword_12CEA8C[busy]`; ChangePlayerAction(`*(meister+0x16C)`,0,0,marker).
  Correct.

### 0x45d618 MeisterDispatchOrders — FIXED
Modes 20, 4/8/3 (found + not-found) verified 1:1. Two real divergences fixed:

**FIX 1 — mode 21 product id source (was a value bug).**
Disasm 0x45db00 `mov ecx,eax` after `VIBE_GameObject_QueryFind(...,2,6,0,221)` puts the
QueryFind RESULT in v4/ecx; 0x45db8c `test ecx,ecx` / 0x45db94 `mov eax,[ecx+2]` reads
`*(QueryFindResult+2)` into the product id (var_CC). The reimpl was reading
`*(orderRow4+2)` (a1[4]) instead — wrong source. Fixed to capture the queryFind result
and read `*(result+2)`; when the result is null the original falls to
`FindWorkProductObject` (0x587674, NOT in g_meisterLeaves → documented gap) and on its
failure sets the product id to -1, which the reimpl now mirrors (extra1 = -1).

**FIX 2 — mode 40 Amt-gated emission (was a rule-8 analogue / fabricated emit).**
The prior reimpl emitted a command for every qualifying worker with NO Amt free-slot
check, and never reproduced the "no free slot → return without ++passIndex" path. The
Amt slot table is a RAW embedded pointer at `*(bldgRec+113)` (disasm 0x45d802,
`*(_DWORD**)(...)`), not a record handle — read it raw. Reconstructed 1:1 using the
existing `world::AmtFindRecordByKey` + `world::AmtSlot`:
- He-loop (0x45d863..0x45d897): `RecordByKey = AmtFindRecordByKey(v85, handler[0xB8])`;
  if found and `*(slot+8)>>16 == (i16)*orderRow` then `--v86`.
- gate `if (v86 <= already) → ++passIndex,return` (0x45d8a3).
- worker loop (0x45d8b2..0x45da41): per qualifying worker, scan 64 slots for a free one
  (`*(char*)(slot+0xD) > 1 && *(int*)(slot+0x10)==-1`, signed byte compare); if NONE →
  `return` WITHOUT ++passIndex (0x45d94c→0x45d7e6); else build cmd with mode byte=1,
  targetId=bldgId, srcId=`*(freeSlot+0)`, extra0=g_personIds[pIdx], push, ++already.
  Tail: `++v87; if(already>=v86)→++passIndex; if(v87>=411648){++passIndex;return}`.

Other verified items:
- actorId `dword_12CE914[134*owner]` (disasm 67*owner via shifts, ×8 scale = person
  record `owner` at +4) == `g_personIds[owner]`. Correct.
- DispatchOrderCount distribution matches `idiv` sequences in every mode. Correct.
- worker-scan predicate (alive/employer/profByte/!busy/actionObj/`*(ao+44)==*(emp+1)`)
  matches modes 20/21/40; modes 4/8/3 found-path drop the actionObj check (orig calls
  ChangePlayerAction directly). Correct.

## Remaining documented gaps (out-of-tree leaves, NOT papered over)
- `VIBE_Building_FindWorkProductObject` (0x587674): not in g_meisterLeaves. Used by mode
  20 (cmd.extra1) and the mode-21 fallback. Mode 20 sets extra1=0; mode 21 fallback sets
  -1 (the orig's not-found branch). Faithful within the available interface.
- Mode 40 Amt table is read from `*(bldgRec+113)` raw; in the headless build with no
  bridge this is whatever the building record holds (null → no slots → no emit, which is
  itself a faithful outcome).
- The 248-byte MeisterCommand is modeled by the abstract `MeisterCommand` struct shared
  across the whole cluster; exact per-mode stack-offset placement (var_3B8/3B4/3B0/...) is
  collapsed onto named fields. Value-level captures verified; byte-offset fidelity is a
  pre-existing cluster-wide modeling choice, unchanged here.

## Tests
`tests/unit/ai_meister_passes_test.cpp`: 31 tests / 69 checks, 0 failures.
Added 4 hardening tests:
- Mode21_ProductIdFromQueryFind — extra1 == `*(queryFind+2)`, decoy orderRow4 ignored.
- Mode21_NullQueryFind_ProductMinusOne — extra1 == -1.
- Mode40_FreeSlot_Emits — emit with mode=1, srcId=`*(slot+0)`, extra0=personId.
- Mode40_NoFreeSlot_NoEmit — no command, passIndex untouched (early return).

Build/run verified standalone (full lib link blocked by unrelated pre-existing break in
command_apply5.cpp/charaction_steps5.cpp — not this module):
`g++ -std=c++17 test + ai_meister_passes.cpp + meister_mgmt_recon.cpp +
world/amt_slot_table.cpp + minimal global defs` → 69 checks, 0 failures.
`-fsyntax-only` clean on both .cpp and the test.
