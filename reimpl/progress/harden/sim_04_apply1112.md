# Hardening sweep — command_apply / command_apply11 / command_apply12

Chunk files: `src/sim/command_apply.cpp`, `src/sim/command_apply11.cpp`,
`src/sim/command_apply12.cpp` (+ headers + tests). MCP live, every provenance
function decompiled AND disassembled and diffed line-for-line vs the binary.

Build: all three .cpp compile clean to object code with the real CMake include
dirs (`-I . -I include -I src -I shim -I third_party`). The full `guild` target
link is broken by the UNRELATED untracked `src/gui/widget_layout.cpp` (`Widget`
has no member `ld`) — NOT in this chunk; confirmed our files are error-free.

Legend: VERIFIED-1:1 / FIXED / BOUNDARY (rule-8 out-of-tree leaf).

## command_apply.cpp — receive-side field-patch + scalar handlers

| Func | Addr | Status |
|------|------|--------|
| RemapEntityId (shared) | (inlined -2/-3/-4) | VERIFIED-1:1 — dword_631288/63128C/631290 = g_lastObject/Scene/Trade. |
| ExPatchObjectFieldsAdd | 0x497c18 | **FIXED** — cursor discipline. |
| ExWriteObjectFields | 0x497da4 | **FIXED** — cursor discipline. |
| ExApplyNeedDeltas | 0x497ed0 | VERIFIED-1:1 — clamp const dbl_61BE74 = 1000.0 (get_global 0x408f400000000000); offset 12*statId+144; else-branch stores 0x40920000=1000.0f. |
| ExPatchObjectBitfield | 0x498024 | VERIFIED-1:1 — order {obj,scene,person}; set@0x1C / mask@0x20; ~mask&dst | set. |
| ExAddObjectFloatField | 0x498104 | VERIFIED-1:1 — order {person,scene,object} (args &v7=obj,v9=scene,&v8=person; picks v8 first); off@0x14, add@0x18. |
| ExRegisterIdPair | 0x4991ec | VERIFIED-1:1 — A=*(a1+20)=kind, B=*(a1+16)=id; ack stamped regardless. |
| ExUnregisterIdPair | 0x499230 | VERIFIED-1:1 — match A==*(a1+20)&&B==*(a1+16); stride 2/4096 = 2048 slots. |
| ExAdjustObjectCounter | 0x49d080 | VERIFIED-1:1 — +0x194, clamp [0,50]; AckBegin {2,0,0}. |
| ExAdjustCharacterReputation | 0x49d0e4 | VERIFIED-1:1 — +0x1B1, >254→0xFF, <0→0; HUD/Office leaves deferred (BOUNDARY, don't mutate record). |
| ExSetObjectFillLevel | 0x49d1fc | VERIFIED-1:1 — index>4 (unsigned) reject; clamp [0,252] via dbl_61C364=252.0; **ConvertX(0x5c6b08)+fistp = TRUNCATE**, reimpl `(int)fv` matches. |
| ExEndTurn | 0x49cec0 | VERIFIED-1:1 — turn-control fail leaves status=2 (JUMPOUT tail). |
| ExAck | 0x49bcd0 | **FIXED** — slot/seq zeroing. |
| ApplyPacket / RegisterApplyHandlers | dispatch | VERIFIED — success stamps status=1; unknown→-1 no-op. |

### FIXED — ExPatchObjectFieldsAdd / ExWriteObjectFields cursor discipline (0x497c18 / 0x497da4)
The binary consumes the 4-byte record header (width,count,off) then advances the
VALUE cursor (v3) **only** inside the recognized-width loop (widths 1/2/4 AND
count != 0). An unrecognized width — or a recognized width with count 0 — branches
to LABEL_10 (`++v27` only) and leaves the cursor at the body start, so the NEXT
record header is read from the un-skipped body bytes.
Before: `p += width*count + 4` unconditionally (skipped a phantom body for unknown
widths → desynced every subsequent record).
After: `p += 4` (header), then body skip `p += width*count` only in the
1/2/4 branches when count != 0. Recognized-width streams are byte-identical to
before; only malformed/unknown-width streams change — now matching the binary.
Evidence: disasm 0x497ca0 `v3 = v4+1` (header), per-width `v8 += k` loops vs the
LABEL_10 path that never touches v3. New golden:
`SimCmdApply.PatchObjectFieldsAdd_UnknownWidthHeaderOnlyAdvance`.

### FIXED — ExAck ack stamp (0x49bcd0)
Binary sets a non-null ack to `{status=1, slot(+1)=0, seq(+6)=0}` (it does NOT
call the AckBegin {2,..} preamble; ring +2 untouched). Before: only `status=1`
(AckOk). After: `{1,0,0}` with ring preserved. Evidence: 0x49bcdf/0x49bce2/0x49bce6.
Golden `SimCmdApply.AckCityHook` strengthened to seed slot/seq/ring and assert.

## command_apply11.cpp — 0x4FB000 debug/cheat command emitters

| Func | Addr | Status |
|------|------|--------|
| PackSlotResetScratch (helper) | — | VERIFIED — gameTime@0x28 (14B), WORD2 overwrite @0x2C. |
| QueueGiveGold | 0x4fb284 | **FIXED** — v10(0x58)=a2=-1 (was 0). |
| QueueAdjustReputation | 0x4fb300 | VERIFIED-1:1 — op80, v9=2, WORD2=16, v10/a2=-1. |
| QueueSpawnGuard | 0x4fb210 | **FIXED** — v13(0x58)=a2=1 (was 0). |
| QueueSpawnSelected | 0x4fb0f0 | VERIFIED-1:1 — gate '-'&&parse>0; v13 byte@0x58=0; a2=0. |
| QueueRemoveAllCarried | 0x4fb180 | **FIXED** — field offsets + id@0x58. |
| QueueSetJusticeSeverity | 0x4fbd00 | **FIXED** — names + law id from slot. |
| QueueAdjustJusticeSeverity | 0x4fbe04 | **FIXED** — full faithful parse + curr + names + law id. |
| QueueRevealSelected | 0x4fb89c | BOUNDARY — Person/Building/Coord chain (ComputeCurrentOutput, AdjustStockAndNotify) out-of-tree; hook stub. Note: binary returns 0 when FindRecordById fails (stub returns 1 on the "ran" path). |
| QueueAdjustAllPersonStat | 0x4fb52c | BOUNDARY — Person scan + wealth scale, cross-module proxy. |
| QueueAdjustBuildingStat | 0x4fb37c | BOUNDARY — sign table {-1,+1}=dword_4F8C40 (qword 0x1FFFFFFFF), scale flt_6207B0=0.01f, emit gated by QueryByGoodType/QueryFind/IterNext chain (out-of-tree). Parse gate (`v<=0`→1) correct. |
| QueueAdjustSelectedStat | 0x4fb614 | BOUNDARY — amount = trunc(SumCurrencyHeld(person)*parsed*0.01 [dbl_6207B8]); from/to = *(person+4). Person-subsystem-coupled proxy (uses selBase). |
| QueueMovePersonsToCoord | 0x4fb754 | BOUNDARY (not re-decompiled in depth; selection-scan proxy via ctx). |
| QueueSetSelectedFlag | 0x4fbbcc | VERIFIED gate/order — parse-before-gate, `!selBase||a2>=8`, State22 delta @off 433 width1 count1. Entity = FindRecordById(*(selBase+8*a2+4)) modeled by selBase (Person proxy). |
| QueueRevealAllPersons | 0x4fbc40 | VERIFIED gate — '-', strlen(tail)>=4, ToLower(tail+4), CountActiveObjects. Scan proxied via ctx; QueueRequest17 a5 should be byte_6477A1 (=0 static) not literal 0 — noted, no ctx accessor. |

### FIXED — QueueGiveGold / QueueSpawnGuard v10(0x58) + a2 (0x4fb284 / 0x4fb210)
v10 (scratch+0x58) and the a2 arg to QueueRequestSlotReset28 are BOTH `ecx`, and
ecx survives `VIBE_Light_SetGrayColorThunk` (0x5c6af0) because it push/pops ecx.
GiveGold sets `mov ecx,-1` (0x4fb296) → v10=a2=**-1**. SpawnGuard sets
`mov ecx,1` (0x4fb222) → v10=a2=**1**. Both were reconstructed as 0. (a2 is not
on the wire per QueueRequestSlotReset28, so no codec-golden churn; v10 IS in the
staged 248-byte body.)

### FIXED — QueueRemoveAllCarried layout (0x4fb180)
The reimpl used a +8-shifted layout (gameTime@0x30, v11@0x34, ...). That was a
misread of the IDA ebp-frame: 3 pushes shift the displayed frame during the field
stores, but `mov eax,esp` after the pops re-bases the byref so fill-base == byref
base (0x4fb18e vs 0x4fb1ef). Corrected to the Gold-identical 0x28-based layout
(gameTime@0x28, v11 word@0x2C, v12 dword@0x2E, v13@0x32, v14@0x34, v15@0x36) AND
added the missing record id at +0x58 (v16 = var_A0 = dword_12CE914[i], same value
as +0x08). Scan stride 0x218=536, 768 records, class byte ∈{6,7}. a2=`this`
(caller's ecx, ~strlen-derived from 0x4fdc40) is caller-coupled → left 0, noted.

### FIXED — QueueSetJusticeSeverity (0x4fbd00)
- Name table (aRechtsprechung @0x4f8cac, 64B, 32-byte stride, 2 entries) is
  **{"RECHTSPRECHUNG_HAERTE", "AEMTEREINKOMMEN"}** (get_bytes) — was
  {"RECHTSPRECHUNG_HAERTE","RECHTSPRECHUNG_HAERTE"}.
- Law id is NOT an external selector: it is `HIBYTE(dword@[esp+slot+0x61])` where
  v17(@0x64)=a1 with LOWORD=word_4F8CEC=0x0E00 (get_bytes 0x4f8cec → 00 0E), i.e.
  slot0→0, slot1→14. Was `(u8)lawSel`. `lawSel` is now unused (ABI placeholder).
- Two-stage clamp confirmed against disasm 0x4fbdb9..0x4fbdd1 (unchanged).

### FIXED — QueueAdjustJusticeSeverity (0x4fbe04)
Rewrote the parser to the binary's exact (quirky) form:
`'-' SIGN  skip(strlen(lawName[signSlot]))  '_'  LAWNAME  '_'  N`.
After matching SIGN∈{MINUS,PLUS} it advances the cursor by
`strlen(lawName[signSlot])` — the LAW name at the SIGN slot index, NOT
strlen(SIGN) — then requires '_', then matches LAWNAME (32-byte stride), then '_'.
(disasm 0x4fbe6f: `edi = signSlot<<5 + esp(lawNameTable)`, repne scasb strlen.)
Other fixes: law id = kLawId[lawNameSlot]={0,14} (word_4F8D3A=0x0E00); base value
is the record's CURRENT severity `rec.curr` (var_30 @ record+0x18), was hardcoded
0. Added `curr` to `GesetzRecord` (header). value = curr + sign*N, sign=+1/-1.
Clamp matches disasm 0x4fbf30..0x4fbf48. Goldens updated to the faithful format.

Header change: `GesetzRecord {applyFlag, lo, hi}` → `{applyFlag, lo, hi, curr}`
(curr read only by the ADJUST variant; default ctor and test inits updated).

## command_apply12.cpp — combat order builders + cursor router

| Func | Addr | Status |
|------|------|--------|
| CopyStrideLabel (helper) | (loop 0x488db6) | VERIFIED-1:1 — 2-byte stride copy, byte0-NUL→1 byte, byte1-NUL→pair. |
| BuildLabeledMoveCommand | 0x488d4c | VERIFIED-1:1 — preclear@0xC; label@0x10; target+4@0; kind=4@4; tileX@0x20, tileZ@0x24; side-effect order FindObjectDef→PointThroughBoneChain→WorldToTile→(fail→0)→FindOrAllocSlot. |
| BuildConquerCommand | 0x488df4 | VERIFIED-1:1 — gate `!WorldToTile || *(a2+0x1AC)`→-1; kind=6; tile bytes LOBYTE@0x20/0x21; RequestBuildOp80(*(a1+0x10),...). |
| IssueOnObject | 0x488ff0 | VERIFIED-1:1 (model) — armed latch dword_67221C; unit class @+535==2; owner @+512; attack gate `*(t+364)!=*(o+364)||byte_671D96`; sp_ESCAPE→MoveTo, sp_CONQUER→LabeledMove, WARE→Conquer; ground raycast→direct slot (kind=1, +0x10/+0x14 outs); reject banner. |

All command_apply12 functions match at the model level (WorldToTile / transform /
raycast / FindObjectDef / attack-packet / RequestBuildOp80 are cross-module leaves
routed through the ctx hooks — rule-8 boundaries, unchanged). The concrete byte
offsets, order kinds (1/4/6), tile-byte truncation (LOBYTE), branch conditions and
side-effect order were all confirmed against the disasm.

## Counts
- VERIFIED-1:1: 18 functions (10 in command_apply, 4 in command_apply12 incl. helper, 4 gate/order-verified in apply11).
- FIXED: 7 functions — ExPatchObjectFieldsAdd, ExWriteObjectFields, ExAck (command_apply); QueueGiveGold, QueueSpawnGuard, QueueRemoveAllCarried, QueueSetJusticeSeverity, QueueAdjustJusticeSeverity (command_apply11). [8 edits incl. GesetzRecord header]
- BOUNDARY: 5 functions — QueueRevealSelected, QueueAdjustAllPersonStat, QueueAdjustBuildingStat, QueueAdjustSelectedStat, QueueMovePersonsToCoord (Person/Building/GameObject subsystems out of this module's tree).
- Goldens fixed/added: 4 (PatchObjectFieldsAdd_UnknownWidthHeaderOnlyAdvance [new], AckCityHook [strengthened], SetClampsIntoRange + SetRejectsOutOfRangeWithFlag + AdjustSignedDeltaClamped [JusticeSeverity reformatted to binary]).

## Handoffs / notes (out of chunk)
- **ODR/duplication**: `command_apply8.h/.cpp` ALSO reconstructs 0x4fbd00 / 0x4fbe04
  as `QueueSetJusticeSeverity(int,i32)` / `QueueAdjustJusticeSeverity(int,int,i32,i32)`
  + `JusticeClampAbsolute/Delta`, in the SAME `guild::sim` namespace. Different
  signatures → C++ overloads (no link clash), but the same binary function is
  reconstructed twice. command_apply11's versions are now the more faithful (names,
  law id, parse quirk, curr). Owner of command_apply8 should decide which survives.
- BUILD blocker (pre-existing, not ours): `src/gui/widget_layout.cpp` references a
  non-existent `Widget::ld` member; it aborts the `guild` link before sim/.
- BOUNDARY return-semantics: QueueRevealSelected should return 0 (not 1) when the
  Person record isn't found; faithful only once the Person/Building chain is wired.
- QueueRevealAllPersons: QueueRequest17 a5 = byte_6477A1 (mutable game global, =0
  in the static image) — no ctx accessor; reimpl's literal 0 matches the static
  default only.
