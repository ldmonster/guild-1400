# Harden sweep — sim_06_effect (effect_script / event_recon2 / eventtable_recon / family_record)

MCP-verified against gilde.exe (imagebase 0x400000). Disasm is the reference of
record; Hex-Rays mislabels were resolved via the InvokeCommand register trace.

## KEY ABI RECOVERY — VIBE_Script_InvokeCommand @0x444f4c (disasm)
The effect/object command handlers are NOT __fastcall as Hex-Rays guessed. The
dispatcher (cases 4/5/6/7 at 0x44502f..0x4450b1) loads the evaluated arg-pointer
slots (esi+0x9E4 = arg0, +0x9E8 = arg1, +0x9EC = arg2, +0x9F0 = arg3, +0x9F4 =
arg4, +0x9F8 = arg5, +0x9FC = arg6) into registers as:

    eax = arg0   edx = arg1   ebx = arg2   ecx = arg3   stack[0] = arg4 ...

This mapping (verified by tracing each handler's disasm) is what every
divergence below hinges on. The reconstruction's `args[]` is `[handle, p1, p2, ...]`
i.e. `args[i] == arg_i`.

---

## effect_script.cpp

- **0x43fd24 CreateEmitter** — BOUNDARY (documented). Fills a default emitter
  template (life 180, scale 10/50/30, white, alpha 255, init bit) then tail-calls
  VIBE_Particle_SpawnSystemByType (scene/Vulkan leaf, rule 3). The reconstruction
  records the call args observably; not a byte-exact emitter image. Unchanged.
- **0x4405c4 SetParticlePos** — **FIXED**. Was `pos = (x, z, y)` (posY=arg3,
  posZ=arg2). Disasm 0x4405c8..0x440606: `esi=arg1`, `ebx=arg2`, `edx<-ecx=arg3`;
  `v8[0]=*esi, v8[1]=*ebx, v8[2]=*edx` = (arg1, arg2, arg3) — NO reorder.
  VIBE_Particle_SetPosition (0x5e1228) -> VIBE_Object_SetPosition (0x5af38c)
  stores a2[0..2] -> object[19..21] in order, so emitter xyz == (arg1,arg2,arg3).
  Now `posX=arg1, posY=arg2, posZ=arg3`. Goldens fixed (unit + e2e, see below).
  Hex-Rays had labeled a3@ecx/a4@ebx then read v8[1]=*a4 — wrong (ecx reused).
- **0x43fe9c SetEmitterAmplitude** — VERIFIED-1:1. f[16..19]=args1..4 *0.01,
  f[44](+176)=arg5 *0.01. Disasm 0x43fee3..: [edx+40h/44h/48h/4Ch]=var18/14/10/8
  = arg1/2/3/4; +176 = arg5. Scale flt_617748 = `0a d7 23 3c` = 0.0099999998f.
- **0x43ff30 SetEmitterPhasespeed** — VERIFIED-1:1. f[20..23]=args1..4, f[45]=arg5,
  *flt_617774 = `0a d7 23 3c` = 0.0099999998f.
- **0x43ffc4 SetEmitterDirection** — VERIFIED-1:1. f[27..29]=args1..3 *flt_6177A0
  = `0a d7 23 3c` = 0.0099999998f.
- **0x44002c SetEmitterSize** — VERIFIED-1:1. f[24..26]=args1..3, no scale.
- **0x4400d4 SetEmitterAcceleration** — VERIFIED-1:1. f[36..38]=args1..3
  *flt_617820 = `6f 12 83 3a` = 0.00099999997f.
- **0x440234 SetEmitterTimeAndAlpha** — **FIXED**. Was `+184(f)=arg1,
  +188/192/196=arg2/3/4`. Disasm 0x440246..0x440262: `esi(=arg1)->+188`,
  `ebx(=arg2)->+192`, `ecx(=arg3)->+196`, `stack(=arg4)` -> `fild`/`fstp` ->
  +184 as float. So `time0=arg1, time1=arg2, time2=arg3, lifeBase=arg4`. The
  FLOAT field takes the LAST arg, not the first. Source + both goldens fixed.
- **0x44028c SetEmitterColor** — VERIFIED-1:1. Disasm: +202=arg1(r), +201=arg2(g),
  +200=arg3(b), +203=arg4(a). Matches struct colR/colG/colB/colA.
- **0x43fe68 KillEmitter** — VERIFIED-1:1 (observable model; original frees the
  render node, reconstruction marks alive=false + killCount).
- **0x43c708 Sleep** — VERIFIED-1:1 (observable model; original sets the script
  context yield latch, reconstruction records sleeping/sleepMs; driver owns yield).
- **0x43c850/0x440618 command-name set** — VERIFIED-1:1 (full ImportCommand list
  cross-checked against RegisterObjectCommands @0x440618 decompile).
- **0x4431dc EnterFunction param-bind / RunMain driver** — unchanged (VM-core
  marshalling model; out of this divergence scope, already wired).

## event_recon2.cpp (+ event_recon2.h inline cores)

- TU is an anchor + deferral ledger; numeric cores are header-inline.
- **0x4d766c RunSimDiseases** — VERIFIED-1:1. Severity rand*100 (flt_61EF3C),
  cat-2 penalty count*0.0125*20 (flt_61EF50/40), else count*0.02*20
  (flt_61EF4C/40), no-building threshold 25.0 - rand*20*0.5 (dbl_61EF54,
  flt_61EF40, dbl_61EF44), outbreak `(double)v27 < (double)v30`, ring (s+stride)%768,
  stride table dword_4C9768 = {1,3,5,7,B,D,11,13,2ED,2EF,2F3,2F5,2F9,2FB,2FD,2FF}
  (byte-exact), step budget 12, sweep-complete `>=768`. All constants byte-verified.
- BardCreateScriptStep 0x4d66b4, BroadcastWinnerPointsStep 0x4d9a60,
  OfficeMatchmakingStep 0x4da978, InitBetriebRun 0x4ef900 (fee 3000),
  AllocProduktion 0x4f2a80 / AllocWorkActorAction 0x4f2530 (favor (avg-0.5)*0.25;
  dbl_620138/620140 and dbl_6200D8/6200E0 = -0.5/0.25 byte-verified),
  OpenBuildingDialogRun 0x4f1d48 (hour gate 22), OpenHelpEventsForKind 0x4f1a00
  (group->ini map), AllocSlotResetAction 0x4f41bc, AllocGebaeudeBauen 0x4f5110 —
  VERIFIED-1:1 (pure trigger/gate/arithmetic cores; RNG draws as explicit params
  by design for determinism).
- BOUNDARY (rule 8, documented, not faked): OpenHelpEventsFromIni 0x4f1850 (INI
  parser + MessageBoxA), SpawnBuildEffectByName 0x4f58d4 (scene/Vulkan),
  BroadcastFamilyNews 0x58c92c (text/UI builder), RegisterHandlerTable 0x4f1ed0
  (fn-ptr wiring), HandleAccidentRandom/SendEntityMessage/Command emitters/
  PickRandomDiseaseEvent/RenderFormattedMessage — world mutation + UI.

## eventtable_recon.cpp

- **0x604510 CreateEvent** — VERIFIED-1:1. enterLock(off_64A940);
  h=CreateEventA(0,0,0,0); if h: `table=CloneOrFreeData(table,4*(count+1));
  table[count]=h; ++count;` else `++failCount`; leaveLock(off_64A944). Matches
  the reconstruction's hook-driven path exactly.
- **0x5f1d00 CloneOrFreeData** — VERIFIED-1:1 (reused from object_lifecycle7.cpp;
  a1==0 -> alloc; size==0 -> free+null; else grow/copy). Decompile matches.
- BOUNDARY: CreateEventA is a Win32 sync primitive — stubbed per the recorded
  Rule-6 decision (monotonic non-null dummy handles). off_64A940/944 lock thunks
  -> inert hooks.

## family_record.cpp

- **0x58c408 GetFamilyRecord** — VERIFIED-1:1. kind in {5,6,7} && (i8)+81<0;
  index = (+0x50 word)&0xF; return &word_13C3110[82 * idx]. word_13C3110 is a
  WORD array so 82*idx == 164*idx bytes (stride 164 = correct).
- **0x5896fc reset (tail)** — VERIFIED-1:1. 16 x {memset 164 -> 0; word[0]=-1};
  dword_647720=0. Matches FamilyRecord_ResetAll.
- **0x58ecf3 allocator** — VERIFIED-1:1. if count<16: +0x50 = count|0x8000;
  rec[+128]=-1.0f (0xBF800000); rec[0]=family word; ++count. else return 0xFFFF
  (caller fails). Matches FamilyRecord_AllocForPerson.
- **0x4967bf..0x49681c ExCreatePersonB stamp** — **FIXED**. The name copy uses
  VIBE_Util_StrNCopyPad(familyRec+2, packet+53, **16**) @0x4967e3. StrNCopyPad
  (0x5d9360) copies up to N source chars (stop at NUL, NO terminator emitted when
  N reached) then zero-pads to N. The reconstruction used a 15-char limit; a
  16-char name would lose its 16th byte. Fixed to copy up to 16 chars then
  zero-pad the 16-byte field. (Short-name goldens unaffected; behavior now exact.)

---

## COUNTS
- effect_script.cpp: 2 FIXED (SetParticlePos, SetEmitterTimeAndAlpha),
  10 VERIFIED-1:1, 1 BOUNDARY (CreateEmitter -> SpawnSystemByType).
- event_recon2: 11 inline cores VERIFIED-1:1, all .rdata constants byte-verified;
  ~9 documented BOUNDARY leaves (unchanged).
- eventtable_recon.cpp: 2 VERIFIED-1:1, 1 BOUNDARY (CreateEventA stub, Rule-6).
- family_record.cpp: 3 VERIFIED-1:1, 1 FIXED (StampName 15->16 char copy).

## GOLDENS FIXED (encoded wrong behavior; corrected to the binary)
- tests/unit/effect_script_test.cpp: SetParticlePos x/z/y -> x/y/z;
  SetTimeAndAlpha lifeBase/time mapping (time0/1/2=arg1/2/3, lifeBase=arg4).
- tests/e2e/effect_script_real_esc_e2e_test.cpp: same two corrections (posY/posZ
  for x=1200/y=800/z=640; emitter[0] and emitter[1] time/life fields).

## BUILD
All four .cpp compile clean under the project flags. family_record_test (236
checks) and effect_script_test (67 checks) pass. The full-library link is
currently broken by an unrelated in-progress file (src/sim/combat_battle.cpp:
ambiguous EvaluateAttack overload) outside this chunk — the effect e2e couldn't
link only for that reason. HANDOFF: combat_battle.cpp owner to resolve the
duplicate EvaluateAttack(...,CutsceneRng&) / (...,CutsceneRng&,bool) overload.
