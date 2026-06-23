# Harden: src/sim/npcaction12.cpp — 1:1 audit vs gilde.exe

Audited every `gilde.exe 0xADDR` function in `src/sim/npcaction12.cpp` against
`decompile` + `disasm` + `get_bytes`/`get_string`. Diffed control flow, constants,
float->int conversions, RNG draw count/order, struct offsets, command strings, and
return values. Tests: `tests/unit/npcaction12_test.cpp`,
`tests/integration/npcaction12_itest.cpp`, `tests/e2e/npcaction12_e2e_test.cpp`.

Result: 3/3 suites PASS after fixes. Library builds clean.

## Constants (get_bytes verified)
- `flt_61E99C` @0x61e99c = `cd cc cc 3a` = 0x3ACCCCCD = **0.0015625** (was wrongly 0.025). FIXED.
- `flt_61A594` @0x61a594 = `9a 99 99 3e` = 0x3E99999A = 0.3. correct.
- `byte_6477A1` @0x6477a1 = 0 (currency). correct.

## Per-function status

| Fn | Addr | Status | Notes |
|----|------|--------|-------|
| GameTimeSet (helper) | 0x5831f0 | VERIFIED-1:1 | field offsets +4/+6/+10 match GameTime struct. |
| FormAllianceGroup | 0x568fac | FIXED | selfGroup offset, broadcast msg-id. |
| AssignWorkPlaceStep | 0x4e7184 | VERIFIED-1:1 (boundaries) | unbounded loop capped at 32 (safety, documented). |
| EvaluateUseFront | 0x4718d4 | VERIFIED-1:1 | control flow + RNG order exact; select/cmd buffers are boundaries. |
| HairGestureBehavior | 0x4c94b4 | FIXED | idle field read off entity record, not person. |
| DemolishBuildingStep | 0x4e4cf0 | FIXED | two command strings. |
| MasterExamStep | 0x4e5c24 | FIXED | gate return value, renderExamResult arg4, state-1 wait semantics. |
| NotifyTrainingStep | 0x4e63dc | VERIFIED-1:1 (boundaries) | text 5402 + good-table are unmodeled boundaries. |
| BeginFollowTarget | 0x4e64f8 | VERIFIED-1:1 (boundaries) | amt key / leader scene base are synthetic boundaries. |
| TavernJoinLeave | 0x4746f8 | FIXED | two stammtisch strings. |
| BeginScanType63 | 0x4eb490 | VERIFIED-1:1 | |
| BeginScanType50 | 0x4e6ea8 | VERIFIED-1:1 | |
| InitWalkState | 0x4e7810 | VERIFIED-1:1 | Set(6,0,15). |
| BeginGotoHomeStep | 0x4e8b88 | VERIFIED-1:1 | Set(5,0,0), copy 14B, +1 day. |
| InitDualCoordWalk | 0x4ecfb0 | VERIFIED-1:1 | +204 +48d, +82 +1s. |
| ComputeWanderPathCoords | 0x4ccad4 | FIXED | constant + member byte/coord offsets. |
| BuildWorkerQuarters | 0x4725c0 | FIXED | two strings + cmd15 idA = -1. |
| QueueRandomActions | 0x5766d4 | VERIFIED-1:1 | LCG draw + per-iter step; timeGetTime reseed deliberately omitted. |

## Fixes applied (source)

1. **ComputeWanderPathCoords (0x4ccad4)** — three bugs:
   - `kWanderCoordScale` 0.025f -> **0.0015625f** (flt_61E99C byte-verified).
   - rank byte at `members+188+i` (`inc edx`), was wrongly `members+2*i+188`.
   - coord dword at `members+192+4*i` (ebx `add ebx,4` BEFORE store), was wrongly
     overwriting the rank slot at `members+2*i+188`.

2. **BuildWorkerQuarters (0x4725c0)**:
   - "upgr_arbeiterunterkunft" -> **"upgr_arbeiter_unterk"** (get_string 0x61a568).
   - "bau_arbeiterunterkunft" -> **"bau_arbeiter_unterk"** (get_string 0x61a580).
   - upgrade-branch `enqueueCmd15` idA: `He_Id(h)` -> **-1** (disasm 4726cd `mov eax,-1`).

3. **TavernJoinLeave (0x4746f8)**:
   - "Stammtisch join" -> **"stammtisch join AI-Plr"** (0x61a63c).
   - "Stammtisch leave" -> **"stammtisch leave AI-Plr"** (0x61a654).

4. **DemolishBuildingStep (0x4e4cf0)**:
   - "GebaeudeAbreissen" -> **"GebAbreissen"** (0x61f688).
   - "_NACHRICHTEN_HS_66" -> **"_NACHRICHTEN_HS_67"** (0x61f698).

5. **FormAllianceGroup (0x568fac)**:
   - selfGroup `field(self,12)>>24` -> **`field(self,6)>>24`** (a1 is u16*, a1+3 = byte +6).
   - host-pick broadcast id `sendEntity(...,3252)` -> **1418** (0x5691e6 He_SendEntityMessage
     uses msgId 1418; 3252 is the rendered text body).

6. **HairGestureBehavior (0x4c94b4)**:
   - idle field read: was `field(person,296)`; original is `*(*(person+388)+296)` —
     reads +296 off the ENTITY record (eax=*(person+388), ebx=*(eax+296), disasm
     4c952e/4c9534). Now resolves the entity record then reads +296.

7. **MasterExamStep (0x4e5c24)**:
   - gate not reached: was `return 0`; original returns the (negative) Compare value —
     now `return cmp` (0x4e5c36).
   - renderExamResult 4th arg: was `passed?4545:4532`; original adds
     `GroupFromCode(code)` (0x4e5d27 `(*(int*)&v18[1]>>24)+v8`, v18[4]=GroupFromCode) —
     now `groupFromCode(code) + base4`.
   - state-1 (case 3): was unconditional dialog+destroy+free; original gates on the
     UI panel result (dword_75BF04/dword_75BF38) and WAITS (no free) when not ready /
     pending / unrecognised. Restructured to route through a new `examPanelVerdict`
     hook (inert -> wait), with 1210/1155 verdict arms matching 0x4e5d74. Dialog open
     additionally gated on `*(rec+368)` (0x4e5da0).

## New hook
- `NpcAction12Hooks::examPanelVerdict(HeRecord*, packet)` — models the
  dword_75BF04/dword_75BF38 UI panel result for MasterExamStep state 1. Inert
  default = 0 (wait), wired nullptr in wire_npcaction3.cpp (faithful boundary).

## Golden test fixes (source+golden, with evidence)
- `FormAllianceGroupPicksAndBroadcasts`: entityText expectation 3252 -> **1418**
  (0x5691e6). Comment for self-group offset corrected to +6.
- `MasterExamStepAppointmentGate`: rc expectation 0 -> **-1** (0x4e5c36 returns the
  Compare result; clock day5 < appt day10 -> -1).

## Documented boundaries (not bugs; genuine synthetic/UI/table edges)
- AssignWorkPlace product-slot loop: original is unbounded stack write (no 32 cap);
  capped at 32 for safety — only differs in the impossible >32 case.
- NotifyTraining text 5402 + good table (dword_13CD6F2) and request17 good arg:
  table lookups with no portable hook.
- FormAlliance panel arg `2*(*(*ctx))+2582`: a 32-bit pointer chain through ctx[0];
  selfMarker substituted in the synthetic panel hook. Pick markers are exact.
- BeginFollowTarget amt key (`*(leader+113)`) / leader scene base (`*(leader+93)`),
  AssignWorkPlace employer (`*(person+364)` ptr) and work-object scene bases: all
  resolved via the synthetic objId/resolveEntity boundary.
- FormAlliance pick-timeout edge: original reads uninitialised v16/v17/v18; C++ uses
  null/0 (UB not reproduced).
- QueueRandomActions timeGetTime reseed: nondeterministic, deliberately omitted.
