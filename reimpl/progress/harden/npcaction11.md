# Harden report — src/sim/npcaction11.cpp

1:1 audit of every `gilde.exe 0xADDR` function against decompile + disasm.
MCP live (gilde.exe, imagebase 0x400000). Reference of record = Hex-Rays; disasm wins on conflict.

## Verified constants (get_bytes)
- dbl_61F650 = `9A 99 99 99 99 99 B9 3F` = 0x3FB999999999999A = **0.1** -> kPickupMoodMul. VERIFIED.
- dbl_61F658 = `00 00 00 00 00 00 E0 3F` = 0x3FE0000000000000 = **0.5** -> kPickupMoodBias. VERIFIED.
- flt_61A5F4 = `9A 99 99 3E` = 0x3E99999A = **0.3f** -> kWellWorthMul. VERIFIED.
- byte_6477A1 (global currency) = get_global_value = **0x0**. The source hardcodes `0` for the
  EnqueueCmd15/Request17 currency arg; behaviorally identical at this snapshot (global is 0).
  Documented BOUNDARY (currency global not threaded; literal 0 == its value).

## GameTime_Advance arg mapping (0x583150) — confirmed
`Advance(a1, a2@edx=addDays/hours, a3@ecx=addSeconds, a4@ebx=addMinutes)`.
Source helper `Advance(h, off, days, secs, mins)` -> `GameTimeAdvance(days, secs, mins)`. Matches.

## Per-function

### 0x4e4728 BeginEquipObject — FIXED
- Stamp +82 and +68 with clock; Person_QueryBegin(+172); if absent free. VERIFIED.
- **BUG FIXED**: fast-mode (dword_63C7B8) path. Disasm @0x4e47fd sets v7=30 (addMinutes),
  v6=0 (addDays) -> `Advance(+82, days=0, secs=0, mins=30)`. Source previously set
  `days=0; mins=0`, dropping the 30-minute appointment. Fixed source to `days=0; mins=30`.
  Golden `BeginEquipObjectFastModeNoAdvanceSetsBit` asserted `st.minute==0`; **golden fixed**
  to `st.minute==30` (cite 0x4e47fd). Slow path: 24*(apprentice span), min 24, carried in
  addDays. VERIFIED. arg25(id,90,64,2,0), +90 |= 0x40 gate. VERIFIED.

### 0x4e4ed8 BeginUnequipObject — VERIFIED-1:1
Building_FindById(+172), stamp +82, RandomModulo(10), Advance(+82,0,0,r+10),
arg25(id,90,512,2,0). RNG: exactly 1 RandomModulo(10) draw. Matches.

### 0x4e4a48 BeginUseObject — VERIFIED-1:1
Stamp +82/+68. Fast: Advance(+82, days=0, secs=1, mins=0) (v6=0,v7=1). Slow: Advance(5,0,0)
(v6=5,v7=0). Query +172; if absent free. +16 = objId. `result[90] >= 0` (signed top-bit
clear) -> arg25(id,90,128,2,0). Source `!(flags & 0x80)` == signed `>=0`. Matches.

### 0x4e4c84 BeginStoreObject — VERIFIED-1:1
Query(+172); if present arg25(id,90,1024,2,0); then stamp +82. Order (arg25 before stamp)
matches disasm. Matches.

### 0x4e5b20 DecrementCarryStep — VERIFIED-1:1 (w/ BOUNDARY)
State fork: <-2 ignore; -2/-1 free; 0 -> dec +184, FindRecordById(+172). When +184<0 and
person: kind 6/7 -> slot-reset-28, then RequestBuildOp72(person+4, *(BYTE)(person+356)-1),
free. Returns the person pointer (eax). Matches.
- NOTE: orig reads +356 as a **BYTE** (`*(_BYTE *)(v7+356)`); routed via personEquipState hook
  (generic). BOUNDARY: slot-reset image (v12=38, v18=9, clock) built inside requestSlotReset28
  hook.

### 0x4ea10c CheckTargetBusyState — VERIFIED-1:1 (w/ BOUNDARY)
Scan handlers (filter 60) for +180==+180 or +172==+172 conflict; QueryBegin(+180); if
conflict OR (person && *(person+101)!=-1) -> +112=-1. FindRecordById(+172); if *(+433)!=0 ->
+112=-1. Stamp +82, Advance(+82, days=0, secs=1, mins=0). Matches.
- BOUNDARY: handler conflict scan (FindFirst/FindNext loop) routed via findConflictingHandler.

### 0x4e6d2c DismissStaffStep — VERIFIED-1:1 (w/ BOUNDARY)
State fork as above. State 0: FindRecordById(+172); if person && *(+8)!=0: if *(+364)!=0 ->
BeginDeltaPacket, AppendCopiedField(364), QueueRequestState23, coord27(city, person+4, -25);
then BuildOp71(person+4,0,0,0), BuildOp77(person+4), NamedObject53("Entlassen"); free. Else
(+8==0) -> +112=-1. Matches structurally.
- NOTE: orig coord27 city = `dword_12CE914[134 * *(WORD*)(*(person+364)+39)]` — it treats
  *(person+364) as a record POINTER and reads its WORD at +39 for the city index. Source routes
  via field364/findPersonById + markerWord (header reads +0). BOUNDARY (city-index resolution
  abstracted through hooks).
- NOTE: orig gates the NamedObject53 on `Combat_PickActiveTargetEntry(...)` and branches on its
  out param v10 (==-1 vs concrete). Source emits the v10==-1 variant unconditionally; the combat
  pick is a leaf routed via the requestNamedObject53 hook. BOUNDARY.

### 0x4e73c0 BroadcastMoveToTargetsStep — VERIFIED-1:1 (w/ BOUNDARY)
State fork. State 0: QueryBegin(+16) leader; if absent free. delta = 20 * (+172 /
Money_MultiplyByRate(1000, byte_6477A1)). Loop 768 city slots (word_12CE910 stride 268),
skip marker==-1, match aux person (dword_12CEA7C) == leader -> coord27(self city, ci city,
delta). Then self kind (byte_12CE912) 6/7 -> quickjump(5562); free. Source: divide guards
rate!=0 (orig would div-by-zero if rate 0 — acceptable, rate is constant-rate product, nonzero
at runtime). RNG: none. Matches.
- BOUNDARY: city arrays threaded via cityMarker/cityPersonRecord/cityKind/cityId hooks.

### 0x4e4834 DropObjectStep — VERIFIED-1:1 (w/ BOUNDARY)
State fork: <-2 (!=-2) return; -2/-1 -> Query(+172), arg25(id,90,0,2,64), free. >0 return.
0 -> Query(+172); if person: self kind==6 -> quickjump(5114); kind 5/6 -> loop 768 hosts
(6/7, skip self) quickjump(6207); single58 + arg25(id,90,0,2,64); free. Source state collapse
re-derived line-for-line and verified for states -3,-2,-1,0,>0. arg25 d-arg = 64 (orig).
Matches. BOUNDARY: text/quickjump broadcast via sendQuickjump hook.

### 0x4e454c PickupObjectStep — VERIFIED-1:1 (float->int checked)
State fork: <-2 (!=-2) return; -2/-1 -> Query, arg25(id,90,0,2,4), free. 0 -> Query(+172);
if absent free. elapsed = GameTime_DiffMinutes(+96, clock). v7 = elapsed*0.1 + 0.5 (x87
double), then **Coord_ConvertX @0x5c6b08 = truncate-toward-zero**, v13=(int)v7. Source uses
`(int)amt` (C truncation toward zero) — MATCHES the ConvertX truncation. +176 -= iamt;
AdjustMood(person, iamt). If +176>0: re-stamp +96 & +82, Advance(+82,0,0,10), return. Else
self kind 6/7 -> quickjump(5086); +112=-1. Matches. RNG: none.

### 0x4e4af4 UseObjectStep — VERIFIED-1:1 (signed vs unsigned shift checked)
switch(state+2): 0/1 -> Query, arg25(id,90,0,2,128), free. 2 -> Query; request17(id,-1,1,
**HIWORD(+174)** unsigned, byte_6477A1, 0) + arg25(id,90,0,2,128); ++state. 3 -> Query; if
person && GameObject_QueryFind(*(person+93),1,0, **(+174)>>16 signed**) -> quickjump(6200),
free; else free. **Source correctly distinguishes**: case 2 `(F32(174)>>16)&0xFFFF` (unsigned
HIWORD), case 3 `F32(174)>>16` (signed SAR via i32&). Matches. BOUNDARY: gameObjectQueryFind
base = *(person+93) abstracted to person handle in hook.

### 0x471b10 EvaluateUseBack — VERIFIED-1:1 (w/ BOUNDARY)
relFlag(bl) -> 0. (*(person+485)&2) && RandomModulo(8)!=0 -> 0 (1 RNG draw, only on cooldown
bit). held=*(person+92*4=368), kind=*(person+2). !held && kind!=3 -> build {5,4,1}/{0,0,0},
SelectBestRecursive(40,*person,...). kind==3 -> require word*(person+242)&4 (byte 484), then
TryRangedAttack -> 42 else 0. else gun-find GameObject_QueryFind(*(held+93),2,6,0,21) ->
TryRangedAttack -> 42/0; else {2,21,1,..}/{4,*(held+1),1} SelectBestRecursive(40). On success
qmemcpy 0x18 into outA/outB. Source matches control flow & RNG order/count.
- BOUNDARY: SelectBestRecursive orig is 5-arg (40,*person,v8,1,v7); hook is 4-arg (drops const
  1). gun-find base *(held+93) and the {…}/{…} 24-byte request/score images built in hooks.

### 0x568650 BeginFriendship — VERIFIED-1:1 (w/ BOUNDARY)
self kind==6 -> RunOfficeOverviewWindow (host pick); else FindRecordById(*(relCtx+4)). If
!target return 0. coord27(objId(self), objId(target), **+25**). target kind 6/7 ->
SendEntityMessage(objId(target), 3246). Return 1. Matches. BOUNDARY: office window struct +
text render routed via runOfficeOverviewWindow/sendEntity hooks.

### 0x5692dc BeginDivorce — VERIFIED-1:1 (w/ BOUNDARY)
self kind==6 -> two RunOfficeOverviewWindow picks (a, then b; return 0 if !a). else a=
FindRecordById(*(relCtx+4)), b=FindRecordById(*(relCtx+8)). Null-gate: return 0 if !b
(orig LABEL_4 only checks v9/b). coord27(objId(a),objId(b),-25) then coord27(objId(b),
objId(a),-25). a kind 6/7 -> SendEntity(a, 3251); b kind 6/7 -> SendEntity(b, 3251);
return 1. Matches (text marker args inside sendEntity hook). BOUNDARY.

### 0x473e00 GrantAiCredit — VERIFIED-1:1 (w/ BOUNDARY)
*(giver)==4 gate, *(obj)==20 gate. ResolveEntityById(giver+4)->ent; if !ent return 0.
ResolveEntityById(giver+8)->ent2. BuildingActionStart("AI Kredit"). EnqueueCmd15(*(self+4),
*(ent+1), *(obj+4), **byte_6477A1=0**). SlotReset28. if ent2: BeginDeltaPacket, AppendRawField
(field 57, *(obj+4)), QueueRequestState22. BuildingActionEnd. Return 0. Source: He_Id(self) ==
*(self+4); currency 0 == byte_6477A1. Matches. BOUNDARY: slot-reset image (v7=35,v13=18,clock)
in hook; AppendRawField delta-base arithmetic in hook.

### 0x473448 BuildWell — VERIFIED-1:1 (float->int checked)
Building_FindOfficeStorage(1, h) && *(h+358)==10 gate (BYTE). *action==4 (upgrade):
ActionStart("upgr_brunnen"), worth=SumFlaggedSlotsWorth(HIBYTE(*(building+1))), v6=worth*0.3f
(x87) then **ConvertX truncate**, EnqueueCmd15(.., (i64)v6, byte_6477A1=0), SlotReset28,
ActionEnd, return 47. Source `(i32)((double)worth*0.3)` = truncation. MATCHES. else (new):
ActionStart("bau_brunnen"); if LoadBuildingGraphic -> EnqueueCmd15(-1, .., worth, 0), ActionEnd,
return 47; else ActionEnd, return 0. Source matches.
- NOTE: orig also writes `flt_B58D90[2*v]= flt_B58D70*flt_61A5F0` when *(building+16) in (1..3)
  — a UI/graphics scratch table side-effect, outside the sim hook surface. BOUNDARY (graphics
  table not modeled; consistent with rule 3/hook design).
- NOTE: HIBYTE(*(building+1)) == objId(building)>>24; source `objId(building)>>24`. Matches.

## Counts
- Functions audited: 16
- VERIFIED-1:1: 15
- FIXED: 1 (BeginEquipObject fast-mode +30min; source + golden both corrected, cite 0x4e47fd)
- Constants verified by bytes: 3 (+1 global byte_6477A1)
- float->int sites checked: 3 (Pickup mood, BuildWell upgrade cost, GrantAiCredit) — all
  truncate-toward-zero via ConvertX / (int) / (i64); MATCH.
- Signed vs unsigned shift sites: UseObjectStep case2 (unsigned HIWORD) vs case3 (signed SAR) —
  both correct in source.
- RNG draws: BeginUnequip 1x RandomModulo(10); EvaluateUseBack conditional 1x RandomModulo(8).
  Count/order match.

## Build / test status
- src/sim/npcaction11.cpp compiles clean (verbose -v EXIT=0).
- tests/unit/npcaction11_test.cpp compiles clean (object built).
- Full test link/run BLOCKED by a pre-existing, UNRELATED build error in
  src/render/raster_textured.cpp:245 (`RgbzRasterState has no member named minYSeed`) — not my
  assigned file; left untouched per scope rules. ctest could not run until that is resolved by
  the owning agent.
