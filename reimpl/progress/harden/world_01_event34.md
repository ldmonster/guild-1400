# Hardening sweep — world/event3.cpp + world/event4.cpp

MCP-driven line-for-line diff of every `gilde.exe 0xADDR` function in
`src/world/event3.cpp` and `src/world/event4.cpp` against the binary
(decompile + disasm + get_bytes). Constants confirmed by bytes; every
float→int site checked against the disasm; RNG draw count/order verified;
signed vs unsigned shifts and struct strides checked.

Verified standalone-compiled both .cpp + both unit tests, then linked &
ran the unit suites: **113 checks, 0 failures** (incl. the rewritten
AllocKillPlayer goldens and all WorkActionRun/UpdateBuildingHeState fixes).
The full `guild` library build is currently blocked by an UNRELATED
pre-existing error in `src/gui/widget_layout.cpp` (`Widget::ld` — not my file).

## Constants verified by get_bytes
- flt_6476FC = {8,7,8,9}; flt_64770C = {20,21,20,19}
- flt_61FD3C=0.1f; flt_61FD40=5.0f; dbl_61FD48=100.0; flt_61FD50=100.0f
- dbl_620418=1.47; dbl_620100=0.01; dbl_620108=-0.5; dbl_620110=0.25
- byte_6477A1 = 0 (the QueueRequest17 "flag" arg in several bodies)
- GetSeasonFromDay (0x58339c) = `day % 4`; ConvertX (0x5c6b08) = trunc-toward-zero
- GameTime layout: day@+0, hour@+4(w), minute@+6(dw), second@+10(dw)

---

## event3.cpp

- **0x4ef500 StartActorAction** — VERIFIED-1:1. +176:=200, clock→+180/+68/+82/+96,
  Advance(&+180,0,0,10*(200/5u))=+400 min. The advanced GameTime image is at +180
  (the +176 dword holds 200), matching the disasm.
- **0x4ef5d4 MoveTowardTargetRun** — VERIFIED-1:1. Both float→int sites use ConvertX
  (trunc): `movedI = trunc(moved)` (fld var_20→ConvertX→fistp) and
  `+176 = trunc((double)(unsigned)+176 - moved)` (fild unsigned 64-bit, high=0).
  Stock-cap gate `if (+176 != 0u && (ratio*100 + moved) < 100f)` matches (jbe==jz).
- **0x4ef270 GatherTargetsInit** — VERIFIED-1:1 (host-state candidate list
  abstraction documented). Slot reset 44..58 (15 dwords), single RandomModulo(count)
  only when seed +172==-1, follower fill at index 43+slot. RNG count/order match.
- **0x4ef7e8 RequestGuardInteraction** — VERIFIED-1:1. Spawn path RNG order:
  RandomModulo(12) → ComputeVariantIndex(+1,1) → RandomModulo(5)+22. Guard-slot
  path checks slot 5 (byte +552) then 4..0 (+551..+547). 5th QueueRequest17 arg = 0.
- **0x4efb64 SinkToGroundStateMachine** — VERIFIED-1:1 (script-finish + person-field
  writes person+8/+364, StandUp/InsertAction are Script/Person BOUNDARY: person is
  opaque void* here, no mutation hook). switch on (unsigned)(counter+2). phase
  transitions, Advance(+82, +1 day) match.
- **0x4efcdc AllocKillPlayer** — **FIXED** (logic was inverted + wrong record).
  Disasm (loc_4EFD54/loc_4EFD1B, edx=self throughout): a matching OTHER kind-114
  handler ⇒ **free SELF** (`mov eax,edx; FreeHandlerEntry`) and return its result;
  NO matching handler (first-null or next-exhausted) ⇒ **reset SELF** (+180=-1,
  +176=-1, +82←+68 via movsd×3+movsw, +132=-1) and return null.
  Before: recon reset the *found* handler when found and freed self when not found
  (backwards). Also fixed the two goldens in event3_test.cpp
  (AllocKillPlayerMatchFreesSelf / AllocKillPlayerNoMatchResetsSelf) to the binary.
- **0x4f01d0 SetActorAnimById** — VERIFIED-1:1. +82←clock, +68←clock,
  +172=(u16)RandomModulo(5)-5, base=flt_6476FC[day%4] via ConvertX, +88=0,
  +86=(int)base, return (int)base.
- **0x4f1bc0 PrepareInfoAction** — VERIFIED-1:1. +82←clock; day dword overwritten
  with clock.day(+4 if a kind-131 handler exists); +86=8, +88=0;
  InitAndShuffleDwordArray(0x1A=26, ...). (advice array = host-supplied, documented.)
- **0x4f24e0 CancelMatchingActors** — VERIFIED-1:1. kind-35 walk, inner arm-gate
  (flag bit1|bit2), match `it+176 == self+4`, StampTimeAndRequest. (self via ecx.)
- **0x4f4114 NewDepositMessageBoxRun** — VERIFIED-1:1 (active-window dialog gate
  `dword_75BF04 == *(a1[29]+8) && dword_75BF38==1210` is host UI state → BOUNDARY,
  inert hooks never trigger it). rich-string arg 2*(+172)+2151. phase advance match.
- **0x4f4fbc RequestSlotResultRun** — VERIFIED-1:1. case 2 QueueRequest17 5th arg =
  byte_6477A1 = 0 (matches recon). HIWORD(+178) objId. case 3 "GetPacketSeqById then
  *(seq+2)" collapsed into the packetSeq hook (BOUNDARY; removed misleading dead
  `objId` code). case 4 cursor never increments (clones the engine's 0>32 loop).
- **0x4f5270 UpdateListenerFromActor** — **FIXED** (float-add precision). The x87
  `fld(float)+fadd(double dbl_620418=1.47)+fstp(float)` adds against the *double*
  1.47 in extended precision; was `pos[0] + (float)1.47`. Now
  `(float)((double)pos[0] + 1.47)`. Rest (read 33/34/35, listener push, --+208) 1:1.
- **0x596074 MatchPersonState15Cmd272** — VERIFIED-1:1 (state byte = host arg).
- **0x5960c0 MatchType26OrJump** — VERIFIED-1:1 (tail-jump→false model).

## event4.cpp

- **0x4f3b34 HarvestWageRun** — **FIXED** (objId signedness). The QueueRequest17
  objId is `HIWORD(*(a1+170))` = UNSIGNED high word (zero-extended u16); was a signed
  `>>16`. The text-id `2*(*(int*)(a1+170)>>16)+2151` correctly stays signed sar.
  Cancel-table loop (768 slots, dword_12CEA8C[134*p]/word_12CE910[268*p]) abstracted
  via hooks. base=table[65*type+54] via resolved-rec +54 (documented). RNG: one
  RandomModulo(40*base). 5th QueueRequest17 arg = 0.
- **0x4f3d7c HarvestProcessRun** — VERIFIED with documented BOUNDARIES. Cancel,
  two resolves, ready-node find (+7==1), final wage (one RandomModulo(30*base)) all
  1:1. The multi-good city scan (word_13CE860 count, dword_13CE298 table, 4-member
  prot gate, dword_13CE27C rows) and the `*(v18+20)` deref are host-table state with
  no hook surface — left as documented BOUNDARY (inert hooks ⇒ no further goods).
- **0x4f2614 WorkActionRun** — **FIXED** (3 divergences):
  1. `rate` (v25) is a FLOAT (`(double)cnt*0.01+1.0` rounded to float, then float
     `+= +196`); was double — changed to float for x87-faithful rounding.
  2. Per-member loop walks **8** dwords at +140 (v5 = a1..a1+28), not 4; the
     favorability call passes count 8. Fixed `slot < 8`.
  3. The drain factor: disasm 0x4f2799 `v11 = 2*a1[5] + a1[6]` (=2*+20 + +24);
     `rem = (double)(signed)+176 - (double)(step*v11)*rate`. Was missing the
     `(2*+20 + +24)` factor (only used `step`). Fixed. ConvertX trunc on the store.
  Also the refill loop's QueueRequest17 objId set to unsigned HIWORD; qty (min(rowWord
  +54, node[7])) and the `+176 += row+34` read the dword_13CE27C building-type table
  (no hook) → documented BOUNDARY (modelled qty 0 / += 0).
- **0x4f0250 NightWatchmanAnnounceRun** — VERIFIED-1:1. Day gate (+68==clock.day),
  morning/evening windows [hour, hour+1), bell on byte_63CC40, announce gate
  `announce && audio && RandomModulo(10)>7 && audio` (one draw), per-hour samples
  7/8/9 & 19/20/21 (autumn==season 2 variants, prio 1000 for 9), spruch second draw,
  +86/+88 sets, ++counter; night window hour>=22 & minute∈[30,50). LABEL_23 nudges
  the GLOBAL clock +1 second. RNG draw counts/order match. All sample strings/prios
  verified.
- **0x4efd88 ConversationSinkRun** — VERIFIED-1:1 (request-39 payload build via the
  hook + dword_6498E4 default broadcast target = BOUNDARY). Front gate
  `rec+520==-1 || counter==6`; all 8 phases, Advance deltas (+5/+30/+1/+2 min,
  +2 days), packet poll, cutscene-slot path match. Script finish IS wired here.
- **0x4f56f8 QueryBuildingHeMax** — VERIFIED-1:1. AppendCollectedHandle stores
  walked nodes at v17[1..count]; the scan processes v17[2..count] (==recon
  nodes[1..count-1] given collectHeNodes returns the walked set). Needs count>1.
  `nodeSetLevelBase(0)`, level-segment parse, max of nodeLevel(node,0). Returns 0
  when <2 nodes.
- **0x4f52f8 UpdateBuildingHeState** — **FIXED** (spurious restore). Branch target<
  current: for segs that are neither 1 nor 2, the original takes LABEL_45 with NO
  RestoreObjectStates call (just advances); recon was calling
  `restoreObjectStates(node,0)`. Removed. segs==1 ⇒ show iff level<=target; segs==2
  ⇒ show iff lo>=target && hi<=target. target==-1 hide-all and target>current
  reveal-next/detach branches verified 1:1. (HideNode helper uses branch-1's
  set-state-then-remove-mesh order; RemoveMesh is independent of +535, so
  behavior-identical to branch-2's order.)

---

## Counts
- Functions audited: **22** (14 in event3, 8 in event4).
- VERIFIED-1:1: **17**
- FIXED: **5** — AllocKillPlayer (inverted logic + wrong record, + 2 goldens),
  UpdateListenerFromActor (float-add precision), HarvestWageRun (objId unsigned
  HIWORD), WorkActionRun (float rate + 8-member loop + 2*+20++24 drain factor +
  refill objId), UpdateBuildingHeState (spurious restore on non-1/2 segment counts).
- Documented BOUNDARIES (rule 8 — host-table/cluster state with no hook surface):
  SinkToGround person-field writes; NewDeposit active-window gate; RequestSlotResult
  seq deref; HarvestProcess city-scan + table rows; WorkAction building-type rows;
  ConversationSink request-39 payload + default broadcast target.
- Tests: event3_test (2 AllocKillPlayer goldens rewritten to the binary) +
  event4_test — **113 checks, 0 failures**.
