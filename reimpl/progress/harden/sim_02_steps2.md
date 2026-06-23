# Harden sweep — sim/charaction_steps2.cpp

Chunk owner files: `src/sim/charaction_steps2.cpp` (+ `.h`), `tests/unit/charaction_steps2_test.cpp`.
MCP live (gilde.exe, imagebase 0x400000). Every provenanced function decompiled and/or
disassembled and diffed line-for-line. GameTimeAdvance args are POSITIONAL (binary
`VIBE_GameTime_Advance(ptr, arg1, arg2, arg3)`; reimpl `GameTimeAdvance(rec, addDays,
addSeconds, addMinutes)`) — every call verified to pass the same triple as the binary.

## Per-function results

### Timestamp / appointment reset leaves
- **0x4d0a98 StateReset24** — VERIFIED-1:1. Copies 14-byte clock (qword+dword+word =
  rep movsd×3 + movsw) into +82, `Advance(_,24,0,0)`.
- **0x4d113c StateReset24Alt (twin)** — VERIFIED-1:1. Byte-identical to StateReset24
  except the relative `call` displacement (target identical absolute addr). Confirmed via
  get_bytes (48B each): only bytes 31-32 of the E8 rel32 differ (call-site relative).
- **0x4d0af0 StateAdvancePos** — VERIFIED-1:1. Clock→+82, then `*(+176)=*(+172)+1`
  (decompile writes (v1+94)=(v1+90)+1 with v1=a1+82 ⇒ a1+176 = a1+172 + 1), `Advance(24,0,0)`.
- **0x4d17b4 StateReset96** — VERIFIED-1:1. Clock→+82, `Advance(96,0,0)`.
- **0x4d1844 StateReset0** — VERIFIED-1:1. Clock→+82, `Advance(0,0,5)` (arg3=+5 minutes).
- **0x4d1904 StateReset0Alt (twin)** — VERIFIED-1:1. Byte-identical to StateReset0 (only
  the call rel32 differs); both pass (0,0,5). Confirmed via get_bytes.
- **0x4d38ec StateReset0Alt2** — VERIFIED-1:1. Clock→+82, `Advance(0,0,2)` (+2 minutes).
- **0x4d0f30 StateCopyPos3** — VERIFIED-1:1. Copies +68→+82 (3 dword + 1 word = 14B),
  `Advance(3,0,0)`.
- **0x4dd574 CopyGoalToTarget** — VERIFIED-1:1. +68→+82, `Advance(0,0,15)` (+15 min).
- **0x4dda88 CopyGoalToTargetDup (twin)** — VERIFIED-1:1. Pseudocode identical to
  0x4dd574, same (0,0,15).
- **0x4e0a08 CopyGoalToTargetState2** — VERIFIED-1:1. +68→+82, `Advance(2,0,0)` (+2 days).
- **0x4e0d54 CopyGoalToTargetState2Dup (twin)** — VERIFIED-1:1. Identical to 0x4e0a08,
  same (2,0,0).
- **0x4cf990 ArrestReset** — VERIFIED-1:1. Stamps clock into BOTH +68 and +82
  (14B each), `Advance(2,0,0)` (+2 days).

### Saved-pose restore + branch / finish
- **0x4d0b38 RestorePosAndBranch** — VERIFIED-1:1. +176=-1; copies +68→+82 TWICE (the
  binary emits the copy block twice — preserved); `r = RandomModulo(4)+2`; `bx=*(+86)`;
  `*(+112)=0`; `*(+86)=(u16)(r+bx)`; returns r. RandomModulo = 0x58b89c.
- **0x4d1384 RestorePosFinish** — **FIXED**. Copies +68→+82; `r=RandomModulo(2)`;
  if `(u16)r` the binary at 0x4d13a9 does `return VIBE_He_FreeHandlerEntry(...)` — i.e.
  returns the FREE result, not the RNG draw. Source previously did
  `freeHandlerEntry(h); return r;` (returned 1).
  - before: `{ freeHandlerEntry(h); return r; }`
  - after:  `{ return freeHandlerEntry(h); }`
  - golden fixed: `RestorePosFinish_Free` expected `rv==1` → now `rv==0` (RecFree stub
    returns 0; binary returns free result). Evidence: disasm 0x4d13a9 `call
    VIBE_He_FreeHandlerEntry` then `retn`.
- **0x4d1674 RestorePosFinishAlt (twin)** — VERIFIED-1:1. Pseudocode identical to
  0x4d1384 (same free-result return); the reimpl shares the (now-fixed) RestorePosFinish.
- **0x4d19ac ClearStateAndTimer** — VERIFIED-1:1. `*(+112)=0; *(+172)=0; return rec`.
- **0x4dc070 RetZero** — VERIFIED-1:1. `return 0`.

### Terminal-state finalizers
- **0x4d0b20 FinishIfTerminal** — VERIFIED-1:1 (control flow + predicate); BOUNDARY on the
  free-path return value. Disasm-confirmed predicate: `edx>=-2 && (edx<=-2 || !edx)` ⇒
  state==-2 || state==0 (verbatim in source). Free path: binary `return
  VIBE_He_FreeHandlerEntry(...)` whose result is a heap address (0x4c6144 returns a
  pointer/0); the reimpl signature is `HeRecord*` and returns `h`. Returning the free
  result as a portable HeRecord* is not representable; `h` is likewise truthy. Callers
  reference it only as a dispatch-table data ptr (xref: handler table 0x4db940). BOUNDARY:
  non-portable pointer-identity return; side-effects (free) reproduced exactly.
- **0x4d17e0 FinalizeEntityStep** — VERIFIED-1:1. state<-1: ==-2 free else return state;
  ==-1 free; ==0 && (flags@+120 & 2): clock→+82, `Advance(0,0,2)`, `cmd29(-1,h)` →
  `*(+132)`; return state. cmd29 = 0x4949c4.
- **0x4db514 RequestEntityFinish** — VERIFIED-1:1 on the (flags&4)==0 path; BOUNDARY on
  the (flags&4)!=0 return. Path: clock→+82, `Advance(24,0,0)`, `cmd29(0,h)`→+132, return
  handle. When (flags&4)!=0 the binary returns eax = the record base pointer (a non-
  portable heap address; unconsumed by callers). Source returns 0. BOUNDARY: pointer-
  identity return not representable; +132 / side-effects faithful.
- **0x4db558 RequestEntityIfValid** — VERIFIED-1:1 on terminal + (flags&4)==0 paths;
  BOUNDARY on the (flags&4)!=0 return (same pointer-return as 0x4db514). Terminal
  (state==-1||-2)→free; (flags&4)==0: clock→+82 (NO Advance), `cmd29(-1,h)`→+132, return
  handle.
- **0x4d3bdc ExtortInit** — VERIFIED-1:1 on the (flags&4)==0 path; BOUNDARY on the
  (flags&4)!=0 return. `*(+132)=-1`; if (flags&4)==0: `*(+184)=-1`, clock→+82,
  `Advance(0,0,2)`, `*(+112)=1`, `cmd29(1,h)`→+132, return handle. Else binary returns the
  record pointer (unconsumed); source returns 0. BOUNDARY: pointer-identity return.

### Per-frame repeat emitters
- **0x4d1870 RepeatCommandStep** — VERIFIED-1:1. Terminal gate (state<-1: ==-2 free else
  return; ==-1 free; !=0 return state); state==0: `EnqueueCmd15(-1,
  dword_12CE914[134*(u16)*(+8)], *(+172), byte_6477A1)` (routed via resolveCityId hook +
  enqueueCmd15(cityId,value)); `v=--*(+176)`; if v: clock→+82 `Advance(24,0,0)` else free.
  EnqueueCmd15 = 0x494604.
- **0x4d1930 RepeatTalkStep** — VERIFIED-1:1. Same gate; state==0:
  `RegisterApEvent((u16)*(+8), 0, -*(+172))` (hook registerApEvent(index, negValue));
  `v=--*(+176)`; if v re-arm (24,0,0) else free. RegisterApEvent = 0x4c703c.

### Group-action retargeters
- **0x4e0fa0 ChangeGroupAction** — VERIFIED-1:1. Loop i in [0, byte@+172): id=*(+140+4i),
  `FindRecordById(id)` (0x58bc6c); if found `LOBYTE(any)|=1` and
  `ChangePlayerAction(0,0,h,(u16)*rec)` (0x4b09c8). If !any → free. Else clock→+82,
  `*(+112)=1`, `Advance(0,0,5)` (+5 min).
- **0x4e1764 ChangeGroupActionAndGoal** — VERIFIED-1:1. Same loop (no "any" gate, never
  frees); then +68→+82, `Advance(0,0,1)` (+1 min). (Hex-Rays types a1 as edx:eax but only
  eax is used.)

### Handler-pool find-by-filter scans (verified via DISASM — Hex-Rays collapses the
### __usercall reg args and the flat selector/value vararg list)
- **0x4dc5dc FindPairedEntityForward (a@eax, b@edx)** — VERIFIED-1:1. ebx=a, ecx=b.
  `FindFirst(count=1, sel=0, val=0x2E=46)` (0x4c63f8). Loop: `edx=[ecx+4]` (b->id@+4);
  if `==[eax+0ACh]` (m+172): `eax=[eax+0B0h]` (m+176), `edi=[ebx+4]` (a->id@+4); if
  `eax==edi` → `xor eax,edi` (=0) return. Else FindNext (0x4c6278); end → return 1.
  Source matches (sel={0}, val={46}, the +172/+176 compares, `aId^mField`).
- **0x4dc678 FindActionByActor (marker@eax, rec@edx, outCount@ebx)** — VERIFIED-1:1.
  esi=rec; `ax=[eax]` = *marker (zero-extended). `FindFirst(count=2, (sel=2,val=marker),
  (sel=0,val=0x35=53))` — push order 0x35,0(edx),marker,2,2 ⇒ args
  (2,2,marker,0,53). edx(count)=0. If m: loop `ecx=[esi+4]` (rec->id@+4); if
  `==[eax+0BCh]` (m+188) → `xor eax,eax` return 0. Else FindNext, `inc edx`, loop.
  End: if ebx!=0 `[ebx]=edx` (count), return 1. Increment is AFTER FindNext on each
  non-match — source `m=findNext(); ++count;` matches order exactly.

## Counts
- Functions in chunk: 30 (incl. 5 twin pairs).
- VERIFIED-1:1: 29 (twins confirmed: StateReset24/Alt byte-identical via get_bytes;
  StateReset0/Alt byte-identical via get_bytes; RestorePosFinish/Alt identical pseudocode;
  CopyGoalToTarget/Dup identical; CopyGoalToTargetState2/Dup identical).
- FIXED: 1 — RestorePosFinish (0x4d1384) return value + its golden
  (RestorePosFinish_Free).
- BOUNDARY: 4 — FinishIfTerminal (0x4d0b20) free-path return; RequestEntityFinish
  (0x4db514), RequestEntityIfValid (0x4db558), ExtortInit (0x4d3bdc) (flags&4)!=0 return:
  all return the record's heap pointer (non-portable identity, unconsumed by callers);
  observable side-effects reproduced 1:1.

## Compile status / handoff
- `src/sim/charaction_steps2.cpp`, `.h`, and `charaction_steps2_test.cpp` all pass
  `g++ -std=c++17 -fsyntax-only` cleanly.
- The full `guild` library build is currently BROKEN by an UNTRACKED, out-of-chunk file
  `src/gui/widget_layout.cpp` (uses `w.ld<i32>(...)`, a member Widget lacks) — another
  agent's WIP. Not touched. HANDOFF: gui-chunk owner must fix widget_layout.cpp before
  charaction_steps2_test can link/run.
