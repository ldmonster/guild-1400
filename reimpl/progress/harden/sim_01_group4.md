# 1:1 Hardening — sim group 4 (building value/stock/storage/production)

Owned files:
- src/sim/building_production.cpp (+ .h)
- src/sim/building_stock.cpp (+ .h)
- src/sim/building_storage.cpp (+ .h)
- src/sim/building_value.cpp (+ .h)

MCP: gilde.exe @ imagebase 0x400000. Every function below decompiled AND disasm'd,
diffed line-for-line. Every float/double constant confirmed via get_bytes. Every
float->int site checked against the disasm (ConvertX/fistp/(int) all TRUNCATE here).

## Per-function verdicts

### building_value.cpp
- 0x58a794 Building_EvalProductionRating — VERIFIED-1:1. statLevel@+128, staffBits@+44,
  all bit extractions ((v>>20)&7 etc.) match the shl/shr forms; consts flt_62670C(1/252),
  flt_626710(.5), flt_626714(1/6), flt_626734(.333), dbl_62671C(.2), dbl_626724(1/6),
  dbl_62672C(.333) byte-confirmed. Clamp [0,1] exact. (Reimpl keeps a defensive stat<0
  guard the binary omits — behavior-identical for the live 0..4 callers; documented.)
- 0x58a6e8 Building_ComputeRatingCurveA — VERIFIED-1:1. Disasm confirms BOTH Eval calls use
  the SAME stat (mov edx,ecx); the Hex-Rays uninit `v4` is an artifact. consts .6/.4/.5.
- 0x58a73c Building_ComputeRatingCurveB — VERIFIED-1:1 (.6/.4/.5).
- 0x58a6bc Building_ComputeProductionPixels — VERIFIED-1:1. flt_6266F0=252; ConvertX trunc.
- 0x58f328 Building_ComputeItemBaseValue — VERIFIED-1:1. a4(inIdx)->+553, a3(outIdx)->+563,
  896*factor, objectKind@+2 ∈{6,7} -> (1-(2-priceMode)*0.25). dbl_6268FC=0.25.
- 0x58f268 Building_ComputeProductionRate — VERIFIED-1:1 (parameterized). sum 0..1 of
  outputFactor*28*32*(1/12)*(1/60), stored to float, then *priceField/divisor. consts
  28/32/(1/12)/(1/60) byte-confirmed. Sum truncates to float at v5 (matches reimpl).
- 0x57d26c/0x57d310/0x57d384 ComputeCurrentOutput / ComputeMaxOutput / ComputeOutputRatio —
  VERIFIED-1:1. Offsets +16/+28/+32/+36 (static_assert'd). outBonus@+36 read as signed int.

### building_stock.cpp
- 0x57d1c8 ComputeProjectedStock — FIXED. Decay rate is the +20 field (float[5]); after the
  field rename it now reads b->decayRate(+20). loop start +28. ConvertX trunc. (Same physical
  offset; rename for correctness vs SyncStockLevel/AdjustStock — see header note.)
- 0x57d0f8 SyncStockLevel (ProjectForward) — FIXED. Projection BASE is result[6] == +24, NOT
  +28. Disasm @0x57d13f `mov eax,[ecx+18h]`. Added BuildingStockRec::projBase(+24); decay
  uses +20. delta vs +28 (fullFill), command field 28. (Golden in SyncStockLevel test seeds
  projBase=full so its earlier value stays valid AND exercises the +24 read.)
- 0x57d3d0 ComputeEfficiencyScore — VERIFIED-1:1. activeFlag<=1 unsigned gate; consts
  flt_62594C(.001), dbl_625954(.2), dbl_62595C(.8) byte-confirmed.
- 0x57d5b4 AdjustStockAndNotify — FIXED. The interpolation zero-fill output is the +16 field
  (float[4]), NOT +20. Disasm @0x57d7b0 `*((float*)v23+4)`==+16. Renamed +16->zeroOut and
  read it. delta@+36 signed int. (Golden recomputed: with zeroOut=0 the lerp -> 5.0 not 6.0.)
- 0x57dcb4 FindMatchingSupplier — FIXED two divergences: (1) when any of +92/+96/+100 ==
  selfId the binary RETURNS 0 (terminate), it does NOT continue to the next record (return
  v7^v6==0 @0x57dd18). (2) the 5-slot customer scan does NOT break on -1: a -1 slot is
  skipped for the count but scanning continues (goto LABEL_17). Both reproduced.
- 0x5913e0 SumFlaggedSlotsWorth — FIXED. ComputeMarketPrice arg is the SceneTypeDef's +0
  KIND byte (`*v5`, v5=65*roomProt+base @0x591437/0x591472), NOT roomProt itself. Now looks
  up SceneTypeDefAt(roomProt)->kind. Flag = bit 0x8000 (sign byte at room+1). 3840*mul base.
  (Golden updated: scene[9].kind set so the lookup self-references the cached price.)
- 0x591480 ComputeSalePrice — VERIFIED-1:1 (with the SumFlaggedSlots fix). taxTier via Gesetz
  hook; q==14 -> 0.85; ((4-tier)*0.1+0.8)*worth*qmul; consts .1/.8 byte-confirmed; ConvertX.
- 0x5905dc SumWorkstationCount — VERIFIED-1:1. 64 rooms, +419 category / +483 worker count.
- 0x5904fc SumWorkstationByCategory — VERIFIED-1:1. RoomPresent(QueryFind) hook; scale path:
  (signed char)+92 * 0.01 * sum + 0.5, ConvertX trunc. consts dbl_6269F4(.01)/dbl_6269FC(.5).
- 0x57d83c DistributeGoodsToCustomers — FIXED (modeled; command/Person/Amt are boundaries).
  Fixed: (a) fallback gate dropped the `*(WORD)(v2+10) <= 0x26` condition — restored via new
  stockWord@+10; (b) fallback customer scan walks SELLER slots 0..4 (v3=3..7), not 1..4;
  (c) the no-customers count = *(int)(v46+6)>>24 (the +6 unaligned dword), not id>>24.
  RNG draws verified in order: high path RandomFloatScaled (drift) [+ RandomModulo(4) inside
  the transfer boundary hook]; fallback RandomFloatScaled (nudge). nudge table flt_641FEC[6]
  byte-confirmed {0.2400154,0.2222,0.125,0.05555,0.01385,0}. flt_625988=-0.5. The high-path
  goods-transfer (EnqueueObjectInteraction + Amt keepalive + owner-rec deref) stays a hook
  (genuine command/entity boundary; RNG-sync note: the real hook must draw RandomModulo(4)).
- 0x58fe68 ComputeProductionWorth — FIXED. Output/input column targets were SWAPPED: the
  0..5 OUTPUT sweep writes a2[6] ([edx+18h]) and the 0..1 INPUT sweep + fallback write a2[4]
  ([edx+10h]) — disasm @0x58ff8f/0x590027/0x59008b. Also the per-kind worth columns (5/7/22)
  gate on the TYPE-DEF +0 kind byte (*v40=589*type+base @0x5900d4/0x5900ef/0x5901de), NOT the
  building record's +2 object kind. Every contribution truncates: ComputeItemBaseValue*qScale
  -> ConvertX -> (int) (dbl_6269CC=0.01, quality read as int@+61). v38/v39 -> a2[16/17/20] exact.

### building_storage.cpp
- 0x591658 SumStorageItemWorth — VERIFIED-1:1. ComputeMarketPrice(prot, qtyByte@+18)+v5;
  ConvertX trunc each step. QueryFind walk = StorageItems hook.
- 0x590360 ComputeStockValue — VERIFIED-1:1 (parameterized; OwnerWealth/StorageItems hooks).
  wealth clamp to flt_6269D4=2,560,000; kind==2 -> *0.09 else *0.04 (typedef kind); ownerShare
  ConvertX trunc, floor 0. Stock loop: reserve goods {42,278,475,476} use count-1; qty*0.3
  (ConvertX trunc, min 1); *MarketPrice(prot,100). consts .04/.09/.3 byte-confirmed.
- 0x59116c ComputeRoomWorth — FIXED (state machine; scene QueryFind walk is a boundary). The
  run ENTRY now gates on (item found) && (next room scene kind NON-storage) — the reimpl
  previously entered the run unconditionally after any storage room. Run EXIT gates on next
  room scene kind ∈{2,6}. Both transitions guarded by i+1<64 (the v4<63 guard). Final:
  (double)mul * flt_626A10(0.01) * v19, ConvertX trunc.

### building_production.cpp
- 0x583304 GameTime_PackToRecord — VERIFIED-1:1 (parameterized). out+2=(u16)(day+1400) (the
  `mov ax,[eax]`/`add eax,578h` only stores `ax`), out+0=1, out+1=3*(day%4)+1 (signed idiv),
  out+4/+5 byte copies, out+8 dword copy. PackedTime layout matches.
- 0x58f3d0 ComputeMarketPrice — FIXED. The component loop now walks UP TO 4 rows (was 1): a
  component-prot word at +46+2k and a parallel priceField word at +38+2k (v13+=2/row); sub-prot
  = (i16)(word@(v13+46)); divisor is the record's +54 word (constant); a 0xFFFF row adds
  nothing (Sprintf) but still advances; loop continues while v14<4 && next +46 word!=0. Cached
  path / remap path / kind==23 path VERIFIED. consts flt_626948(.01),62694C(1.5),626950(28),
  626954(32),62695C(1/12),626964(1/60),62696C(.5),626974(2.2),626978(.03125) byte-confirmed.
  cachedPrice = trunc(v28*2.2*0.03125) via ConvertX.
- 0x584d34 ComputeSlotInput — VERIFIED-1:1. building!=0: inValue*factor*0.0005; building==0:
  flt_641DA8(=0.0 static)*factor*0.0025+0.5. ConvertX trunc. consts .0005/.0025/.5 confirmed.
  (flt_641DA8 is a runtime-set data global; reimpl uses its static 0.0 — documented.)
- 0x584de8 ComputeSlotOutput — VERIFIED-1:1. SlotWorkerOutput hook (Person walk); *0.5
  (dbl_6264BC) ConvertX trunc; then if [+32]!=0 (bit&0x7FFFFFFF): [+36]/[+32]*v13 ConvertX
  trunc. (reimpl guards num==0 -> benign vs binary inf.)
- 0x584ec8 ComputeSlotYield — VERIFIED-1:1. Confirmed flt_13C3B98[1988*a+32*s] == v11[14] ==
  slot+0x38 (same field, +56). cap@+0x20, out@+0x24. clamps 0.25..5.0 (dbl_6264C4/CC); step
  0.4 (flt_6264D4); min 512 (flt_6264D8). Person/QueryFind resolution = SlotStoredQuantity hook.
- 0x5851fc FindSlotByProt — VERIFIED-1:1. walk 62 slots, prot via (slotPacked+2)>>16, return
  &slotFieldC(k).
- 0x585198 GetSlotYieldByProt — VERIFIED-1:1. walk 62, (float)ComputeSlotYield.
- 0x5847a0 RunProductionTick — FIXED. (a) InterpCurve `k>=n-1` early branch returns
  keys[n-1].value (dword_13CD700[2*n+189*a1], disasm @0x584877 `dword_13CD700[edx+eax*8]`;
  dword_13CD700 is the VALUE column shifted back one keyframe — proven by the non-exact branch
  where dword_13CD700+v33==keys[k-1].value). Was keys[n].value. (b) the slot smoothing lerp
  is computed in DOUBLE: prev + ((double)in - prev)*dbl_62649C(0.5), then stored to float
  (matches @0x584a17). inValue/outValue scale = inScale*v/1000 (integer). 62-slot loop, slot
  field writes (smoothIn/outComp/yield, cust0..3=0) VERIFIED. post: standalone==-1 -> Sync +
  RandomizeStockTransforms hooks.
- 0x583c3c RecalcAllProduction — VERIFIED-1:1. gate schedule[0].hasProduction; loop while
  idx<4 && schedule[idx].hasProduction (stride 756). Parameterized time source.

## Constants table (all byte-confirmed via get_bytes)
flt_62670C=1/252, 626710=.5, 626714=1/6, 626734=.333, dbl_62671C=.2, 626724=1/6, 62672C=.333,
flt_6266F0=252, F4/F8/FC=.6/.4/.5, 626700/704/708=.6/.4/.5; dbl_6268FC=.25, flt_6268E4/E8=28/32,
dbl_6268EC/F4=1/12,1/60; dbl_6269CC=.01; flt_62594C=.001, dbl_625954/62595C=.2/.8, flt_625988=-.5,
flt_626A14/A18=.1/.8; dbl_6269F4/FC=.01/.5; flt_6269D4=2560000, dbl_6269DC/E4/EC=.04/.09/.3,
flt_626A10=.01; flt_626948=.01,62694C=1.5,626950/4=28/32,dbl_62695C/64=1/12,1/60,62696C=.5,
flt_626974=2.2,626978=.03125; dbl_62649C=.5,6264A4=.0005,6264AC=.0025,6264B4=.5,6264BC=.5,
6264C4=.25,6264CC=5.0,flt_6264D4=.4,6264D8=512; flt_641FEC[6]={.2400154,.2222,.125,.05555,.01385,0}.

## Counts
- Functions audited (with provenance): 30
- VERIFIED-1:1: 16
- FIXED: 11 (ProjectedStock, SyncStockLevel, AdjustStock, FindMatchingSupplier,
  SumFlaggedSlotsWorth, DistributeGoodsToCustomers, ComputeProductionWorth, ComputeRoomWorth,
  ComputeMarketPrice, RunProductionTick, GameTime/parameterized-OK counted as verified)
- BOUNDARY-modeled (hooks, per rules 3-5 / entity-scene / command queue): DistributeGoods
  high-path transfer, ComputeRoomWorth/ComputeStockValue/SumStorageItemWorth scene walks,
  ComputeSlotOutput/Yield Person walks, EvalProductionRating handler list.

## Float->int truncation fixes / audit
All ConvertX@0x5c6b08 / fistp / (int) sites confirmed TRUNCATE-toward-zero and reproduced via
truncToZero(). The accumulation-precision fix of note: RunProductionTick's slot lerp moved from
float to double intermediate to match the x87 `(double)v55 - flt_13C3B80[v24]) * dbl_62649C`.

## Golden tests fixed to match the binary (source + golden, with addr evidence)
- tests/unit/sim_remaining_test.cpp: ProductionWorth col[6]/col[4] swap; AdjustStock lerp uses
  +16 zeroOut(=0)->5.0; FlaggedSlots scene[9].kind self-ref; MakeStock field map (+16/+20/+24).
- tests/unit/sim_building_lifecycle_test.cpp: ProductionTick nowDay=25 -> 500 (keys[n-1]).
- tests/e2e/sim_remaining_e2e_test.cpp: ProductionWorth kind gate set on g_buildingTypes[0].kind.

## Validation
src/sim/{building_production,building_stock,building_storage,building_value}.cpp compile clean
(-c). Linked + ran sim_remaining_test + sim_building_test: 127 checks, 0 failures. Standalone
drivers confirm: InterpCurve nowDay=25->500 (others 175/112/100/87 unchanged); ProductionWorth
col[3]=6272,col[6]=3584,col[4]=2688; ComputeRoomWorth=767=expected.
(The full-library build is currently broken by an UNRELATED untracked sibling file
src/gui/widget_layout.cpp — not in this chunk; my TUs build and link in isolation.)
