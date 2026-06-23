# Harden sweep — src/sim/charaction_steps3.cpp

Full line-for-line re-verification against gilde.exe (imagebase 0x400000). Every
provenance'd function was decompiled AND disassembled where Hex-Rays collapsed
__usercall args or showed uninitialized temporaries. The working-tree edits left by
the previous (killed) agent were independently re-derived, not trusted.

## Per-function status

| addr | function | status | notes |
|------|----------|--------|-------|
| 0x4ce9dc | PatrolFindTarget | VERIFIED-1:1 | personQueryBegin(1,1,counter@+172); query-miss arms cmd29(-1) then unconditional cmd29(0); both stamp +82 and +1s. |
| 0x4d0724 | GuardRequestTarget | VERIFIED-1:1 | resolve +176; on miss free (falls through); guard61(entity)->+188; +82 +6min. rankByte table dword_13CE294 = entity-cluster BOUNDARY (in hook). |
| 0x4dc590 | IsAnimalTargetBusy | VERIFIED-1:1 | disasm confirms `cmp [eax+0B0h],[ecx+1]` (ecx=eax=h) and `cmp [eax+0B8h],1`; filter FindFirst(1,0,98). key read is unaligned [h+1]. |
| 0x4d1fb8 | FindInteractionPartner | VERIFIED-1:1 | disasm: FindFirst(count=2, sel0=0 val0=95, sel1=2 val1=word[h+8]); cityIndex is a WORD (mov ax,[ecx+8]). skip-self loop; none->state(+112)=0, byte+186=0. |
| 0x4d286c | FindBeggarTarget | VERIFIED-1:1 | FindFirst(1,0,89); none-> state=5, +68->+82 (14B restore), +132=-1, cmd29(5)->ret 5. |
| 0x4d30f4 | GroupGatherInit | VERIFIED-1:1 | count !=-1 of 6 dwords @+140 stride4 into byte+216; reset 6 dwords +172..+192 to -1 (loop eax=4..24 +[eax+168]); byte+210=0; clock->+196 and +82; +2min. |
| 0x4dd150 | InitTargetState | VERIFIED-1:1 | disasm: `mov ecx,[eax+0AAh]; sar ecx,10h` (arithmetic >>16, signed); type stride 65, duration field +0x22(+34); fast-time gate dword_63C7B8. |
| 0x4dfeb4 | InitLagerErweitern | VERIFIED-1:1* | +68->+192 14B then advance (TargetId*Mult); +68->+82 then +1s; resolve +172; bit 0x20@+19 -> free; cmd25([obj+2],19,32,1,0); clock->+96; result=[+176]+u8[obj+18] clamp 100 ->+180. (*see null-deref note.) |
| 0x4e20a0 | InitSabotage | VERIFIED-1:1 | +82 +1s; slot16==-1 query (1,5,22) else (1,5,15), hit-> slot16=[begin+1]; +196=-1; buildop73(slot16)->+200; ret handle. |
| 0x4cf9c8 | DuelIntroMessage | VERIFIED-1:1 | disasm: cmd25([a2+4]/[a3+4],456,1024,4,0); if [a2+2] in {6,7} -> render 6531 name=word[a3] to=[a2+4]; if [a3+2] in {6,7} -> render 6530 name=word[a2] to=[a3+4]; ret cmd29(2). |
| 0x4d0918 | NotifyMessageInit | VERIFIED-1:1 | disasm resolved the Hex-Rays uninit `v3`: edx=rec176 (id@+176). +132=-1; flag4 early-out; +82 +24days; flag2==0 -> cmd29(0); else resolve +176(rec176) & +172(rec172); if both: kind = **rec176->byte+2** (NOT +172), name=word[rec172], to=rec176->id@+4, then cmd29(0); else restamp +82, cmd29(-1). |

## Counts
- VERIFIED-1:1: 11 / 11 functions
- FIXED (behavioral): 0
- BOUNDARY: guard-rank table dword_13CE294 (entity cluster) and the
  render/SendEntityMessage text pipeline remain routed through hooks per Rules 3–8
  (documented; not divergences).

## Findings / actions
- Prior agent's working-tree edits were UB-safe `LoadI32At` / `Poke32`/`Peek32`
  refactors for the unaligned reads ([h+1], [obj+2], [+170]). Re-derived: they are
  byte-identical little-endian loads, behaviorally 1:1. Kept.
- Hex-Rays for NotifyMessageInit showed an uninitialized `v3`; disasm
  (0x4d0972 `mov edx,eax` after resolving +176) proves the kind byte is read off
  **rec176** (+176), and the existing source code was already correct. Fixed the
  misleading `.h` doc comment that said "+172 person's byte+2" -> "+176 person's".
- InitTargetState `>>16` is `sar` (arithmetic, signed) — source uses signed i32, OK.
- (*) InitLagerErweitern: the original unconditionally derefs the resolved object
  (`u8[obj+18]`, and at 0x4dff78 frees with obj possibly null) even on a failed
  resolve; the reimpl guards `obj ? ... : 0`. The original would null-deref in that
  case; the bridge guarantees a valid record, so behavior is identical for all live
  inputs. Documented, not a divergence in-tree.
- Return-value convention: the three flag-gated early-outs (Patrol flag-clear,
  Beggar flag-set, Notify flag-set) return eax = the record pointer in the binary;
  the reimpl returns 0 (an ignored arm-leaf return; callers dispatch via the handler
  table and discard it). Consistent with sibling charaction_steps2; tests encode 0.

## Build / tests
- `cmake --build . --target guild` : clean (warnings only in unrelated
  ai_meister_subplanners.cpp).
- charaction_steps3_test: 118 checks, 0 failures.
- charaction_steps3_e2e_test: 21 checks, 0 failures.
