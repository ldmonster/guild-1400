# Harden sweep — src/sim/charaction_steps6.cpp (+ .h, + tests)

MCP-verified 1:1 against gilde.exe (imagebase 0x400000). The file had UNVERIFIED
edits from a rate-limited prior agent; everything was re-derived from decompile +
disasm. NOTE: an external revert wiped my first pass mid-run; all fixes were reapplied.

Build: `src/sim/charaction_steps6.cpp` + `.h` and all four test files compile clean
(`-fsyntax-only`, full project flags). The `guild` target itself does NOT link, but
solely because of a pre-existing break in `src/gui/widget_layout.cpp` (`w.ld<...>`),
which is OUTSIDE this chunk — handoff to that file's owner. To prove my code, I linked
a standalone binary (my obj + tests + test_main + npcaction/gametime/rand/math_random):
- unit  charaction_steps6_test:  74 checks, 0 failures
- itest charaction_steps6_itest:  8 checks, 0 failures
- e2e   charaction_steps6_e2e_test: 19 checks, 0 failures

## Per-function status

### RegisterHandlerTable @0x4db940 — FIXED (table data)
Re-derived the 66-entry (type, init=edx, step=ebx) table from the disasm. The numeric
init/step columns were corrupted for a large block (0x5E onward). Fixes (init/step):
- 0x5E init 0x4d1cf0→0x4d1ccc
- 0x62 step 0x4d49b8→0x4d3c50
- 0x64 0x4d4b00/0x4d4d24→0x4d43f8/0x4d4460
- 0x65 init 0x4d4e40→0x4d493c
- 0x66 0x4d5180/0x4d54b0→0x4d4f34/0x4d4fcc
- 0x67 0x4d56c0/0x4d5900→0x4d52b4/0x4d5308
- 0x68 0x4d5b40/0x4d5d80→0x4d5c24/0x4d5cdc
- 0x69 0x4d5fa0/0x4d61c0→0x4d577c/0x4d57a0
- 0x6A 0x4d63e0/0x4d6600→0x4d63bc/0x4d63e8
- 0x6B 0x4d6820/0x4d6a40→0x4d665c/0x4d66b4
- 0x6D 0x4d6c60/0x4d6e80→0x4d6a28/0x4d6a8c
- 0x6F 0x4d70a0/0x4d72c0→0x4d71d8/0x4d72ec
- 0x70 0x4d74e0/0x4d7700→0x4d75e8/0x4d766c
- 0x74 0x4d7920/0x4d7b40→0x4d79e8/0x4d7bd0
- 0x76 0x4d7d60/0x4d7f80→0x4d877c/0x4d8900
- 0x77 0x4d81a0/0x4d83c0→0x4d8a94/0x4d8af4
- 0x78 0x4d85e0/0x4d8800→0x4d9600/0x4d96f8
- 0x79 0x4d8a20/0x4d8c40→0x4d9a38/0x4d9a60
- 0x7A 0x4d8e60/0x4d9080→0x4d9f1c/0x4da7c0
- 0x7D 0x4d94c0/0x4d96e0→0x4da920/0x4da978
- 0x7E 0x4d9900/0x4d9b20→0x4dada0/0x4dadd4
- 0x7F 0x4d9d40/0x4d9f60→0x4daf28/0x4daf88
- 0x80 0x4da180/0x4da3a0→0x4db218/0x4db248
- 0x81 init 0x4da5c0→0x4db2b4
- 0x87 0x4dab00/0x4dad20→0x4db8ac/0x4db8c8
GOLDEN FIXED: unit test asserted `[65].initAddr==0x4dab00` (wrong) → 0x4db8ac, +stepAddr.

### InitPruegel @0x4e3734 — FIXED
- class-4 escort query 4th arg was `He_Id(victim)` (+4); disasm `(u16)*RecordById` is
  the WORD at victim+0 → fixed.

### RunPruegel @0x4e3894 — FIXED (state machine, multiple)
Jump table (jpt @0x4e3864, 12 entries) re-read by bytes. Found mislabeled states:
- case 3u (state 1) no-target: set state `4-2`(=2)→ literal **4** (disasm 0x4e39e5).
- case 4u (state 2): success set `2-2`(=0)→ literal **2** (0x4e3aa0); fail `7-2`(=5)→
  literal **7** (0x4e3a5a).
- case 6u (state 4) final branch: gated on `cityCategory`→ **victim record +2** byte
  (disasm 0x4e3e97 `mov dh,[edi+2]`); coord27 1st arg fixed (city-row +4, modelled via
  cityPersonId; no hook for word_12CE910[+4]).
- state 6 (switch 8) had a bogus body; jump table [8]==def → now pure `return sw`.
- ADDED case 9u (state 7, jpt[9]@0x4e3ab4): QueryBegin(+208)+FindRecordById(+180)+
  FindGestureTarget gating; EvaluateViolation(20,1,*(partner+4),cityPersonId,*(victim+4))
  — the prior code passed (He_F180, cityPersonId, He_F172). Sets state 8.
- case 10u (state 8): added the missing `QueryBegin(+172)==null → state=-1` guard;
  Pair36 2nd arg fixed from `He_F212` to `*(FindRecordById(+212)+4)`, gated on non-null.

### InitSpionage @0x4e2e58 — FIXED
- "already spawned" gate read +121; disasm `BYTE1(result)=*(a1+120); result&0x400` is
  `*(a1+120)&0x04` → He_Flags(h)&0x04.
- spy stride table was fabricated {1,3,5,…,31}; dword_478450 (get_bytes @0x478450) is
  {1,3,5,7,11,13,17,19,237,239,243,245,249,251,253,255} → fixed.
- dispatch advance was `72/24`(=3) days; disasm Advance(+200, v9, 0, 0) with v9=addDays
  =72 (or 96 via DispatchByType(38)==2, opaque AI query, modelled as 72) → fixed to 72.
- success gate 1st byte was resolved-object +2; disasm is the CITY-row +2 → cityCategory.
GOLDEN FIXED: itest `kStride` copy (wrong) + two "already spawned" tests (+121→+120).

### RunSpionage @0x4e3164 — FIXED
- terminal states (-1/-2) just returned state; disasm (0x4e3172/0x4e31c1) takes the
  FREE path with an optional Single49/Pair33 cleanup gated on flag bit 1 (*(a1+120)&2),
  then `freeHandlerEntry`. Reconstructed. (Cases 0–4 verified; the 256-slot scan +
  bitmask 0x7FCC3A6, the walk-in/near-door gates, the report all match.)
GOLDEN FIXED: unit RunSpionageTerminalStateBails → ...Frees (returns freeRet, free++).

### RunMeisterEinstellen @0x4dd5b4 — FIXED
- wage class arg `*(u16*)self` → signed byte `(i8)*self` (disasm `movsx edx,byte[esi]`).
- case 3 was a stub `return free`; reconstructed the full LABEL_33 path: first scan
  (recruit must resolve) pays wage/decrements slot/bumps family +76, then a FRESH full
  member scan → if a slot remains, reschedule (RandomModulo(10)+30 minutes, state=1),
  else free. RNG draw count preserved (1× RandomModulo(10), only when rescheduling).

### RunMeisterEntlassen @0x4ddac4 — FIXED
- wage class arg → signed byte (case 1 and case 2), as above.
- case 2 final reschedule condition INVERTED: source freed when slot occupied; disasm
  (0x4ddeb1) frees when the slot is EMPTY (*(a1+176)<=0), else reschedules → fixed.

### RunMoveCrowdToObject @0x4e1054 — FIXED (window) / partial BOUNDARY (harvest)
- seasonal window: hardcoded [8,20) replaced with the recovered per-season float bounds
  (flt_64770C={20,21,20,19}, flt_6476FC={8,7,8,9}, get_bytes), keyed on
  season=clk.day%4 (GetSeasonFromDay @0x58339c == day%4). Exact as integer compare.
- state machine (raw-state switch, default returns state+2), member loops, the state
  transitions and the single RandomModulo(6) harvest draw verified 1:1.

## He_SubMethodByte (header) — FIXED
Was `*(u8*)(h+169)` (low byte). Disasm `mov ebx,[ebp+0A9h]; sar ebx,18h` = signed
`(i32)*(h+169) >> 24`. Changed to a value-returning helper doing the unaligned dword
load + signed sar 24. Added `#include <cstring>` to the header.

## BOUNDARIES / documented approximations (Rule 8, out-of-tree, routed via hooks)
- DispatchByType(38) (InitSpionage 72 vs 96 days) — opaque AI query; modelled 72.
- RunMoveCrowdToObject harvest payout: uses Building_SumWorkstationByCategory,
  Inventory_ComputeFreeCapacity, ConvertX-truncated `((work*dbl+1)*amount)` and
  **QueueRequest17** (the hook surface only has queueRequest16); the amount math is
  dominated by the building/inventory subsystem. Kept as the existing approximation;
  the RNG draw (RandomModulo(6)) and control flow are exact.
- Wage value: ComputeWageByCategory returns a float converted to int via `fistp`
  (round-to-nearest-even), folded into the int-returning computeWage hook.
- coord27 1st arg / various rendered-message recipients read city-row (word_12CE910)
  +4 fields not exposed by a hook; modelled via cityPersonId where applicable.

## Counts
VERIFIED-1:1 (already correct, no churn): RegisterHandlerTable control flow.
FIXED (functions with ≥1 binary divergence): 8 functions
  RegisterHandlerTable(data), InitPruegel, RunPruegel, InitSpionage, RunSpionage,
  RunMeisterEinstellen, RunMeisterEntlassen, RunMoveCrowdToObject + He_SubMethodByte.
GOLDEN tests fixed to the binary: 5 (unit ×3, itest ×2).
BOUNDARY (documented): 3 areas (DispatchByType, Crowd harvest economy, ConvertX wage).
Tests: 22 unit + 2 itest + 5 e2e = all pass (101 checks total, 0 failures).
