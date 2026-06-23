# HOUSE-2 — Workshop/building interaction slices: full-tree 1:1 hardening sweep

Chunk: `src/play/slice_{bank,church,council,estate,market,personnel,production,tavern}.cpp`
(+ headers + unit/integration/e2e tests). MCP-driven diff of every provenance'd
function against `gilde.exe`. No git. Built only the chunk's test targets.

These slices are the IN-HOUSE/WORKSHOP GUI interaction reconstructions: each is a
`classify -> build the REAL wire packet -> route through the REAL sim::CommandQueue
codec / real apply -> run a real game-day -> HashFullWorld determinism oracle` loop.
The load-bearing 1:1 claims are (a) the wire packet BYTE LAYOUTS of the cited command
builders, (b) the float->int rounding sites, (c) the apply field offsets, and (d) the
classifier gates. All verified by `decompile`/`disasm` of the named addresses.

## Per-function verification

### slice_bank.cpp — loan (Kredit)
- `EnqueueCmd15 @0x494604` (the loan builder). Disasm: buffer `v5[16]@ebp-9Ch`;
  `v6@ebp-8Ch=+0x10` (lender a1), `v7@ebp-88h=+0x14` (borrower a2), `v8@ebp-80h=+0x1C`
  (player byte a4), `v9@ebp-7Fh=+0x1D` (amount dword a3), `v5[0]=15`. Header constants
  kLoanLenderOff/BorrowOff/PlayerOff/AmountOff = 0x10/0x14/0x1C/0x1D. **VERIFIED-1:1.**
- Classifier gate (side != kNone, amount > 0), take/repay inverse, inert cash+debt
  apply on the folded Person record. Take-vs-repay sign carried as lender==-2 marker
  (documented; the binary uses two dialogs on one channel). Faithful. **VERIFIED-1:1.**
- BOUNDARY (declared, addr-cited in header): the binary's LITERAL principal write is
  `family.+18 += amount` into word_13C3110 (NOT folded by HashFullWorld); applied to
  the folded borrower Person record instead, family write offered as a hook.

### slice_church.cpp — donation (Spenden)
- Builder = same `EnqueueCmd15 @0x494604`. Donation mapping Dst=+0x10, Src=+0x14,
  Type=+0x1C(byte), Amt=+0x1D(dword). **VERIFIED-1:1** (same disasm as bank).
- Apply = REAL `ExRemapObjectPair @0x496978` (called directly via ChurchCmdHandler).
  Disasm confirms: `v5=*(a1+20)`=+0x14 = REMOVE source (donor), `v8=*(a1+16)`=+0x10 =
  ADD destination (church), `*(u8)(a1+28)`=+0x1C currency-type proto index,
  `*(a1+29)`=+0x1D amount. Encode() puts churchId@Dst(+0x10)/donorId@Src(+0x14) so the
  apply moves donor->church. **VERIFIED-1:1.**
- BOUNDARY: the two object-stock leaves RemoveObjektAmount(0x5863b4)/AddObjektToParent
  (0x5862a4) are routed through the engine's OWN deferred hooks to the folded money
  field (object+77); inert default nets +amount/-amount. Declared in header.

### slice_council.cpp — candidacy (Amtsbewerbung) — **FIXED**
- Builder `RequestBuildOp68 @0x495454`. Disasm: buffer `v3[16]@ebp-ACh` (`v3[0]=68`);
  `v4[4]@ebp-9Ch == buffer +0x10` = `qmemcpy(a1, 4)` (applicant dword v17);
  `v5[3]@ebp-98h == buffer +0x14` = `qmemcpy(a1+4, 3)` (v18/v19/v20). Caller
  `ApplyForCandidacy @0x47e1b8` passes `RequestBuildOp68(&v17)` where
  `v17=*(person+4)` (applicant), `v18=holderA`, `v19=holderB`, `v20=a2=officeType`.
  So the wire image is: applicant @ **+0x10**, holderA/B/officeType @ **+0x14/+0x15/+0x16**.
- DIVERGENCE (wrong golden): `CouncilPacket::encode(u8 out[8])` packed the fields
  CONTIGUOUSLY at out[1..7] ([1..4]=applicant, [5]=holderA, [6]=holderB, [7]=officeType)
  — NOT the real +0x10 / +0x14 staging. The unit test golden asserted the same wrong
  layout.
- FIX: `encode()` now returns a full 153-byte `sim::CommandPacket` with applicant @
  +0x10 (kCouncilApplicantOff) and holderA/B/officeType @ +0x14/+0x15/+0x16
  (kCouncilHolderAOff/HolderBOff/OfficeOff). Header gained those four offset constants
  + `#include "sim/command.h"`. Test `BuildPacketEmitsOpcode68WithRealLayout` updated
  to read the real offsets off the CommandPacket. Evidence: disasm @0x495454 +
  caller @0x47e1b8. Council apply path (ApplyCouncilPacket -> real
  `OfficeAssignToCandidate @0x47e4e0`) unchanged; it reads struct fields, not the wire
  image, so the apply was already correct. **FIXED -> VERIFIED-1:1.**

### slice_estate.cpp — buy building — **FIXED**
- Builder `QueueRequestQuad56 @0x495098`. Disasm: buffer `v5[16]@ebp-A0h` (`v5[0]=56`);
  `v6@ebp-90h=+0x10` (a1), `v7@ebp-8Ch=+0x14` (a2), `v8@ebp-88h=+0x18` (a4); `v9=a3` is
  a LOCAL @ebp-4h and never reaches the wire (matches header note).
- FIELD ROLES recovered from builder `EnqueueBuyBuilding @0x588798` and apply
  `ExSetObjectParent @0x49ae60`:
    - builder: `QueueRequestQuad56(*(Begin+1), *(v19+1), v11/*not in pkt*/, *(v19+1))`
      => a1@+0x10 = the bought OBJECT id (Begin = the reparented object),
         a2@+0x14 / a4@+0x18 = the new-owner record id.
    - apply: `Begin=QueryBegin(...,*(pkt+0x10))`; `parentRec=FindRecordById(*(pkt+0x14))`;
      `ownerRec=FindRecordById(*(pkt+0x18))`;
      `SetObjectParent(Begin, *parentRec, (u16)*ownerRec)`.
    - `SetObjectParent @0x58820c`: `*(WORD*)(obj+37)=a2` (parent handle),
      `*(WORD*)(obj+39)=a3` (owner). Confirmed.
  => the real wire roles are: **+0x10 = object**, **+0x14 = parent-handle source**,
     **+0x18 = owner-id source**.
- DIVERGENCE (wrong golden): the reconstruction labelled +0x10="newOwner", +0x14="object",
  +0x18="parent" (kEstateNewOwnerOff=0x10, kEstateObjectOff=0x14, kEstateParentOff=0x18)
  — the object/owner/parent roles were scrambled vs the binary. A packet built by the
  slice would be misdecoded by the real ExSetObjectParent. Unit test golden asserted
  the wrong offsets too.
- FIX: offset constants corrected to the binary — kEstateObjectOff=0x10,
  kEstateParentOff=0x14, kEstateNewOwnerOff=0x18 — with the builder/handler comments
  and the unit-test golden (`QueueRequestQuad56OffsetsGolden`) updated to the real
  roles. Because both BuildEstatePacket and EstateCmdHandler reference the fields by the
  named constants, the slice round-trips correctly and the +37/+39 field offsets
  (kEstateParentFieldOff=37 / kEstateOwnerFieldOff=39) were already right. Evidence:
  disasm @0x495098, @0x588798, @0x49ae60, @0x58820c. **FIXED -> VERIFIED-1:1.**
- BOUNDARY: the deep SetObjectParent leaves (storage-room attach, scene-slot fixup,
  character flag/animation, texture-set) are render/scene state outside the folded
  world; the slice applies the folded net effect (+37/+39 + the EnqueueCmd15 cash leg).

### slice_market.cpp — trade (buy/sell goods)
- Builder `QueueRequest17 @0x49465c`. Disasm: buffer `v7[16]@ebp-A0h` (`v7[0]=17`);
  `v8@ebp-90h=+0x10` (seller a1), `v9@ebp-8Ch=+0x14` (buyer a2), `v10@ebp-88h=+0x18`
  (proto word a4), `v11@ebp-82h=+0x1E` (player byte a5), `v12@ebp-81h=+0x1F` (qty dword
  a3), `v13@ebp-7Dh=+0x23` (price dword a6). Header kTrade{Seller,Buyer,Proto,Player,
  Qty,Price}Off = 0x10/0x14/0x18/0x1E/0x1F/0x23. **VERIFIED-1:1.**
- FLOAT->INT site. `RequestSellObjekt @0x46bff0` computes
  `v8 = LookupCachedMarketPrice(...)` then calls `VIBE_Coord_ConvertX()` (the
  truncate-toward-zero fixup @0x5c6b08) and passes `(__int64)v8` to QueueRequest17 ->
  the price is TRUNCATED toward zero. The reconstruction uses
  `TruncToInt = static_cast<i32>(double)` (truncate). `LookupCachedMarketPrice @0x58f6b8`
  falls back to `ComputeMarketPrice(proto, 0x64)` — the reconstruction calls
  `Building_ComputeMarketPrice(ware, 100)` (qty=100=0x64). `ComputeMarketPrice @0x58f3d0`
  cached path = `(32*cachedPrice)*qty*flt_626948`, and writes `(int)v20` into record+56
  after a ConvertX (truncate) on first compute. All float->int sites = truncate.
  **VERIFIED-1:1.** (The price model itself lives in the sibling building_production.cpp,
  out of this chunk.)
- BOUNDARY: BUY-from-market is the inverse of the reconstructed SELL credit (no single
  reconstructed buy command; buy paths are GUI dialog loops). buyer==-2 marker, treasury
  in folded object+77. Declared in header.

### slice_personnel.cpp — hire / marry
- Eligibility = REAL `RecruitCheckRecruitProximity @0x55d5c0` (sibling). Disasm confirms
  it reads the employer field at `*((DWORD*)rec + 23)` = **+92 (0x5C)**, returns negative
  reject codes (-1024..-1028) or `abs(rankA-rankB) < 5` (0/1). The slice gate
  `issued = (proximity >= 0)` correctly separates reject (negative) from accept (0/1).
  **VERIFIED-1:1.** Cost = REAL `RecruitComputeRecruitmentCost @0x55d674` (sibling).
  Hire dialog builds opcode-28 via QueueRequestSlotReset28 (see production). The +92
  employer offset matches `person_personnel2.h` kPf2RelArray=0x5C. **VERIFIED-1:1.**
- BOUNDARY: the confirmed-hire apply (employer +92 bind + fee debit) and the MARRY
  reciprocal-binding apply are inert-default field mutations on the folded Person
  records (the real commands' net observable effect); installable hooks.

### slice_production.cpp — workshop production order
- Builder = REAL sibling `sim::QueueRequestSlotReset28` (`@0x4948c8`). Disasm confirms
  `v5[0]=28`, a `StagePendingBlock(0xF8/*248*/, ...)` side-block, then EnqueuePacket —
  the wire packet is opcode-only; the 248-byte order body travels via StagePendingBlock.
  Header kProductionCmdOpcode=28, kProductionBodyBytes=0xF8. **VERIFIED-1:1.**
- Dispatch coupling: `ClassifyProductionAction` keys off the REAL
  `BuildingDialogKindToActionGroup` (interact_building) and the kind ladder 116/133/155/247
  (Smith/Carpenter/Stonemason/Production) confirmed in interact_building.cpp's
  EnterAndDispatch (@0x51defc) switch. `IssueProductionClick` drives the REAL
  PickAndResolveSceneEntity + BuildingDialogFsm. **VERIFIED-1:1** (structurally in the
  dispatch tree).
- BOUNDARY: the opcode-28 RECEIVER (reassembly of the StagePendingBlock body into a slot
  write) is not reconstructed; modeled via a per-queue side channel + installable apply
  hook. The production tick itself is the REAL `Building_RunProductionTick @0x5847a0`
  (sibling). Declared in header.

### slice_tavern.cpp — dark-corner recruit (thug)
- Builder `RequestBuildOp83 @0x495954`. Disasm: buffer `v2[16]@ebp-A8h` (`v2[0]=83`);
  `v3..v7` = `a1[0..4]` staged at +0x10/+0x14/+0x18/+0x1C/+0x20 (five dwords). Caller
  `TavernDarkCornerBuy @0x5186b4` fills `v25[5]`: `v25[0]=0x62757920` ("buy "),
  `v25[1]`=acting player handle, `v25[2]=*(v35+1)` (location id), `v25[3]`=target NPC id,
  `v25[4]`=price. `encode(out[36])` writes tag@16/player@20/location@24/target@28/price@32
  = +0x10/+0x14/+0x18/+0x1C/+0x20. kTavernBuyTag=0x62757920. **VERIFIED-1:1.**
- Affordability gate. Binary: `CheckResourceAmount(price, player) @0x4ad62c` passes when
  `available >= price` (or price==0). The slice builds only when `cash >= price` (rejects
  when `price > cash`). Matches. **VERIFIED-1:1.**
- BOUNDARY: the apply is op-36 `QueueRequestPair36 @0x494b98` (master binding) + op-16
  `QueueRequest16 @0x494630` (price transfer); modeled as an inert-default field mutation
  (relation +0x5C, superior +0x60, cash -price) on the folded Person records. Installable
  hook. Declared in header. +0x5C/+0x60 match person_personnel2.h.

## Rule-13 reachability (building/context-action dispatch)
- WIRED in src (live tree):
  - bank    -> `dialog_bank.cpp` (RunBankSlice / ClassifyBankInteraction)
  - market  -> `dialog_market.cpp` + `playthrough.cpp` (RunMarketSlice / Classify…)
  - council -> `dialog_council.cpp` + `playthrough.cpp` (RunCouncilSlice / BuildCouncilPacket)
  - church  -> `playthrough.cpp` (SeatChurchObject / ReadObjectMoney / the donation path)
- NOT YET WIRED from src (reached only from tests today): estate, personnel, tavern,
  and production's RunProductionSlice/IssueProductionClick. production is STRUCTURALLY in
  the dispatch tree (its classifier keys off the real BuildingDialogKindToActionGroup and
  it drives the real BuildingDialogFsm), it simply lacks a src invocation.
  The live-tree integration for these is the dialog/session DRIVER layer
  (`dialog_*.cpp` / `playthrough.cpp` / interact_building), which is OUTSIDE this chunk's
  ownership — exactly the layer that wired bank/market/council via their own dialog files.
  Per ownership rules I did not fabricate dispatch coupling or create driver files. This
  is the only remaining reachability gap and is a driver-layer task, not a slice defect.

## Build / test (chunk test targets only; never touched build/ layout)
Green: play_slice_estate_test, slice_council_test, play_slice_estate_itest,
play_slice_estate_e2e_test, slice_council_itest, slice_council_e2e_test,
play_slice_bank_test, play_slice_market_test, slice_personnel_test,
slice_production_test, slice_tavern_test, slice_church_itest. 0 failures.

## Counts
- Provenance'd command builders / applies diffed against the binary: 11
  (EnqueueCmd15 0x494604, QueueRequest17 0x49465c, RequestBuildOp68 0x495454,
   QueueRequestQuad56 0x495098, RequestBuildOp83 0x495954, QueueRequestSlotReset28
   0x4948c8, ExRemapObjectPair 0x496978, ExSetObjectParent 0x49ae60, SetObjectParent
   0x58820c, ComputeMarketPrice/LookupCachedMarketPrice 0x58f3d0/0x58f6b8 + ConvertX
   0x5c6b08, CheckRecruitProximity 0x55d5c0 / CheckResourceAmount 0x4ad62c).
- FIXED: 2 (slice_council wire layout; slice_estate packet field roles) — source +
  golden corrected to the binary, with addr/disasm evidence.
- VERIFIED-1:1 (no churn): bank, church, market, personnel, production, tavern wire
  layouts/applies/gates + the float->int truncation sites.
- BOUNDARY (declared, addr-cited): family-table loan principal; object-stock leaves;
  SetObjectParent render/scene leaves; market buy-from-market inverse; op-28 receiver
  reassembly; op-36/op-16 tavern apply; personnel hire/marry apply.
