# Harden sweep — sim group D (cutscene_misc / cutscene_misc2 / cutscene_rand)

MCP-driven 1:1 diff of every provenance-carrying function in:
- `src/sim/cutscene_misc.cpp`
- `src/sim/cutscene_misc2.cpp`
- `src/sim/cutscene_rand.cpp`

Reference of record: gilde.exe @0x400000 (decompile + disasm via IDA Pro MCP).

## Counts
- Functions verified: 28 (15 misc, 11 misc2, 2 rand) + RandInt cross-check.
- VERIFIED-1:1: 25
- FIXED: 3 (1 source+golden, 2 source-only edge casts)
- BOUNDARY / documented simplification: 4
- Tests: all green — cutscene_misc_test 89, cutscene_misc2_test 74,
  cutscene_misc2_e2e 29, cutscene_misc2_itest 32, smallleaves_wave22 47.

---

## cutscene_misc.cpp

- **CutsceneRollDuelOutcomeTier @0x4a6908** — VERIFIED-1:1. duelMode→RandInt(100):
  r>50→4, r<=15→3, else 2; else RandInt(10): `(unsigned)r>7`. One LCG draw. Matches.
- **CutsceneCheckDeathTimer @0x4a7fbc** — VERIFIED-1:1. `output <= 0.0` (lookup side
  effect modelled as input).
- **CutsceneTickCompareCounter @0x4aa5e4** — VERIFIED-1:1. signed `counter>limit`
  (setg), latch, ++counter.
- **CutsceneLatchFrameCountToFloat @0x4ad4ac** — VERIFIED-1:1. `fild dword_631634`
  (SIGNED int)→fstp float; zero. `static_cast<float>(i32)` matches.
- **CutsceneRegisterTickProc @0x4ad4c4** — VERIFIED-1:1. RegisterProc(Latch, 71). Two
  SetGrayColor render pokes = host (documented).
- **CutsceneUnregisterTickProc @0x4ad4fc** — VERIFIED-1:1.
- **CutscenePauseGame @0x4aa944** — VERIFIED-1:1. `!gate`→VoiceFlush+MusicPlayCutscene.
- **CutsceneResumeGame @0x4aa960** — VERIFIED-1:1. `!gate`→MusicRestore.
- **CutsceneFinishPendingScripts @0x4aa14c** — VERIFIED-1:1. stride 2584, 330752,
  +128≠-1 && +164&1 && +132==-2 → Finish; ret 0. Bytes confirmed.
- **CutsceneWaitForPendingScripts @0x4aa188** — VERIFIED-1:1 (inner scan). BOUNDARY:
  the original pump is `RunFrameLoop(497414, 1, -1, table)` (frame-loop leaf, rules 3-5
  region); reconstruction routes it through `pumpFrame()`. Inner record scan
  (handle≠-1 && kind==-2 → keep waiting; else i+=2584; i>=330752 → return i) is exact.
- **CutscenePauseAllActorAni @0x4ab4e0** — VERIFIED-1:1. 512 slots, `(flags&4)==0`→
  Toggle(c,1). Prologue SetProcInterval = host poke (documented).
- **CutsceneResumeAllActorAni @0x4ab520** — VERIFIED-1:1. `(flags&4)!=0`→Toggle(c,0).
- **CutsceneUpdateProgressBar @0x4acb4c** — VERIFIED-1:1 (FLOAT→INT CONFIRMED).
  Disasm: `fmul flt_61D94C; call VIBE_Coord_ConvertX; fistp`. ConvertX @0x5c6b08
  (disasm: fstcw / set RC=chop 0x1F high byte / `frndint` / fldcw restore) rounds
  st(0) **toward zero in place**; the following `fistp` (default mode, now restored)
  stores the already-integral value → net **truncation toward zero** == C `(int)v`.
  flt_61D94C bytes = `00 00 C8 42` = 100.0f (confirmed). elapsed is unsigned
  (`dword_62EB38 - dword_6315F8`), `(int)v <= 0 ? 0 : (int)v`. Reset path leaves the
  returned/value register undefined in the binary; reconstruction reports 0 (documented).

## cutscene_misc2.cpp

- **CutsceneLoadAndRunScript @0x4aa01c** — VERIFIED-1:1. gate→0; load; if load RunMain;
  return s[+128] (modelled as out-param).
- **CutsceneRunScriptLoop @0x4aa06c** — VERIFIED-1:1. flags `BYTE1|=0x80`, pump, find.
- **CutsceneRunScriptUntilSkip @0x4aa0a4** — VERIFIED-1:1. skip→skipCb()|1.
- **CutsceneRunScriptWait @0x4aa0fc** — VERIFIED-1:1. literal flags 497414.
- **CutsceneRunCombatScript @0x4aa7b0** — VERIFIED-1:1. `deadline <= dword_6315A8`
  (signed). Tail JUMPOUT = plain return (documented).
- **CutsceneFadeIn @0x4aa450** — VERIFIED-1:1. NOTE: pump flags here are
  `LOBYTE(v)=dword_631598|0x80` (byte0), distinct from the byte1-set runners;
  reconstruction's `frameFlags | 0x80` (byte0) matches. done-bit checked first.
- **CutsceneSetupSky @0x4aa6a8** — FIXED (source). Added `dword_64A7C8 = dword_6315F0`
  mirror write (`g_sky.mirror = g_sky.sky`) which the binary performs and the
  reconstruction omitted. Sky/layer create + scroll(0.0)/fade(255,10.0) unchanged.
- **CutsceneDestroySky @0x4aa740** — **FIXED (source + golden)**. Evidence (decompile/
  disasm): the binary zeroes **only** `dword_64A7C8 = 0` (the mirror) and leaves
  `dword_6315F0` (sky) and `dword_6315EC` (layer) **intact**. The reconstruction was
  clearing `g_sky.sky`/`g_sky.layer` to null — a divergence (a 2nd DestroySky would
  no-op instead of re-RemoveLayer/Destroy). Fix: added `void* mirror` to `CutsceneSky`;
  DestroySky now clears only `g_sky.mirror`, leaves sky/layer. Goldens that encoded
  the wrong null-out fixed in: tests/unit/cutscene_misc2_test.cpp
  (DestroySkyRemovesLayerThenDestroys, SetupSkyBuildsPair), tests/e2e/
  cutscene_misc2_e2e_test.cpp (phase 5), tests/integration/cutscene_misc2_itest.cpp
  (SkyLifecycleInert captor path).
- **CutsceneShowDuelWindow @0x4a6964** — VERIFIED-1:1. duelMode?Outcome(a1,a2,a3):
  Choice(a1,a3).
- **CutsceneRunTimedScript @0x4aa808** — FIXED (source, edge). Disasm 0x4aa8ab:
  `(double)(unsigned int)dword_62EB38 >= (double)(int)a1*dbl_61D7A4 + (double)v4`,
  where v4 = start snapshot is an `int` → **signed** to-double. Reconstruction held
  `start` as u32 and did `static_cast<double>(start)` (unsigned conversion) — diverges
  when bit31 of the tick is set. Fixed to `static_cast<double>(static_cast<i32>(start))`.
  frames cast signed (matches `(int)a1`), LHS tick unsigned (matches). dbl_61D7A4 bytes
  = `93 24 49 92 24 49 B2 3F` = 1/14 (confirmed).
- **CutsceneRunDelayedScript @0x4aa8bc** — FIXED (source, edge). Same signed-start
  fix (disasm 0x4aa91f, `(double)v1`, v1=start is int). frames is an inbound register
  (Hex-Rays `v4 // ecx`, uninitialised in the decompile) correctly taken as a param.
  dbl_61D7AC == dbl_61D7A4 == 1/14 (bytes confirmed identical).
- **CutsceneCheckBirthParticipants @0x4a7a64** — VERIFIED-1:1. Disasm-resolved the
  Hex-Rays register confusion: `RecordById` (var_10, eax) == record-a == edx — the
  same record is used for parent lookup (`[edx+5Ch]`), kind check (`[edx+2]`) and as
  the BuildSpeechPacket speaker. a1[+0x34]/[+0x38] = partIds[0]/[1]. parent
  null||kind==15 → failure; kind 6||7 → speech. SIMPLIFICATION (documented): failure
  path calls `QueueRequestPair33(record_b[+4], 1)`; reconstruction passes
  `slot->partIds[1]` — equal by construction (record_b[+4] is b's own id == the id used
  to resolve it), routed through the simplified `queueBirthFailure` hook.
- **CutsceneTeardown @0x4aa4a8** — VERIFIED-1:1. gate `!gate && nestDepth>0` (De Morgan
  of source `gate||nestDepth<=0`→return). Order: handleA Finish → (off_649D64 +660
  dtor = host) → handleB busy-wait (`found && +164&1`) → FinishPending/WaitPending →
  handleA busy-wait → scene/heightmap/anim free + Universe reset → --nestDepth. Pump
  flags `LOBYTE|0x80` (byte0) match. Scene-free leaves subsumed by sceneTeardown hook.
- **CutsceneRestoreParticipantState @0x4aab0c** — VERIFIED-1:1. loop `i<*(u8*)(slot+48)`
  (unsigned byte count), match `person==dword_11AB010[i*215]` (215-dword stride), on
  match stage cmd28 (flags [0]=[1]=[3]=1, byte5=0, [2]=0)+SendCutInfo+busy-wait status;
  return partCount. Per-participant array stride + cmd block routed through the
  `restoreBroadcast` hook; control flow / count exact.

## cutscene_rand.cpp

- **CutsceneSetRandSeed @0x4ac9a0** — VERIFIED-1:1. `dword_11B4E38 = seed`; return
  Sprintf("cut_randseed: %i", seed) char count. Format bytes confirmed @0x61d8ec.
- **CutsceneGetRandSeed @0x4ac9c0** — VERIFIED-1:1. Sprintf("cut_getrandseed: %i",
  seed); **reload** and return dword_11B4E38. Format @0x61d900.
- RNG draw cross-check: **VIBE_Cutscene_RandInt @0x4ac9e8** decompiled —
  `if(range){ state=1103515245*state+12345; log; return ((state>>16)&0xFFFF)%0x7FFF
  % range } else return 0`. **One** LCG advance per call (only when range≠0). This
  matches `CutsceneRng::RandInt` in src/sim/cutscene.h (lines 52-55) exactly. The
  group-D RollDuelOutcomeTier issues exactly one draw per call → stream stays in sync.

## Handoffs (outside group-D ownership — NOT edited)
- `src/sim/cutscene.h` `CutsceneRng::RandInt/RandFloat` — verified 1:1 against
  0x4ac9e8 (LCG 1103515245/12345, `(state>>16)&0xFFFF % 0x7FFF`, then `% range`).
  Note: the binary's RandInt mutates the shared global `dword_11B4E38` directly, while
  CutsceneRng keeps a private `state_`; cutscene.h's snapshot/restore via
  `g_cutsceneRandSeed` reconciles them. Owner of cutscene.h should keep that link.
- Render pokes (Light_SetGrayColorThunk, TimeBase_SetProcInterval), the off_649D64
  +660 destructor, scene/heightmap/anim free, and the frame-loop pump (RunFrameLoop)
  remain host/rules-3-5 leaves (BOUNDARY), as already documented in the headers.
