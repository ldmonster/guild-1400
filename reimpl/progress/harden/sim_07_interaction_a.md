# Hardening report — sim_07 interaction (a)

Scope: `src/sim/interaction.cpp`, `src/sim/interaction_eval.cpp`,
`src/sim/interaction2.cpp`, `src/sim/interaction3.cpp` + their tests.
Method: per-function decompile + disasm diff vs gilde.exe (imagebase 0x400000).
Disasm authoritative. MCP IDA Pro live.

Tests (all green after fixes):
- `sim_interaction_eval_test` — 100% (added 4 new suites)
- `interaction2_test` — 100%
- `interaction3_test` — 100%
- `sim_interaction_eval_e2e_test` — 100% (fixed wrong golden)
- `sim_interaction_handlers_test` — 100% (collateral, unchanged, still green)

---

## interaction_eval.cpp — assigned Eval* funcs

### 0x46ef04 EvalChooseGesture — FIXED -> VERIFIED-1:1
Was using an over-generalized shared `RunSocialChoice` that diverged. Found:
- **Acquire-branch slot start is OFFICE-RANK derived, NOT RNG.** Disasm 0x46f00a
  `mov al,[esi+166h]` -> `VIBE_Office_GetDefinition`; on fail (eax==0) start=7; on
  success branch on rank byte (BYTE2): rank<4 ->0, <7 ->2, else 4. The old code
  drew `RandomModulo(9)` here — an RNG-order divergence (acquire consumes 0 draws in
  the binary). Added `EvalLeafHooks::officeRank(u8)` hook (default -1 == lookup fail
  -> slot 7) and wired the rank ladder.
- **Use-now planner gate is `slot==8`** (not `slot==0`): only slot 8 -> planner
  mode 3; any other usable slot builds frames and returns 28 directly. Old shared
  core gated on slot 0. Fixed.
- Item table {341,349,STALE,351,345,357,358,363,368} — already correct (stale slot2
  = v18=v9 stale reg -> 0). Confirmed via stack offsets (12-byte stride).
- Office byte offset is +0x166 (358), not 360 — doc/comment corrected.

### 0x46f2a4 EvalChooseTalkAction — FIXED (re-expressed) -> VERIFIED-1:1
- Acquire start = `RandomModulo(2)` (RNG). Use-now: slot 0 -> planner mode 4; slot
  !=0 -> return 29 directly. The old shared core happened to match this, but it is
  now an explicit body matching disasm. Table {371,347} confirmed.

### 0x46f578 EvalChooseFlirtAction — FIXED -> VERIFIED-1:1
- **Use-now branch NEVER calls the planner** — for any usable slot it builds the
  frames and `return 30`. The old shared core called SelectBest when slot==0
  (useMode=0). Divergence fixed. Acquire start = `RandomModulo(2)`. Table {360,364}.

### 0x470620 EvalChooseInsultAction — FIXED -> VERIFIED-1:1
- **Item table order was wrong.** Was {380,369,348,346,0}; binary is
  {380,369,STALE,348,346} (v16[0]=380,v17=369,v18=stale,v19=348,v20=346; 12-byte
  stride -> stale is SLOT 2). Fixed to {380,369,0,348,346}.
- **Use-now slot tiers:** slot 0 -> planner mode 4; slots 1..3 -> planner mode 3;
  slot >=4 -> return 34 directly. Old shared core only handled slot==0. Fixed.
- Acquire start = `RandomModulo(5)`.

### 0x46f7fc EvalChooseDrinkAction — FIXED (re-expressed) -> VERIFIED-1:1
- Sober: `RandomModulo(3)!=0 -> 0`; drunk (a2[11]/+0x2C): item 382; sober item 373.
- Disasm-verified the early rejects use usableCount (var_20 @+0x68), not the
  Hex-Rays `!*(_DWORD*)v13` misread. Single usable slot -> planner mode 4.
  Re-expressed without the shared core; behavior identical.

### 0x46ff00 EvalChooseSocialGesture — FIXED (table + default) -> VERIFIED-1:1 (slot logic already faithful)
Not in the assigned deep-diff list, but its table/default live in this file:
- **Planner classId default is 28, not 33** (`v7 = a1 ? a1 : 28`). Reimpl had
  `actionCode = ... : 33`, passing 33 to the planner — FIXED to 28. The
  direct-return short-circuit and companion frame tag remain 33 (v28[4]=33;
  `return 33`), which the reimpl already had correct. Body slot logic `slot>1` ->
  planner mode 4 matches `v16>1`.
- **Item table order was wrong.** Was {361,362,376,381,365,379,0}; binary places
  the stale slot at index 2: {361,362,STALE,376,381,365,379}. Fixed to
  {361,362,0,376,381,365,379}.

### 0x46dbf0 EvalEnterTavern / 0x46dc3c EvalEnterTavernDirect — VERIFIED-1:1
DispatchByType(2,personId)==2 -> 22 else 0; Direct skips the result short-circuit.
Unchanged; matches.

### Other funcs in this file (not assigned, spot-confirmed unchanged)
BuyObject/SendMessage/DuelChallenge/EvalChooseGroupAction/SelectWorker/
AssignPatrol/AssignDestination/combat budget gates — left as-is. GroupAction
(0x46fa54) and SocialGesture (0x46ff00) retain documented hook-modeled
object-search/render leaves; their slot/frame RULE matches disasm.

---

## interaction.cpp — VERIFIED-1:1

### 0x411f8c VIBE_Interaction_Handler (InteractionResolveHandler)
Two-byte unrolled scratch copy, `ScanToBar` one byte in, field-N boundary scan,
NUL-terminate + copy-out. Matches disasm line-for-line (stride 740, +216 string
offset). No change.

### 0x40e6c0 VIBE_GameObject_DispatchInteractions (InteractionDispatch)
Loop over `16 * count` byte cursor advancing 16; reads +0/+4/+8/+12; calls the
8-arg result handler with (v6,v7,v4,v5,ctxA,v6,v7,ctxB); resets the count slot.
Reimpl iterates `count` events with the equivalent field mapping. Faithful.

---

## interaction2.cpp — PROVENANCE PRESENT; VERIFIED-1:1 (spot-checked)

The task brief said "no provenance markers" — **FALSE**. Every function carries its
address as a `// 0x595xxx/0x596xxx` comment (just not the `gilde.exe 0x...` form, so
a naive grep missed them). No fabrication needed.

Spot-diffed against binary (all match):
- 0x595f70 IsPanelActive: `!active || (panelMode!=2 && state!=5)`. OK.
- 0x595e74 InvokeHandlerSlot60: active/slot/suppress/hasSlot60 order + arg order
  (a1,a2,a4,a3). OK.
- 0x596e1c QueryEventStateRange: switch 0x92..0x97, dragSubState (+13) ladder. OK.
- 0x5961ac HandlePickupDropTransition: event 10 phase 0/1 + event 7 peerKind==5,
  StampTransition writes (state,subState,ts). OK.
- 0x596f2c OpenTownHallDialog: `!(hover && kind==18)` proceed predicate equals the
  binary's `!hover || kind!=18`. OK.
Remaining handlers are structurally identical state-stamp machines (HandleDropStep
family, Allow*DropTarget, HandleMultiStageDrop, etc.) using the same hook-modeled
`BuildingKindOf` (589*idx + table) leaf. No divergence found.

---

## interaction3.cpp — PROVENANCE PRESENT; VERIFIED-1:1 (spot-checked)

Same note: every function has its `// 0x46bxxx..0x470bxx` address comment. Not glue;
real translations.

- 0x46eb10 EvalRestSlotFree: SelectBestRecursive(26,...,mode 0) then two 4-step
  column scans over byte_B57210[148*method] at +48/(float)+13 and +112/(float)+29,
  cursor += 8 BYTES/step (fixed a comment that said "8 dwords"). Both scans must run
  to completion (4 each) for success. Logic matches disasm.
- 0x46b654 ComputeApproachOffset, 0x46dc80/0x46e2c4 MoveTargetA/B (law 23/20),
  0x470998/0x470b68 MoveTargetSocial/Alt (law 16/17, actionCode 28..34 gate),
  OrientToActionTarget27..34 / OrientQuad33/35: shared `OrientScalar` body
  (QueryFind -> HasInventorySlot -> UseObjectAction spin -> base*scale, half factor
  on negative for the documented subset). Matches the binary shape; the
  relation/coord/method-catalog leaves are hook-modeled as documented.

---

## Test changes
- `sim_interaction_eval_test.cpp`: fixed wrong goldens for the recovered tables
  (`kSocialGestureItems[4]` was 365, is 381; stale slot is index 2 for both
  SocialGesture and Insult). Added 4 suites pinning the corrected behavior:
  FlirtUseNowNeverCallsPlanner, GestureAcquireOfficeRankStart (asserts zero RNG
  draw via RandNext state), InsultUseNowSlotTiers (modes 4/3/direct),
  GestureUseNowSlot8Planner.
- `sim_interaction_eval_e2e_test.cpp`: corrected the gesture step golden — slot 0
  returns 28 directly with no planner call (was asserting planner kind 1); planner
  call count 3 -> 2.

## Divergences fixed -> binary
1. Gesture acquire start: RNG -> office-rank ladder (+ new officeRank hook).
2. Gesture use-now planner gate: slot 0 -> slot 8.
3. Flirt use-now: dropped the slot-0 planner call (binary never calls it).
4. Insult item table: stale slot moved index 4 -> 2.
5. Insult use-now: added the 3-tier slot dispatch (0:mode4, 1..3:mode3, >=4:direct).
6. SocialGesture item table: stale slot moved index 6 -> 2.
7. SocialGesture planner classId default: 33 -> 28.
8. EvalRestSlotFree stride comment (8 dwords -> 8 bytes).

## Deferred / unchanged (hook-modeled leaves, as already documented)
ClassifyAccessibleItems, SelectBestRecursive, object-search (FindNearestEntity /
FindMatchingColors), relation/coord/law/method-catalog leaves remain mockable hooks.
No new fabrication; no fake analogues introduced.
