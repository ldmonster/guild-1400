# Hardening pass — meister_storage / meister_supervision / meister_workstation

Scope: every function carrying `gilde.exe 0xADDR` provenance in
`src/ai/meister_storage.cpp`, `src/ai/meister_supervision.cpp`,
`src/ai/meister_workstation.cpp`. Each original DECOMPILEd + DISASMed and diffed
line-for-line against the binary; constants confirmed via get_bytes.

## Constants (get_bytes, all VERIFIED byte-exact)
| symbol | VA | bytes | value |
|---|---|---|---|
| dbl_619908 / dbl_619918 | 0x619908/918 | C202A05F20000000 | -1e10 |
| dbl_619910 | 0x619910 | 3FECCCCCCCCCCCCD | 0.9 |
| flt_6198FC / flt_619904 | 0x6198FC/904 | 3C23D70A | 0.01 |
| flt_619900 | 0x619900 | 40F00000 | 7.5 |
| flt_619D2C / flt_619E9C | 0x619D2C/E9C | 3FA00000 | 1.25 |
| dbl_619D38 | 0x619D38 | 3FD0000000000000 | 0.25 |
| dbl_619970 | 0x619970 | 4065000000000000 | 168.0 |
| dbl_619968 | 0x619968 | 406F800000000000 | 252.0 |
| dbl_61E884 | 0x61E884 | 3FE0000000000000 | 0.5 |

## Item-record byte offsets (base 0xB5444E, all confirmed by symbol addresses)
+0 id, +6 backIndex, +10 sourceBuilding, +14 src/freeCap-alias, +18 field18,
+22 required(B54464), +26 field26(B54468), +30 unitPrice, +34 margin,
+38 reserved(B54474), +42 stock(B54478), +46 freeCap(B5447C), +50 flags50(B54480),
+54 plannedQty, +58 destStock, +62 bits(B5448C).
Pseudocode `*((int*)v6+N)` with v6 = base+2 → byte base+2+4N.

---

## meister_workstation.cpp

- **ComputeWorkstationOutput @0x45b948** — VERIFIED-1:1. Gate `outCache<=-1e10`,
  per-slot price gate `unitPrice<=-1e10`, chain discount *0.9, outputValue/=divisorOutputs,
  margin=(sellPrice-(out+rate))/divisorRate all match.
- **SortWorkstationsByScore @0x4599f0(tail)** — VERIFIED-1:1 (ascending bubble,
  reserveTarget=index).
- **SortItemsByPlannedValue @0x45aa78(tail)** — VERIFIED-1:1 (qty*margin desc).
- **GatherNetShortfall / GatherClampToBudget @0x45c10c** — VERIFIED-1:1 (net=req-(resv+stock)
  clamp0; budget while-drop loop; accum via ConvertX truncate).
- **DistributePlanQty @0x45aa78** — VERIFIED-1:1 (min(srcFree,destRoom); (budget>>2)/price;
  ConvertX trunc).
- **CheckWorkstationCapacity @0x45ba84** — **FIXED** (3 field-mapping bugs + supply math):
  - gate was `need<=reserved`; binary `cmp eax,[ebx+28h]` → **need<=stock** (0x45bace). FIXED.
  - headroom was `flags50==0`; binary `[ebx+2Ch]` → **freeCap==0** (0x45badb). FIXED.
  - "already feasible" was `stock!=0`; binary `[ebx+24h]` → **reserved!=0** (0x45bae2). FIXED.
  - `AffordableSupply` was a cheap analogue (min(budget/price,need) clamped deficit). Replaced
    with the faithful 0x45bc62..0x45bd3f computation:
    `v23=max(station.freeCap>>1,1); chainQty=need*v23-(reserved+stock);
     chainQty=max(.,(item.freeCap>>1)-reserved); chainQty=min(.,freeCap-reserved-1);
     chosen=min(budget*(1/price),chainQty); v25=min(deficit,trunc(chosen))`;
     feasible iff `need<=v25`. sameOwner → immediate feasible (LABEL_25).
  - Goldens fixed in ai_meister_workstation_test.cpp (Capacity* tests now set stock/reserved/
    freeCap + station.freeCap per binary semantics).

## meister_storage.cpp

- **StorageDemandBump @0x45f1e4** — VERIFIED gate+value. gate `(bits&6)==0 && (bits&8)!=0 &&
  slotCap/3<stock`; value `aiType==22 ? stock/2 : stock` (0x45f284 sar trick = /2). NOTE:
  binary writes field26(+26); the C++ collapse stores into the DemandBump command (qty correct)
  + it.required — documented orchestration-collapse representation, not a behavior diff in the
  emitted command.
- **ProfitMarginSell @0x4614d0/0x45f1e4** — VERIFIED-1:1. `(bits&6)==0`; `(bits&8)==0 ||
  slotCap/3<stock`; `ratio<1.0 && roll*1.25(+0.25 storage) >= ratio`.
- **EmergencySell @0x45f1e4** — VERIFIED-1:1. start=min(held,funds); qty=max(1,32000/price);
  raw drives proceeds; clamp to stock. (div-by-zero price<=0 guard added defensively.)
- **OverstockSell @0x45f1e4** — VERIFIED-1:1. `3*slotCap/4<stock`; qty=max(5,stock/4) clamp stock.
- **RunStorageSellPass @0x45f1e4 (orchestration)** — VERIFIED (gates/order: demand-bump sweep;
  odd-hour guard +437&2; profit→emergency→overstock). Cart-unload/reconcile tail + German
  cm_RequestSellObjekt logging remains DEFERRED (BOUNDARY, documented in header).
- **AssignWorkstations @0x4599f0** — VERIFIED-1:1 for the decision/build logic: count>1 station
  collect (cap 32); slot→item expansion (cap 128); special-output dup 452..454→bits 0x24,
  449..451→0x204; back-link; stock/freeCap snapshot + Notify20 on zero stock; delivery fold;
  ComputeOutput+sort; post-sort 0x40 match stamp. The over-32 guard on special-station add and
  the env-injected leaf reads are the documented BOUNDARY.
- **CollectStorageItems @0x45a62c** — VERIFIED-1:1 (import then export list, skip-present,
  AddItem(flags50=129, bits=0, unitPrice=market), stock/reserved/freeCap snapshot, delivery fold).
- **GatherRequiredItems @0x45c10c** — VERIFIED-1:1 (required++/0x2 for in-list needs; net
  shortfall; seller pick → required=0 if none; GatherClampToBudget).
- **ReserveWorkstationItems @0x45bd68** — **FIXED** (multiple field + control-flow bugs):
  - slot clamp target was `field18`; binary `[edx+30h]` → **flags50(+50)** clamped to
    station.reserveTarget (`[ebx+48h]`=+76). FIXED.
  - special-id headroom: `4*(stock+incoming)<freeCap && reserved+stock<4*need` confirmed.
  - chained branch: added missing **bits|=0x8** when producer typedef divisorOutputs(+54) <
    producer.freeCap (0x45beec/bef4). FIXED.
  - raw branch was a cheap analogue writing field18 with a `break`. Rewritten faithfully:
    iterate ALL sellers (no break); set item.sourceBuilding(+10)=seller; sameOwner →
    `required(+14)=freeCap`; else priced supply math identical to CheckWs::AffordableSupply with
    fistp-trunc; `q=min(deficit,trunc(chosen))`; if `q>=need` then `required=max(required,q)`.
    FIXED. (Added `void* building` to StockSeller for the sourceBuilding write.)

## meister_supervision.cpp

- **RequestCmd134 @0x4c73b4** — VERIFIED-1:1 (handler(1,0,134) probe; queue type 134/-1/kind2).
- **RequestBuildingCmd43 @0x4c7308** — VERIFIED-1:1 (sticky `found` across persons; per-pid
  handler probe; queue type 43).
- **ClearDarkCorner @0x4c7590** — VERIFIED-1:1 (currentDay-lastUse>1 → op83; INT_MIN unresolved).
- **SuperviseStammtisch @0x4c74c8** — VERIFIED-1:1 per-seat decision (occ!=-1 && (!found ||
  !active || state==15) → op84). Seat count is caller-injected (binary loops 4 stride-4-byte
  seats; header's "8" comment is cosmetic). BOUNDARY: seat iteration count.
- **FlagIdleStaff @0x45df7c** — **FIXED**:
  - already-supervised returned **-1**; binary `return 0` (0x45e07a). FIXED (source+goldens).
  - both passes were **missing the owner gate** `dword_12CEA7C[slot]==building+364`. Added
    `s.ownerBuildId == buildId` to pass 1 and pass 2 (0x45e09b / 0x45e052). FIXED.
  - firstSlot(v3) increment made unconditional (++v3 placement) — observably identical (only
    used on the no-candidate path where it equals slotCount).
  - Goldens fixed in meister_supervision_test.cpp + integration + e2e (set ownerBuildId;
    already-supervised expectation -1→0).
- **CountStaffByType @0x45d3ec** — classification rule (trade 23/37→master else other; counted
  bit) VERIFIED-1:1. The recursive table-walk is modeled via an injected head+members+subtree
  abstraction (documented in the .cpp). BOUNDARY: tree-walk modeling, not the per-node rule.
- **UpdateBuildingHealthState @0x4c9104** — VERIFIED-1:1 (trade 6/7 && maxHealth>0; ratio>=0.5
  → 5297 else 5296; always queue_state22; skip !inBuilding/noUpdate). Message routing collapsed
  to hook (BOUNDARY).
- **RunBuildingTasks @0x4c930c** — VERIFIED (dispatch order via RunBuildingTasksOrder;
  AssignWorkersToBuilding @0x4c8f14 still the inert deferred leaf, documented).

## Counts
- VERIFIED-1:1: 20
- FIXED: 3 functions (CheckWorkstationCapacity, ReserveWorkstationItems, FlagIdleStaff) —
  6 field-mapping bugs, 1 missing flag (0x8), 1 missing flag/clamp, 1 wrong return,
  2 missing owner gates, 2 cheap-analogue supply-math replacements, 1 break-vs-iterate-all.
- BOUNDARY (documented, unchanged): sell/log tail of TradeManageStorage, env-injected leaf
  reads, CountStaffByType tree-walk model, stammtisch seat count, quickjump message routing.

## Build/test
`cmake --build build --target meister_supervision_test ai_meister_workstation_test
 meister_supervision_itest meister_supervision_e2e_test ai_meister_workstation_e2e_test
 ai_meister_trade_test ai_meister_subplanners_test ai_meister_equip_test ai_meister_farming_test -j`
→ all green. ctest `supervision|workstation|wache|diebe|angriff|passes|equip|meister_mgmt|
meister_trade|subplanners` = 14/14 passed; farming 41/41.
