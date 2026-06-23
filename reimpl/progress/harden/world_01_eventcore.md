# Hardening sweep — world event-core cluster

Chunk files:
- `src/world/event.cpp`
- `src/world/event2.cpp`
- `src/world/event_bindings.cpp`
- `src/world/event_effects.cpp`
- `src/world/event_fire.cpp`

MCP module `gilde.exe`, imagebase 0x400000. Every provenance-tagged function
decompiled (and disasm'd where Hex-Rays collapses register args / float→int
sites) and diffed line-for-line. Constants/tables confirmed via get_bytes /
get_global_value.

Counts: 30 functions reviewed — **27 VERIFIED-1:1**, **2 FIXED**, **0 unfixed
divergences**; plus documented boundaries/notes below. All 5 files compile
clean (`g++ -std=c++17 -fsyntax-only`). Golden vectors re-verified.

---

## event.cpp  (descriptor table + accessors)

Table image `dword_63CD48` @0x63CD48 (1152 bytes, 48×24) confirmed byte-exact vs
`kDefaultImage` (head/tail/mid rows spot-diffed, all match). `dword_5383F0`==0x30
(=48, `g_eventTableCount`), `dword_122F49C`==0 (`g_missionLcgState`) confirmed via
get_global_value.

- **VIBE_GesetzTable_CountByType 0x538550** — VERIFIED-1:1. `a1>5u → -1`; loop
  `v3+=6` over `6*count` dwords; category = `*(int*)((char*)&dword_63CD48[v3]+2)>>24`
  = byte at record+5 = `.category`. Matches.
- **VIBE_Mission_FindByType 0x5385b0** — VERIFIED-1:1 (logic). NOTE/BOUNDARY: the
  original (cursor==0 form) returns `&byte_63CD4C[v3*4]` (a *pointer* to record+4);
  the C++ returns the table *index* of the first category match. Documented
  reduction in event.h; the first-match scan and bound (`v3>=6*count`) are faithful.
- **VIBE_Mission_PickRandomByType 0x538680** — VERIFIED-1:1. LCG advance
  `state=1103515245*state+12345` (32-bit wrap); `pick = (HIWORD(state)%0x7FFF)%count`;
  walk counting matches until `seen>pick`, return `byte_63CD4C[24*v4]` = record+4
  `.value`. Draw count/order and the `g_eventTableCount<=0 → -1` guard all match.
- EventTableReset / EventTableLoadDefault — support shims for the static image;
  consistent with the 48-entry default and 0xFF free sentinel (row 0 category 0xFF).

## event2.cpp  (He action bodies + phase machines)

GameTime layout confirmed (day@+0 dword, hour@+4 word, minute@+6 dword, second@+10).
`VIBE_GameTime_Advance` arg map confirmed against disasm: eax=rec, edx=hours,
ecx=seconds, ebx=minutes (the gametime.h param *name* "addDays" is misleading but
that header is out-of-chunk; the event2 call sites use the correct mapping).

- **ResetActionState 0x4f06bc** — VERIFIED-1:1. stamp 14-byte clock @+82; Advance(0,1,0)=+1s.
- **ResetActionStateZero 0x4f168c** — VERIFIED-1:1. stamp @+82; Advance(0,0,20)=+20min.
- **SetActionAnim7 0x4f1d14** — VERIFIED-1:1. stamp @+82; +86:=7; +88:=0; return rec.
- **AllocActionAnim4 0x4f3ae0** — VERIFIED-1:1. +82←+68, +96←+82; RandomModulo(0x3C);
  Advance(4,0,rng).
- **AllocActionAnim7 0x4f3d28** — VERIFIED-1:1. +82←+68, +96←+82; RandomModulo(0x1E);
  Advance(7,<uninit ecx>,rng+30). BOUNDARY: original passes an *uninitialised* ecx
  (seconds); minute→hour carry makes it observationally inert here; C++ passes 0
  deterministically (documented).
- **AllocActionResetFields 0x4f4f54** — VERIFIED-1:1. Advance(0,0,1); zero +188/+192/+196.
- **AllocActionResetFlags 0x4f6cd4** — VERIFIED-1:1. Advance(0,0,1); zero +172/+176/+180.
- **AppendCollectedHandle 0x4f52d0** — VERIFIED-1:1. push into +4 array, ++count@+260,
  return count<64.
- **RetZero 0x4f24dc / NullSub7 0x4f3adc / NullSub8 0x4f3de0** — VERIFIED-1:1.
- **HelpTextPlaybackRun 0x4f1ac4** — **FIXED**. Switch on `*(a1+112)+2` (state+2),
  send gate `textId && textId!=-1 && helpEnabled`, day/hour/minute writes
  (+82 dword / +86 word / +88 dword) and return = next minute all match.
  - FIX: the cursor at He+172 is a **full 32-bit dword** in the original
    (`v5=4**(_DWORD*)(a1+172)`, `result=16*++*(_DWORD*)(a1+172)`). The C++ was
    reading/writing it through `He_Counter` which is a `u16&` (sim/he.h, +172),
    truncating to 16 bits and leaving +174/+175 stale. Changed to `Dword(h,172)`
    (i32) for both read and write. Before: `He_Counter(h)=static_cast<u16>(cursor)`;
    After: `Dword(h,172)=cursor`. Evidence: disasm `*(_DWORD*)(a1+172)`.
    (Did NOT edit the shared `He_Counter` accessor — fixed locally via the existing
     `Dword()` helper to avoid touching sim/he.h. Handoff note: `He_Counter` at
     sim/he.h:91 is typed u16 but +172 is a 32-bit field in this usage.)
- **HelpAdviceLoopRun 0x4f1c2c** — **FIXED** (same +172 width fix). Switch state+2,
  advice gate `byte_12335BB`, send before increment, `v5=cursor+1; if(v5>26) state=1`,
  then RandomModulo(4) once and Advance(rng+8,<uninit ecx>,0); else Advance(1,0,0).
  Draw count/order match. FIX: cursor +172 read/write changed `He_Counter` → `Dword(h,172)`.
  BOUNDARY: same uninitialised-ecx seconds arg as Anim7 (C++ uses 0).

## event_bindings.cpp  (name↔id table, register, serialize)

Name table `aNone_0` @0x64A7FC (264 bytes, 8×33: 32-byte name + id@+32) confirmed
byte-exact via get_bytes: NONE/0, TEST/1, ZOOM_IN_OBJECT/2, ZOOM_OUT_OBJECT/3,
**TEST/1** (duplicate row preserved), SCENE_ENTER/4, SCENE_EXIT/5,
SCENE_SUPERVISOR/6. Matches `kEventNames` exactly.

- **VIBE_Util_StrCmpNoCase 0x5cb8f0** — VERIFIED-1:1.
- **VIBE_Util_StrNCopyPad 0x5d9360** — VERIFIED-1:1 (copy-until-NUL then zero-pad).
- **LookupIdToName 0x5f494c** — VERIFIED-1:1. id read at record+32 (`&dword_64A819+v2+3`),
  stride 33, bound 264 (8 rows), default "NONE".
- **LookupNameToId 0x5f4910** — VERIFIED-1:1. 8-entry case-insensitive scan, id@+32.
- **RegisterEvent 0x5f4980** (and RegisterSceneEvent 0x5f4a70, same body, +468 vs +968)
  — VERIFIED-1:1. `!owner→0`, `id==0→1`, install: lazy-alloc table, slot=132*id,
  clear flag@+131, StrNCopyPad(slot+4,name,127), store handler@+0; remove: count
  leading-empty over 7 slots (924/132), free iff all 7 empty. NOTE: C++ adds a
  defensive `idx∈[0,7)` bound the original lacks (original would OOB); documented.
- **WriteEventNames 0x5f4b60** — VERIFIED-1:1. null→WriteDword(0); else count populated
  over 7 slots, WriteDword(count), per populated slot WriteString(LookupIdToName(i)) +
  WriteString(slot+4). Golden bytes in world_event_bindings_test confirmed.
- **LoadEventBindings 0x5f4bc8** (and scene 0x5f4c6c) — VERIFIED-1:1 (semantics).
  Disasm-resolved the buffers Hex-Rays lost: first ReadString = event name (→ id via
  LookupNameToId), second ReadString = handler string, and RegisterEvent's a3/ecx is
  the **handler string** (`lea ecx,[var_11C]`), stored as the slot name — matching the
  C++ `RegisterEvent(table,id,handlerStr,fn)`. Read order (name then handler) faithful.
  NOTE: original returns the last sub-call's `al` (1 when count>0+table, else the
  ReadDword result); C++ returns the iteration count. Documented in event_bindings.h;
  callers in-tree ignore the return.
- ByteWriter/ByteReader (Bio_* primitives) — VERIFIED-1:1 (4-byte LE dword, NUL string).

## event_effects.cpp  (duration / price / production cores)

Constants confirmed via get_bytes:
dbl_61FC60=0x3F50624DD2F1A9FC=0.001, dbl_61FC68=0x3FF8000000000000=1.5.

- **FireRaidComputeDuration / FireRaidDurationFactor 0x4ee804** — **FIXED**. Clamp
  `(1.5>=raw)?raw:1.5`, no-neighbour raw=1.0, `duration = (int)(factor*baseValue)` via
  ConvertX (trunc toward zero) — all match.
  - FIX: the original stores the scaled distance into a **32-bit float** `v21`
    (`v21 = sqrt(...)*dbl_61FC60`, 0x4ee8d7) and promotes it back to double
    (`v20=v21`) before the clamp. The C++ kept it in double. Changed to
    `float v21 = (float)(distance*kFireDistScale); double raw = v21;` so the
    float round-trip (and its effect on the truncation boundary) is reproduced.
    Golden vectors (0.5/1.5/1.0 and 50/80/150) re-verified — unaffected since the
    test inputs are exactly float-representable. Evidence: disasm `v21` is `float`.
- **PriceStateMachine 0x4ef408 / PriceEventClassify 0x4ef41a** — VERIFIED-1:1. switch
  on `*(a1+112)+2`: 0/1→free, 2→spike (write 1000 / class 3) + advance, 3→penalise,
  else idle. **PriceEventMoodPenalty 0x4ef4e2**: `-(RandomModulo(5)+5)` → [−9,−5]. Match.
- **BuildingProductionTrigger 0x4f16b8 / ProductionTriggerStep** — VERIFIED-1:1.
  Switches on the **raw** counter (not +2): `<-1`→(==-2 free else idle); `<=-1`→free;
  `0`→gauge branch (dword_12CEAD8[..]<0 → +4h; DrawProductionGauge()<1.0 → +20s;
  else produce + 4h); `>0`→idle. Reschedule intervals (4h / 20s / 4h) confirmed.

## event_fire.cpp  (VIBE_Event_FireRaidRun core)

Constants confirmed via get_bytes:
flt_61FCFC=0x3E2AAAAB=0.16666667f, flt_61FD00=0x40000000=2.0f,
dbl_61FD08=0x3F60624DD2F1A9FC=0.002, dbl_61FD10=0x3FE6666666666666=0.7.

- **FireEventShouldRunBody 0x4ee97d** — VERIFIED-1:1. `(flags&2)&&pendingCmd!=-1` →
  run only when the in-flight command resolves.
- **FireEventComputeDamage 0x4eef4f..0x4eefe2** — VERIFIED-1:1. `base = (int)(output*
  flt_61FCFC*flt_61FD00)` with **ConvertX trunc before the `(int)`** (0x4eef7b precedes
  `v53=(int)v32`); catalyst factor `catalyst*dbl_61FD08*dbl_61FD10+1.0` (or 1.0);
  `newValue = (int)(remaining - base*factor)` again ConvertX-trunc (0x4eefdd precedes
  `(int)v34`). Multiplication order (left-to-right) and `(int)` truncation both match;
  the x87-80bit vs double intermediate is inert here (2.0 exact, result truncated).
- **FireEventVoiceTier 0x4ef02d** — VERIFIED-1:1. `<=0→2`, `<=10→1`, `<=25→0`, else −1.
  The delta source in the original is `(*(int*)(person+89)>>24) - v53` (signed `sar`
  of the building wealth byte minus the production base). BOUNDARY: the wealth byte
  (person+89>>24) is engine person-record data not modeled in `FireEvent`; the C++
  reconstructs the delta as `prev - base`. Tier *thresholds and direction* are 1:1;
  the delta *operand* is the documented approximation.
- **FireEventInit / FireEventIgnite (case 0) / FireEventBurnTick (case 1) /
  FireEventTeardown (case -1/-2)** — VERIFIED-1:1 for the recoverable data-rules:
  subPhase cap `if(<2) ++` (case 0) and `if(subPhase<3) step=0` loop (case 1);
  ignition flag `RandomModulo(2)+1`; teardown cancels spawned commands. BOUNDARY:
  the .esc script VM, 3D sound, universe-slot swap, person/command-queue plumbing,
  and the building scan are engine-owned (driven through FireEventHooks per rules 3-5
  / data-not-in-tree). AdvanceTime is a hook (original `GameTime_Advance(a1+82,0,0,4)`
  = +4 minutes).

---

## Changes applied (all within chunk)
1. event2.cpp — HelpTextPlaybackRun & HelpAdviceLoopRun: cursor at He+172 now
   accessed as a 32-bit dword (`Dword(h,172)`) instead of the u16 `He_Counter`.
2. event_effects.cpp — FireRaidDurationFactor: scaled distance now round-tripped
   through `float` before the clamp, matching the original's `float v21`.

No goldens needed changing (the affected ones use exactly-representable inputs and
still pass). No files outside the chunk edited. Build of the shared lib currently
fails only in `src/gui/widget_layout.cpp` (out-of-chunk, pre-existing: `w.ld<i32>`
member does not exist) — unrelated to this cluster; all 5 chunk files + the 4
relevant test files compile clean in isolation.

## Handoff
- `sim/he.h:91` `He_Counter` is typed `u16&` at +172, but in the FireRaid/Help
  phase machines (and AllocActionResetFlags zeroes +172 as a dword) the field is a
  32-bit dword. Other cluster owners using `He_Counter` should confirm width.
