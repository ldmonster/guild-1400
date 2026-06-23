# Harden sweep — chunk sim_05_groupF

Files owned: `src/sim/cutscene_process.cpp`, `src/sim/cutscene_wedding.cpp`
(+ goldens `tests/unit/sim_cutscene_process_test.cpp`,
`tests/unit/sim_cutscene_types_test.cpp`).

MCP module gilde.exe, imagebase 0x400000. Every provenance-tagged function
decompiled + disassembled and diffed line-for-line.

Note on the float→int brief item: BOTH files are integer / GameTime based — there
are **zero** float→int cast sites in either file (verified by grep + by reviewing
the decompiles). The ConvertX-truncate concern does not apply here. The
float-heavy work (ConvertX, SetupViewTransform) lives in `ExecMainFunc`'s
render/voice body, which is an out-of-tree presentation leaf (BOUNDARY) and is not
reconstructed.

## cutscene_process.cpp

### CutsceneActorHasParticipant — 0x4abec8 — VERIFIED-1:1
Loop `i < slot[+48]`, `personId == partIds[i]`, return FindRecordById!=0. The
`personKind(id) != 0xFF` indirection faithfully models `FindRecordById != 0`;
null hook → resolves-always (test path). Matches.

### CutsceneAddActorToSlot — 0x4b0844 — VERIFIED-1:1
find-existing then first-free(-1) walk over 8 entries; returns settled index.
The (masterEntityId, actorList) split of the original `word_12CE910[268*a1]` /
`*((DWORD*)v3+1)` operands is the documented host-side decoupling —
observationally identical.

### CutsceneRemoveActorFromSlot — 0x4b08bc — VERIFIED-1:1
Clear entry == masterEntityId → -1, return index. Matches.

### CutsceneParticipants::Init — 0x4aa9b4 — FIXED
- Evidence (disasm 0x4aa9c3..0x4aaa16): the row loop runs over ALL 16 rows
  (`v8` from `a1` to `a1+64`, step 4); for every row when slot != null it copies
  the RAW `*(v8+52)` = `partIds[i]` into personId — there is **no partCount gate**
  on the personId copy (the partCount gate at 0x4aaa39 only drives the type==3
  battle-only nested column clear, which is out of tree).
- Before: `if (slot && i < count) personId = partIds[i]; else personId = -1;`
- After: `if (slot) personId = partIds[i]; else personId = -1;` (all 16 rows).
- Golden FIXED: `tests/unit/sim_cutscene_process_test.cpp` ParticipantStateGates —
  the template now sets `partIds[k] = -1` for k>=count so rows beyond count carry
  -1 (was implicitly relying on the wrong `i<count` gate). Added a
  `pt[15].personId == -1` assertion. Cited addr + the "copies all 16 raw partIds"
  semantics in the comment.

### CutsceneParticipants::AllDone — 0x4aaa74 — VERIFIED-1:1
`if (enabled && !done && personId != -1) return 0` over partCount rows. Matches.

### CutsceneParticipants::AllReady — 0x4aaab8 — VERIFIED-1:1
`if (enabled && personId != -1 && FindRecordById && !ready) return 0`. The
`resolves(id)` indirection models FindRecordById; order (resolve before !ready)
matches. Matches.

### CutsceneExecMainFunc — 0x4ab55c — VERIFIED-1:1 (spine) / BOUNDARY (body)
Deterministic spine verified: `stateFlags |= 4` early (0x4ab632), SetRandSeed
from slot[30]=+0x78 (0x4ab796), run main fn `dword_11AE5C0[5*type]` (0x4ab7e0),
InitParticipantTable at tail. Order matches. The render/voice/fade/window/cmd28
body (Fade_Register, RunFrameLoop, SetupViewTransform, ConvertX, the cmd23 delta
packets) is a presentation/networking leaf — BOUNDARY, routed via hooks.
- Note: the original returns the global `dword_11B4E3C` (render bookkeeping), not
  the main-fn result. The return is DISCARDED by both live callers
  (ProcessActive, RunForMaster), so the reimpl returning the main-fn result is
  observationally inert. `++execCount` and `started=1` are documented reimpl
  bookkeeping (the binary stores ecx — a transient — into the started field).

### CutsceneRunForMaster — 0x4ac680 — VERIFIED-1:1
while(FindLowestPriority): if participant → ExecMainFunc + ran=1; then
FindSlotById→clear→id=-1. The reimpl's `guard` cap (kCutsceneSlotCount) is a
safety bound on the original's `while(1){break on null}`; each iteration removes
the picked slot so the bound is never the terminating condition. The transient
`dword_12CEB18[v2] = slot->id` save/restore around ExecMainFunc (localSlotId) is
unread by the spine — noted.

### CutsceneProcessActive — 0x4ac31c — FIXED (3 divergences)
GameTime_Advance proto confirmed at 0x583150: `(rec@eax, days@edx, sec@ecx,
min@ebx)` == reimpl `GameTimeAdvance(rec, addDays, addSeconds, addMinutes)`.

1. **Re-arm amounts were seconds, must be MINUTES.** disasm 0x4ac412/0x4ac417:
   `mov ebx,5; mov eax,esi; xor ecx,ecx; xor edx,edx; call Advance` → min=5,
   sec=0, days=0. And 0x4ac5bf: v5=30 → min=30.
   - Before: `GameTimeAdvance(&readyTime, 0, 5, 0)` (sec=5) / `(…,0,30,0)` (sec=30)
   - After: `GameTimeAdvance(&readyTime, 0, 0, 5)` / `(…,0,0,30)` (minutes).

2. **The >=0x16 bump was +9h (9*60 min), must be +9 DAYS.** disasm 0x4ac42c:
   `mov edx,9; mov eax,esi; xor ecx,ecx; xor ebx,ebx; call Advance` → days=9,
   sec=0, min=0.
   - Before: `GameTimeAdvance(&readyTime, 0, 0, 9*60)` (9 hours)
   - After: `GameTimeAdvance(&readyTime, 9, 0, 0)` (9 days). (Applied in both the
     ready==-1 and ready==0 arms, matching the shared LABEL_13 path.)

3. **Final exec/teardown compared windowB, must compare windowA.** disasm
   0x4ac560: `mov edx, esp` → Compare(clock, **esp**=v14=windowA, the -60min
   copy), NOT var_1C(windowB). The earlier windowB compares (no-step finish
   0x4ac4cc, cleanup 0x4ac4f6) DO use var_1C=windowB and were already correct.
   - Before: `if (Compare(clock, windowB) > 0 && finished)` exec
   - After: `if (Compare(clock, windowA) > 0 && finished)` exec.

Everything else verified against disasm: alive-gate byte_11AE6E0[+48]; state skip
bits 0x01/0x04 (0x4ac351/0x4ac363); master-fill (byte_63CC28&8)||(word_63C740&8)
(0x4ac38f); ownedByLocal==dword_12CE914[…]; windowA = readyTime-60min (0x4ac381);
no-step+windowB finish (0x4ac4cc); cleanup participant&0x80 / localSlotId branch
(0x4ac525/0x4ac54e) and orphan master==local&&(flags&2) else remove
(0x4ac5d9/0x4ac5fc); type index = (i8)(slot+8) sar — equivalent for u8 types 0-11.

### InitCutsceneTypeTable — 0x4acefc — VERIFIED-1:1
Decoded the full initializer at base dword_11AE5C0, stride 20 (5 dwords),
columns +0 main / +4 next / +8 f8 / +12 flag / +16 step. All 12 types diffed
slot-by-slot against the per-address writes — **byte-for-byte match**, including
the two flag bytes (type 3 byte_11AE608=1, type 10 byte_11AE694=1) and every
fn↔column placement (e.g. type1 next/f8 = SuccessorA/SuccessorB at +24/+28;
type2 step = BuildElectionForm at +56; type6 step = CheckMarriageEligible at +136).

## cutscene_wedding.cpp

### CutsceneCheckMarriageEligible — 0x4a7118 — FIXED (missing vow side-effect)
Control flow verified: both resolve & factionTag differ (the `+3>>24` sar byte,
equality compare) → kind=A+2; notify path for kind∈{6,7,5}; return 1 iff
kind∈{6,7}; else cancel for each resolved spouse. The notify loop's per-player
kind 6/7 filter and the `!= groomEntity && != brideEntity` guard are folded into
the documented `otherPlayers` (pre-filtered) + `notifyBetrothal` hook.
- Divergence: the original, for kind∈{6,7}, FIRST renders the vow line (text id
  3621) and emits the cmd28 speech packet via BuildSpeechPacket (0x4a71c5/
  0x4a71d1) BEFORE the notify loop. The reimpl skipped this network side effect.
- Fix: added `WeddingCutsceneHooks::vowSpeech(groomId, brideId, ctx)` and call it
  for kind∈{6,7} before the notify loop (default null → inert; backward
  compatible with existing goldens which don't install it).
- Minor: the cancel-path `&& personId >= 0` guard is a harmless superset of the
  binary's `if (v3)` (FindById!=null ⇒ valid id) — left as a defensive no-op.

### CutsceneWedding — 0x4a73a4 — VERIFIED-1:1 (spine) / BOUNDARY (script/voice)
- Quest hook: `if (A+2==6) TrackCrimeProgress(A)` then B (0x4a73e6/0x4a73f8) ✓.
- Marriage cmds: QueueRequestArgs25(A.entity,456,0,4,0x40000) then B
  (0x4a7433/0x4a744f), order A→B ✓.
- Name swap: `if (A+9) swap & sprintf("%s %s %i", bride, groom, 1)` else
  (groom, bride, 1) (0x4a746f) ✓ (isFemale of FIRST spouse).
- Abort if either FindRecordById fails (0x4a73c5/0x4a73e0) ✓.
- **Panel-id sequence VERIFIED**: the RenderRichString content ids in order are
  0x16B6,0x16B9,0x16B8,0x16B7,0x16B8,0x16BA = 5814,5817,5816,5815,5816,5818 —
  matches `kWeddingPanelIds` exactly (the interleaved `$C` clears and
  RunTimedScript waits / .esc script loads are presentation leaves: BOUNDARY).

## Counts
- Functions reviewed (provenance-tagged): 12
- VERIFIED-1:1: 8 (ActorHasParticipant, AddActorToSlot, RemoveActorFromSlot,
  AllDone, AllReady, RunForMaster, InitCutsceneTypeTable, CutsceneWedding spine)
- VERIFIED-1:1 spine + BOUNDARY body: 1 (ExecMainFunc)
- FIXED: 3 (InitParticipantTable; ProcessActive — 3 distinct fixes;
  CheckMarriageEligible — vow side-effect)
- Goldens fixed: 1 (ParticipantStateGates)
- New hooks wired: 1 (vowSpeech)
- float→int sites: 0 (N/A for this chunk)
- Compile: both .cpp `-fsyntax-only` clean; the 2 touched unit tests compile clean.

## Handoffs
None. All edits stayed within the owned files + their two goldens. No shared
symbols changed (vowSpeech is an additive field on WeddingCutsceneHooks; existing
callers leave it null).
