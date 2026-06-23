# Harden: sim/marriage.cpp — VERIFIED-1:1

File: `src/sim/marriage.cpp` (+ `src/sim/marriage.h`)
Test target: `sim_marriage_test` — **PASS** (1/1, 7 TEST blocks).
Method: per function, `decompile` + `disasm` (disasm authoritative), line-for-line diff
against the reimpl. No divergences found; no goldens were wrong. **No edits made.**

---

## 0x58c454 — VIBE_Person_CountAdultChildren — VERIFIED-1:1
- `lea edx,[eax+0Ch]`, reads `[edx+5Ch]` = person+104; loop until `edx == eax+0x20`
  → 5 child slots person+104/108/112/116/120, stride +4. ✓
- `count` (ecx) `xor`-zeroed @0x58c460 (reimpl `count=0`). ✓
- live-actor gate `cmp byte[eax+8],0 / jbe` = byte > 0 unsigned ≡ `!= 0`. ✓
- adult gate `mov bx,[eax+0Ah]; cmp bx,0Bh; jbe` = `(u16)word > 0xB` (>11). ✓
- `*a2 = count` only if a2 non-null (`test ebp,ebp / jnz`); returns esi (adult flag). ✓

## 0x5556c4 — VIBE_Office_CollectRelativeCandidates — VERIFIED-1:1
- He filter 44; 6 relative-id fields +172/176/180/184/188/192 (`[esi+0xAC..0xC0]`). ✓
- **count pass**: `found` (edx)=1 after id#1, `inc edx` per resolve; `inGroup` (edi)
  set on any pointer-eq to refRec. id#1..#5 failure → next handler. id#6: if null →
  tail `if(inGroup) total+=found` (0x5557DA `test edi`); if resolves & ==refRec →
  `add ebp,edx` directly (0x5557C0); if resolves & !=refRec → ++found then same tail. ✓
- **fill pass**: date gate `[h+0xC5](+197) - LODWORD(qword_13CE852) <= minDelta(init 0xC8=200)`,
  signed `jle`. ✓ class table written from edx/edi/ebx regs = {0,1,2,3,3,4} into
  var_40..var_2C (`cls[i/4]`). ✓ recs[0..4] must resolve; recs[5] null/!=refRec qualifies
  only via inGroup (0x5559CA `test edx`). ✓
- fill loop: `edi` steps 0..<24 and `< 4*capacity` (`var_18`); skip null recs; write
  person(+0), class(+0x2C), handler id(+0x34, not surfaced — documented), ResolveStatusFlags,
  candMarker=`**v18`(person->marker), `++written`, eligibility(+0x30)=Eligible(refMarker,
  candMarker,filter). Side-effect order matches. minDelta updated; LAST fill's count returned. ✓

## 0x5559d8 — VIBE_Office_CollectSpouseAndBusinessCandidates — VERIFIED-1:1
- married gate `test byte[eax+1C9h(+457)],4`. ✓
- spouse: filter-71 scan, refId=`[edi+4]`; if k172(+0xAC)==refId resolve k176(+0xB0),
  else if k176==refId resolve k172; else next. ✓ (reimpl lambda identical)
- business partners: filter-24, partner id `[i+0xC4]` = +196. ✓
- count pass: spouse `inc ebp`, partners `inc ebp`. ✓
- fill pass: out[0].relClass(+0x2C)=0 then out[0].person(+0)=spouse; partners appended at
  `out[count]` stride 56, relClass(esi-0xC=+0x2C)=1, person(esi-0x38=+0); `cmp ebp,a2 / jnb`
  capacity clamp. ✓ final loop ResolveStatusFlags + eligibility(+0x30) for every entry,
  refMarker=`[edi]`, candMarker=person->marker. ✓
- NOTE: the real binary calls the filter-24 finder as `FindFirstHandlerByFilter(2,2,marker,0,24)`
  (5-arg varargs); reimpl routes it through the single-arg `HeFirst(24)` ctx leaf — the
  documented module-wide PersonRelCtx abstraction (same as person_relations.cpp). Not a
  behavioral divergence at the reconstructed boundary.

## 0x5687b0 — VIBE_NpcAction_BeginMarriage — VERIFIED-1:1
- ctxKind = `[eax+2]`; ==6 → interactive picker path, else direct. ✓
- direct: BOTH `FindRecordById([edx+4])` and `([edx+8])` always called, then check A
  (ecx) then B (esi); either null → return 0. ✓ (reimpl order matches)
- interactive: RenderText 153(0x99) → pick A; exclude slot var_2E pre-filled 0xFFFF (-1)
  by `SetGrayColorThunk(255,8,…)`; RenderText 155(0x9B) with A marker → pick B excluding A. ✓
- success: QueueCoord27(A_id=[edi+4], B_id=[esi+4], -40=0xFFFFFFD8) then (B,A,-40). ✓
- A player-class (`[edi+2]`==6||7): RenderText 3247(0xCAF, B marker), SendMessage(A_id, 1418=0x58A). ✓
- B player-class (`[esi+2]`==6||7): RenderText 3248(0xCB0, A marker pushed twice), SendMessage(B_id,1418). ✓
  NOTE: template 3248 takes the A marker as TWO varargs in the binary; the ctx `renderText`
  leaf carries one u16 — a documented UI-text-formatting abstraction gap (string layer only,
  no sim effect). Recorded in the .cpp.
- returns ebp (0/1); eax only (no edx). ✓

---

## Goldens audit
All `sim_marriage_test` golden vectors checked against the verified binary semantics and
found correct — including: adult cutoff (20=adult, 11/5 not), live-actor +8 gate, spouse
both-direction handler keying, class table {0,1,2,3,3,4}, in-group=6 / out-of-group=0 count,
capacity clamp to 2 (4*capacity dword bound), reciprocal coord -40 commands, msg kind 1418,
unresolved-partner → 0, non-player → no message. No golden changes required.

Verdict: 4/4 functions VERIFIED-1:1. 0 divergences fixed, 0 goldens corrected.
Build: green. Tests: 1/1 pass.
