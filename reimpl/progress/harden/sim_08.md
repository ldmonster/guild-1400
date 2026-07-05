# HARDEN sim_08 — 1:1 verification vs gilde.exe (IDA MCP)

Chunk: 22 files (npcaction*/npcevent*/npc_daily/npc_clip_select/name_tables/
misc_recon4_*). Method: per-function decompile diff (+ disasm where Hex-Rays
collapsed registers), `get_bytes` diff for every `@0x…` const table/float block.

## Verdicts by file / function

### name_tables.{h,cpp} — VERIFIED-1:1
- Slice bases 599/796/918 = (0x8C400C/0x8C4320/0x8C4508 − 0x8C36B0)/4 ✓.
- Counts 191/112/149: disasm 0x58e34e (`mov ebx,0BFh`), 0x58f0e5 (`70h`),
  0x58ee64 (`95h`) ✓. Reader tables at each idiv site ✓.

### npcevent.cpp — 4 FIXED, 6 VERIFIED
- 0x4d577c RestorePoseReset — **FIXED**: +180 store is a DWORD
  (`mov dword ptr [eax+0B4h],0` @0x4d5790); was a u16 store (left +182/183
  stale). Now He_ScanStep (dword).
- 0x4d63bc RestorePoseSetRandom — **FIXED**: same +180 dword store
  (@0x4d63db after `and eax,0FFFFh`).
- 0x4d75e8 SetupDuration10Reset — **FIXED**: three DWORD zero-stores at
  +172/+176/+180 (@0x4d760b/15/1f); +172 and +180 were u16 stores.
- 0x4d665c InitRandomDurationEntity — **FIXED**: +180 := −1 as DWORD
  (@0x4d6674); +172 word store kept (0x4d6683 `mov [ecx+0ACh],ax`) ✓.
- 0x4dada0 / 0x4d52b4 / 0x4d493c / 0x4daf28 / 0x4d8a94 / 0x4da920 — VERIFIED-1:1
  (incl. 0x4daf28 `xor ecx,ecx; mov cx,ax` — rand(4) zero-extended ✓; 0x4da920
  in-place slot+1 increment ✓).

### npcaction5.cpp — VERIFIED-1:1
- 0x4db248 CountdownTickEntity, 0x4d877c ResolveTargetAndReset: gates, +24h
  advance, HIWORD(+192) item id read before the +180 clock stamp ✓.

### npcevent_reaper_full.{h,cpp} — VERIFIED-1:1 (He state machine + control flow)
- 0x4d8c34 / 0x4d8f74 / 0x4d92a4 / 0x4d9440: gate chains (+97/+460/+16), +196/
  +200/+208/+224/+236 writes, blocked-early-return, arrived (+196=0, ret 2),
  pose refresh order, sound consts 120/8 — all match. Seam notes: the attach
  hook does not carry the prev-pose/VectorAngle triple, and the sound facing
  triple (euler.y + angle) is delegated to the leaf — documented interface
  seams (leaves unreconstructed), not state-machine divergences.

### npcaction.cpp — 3 FIXED, rest VERIFIED
- funcs_5766CB table @0x63d964 (69 dwords): byte-diffed — MATCHES.
- 0x56840c AdjustRelationByMood — **FIXED (3 items)**:
  1) `kMoodInvCeiling` was 0.0039682314f; binary flt_624E3C @0x624E3C =
     `21 08 82 3B` = 0x3B820821 = 0.003968254197388887 (~49 ulps off). Now
     `0x1.041042p-8f` (byte-exact).
  2) local RandomFloatScaled multiplied by 1/32768; flt_62675C @0x62675C =
     0x38000100 = float(1/32767). Replaced by the canonical
     util::RandomFloatScaled (reuse; kCrtScale already correct).
  3) headroom (v12) and amplitude (v11) are 4-byte float spills in the binary;
     were kept as doubles — now rounded through float.
- 0x4e7538/0x4eaeb0 BeginIdleAnim — comment fix only: disasm proves
  `xor ecx,ecx` (addSeconds IS zeroed; not an uninit register as claimed).
- 0x5766a0 Dispatch, 0x4c9458, 0x4e5b10, 0x4e5c00, 0x4c9484 (stride 536 ✓),
  0x4cc018, 0x4cdcc0 (RandomModulo word-arg semantics vs 0x58b89c `mov cx,bx` ✓),
  walk-begin family 0x4e6d00/0x4e7394/0x4ec6b0/0x4ed910, 0x4c9d58
  (+182 dword=0, +180 word=rand(4)+19, +192=1 ✓), greet trio kinds 1/3/4 ✓ —
  VERIFIED-1:1.

### npcaction2.cpp — VERIFIED-1:1
- kPlagueScanSteps @0x478450 byte-diffed ✓. 0x4d25c4 (probe budget 256 via
  `mov ecx,100h` @0x4d25d9; RNG order; PEST ×4 → +188..+200; +208; +1 min) and
  0x4d2900 (all 4 states + teardown; scan-cursor post-step; delta 5/20, 1/10;
  msg/broadcast order) match. Seam notes: cmd28 packet contents and the case-2
  infection scatter live behind documented hooks.

### npcaction3.cpp — 1 FIXED, rest VERIFIED (with documented seams)
- Float consts (0x61FB80..0x61FBA8, 0x61FA8C..0x61FAA8, 0x61E9A8): all 13
  byte-verified ✓ (0.01/0.001f/0.01f/0.35/0.001/0.0025f/0.1; 0.1f/0.01f/0.3/
  0.5/0.7; 1.1f).
- 0x4cce04 RecruitmentState — **FIXED**: ComputeRecruitmentCost is
  __usercall(eax=idA, edx=idB) — disasm 0x4cd46d/0x4cd473 loads +176 into edx
  and +172 into eax; the reimpl passed a literal 1 as the first arg. Hook
  signature + call site corrected (hook doc updated).
- 0x4eb518 BurglaryStep / 0x4ea1e8 JailCellStep: state transitions, advances
  (+4/+1s/+15/+10/+6/+30/+10 min), member-loop bounds (8×4), teardown recalls,
  jail msg 5579/5580, "Einbruch"/"Entf"/"Entdeckt" — VERIFIED. Seam notes: the
  gesture-target record fields (+184/+188 sources), the caught-path fine
  (550+rand(200), member stock −100−rand(100)) and loot valuation are folded
  into documented hooks (burglaryDetectionRoll/LootValuation/jailEscapeRoll);
  their inner float blocks are not separately reconstructed.

### npcaction4.cpp — 4 FIXED, rest VERIFIED
- Consts @0x61EA24/0x61EA34/0x61FC28 byte-verified ✓ (1/42, 0.01/100.0/100f/125f ×2).
- 0x4cdf74 PatrolStep state 1024 — **FIXED**: all-idle exit writes −1 to
  rec+212 (disasm 0x4ce4ea `mov dword ptr [eax+82h],-1` with eax=rec+0x52),
  i.e. it CLEARS He_PatrolForce1024 — the old code wrote ApptTime.day=−1
  (decompile misread). Unit-test pin updated (old: day==−1; new: day==clock
  day, +212==−1) with the address as evidence. The busy re-arm stays
  entity29(1024) (disasm 0x4ce9b5 `mov eax,400h`).
- 0x4e2158 RunSabotage case 7 — **FIXED**: missing Person_QueryBegin(+172)
  existence gate (disasm 0x4e24c7/0x4e24e4: `test edx,edx; jz abort` runs
  before the handler check). Case-5 rand(2)-minute advance verified
  (0x4e2d40 `xor ecx,ecx`).
- 0x4cea84 RaidStep case 4 — **FIXED**: missing `flags & 2` gate at the combat
  resolve head (decompile `if ((*(BYTE*)(a1+120)&2)==0) return`). Tests updated
  to set the flag (documented in-test).
- 0x4ed95c AttackTargetStep: all 8 cases match (detected→5, cmd39→3, direct→5,
  +10/+1s/+5 advances, quad43 fold noted). Seam note: the cmd39 witness-count
  gate (v30, byte_12CE918 owner scan) is folded into the roll seam; reimpl
  gates on escorts only.

### npcaction6.cpp — VERIFIED-1:1
- 0x575eac/0x575fac (re-roll draw shape, −(u8) negation, 252 ceiling),
  0x575da0/0x5760a8 (dbl_625584/dbl_62558C = 0.01 byte-verified; +44/+130
  gates; swapped req16 ids), 0x576258/0x576198/dialog/message cmds, 0x5694c0
  NotifyWanderPair (filter 53, +188 vs +4, clock→peer+82, entity29(−1)) ✓.
  Return codes 0/1/1024 ✓.

### npcaction7.cpp — 2 FIXED, rest VERIFIED
- 0x5755ac AssignWorkCmd — **FIXED (2 items)**:
  1) Scan windows: disasm 0x575667/0x5757bd — half window = 0..384, "full"
     window = **384..768** (not 0..768); the descending variants (lo≥hi) never
     enter the loop (single signed `cmp/jge` guard) and scan NOTHING. Old code
     scanned 0..768 and iterated descending windows.
  2) marker byte compare `cmp bl,0Ah; jge` @0x5756bf is SIGNED — now i8.
- 0x575c64 SelectRoomCmd — **FIXED**: room query key is the dword at
  Person+0x178 (disasm 0x575cb3 `mov ebx,[edx+178h]`), not +94 (dword-index
  misread). Test updated.
- 0x575414 HealCmd (re-roll shape, stride-536/768 scan, −(u8) emit),
  0x575804 CollectTargets (+93/+20/+2/+14 offsets, cap 8/12, trunc ≥1),
  0x575948/0x575a4c/0x575b58 AdjustStat (spans 0xD/0xF/0x14; bases 13/5/5 and
  0.01/100.0 blocks @0x62553C/0x625554/0x62556C byte-verified), gossip
  0x568998/0x568ec4 (desc+16=1, shuffle-5, <0xFC gate, stop counts) ✓.

### npcaction8.cpp — VERIFIED-1:1
- All consts byte-verified (−2.0/0.4/3.0 @0x61A538; 6/2 @0x61A5F8; 2/3
  @0x61A624; 0.5 @0x61A4E8). Law ids 18/3/4/21 ✓; search codes 39/45/43/44 and
  req-block writes (byte0=16, dw1=8/4) ✓; ApproachMarket/Tavern/Shop gates
  (+358==15, +2==5, +361, rank≥3/<6 roll) ✓; AimTurret table stride 37 + 0.5
  reverse damp ✓; RequestSellObjekt double price lookup + HIWORD ✓.
  Seam note: the "shopStateWord" hook models WORD2(qword_13CE852) — the clock
  HOUR ≥ 20 gate.

### npcaction9.cpp — 1 FIXED, rest VERIFIED (constants all byte-verified)
- All 20 float consts verified @0x61A4F0..0x61A67C (incl. 0.166667f).
  Stride table = 0x478450 bytes ✓.
- 0x470d38 EvaluateShoot — **FIXED**: the self wealth term (v17) is a float
  stack spill before the target term is added; was full-double. Fixed in both
  NpcAction9_ShootTooExpensive and the inline copy.
- RecruitWorker/FromBuilding/HirePersonnel/BribeJailed/Arrest/ShopInteract/
  SocializeGroup: structure and emit blocks match; the populated-grid
  socialize branches remain documented-deferred (grid records unmodeled);
  LABEL_34 emit bytes (0,−1,1852796784) ✓.

### npcaction10.cpp — VERIFIED-1:1
- dbl_61F720=0.01, flt_61FABC=1604000.0, ransom factors 0.02/0.04/0.06/0.1
  (in-code consts), kWanderSeedTable @0x478410 byte-diffed ✓.
- RunCreditStep (float charge slot v29 ✓, msgs 5366..5369/5351),
  MasterExamState (1210/1155, 4983/4984, −20/−15×2), KidnapCarryStep (80-bit
  ransom product note verified), FireSpread/TavernSocialize/WanderSearch/
  DetachFromGroup/DismissApprentice — spot-verified. Seam note: the building
  +85 running-total accumulation lives behind queueRequest16/distributeCredit.

### npcaction11.cpp — VERIFIED-1:1
- 0x4e4728 (fast 0/30min vs 24×span hours), 0x4e4ed8 (rand(10)+10), 0x4e4a48
  (5h vs 1s; +90 sign gate), 0x4e4c84, 0x4e5b20 (byte +356−1), 0x4ea10c
  (filter-60 conflict scan, +101/+433 aborts), 0x4e6d2c, 0x4e73c0 (20×(+172/
  rate) delta, 5562), 0x4e4834 (5114/6207 sweeps), 0x4e454c (0.1/0.5
  @0x61F650 byte-verified; +10min re-arm), 0x4e4af4 (HIWORD(+174) req17),
  0x471b10 EvaluateUseBack (cooldown +485&2, rand(8)), 0x568650/0x5692dc,
  0x473e00, 0x473448 (0.3f @0x61A5F4 byte-verified) — all match.

### npcaction12.cpp — VERIFIED-1:1
- flt_61A594=0.3, flt_61E99C=float(0.0015625) byte-verified.
- 0x568fac FormAllianceGroup (strides 1/7/13, group byte +6>>24, eligibility
  only on pick 1, 3252/1418/3245 emits) ✓; 0x4746f8 TavernJoinLeave (3 tags,
  op84 + args25(0x2000000), 53/0) ✓; walk leaves 0x4eb490/0x4e6ea8/0x4e7810
  (Set(6,0,15))/0x4e8b88 (Set(5,0,0)+copy→+96+1day)/0x4ecfb0 (+48h/+1s) ✓;
  0x4ccad4 ComputeWanderPathCoords disasm-verified (inc edx rank bytes,
  ebx+4 pre-store coords, 32000 clamps, double×0.0015625 trunc) ✓;
  0x5766d4 QueueRandomActions (LCG steps + %0x45 draw; the timeGetTime reseed
  is documented as the platform-time seam) ✓.

### npcaction_notify.cpp — VERIFIED-1:1
- 0x4c9dec: all message ids (5282..5292), prices 377+378 single ConvertX,
  per-pair double coord27 emission (4 per unordered pair), leave-4 roster
  reconfirmation sweep — match.

### npcaction_perform.cpp — VERIFIED-1:1
- 0x470ea4 (packet 24/2/−1, +368→+1 owner), 0x471840/0x471cb4 (484,512,4,0)/
  0x471dfc/0x471f24 (43/44), 0x4737cc (4&1→spy, 19&4→dark corner),
  0x4742ec (456/0x1000000, op72 byte ctx+16, op90(8)), 0x474c18 (21→kind 3,
  54), 0x474da0 (4→kind 2, 55), 0x4756c4 (7|22→kind ctx+16, 56) — match.

### npc_daily.cpp — 4 FIXED, rest VERIFIED
- Season tables @0x6476FC/0x64770C and −1.0/2.0/0.0125 @0x61F968/64/38
  byte-verified ✓. State-0 morning sweep (v4<1 cap on the named dispatch only,
  +172 resume-cursor writes, 7000.0f distance gate, prod/chr-move branch) and
  the work-start transition (Set(endHour,0,0), state=1, +172=0) — VERIFIED.
- State 1 — **FIXED (4 items)** against the decompile/disasm:
  1) missing global pause gate `(word_63C740 & 0x80) → free` at state-1 entry
     (new pauseFlag80 hook).
  2) pass structure: pass A (owner-kind 6/7) has NO dispatch cap (loop runs
     all 768 — disasm 0x4e85b6); the `cmp edi,1/jge` cap @0x4e85c7 gates only
     pass B (no kind filter, stops at 1), which was missing entirely.
  3) tavern-roll pick FAILURE emits NO movement (disasm 0x4e8527/0x4e8716 jz
     straight to bits/args25); old code emitted "Go home" on pick failure.
     The evening sweep also rolls the tavern (chr-move variant) — was missing.
  4) the evening sweep ends in VIBE_He_FreeHandlerEntry (loop exit → LABEL_2);
     was missing. E2E pin updated (3 social dispatches, not 1) with evidence.

### npc_clip_select.cpp — VERIFIED (distilled selector)
- All 5 clip strings byte-verified at 0x610170/0x61024c/0x61070c/0x610158/
  0x6103bc; dbl_6103FC=0.01; byte_610134="" ✓. (This module is a documented
  distillation of the 0x405148 clip decision, not a body translation.)

### misc_recon4_{entitybar,textlayout,leaves}.cpp — 1 FIXED, rest VERIFIED
- 0x41089e entitybar: disasm-verified (double div, float spills, int-bit
  compare vs 0x3F800000 clamp) ✓.
- 0x4159e8 PropertySetDrawText: strlen-per-pass loop, glyph +22/+26, mode
  bits 4/2/1 draw order, space width, clip `>>16` compare, extent publish ✓.
- 0x41e57c StateFinalize (tag 7, spacing 8/2 vs 4/2), 0x429290 RainDestroy,
  0x54f884 DragSlotResetGridTable (cells 1..6, dword −1 + word 0) ✓.
- 0x423648 ResultFinalize — **FIXED**: the GPU-blit return mapping was
  inverted. Binary: nonzero COM HRESULT (failure) → return 0 immediately
  (0x4238d0, no bookkeeping); DD_OK falls through to the E_NOTIMPL software
  fallback + LABEL_33 bookkeeping → return 1. The old code returned 0 on hook
  SUCCESS and ran the fallback on failure. Hook contract re-documented
  (gpuBlit true == DD_OK); headless default = success+E_NOTIMPL.

## Test targets run (all green)
sim_npcaction_test 124 · sim_npcaction_e2e_test 27 · sim_npcevent_steps_test 80 ·
sim_npcevent_steps2_test 50 · wire_npcevent_test 52 · sim_npcaction5_test 97 ·
npcevent_reaper_full_test 52 · real_reaper_wiring_test 7 ·
npcevent_recon4_reaper_move_test 40 · npcevent_recon4_eval_target_test 19 ·
sim_npcaction3_test 75 · sim_npcaction3_e2e_test 30 · sim_npcaction4_test 75 ·
sim_npcaction4_e2e_test 20 · npcaction6_test 63 · npcaction7_test 61 ·
npcaction9_test 58 · npcaction10_test 37 · npcaction10_e2e_test 12 ·
npcaction11_test 96 · npcaction12_test 75 · sim_npc_daily_test 102 ·
sim_npc_daily_e2e_test 20 · misc_recon4_leaves_test 602 ·
misc_recon4_entitybar_test 9 · misc_recon4_textlayout_test 20 ·
name_tables_test 34 · name_tables_e2e_test 465 · npc_clip_select_test 96 ·
npcaction_notify_test 37 · sim_npcaction_perform_test 73 ·
sim_npcaction_perform_itest 66 · sim_npcaction_perform_e2e_test 8
= 33 targets, 2582 checks, 0 failures.

## Test pins updated (with binary evidence)
- sim_npcaction4_test Force1024 test: ApptTime.day==−1 → day==clock-day &&
  +212==−1 (disasm 0x4ce4ea). Raid state-3 tests now set He_Flags=2
  (decompiled flag gate at 0x4cea84 case 4).
- sim_npc_daily_e2e_test FullDaySweep: 1 social dispatch → 3 (pass A is
  uncapped; disasm 0x4e85b6/0x4e85c7).
- npcaction7_test SelectRoom: room key moved +94 → +0x178 (disasm 0x575cb3).
