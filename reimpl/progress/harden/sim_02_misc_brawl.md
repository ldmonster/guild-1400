# Hardening sweep — sim_02 (charaction_misc + charaction_brawl)

Files owned: `src/sim/charaction_misc.cpp`, `src/sim/charaction_brawl.cpp`
(+ their headers `charaction_misc.h` / `charaction_brawl.h` and the unit tests
`tests/unit/sim_charaction_misc_test.cpp`, `tests/unit/sim_charaction_brawl_test.cpp`).

MCP live. Every provenanced function decompiled AND disassembled; every float/int
constant confirmed via get_bytes; every float->int site checked against the disasm.

## Constant verification (get_bytes, all CONFIRMED byte-for-byte)
| sym | addr | bytes | value |
|---|---|---|---|
| dbl_6109E4 | 0x6109e4 | 7B 14 AE 47 E1 7A 94 3F | 0.02 |
| dbl_6109EC | 0x6109ec | 9A 99 99 99 99 99 B9 3F | 0.1 |
| dbl_6109F4 | 0x6109f4 | 00 00 00 00 00 E0 6F 40 | 255.0 |
| flt_6109FC | 0x6109fc | 00 00 7F 43 | 255.0f |
| dbl_61EB2C | 0x61eb2c | 00 00 00 00 00 00 08 40 | 3.0 |
| flt_61EB34 | 0x61eb34 | 00 00 A0 40 | 5.0f |
| flt_61EB38 | 0x61eb38 | CD CC 4C 3E | 0.2f |
| dbl_61EB3C | 0x61eb3c | 9A 99 99 99 99 99 B9 3F | 0.1 |
| flt_62675C | 0x62675c | 00 01 00 38 | RandomFloatScaled scale (~1/32766) |

VIBE_Coord_ConvertX @0x5c6b08: `fstcw; HIBYTE(cw)=31 -> RC field = 11 (round
toward zero); frndint` => TRUNCATES toward zero. Confirmed.

## Per-function results

### charaction_misc.cpp
- **0x4055d4 SoundActionUpdate** — VERIFIED-1:1. Branches, +140&~2 / |=2, count<1->1
  clamp, CheckQueueReady gate, +376&0x7FFFFFFF speed-propagate all match. (The
  anim+96 write is a render-leaf mirror — boundary, unchanged.)
- **0x405670 CreateSoundAction** — FIXED-DOC / KNOWN-DIVERGENCE (see Handoff #1).
  Disasm sets +12,+0,+9,+376(=1.0f),+40,+16,+20,+48,name. It does **NOT** write +8
  (ready); QueueInsertEntry @0x40c15c zero-fills the whole 404-byte node (SetGray
  Color(0,404,node)), so +8 stays 0. The recon writes `n->ready = 1`. Left in place
  (removing it would break this reconstruction's own dispatch gate in actionqueue.cpp
  line 22 `if(!node->ready) return`, which the binary's DispatchCurrent @0x404768 does
  NOT have — it gates on +20 and +0 only). Documented as Handoff #1.
- **0x405740 PlaySampleActionUpdate** — VERIFIED-1:1. callCount gate, StepMotionQueue
  ==-1 teardown (+140|=0x10, &=~2), name[0] attach, seek (+396&1), LABEL_9 anim
  marking all match.
- **0x405838 CreatePlaySampleAction** — same +8 divergence as CreateSoundAction
  (Handoff #1). Otherwise 1:1.
- **0x4058f0 SampleLoopActionUpdate** — VERIFIED-1:1. defer (motion||animB && !state&1
  -> callCount=-1), started gate, idle-pending (+140&0x10) attach, else Unlink match.
- **0x40598c CreateSampleLoopAction** — guard `(+140&0x10)==0 -> return 0` matches;
  same +8 divergence (Handoff #1).
- **0x4059f4 UseGateActionUpdate** — VERIFIED-1:1 for the translated control flow
  (slot==destRoom early-free; resolve tile -> walk type45; conditional fade type56;
  reloc type51 + name copy + fade-in type56; final Unlink). BOUNDARY: the gate-open
  test `*(mesh+533) != 1` (mesh field, render leaf) is modeled as an unconditional
  fade; and the gate object name at node+304 (used by Object_FindByHandle) is folded
  into the worldToTile hook (Handoff #2). The scene for WorldToTile is
  `dword_13ECF78[246*slot]` in the binary, modeled via the hook arg — boundary.
- **0x405bec CreateUseGateAction** — KNOWN-DIVERGENCE/BOUNDARY. Binary prototype is
  `(name->+240, room(a2)->+368, secondName(a3)->+304, ch(a4), scene(a5)->+372,
  speed(a6)->+376)` and writes ONLY +0,+9,+240,+304,+368,+372,+376 (+12/+16/+20/+40
  come from QueueInsertEntry's zero-fill; +8 never written). Recon prototype is
  `(ch,name,room,scene,speed)` — it omits the second-name buffer (a3->+304). +304
  feeds the gate-object render-leaf resolution which the step models via worldToTile,
  so the missing field is subsumed into that boundary (Handoff #2). Room->+368 /
  scene->+372 mapping in the .cpp is CORRECT vs `v9[92]=a2(room)`, `v9[93]=a5(scene)`.
  Same +8 divergence (Handoff #1). Binary's `if(+376==-1) ReportMessage` is a log
  leaf — omitted.
- **0x40b6a8 QueueWalkToTarget** — VERIFIED-1:1 (logic). Resolve point->tile, fail
  ->null, InsertActionVararg type45 (0x2D), +345|=0x10, +324..+332 world point.
  Boundary: PointThroughBoneChain/ResolveMesh/Heightmap are render leaves via hooks;
  the +324..+332 exact world point is modeled (no such slots in the node model).
- **0x40b760 QueueWalk2RndDummy** — VERIFIED-1:1 (logic). callCount gate, slot==dest
  gate (else Unlink), PickWaitAnimation->point->tile->InsertAction type45, +345|=0x10,
  "Walk2RndDummy" name copy, Unlink. SwitchActiveSlot/PickWaitAnimation are leaves.
- **0x40b998 RotateInterpolate** — VERIFIED-1:1 (fade math). Verified the fade ramp
  (fade-out: elapsed*0.02; fade-in: 1.0 - elapsed*0.02), the two clamps
  (`<=0 ->0`, then `>=1 ->1`), the social-interrupt gate (scene==off_649D64 &&
  +48 && (+141&0x40)==0 && t<0.1 && FindNearby(30.0)) -> restamp + neighbour
  |=0x40 + InsertAction type59(0x3B) arg75, and the completion test (alpha 0 /
  t==255.0f terminal). FLOAT->INT site `v26*255.0 -> ConvertX -> (int)v12`: the recon
  `FadeAlpha` uses `std::trunc` == ConvertX truncation. CONFIRMED CORRECT (NOT the
  documented "round-to-nearest"). FIXED the misleading doc comment in the header +
  the golden (see below). The morph-slot first-dispatch install (mesh+460,
  ChangeTransparency writes) are render leaves — boundary.
- **0x4d19c0 GroupInteractStep** — FIXED (float precision). Control flow, branch
  arms (state<0: -2 free else return; state==0 count/full/roll; state==1 dwell/emit),
  member loop (5 ids at +104/+108/+112/+116/+120 via word index walk), the ready
  predicate (`*(partner+8)` && `*(leader+8)`), and the RNG draws (state0: exactly one
  RandomFloatScaled; state1-emit: exactly one RandomModulo(8) AFTER enqueue,
  `(u16)…+1`) ALL match the disasm. FIXED the state-0 leave-probability roll to
  reproduce the x87 FLOAT-narrowing at each `fst var_24`/`fstp var_30` and the
  `(1.0 - v33) + rand` add order (was all-double `rand + 1.0 - sq`, no float
  narrowings — observable, gates the branch). Also FIXED state-1 `accum + 1.0f` to
  `(float)((double)accum + 1.0)` to match `fld accum; fadd 1.0; fstp accum`.
  MODELING NOTE: enqueueObjectInteraction ids in the binary are `*(leader+4)` /
  `*(partnerRecord+4)` / `v32 = *(*(leader+368)+1)`; the recon plumbs these through
  the GroupInteractHooks id-args (host responsibility) — values flow through the
  hook boundary, control flow/RNG are 1:1.

### charaction_brawl.cpp
- **0x4d201c BrawlStep** — VERIFIED-1:1. Confirmed against decompile + the
  0x4d201c/0x4d1a3e disasm: terminal (-2/-1) free; busy gate (+120&4); packet gate
  (+132==-1 || PacketStatus(+132)!=0); clear +132; state!=0 -> (state==1 knockout:
  victim lookup, defeat message + family[+24] bump, pose restore + RequestEntity29)
  else fall-through Idle; state==0 landed blow: aggressor = &word_12CE910[268*+8],
  victim = FindRecordById(+172); mood1=byte+184 (HIBYTE +181), mood2=byte+185
  (HIBYTE +182); hit=+186+1; state=(hit>=5u); +86 += 24 (WORD); negAp = -(+180);
  RegisterApEvent((u16)*victim, negAp). NO RNG anywhere. NO float->int sites. All
  offsets, the 5-hit gate, the 24-tick cadence, the negated AP, and the
  knockout-victim-gone vs landed-victim-gone requeue paths are exact.
  MODELING NOTE (header-documented boundary): the knockout defeat message uses the
  victim record's +4 dword (`SendEntityMessage(*(victim+4), …)`), while the AP event
  uses the +0 word (`(u16)*victim`); the `sendDefeatMessage(u16)` hook can only carry
  the word — the real +4-dword message is wired by the host (cross-cluster He leaf).

## Test golden fixes
- `tests/unit/sim_charaction_misc_test.cpp` `FadeMathEndpoints`: was
  `CHECK_EQ(FadeAlpha(0.5), 128)  // round(127.5)->128` — WRONG (ConvertX truncates).
  FIXED to `CHECK_EQ(FadeAlpha(0.5), 127)  // trunc(127.5)->127 (ConvertX RC=11)`.
  Evidence: decompile 0x5c6b08 (RC=11) + the 0x40bbea call site. The source
  (`std::trunc`) already produced 127; only the golden encoded the wrong expectation.
  Verified standalone: trunc(0.5*255.0)=127.

## Handoffs (outside owned files — NOT modified)
1. **+8 (ready) field** — Two contradictions to resolve in `src/sim/actionqueue.cpp`
   (owner: actionqueue chunk): (a) the binary's Create* builders never write +8, and
   (b) DispatchCurrent @0x404768 gates dispatch on +20 (owner) & +0 (step), NOT +8.
   The recon's actionqueue.cpp line 22 `if(!node->ready) return` adds a gate the
   binary lacks; line 47 uses `live->ready` for a priority compare. To make the
   builders 1:1 (drop `n->ready = 1`), DispatchCurrent's gate must first be aligned
   to the binary (+20/+0). Left builders writing +8 to avoid breaking the current
   live tree. Recommend: align actionqueue dispatch to the disasm, then drop the
   four `n->ready = 1` writes.
2. **CreateUseGateAction second-name (+304) / gate-open (mesh+533)** — adding the
   `secondName` (a3->+304) param + the `*(mesh+533)!=1` conditional would require
   touching the ActionNode model in `src/sim/charaction.h` (owner: charaction chunk)
   and the UseGate test/callers. Currently subsumed into the worldToTile hook
   boundary. Note also: `charaction.h` struct comments for `gateRoom`/`gateScene`
   have the +368/+372 byte labels swapped vs the (correct) .cpp; harmless doc.

## Counts
- Functions audited: 13 (12 misc + 1 brawl).
- VERIFIED-1:1: 8 (SoundActionUpdate, PlaySampleActionUpdate, SampleLoopActionUpdate,
  UseGateActionUpdate*, QueueWalkToTarget, QueueWalk2RndDummy, RotateInterpolate,
  BrawlStep).  (*control-flow 1:1; gate-open is a boundary.)
- FIXED: 1 source (GroupInteractStep float narrowing + add order; +1 state-1 fma) +
  1 doc (FadeAlpha header) + 1 golden (FadeAlpha(0.5)=127).
- KNOWN-DIVERGENCE/HANDOFF: 4 Create* builders (+8 ready, Handoff #1);
  CreateUseGateAction signature/+304 (Handoff #2).
- BOUNDARY (render/anim/sound/command leaves via hooks, unchanged): per-function
  noted above (ChangeTransparency, AttachAni, StepMotionQueue, Object_FindByHandle,
  Heightmap, SwitchActiveSlot, Person/command/He leaves).
- Constants confirmed: 9/9. Float->int sites checked: FadeAlpha (trunc, fixed golden);
  BrawlStep (none); GroupInteractStep (float narrowings reproduced). RNG draw
  count/order verified: GroupInteractStep (1 float roll / 1 modulo), BrawlStep (none).
