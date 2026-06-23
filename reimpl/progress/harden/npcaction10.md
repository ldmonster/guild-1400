# Hardening pass — src/sim/npcaction10.cpp

1:1 verification of every provenance-tagged function in `src/sim/npcaction10.cpp`
against the gilde.exe decompilation + disassembly (imagebase 0x400000). MCP live.

## Constants (get_bytes verified)
- `dbl_61F720` @0x61F720 = `7B14AE47E17A843F` = **0.01** -> kCreditChargeMul. OK.
- `flt_61FABC` @0x61FABC = `00CDC349` = **1604000.0f** -> kKidnapWealthCap. OK.
- `dword_478410` @0x478410 (16 dwords) = 1,5,7,11,13,17,19,23,745,749,751,755,757,
  761,763,767 -> kWanderSeedTable. OK (byte-exact).
- Ransom factor floats (inline immediates in 0x4eab30): rank1 0x3CA3D70A (0.02f),
  rank2 0x3D23D70A (0.04f), rank3 0x3D75C28F (0.06f), rank>=4 0x3DCCCCCD (0.1f). OK.

## KEY GLOBAL FINDING — Coord_ConvertX @0x5c6b08 TRUNCATES (not fistp-nearest)
Disasm of ConvertX: it saves the x87 control word, forces RC=truncate-toward-zero,
runs `frndint` (rounds st0 to an integral value using truncation), restores the CW.
The following `fistp` then merely stores the already-integral st0. NET effect of the
`fld x; call ConvertX; fistp dst` idiom used throughout this file = **truncate toward
zero**, with the multiply kept in x87 80-bit precision until ConvertX. This drove two
float fixes below. (The brief's premise "ConvertX truncates" is confirmed.)

## Per-function status

### 0x4e5754 RunCreditStep — FIXED (float-narrow) + VERIFIED
- Control flow (packet gate, state <-2/-2/-1/0, flag&2 charge/repossess, +188 retry
  fork, 5366/5367/5368/5369/5351 message ids, kind 6/7 gates) all 1:1.
- FIXED: `charge` was `double`. Disasm @0x4e596c shows v29 is a 4-byte FLOAT slot
  (`fstp [var_20]` narrows the triple-double product `(double)+192 * 0.01 *
  (double)+180` to float); the held-vs-charge compare and the ConvertX truncation
  both read the float. Changed `double charge` -> `float charge = (float)(...)`, and
  compare via `(double)charge`. (No golden change — itest uses 200*0.01*50=100.0,
  exact in float.)

### 0x4cb880 MasterExamState — FIXED (state-0 order) + VERIFIED
- state -2/-1/missing-examiner teardown, host-kind fast-forward to state 2, state-1
  24h-deadline poll + form-event 1210/1155 fork, state-2 rating-curve pass/fail all
  1:1. RNG: exactly one RandomFloatScaled() draw, in order (drawn before the rating
  read). OK.
- FIXED: state 0 emission order. Orig (0x4cb9f0) calls EventPanel_CreateSlot FIRST
  (it sets +116), THEN checks +116, THEN RenderRichString/clocks/Advance/voice. The
  reconstruction had renderRichString before the create and the create after Advance.
  Reordered to create -> (if window) rich -> stamp +68/+82 -> Advance +24h -> voice
  -> ++state, matching disasm side-effect order.
- BOUNDARY: rating curve uses buildingRating(nullptr,4)/100.0 to model
  Building_ComputeRatingCurveA(4); the early-return paths in state 1 return the panel
  window pointer in the binary, not representable through the i32/void* hook split
  (returns state instead). Form-event globals dword_75BF04/0x75BF38 modelled via
  formEventMatches/formEventCode hooks.

### 0x4eab30 KidnapCarryStep — FIXED (ransom float->int) + VERIFIED
- states -2/-1 clear victim +433 then free; state 0 query victim/captor; rank!=0
  ransom path vs rank==0 carry-out path; ransom factor switch (rank>=4/3/2/1) 1:1.
- FIXED: ransom = `(i32)(w * factor)` with float*float product. The binary keeps the
  product in x87 80-bit then ConvertX-truncates. float*float pre-rounds 50000*0.02f
  to 1000.0 (-> 1000); the binary computes 999.99998 and truncates to **999**. Changed
  to `(i32)((double)w * (double)factor)` (wide multiply + truncating cast).
- GOLDEN FIXED: e2e KidnapRansomEmitsSlotReset expected `slot28:62,1000`; corrected to
  `slot28:62,999` with addr+evidence (rank1 0x3CA3D70A, ConvertX frndint truncate).
  Unit Rank4 test (1000000*0.1f=100000.0014 -> 100000) unaffected.
- BOUNDARY: ransom gate `*(active+436)&1` is a record-field not in the hook contract
  (modelled as `if(active)`); RegisterApEvent arg is the cityRec marker (`*v9`) not the
  victim marker — hook abstraction uses markerWord(victim). Noted, not in test scope.

### 0x4ee4dc FireSpreadStep — FIXED (case-1 offsets) + VERIFIED
- switch -2/-1 free; case 0 Brand_Beginn (channel -8) + ++state; case 2 burn/clear
  +172..+232 16-slot loop + slot28(79,tgt,heId,25) + +10min re-arm -> state 1; case 3
  sibling handler-79 wait else Brand_Ende + free. All 1:1 (case 2 loop bound and slot28
  args verified).
- FIXED: case 1 nearest-neighbour scan read the WRONG slots (+8..+60) and mishandled
  the candidate/best. Orig (v5: a1+0..a1+60) reads candidate ids at `*(v5+172)` i.e.
  +172..+232 (16 slots), skips -1/anchor-self, keeps the nearest record, writes
  *(a1+236)=nearest objId. Rewrote to walk off=172..232 reading the candidate id and
  resolving the record.
- BOUNDARY: per-candidate distance uses Transform_PointThroughBoneChain @0x5c8b38 +
  sqrt over real 3D scene geometry — not in the NpcAction10Hooks contract; proxied via
  withinTolerance so the machine advances. Documented inline.

### 0x4cbd04 EvaluateGroupCompositionState — VERIFIED-1:1 (RNG) / BOUNDARY (roster)
- flag&4 gate, -2/-1 free, no-group re-arm, state 0 roster + AiMethod eval + bad-comp
  message, state 1 re-arm 1:1 in structure. RNG: per-member RandomModulo(3) drawn
  `count` times inside the kind-6/7 bad-composition block, matching the orig loop
  (0x4cbef6). Draw count + order OK.
- BOUNDARY: the gameObject member roster walk (v40 records / +172 slots) and the
  per-member rumour-text concatenation (5018/5019/5020 + 3*(rank>>24+2)) operate on
  record/string data routed through groupMemberCount/groupMemberRecord and the text
  hook; not reproduced byte-for-byte (data not in synthetic scene).

### 0x4ca658 TavernSocializeState — VERIFIED-1:1
- -1/-2 free; flag&4 + packet gate; resolve tavern (+172) then type-301 seat;
  occupancy check; +192 counter sequencing clear-op / join("new ") / deadline-wait /
  leave("exec"); +176 24h re-arm; +182=0; +180 = RandomModulo(4)+19. Single RNG draw,
  correct order, unsigned. NotifyJoinLeaveGroup FourCC tags 0x6E657720/0x65786563 OK.
- BOUNDARY: occupancy predicate reads cityRec bytes (+8 != 0 && +2 != 15) modelled via
  kind hook; build-op packet (EnqueueBuildingActionStart/RequestBuildOp84/End) routed
  through enqueueBuildOp84.

### 0x4e7588 MasterExamPayStep — VERIFIED-1:1 (with boundary)
- a1[28]/[29]/[43..45] offsets (+112/+116/+172/+176/+180) verified; examiner kind 6/7
  fast-path vs +82-deadline-then-state-2; switch arms 0 (create+rich 0x15C1+ ++state),
  1 (form 1210 pay / 1155 reject / other -> destroy+free), 2 (affordability fork),
  default state+2 all 1:1.
- BOUNDARY: state-1 early returns ("no form match" / code -1) return the panel window
  pointer in the binary; the hook split returns `state`. Affordability uses
  buildingSlotsWorth >= 16000*rank to model Amt_CheckExamFeeAffordable (16000*rank
  constant verified).

### 0x4cc690 StartWanderSearchState — VERIFIED-1:1
- flag bit-splice gate ((ptr|flag<<8)&0x400 == flag&4); no-wanderer re-arm; >4 type-53
  handler abort (historyWanderA); seed sequence: stamp +82/+68/+172, +82+1min,
  +172+24h, BuildOp90, Gesetz_EvaluateViolation(3,...); RNG order: RandomModulo(0x300)
  -> +196, RandomModulo(0x10) -> table index -> +200, +208 rating, +204=768; then
  inventory-348 paired vs solo fork. Two RNG draws, correct order. Table index
  `dword_478410[v9]` (v9 in 0..15) == kWanderSeedTable[seed&0xF]. OK.
- BOUNDARY: +208 = EvalProductionRating(cityRec,4) modelled as buildingRating/100.0f.

### 0x474070 EvaluateAssignProfession — FIXED (RNG draw count) + VERIFIED gates
- Gates: kind==5, +101>=4 (boundary field), family present, (cash signed /32) < 5000
  reject (signed div-by-32 idiom -> C++ `cash/32`, truncates toward zero, OK), pending
  type-36 handler abort. outAction[0..5]/outExtra writes + return 52/0 OK.
- FIXED: the bucket-pick drew a bogus THIRD RandomModulo(1) for the loop guard,
  corrupting the global LCG stream. Disasm: the orig draws EXACTLY RandomModulo(4)
  @0x47416f (discarded) + RandomModulo(0xD) @0x47418d (start). The guard `v18+1`
  @0x4741ff reuses the SAME ax register as the RandomModulo(0xD) result, so
  guard == start+1, NO third draw. Changed `guard = RandomModulo(1)+1` -> `guard =
  start+1`.
- BOUNDARY: the 13-bucket histogram (v25[cat+15]) is built by walking the 256-cell
  building grid (43264=256*169) via dword_13CE298/dword_13CE294 + MapActionToCategory,
  and the rank table v25[0..12] via ComputeRankWithinGroup — neither is in the hook
  contract; the reconstruction zeroes the histogram and routes
  variant/profession/worth/meister through hooks. The loop predicate (`v25[v19+1]`
  rank-region read vs the histogram) thus operates on empty data.

### 0x4ee288 GatherFollowersStep — VERIFIED-1:1
- candidate roster (non-production/storage live persons), clear +176..+232 (`v9` loop
  +172.. dwords), leader pick (random if +172==-1 else resolve), proximity recruit into
  slots 1..15, +236=leader, +82 stamp, +86 wait-word logic. RNG: leader RandomModulo
  (guarded by candidates!=0, but RandomModulo(0) draws nothing -> equivalent), then the
  two +86 re-seed draws RandomModulo(0xA)+8 gated by +86>0x14 (unsigned) and +86<7
  (unsigned). Draw count/order OK.
- BOUNDARY: proximity uses Transform_PointThroughBoneChain + VectorWithinTolerance over
  3D geometry; modelled via withinTolerance.

### 0x4ccbb4 DetachFromGroupState — VERIFIED-1:1 (with boundary) 
- +112=0 always; flag&2 gate; resolve a(+172)/b(+176); handler-65 match -> +2min
  re-arm else full reset (+88=0, +86=8, +112=0, +184=0, +185=recruit cost, +186/+187=0,
  +24h re-arm); not-both-present -> +2min re-arm. Structure 1:1.
- BOUNDARY: the orig predicate `a && b && (*(a+6)>>24 != *(b+3)>>24)` (city/group high
  bytes) and the per-member `a[23]/b[23] != -1` guards on the +0x5C clear read
  record-internal fields not in the hook contract; the reconstruction checks `a && b`
  and clears unconditionally. Recruit cost uses buildingSlotsWorth to model
  Recruit_ComputeRecruitmentCost; ComputeWanderPathCoords routed through wander hook.

### 0x4e6f4c DismissApprenticeStep — VERIFIED-1:1 (with boundary)
- state <-2/-2/-1/0; resolve apprentice; workplace presence; relation gate `> -26 ->
  keep+free`; on dismiss: BuildOp71/77, clear +364, carry target (named-object-53 with
  flag 1 when target==-1 else 0, "Entlassen"/"Entlassen"), -5 coord27, message 6073,
  free. Control flow + message id 6073 + gate threshold -26 1:1.
- BOUNDARY: workplace = *(appr+91) and the relation matrix indices (workplace+39,
  appr marker) are record fields modelled via hasCharacter/relationEntry; the
  quickjump message (SendQuickjumpMessage with _NACHRICHTEN_HS_01) is modelled by
  sendMessage(cityId, 6073).

## Counts
- Functions audited: **11** (all provenance-tagged functions in the file).
- VERIFIED-1:1 (no source change): 5 (GroupComposition, Tavern, StartWander,
  GatherFollowers, DismissApprentice — modulo documented boundaries).
- FIXED: 5 (RunCreditStep float-narrow, MasterExamState state-0 order, KidnapCarryStep
  ransom truncation + golden, FireSpreadStep case-1 offsets, EvaluateAssignProfession
  RNG draw count). MasterExamPayStep: VERIFIED (return-value boundary only).
- Golden vectors corrected: 1 (e2e KidnapRansomEmitsSlotReset 1000 -> 999).
- Boundaries (data/3D-transform not in hook contract): noted per function.

## Tests
Targets `npcaction10_test`, `npcaction10_itest`, `npcaction10_e2e_test` rebuilt and
run via ctest -R npcaction10: **3/3 pass** after the fixes.
