# Wave-16 TRUE 1:1 binary diff — sim combat cluster (W16-COMBAT)

MCP LIVE. Decompiled every wave-14 NEEDS-LIVE-MCP target at its real address and
diffed it line-for-line against the reconstruction in `src/sim/combat*.{h,cpp}`.
Result: the cluster is overwhelmingly VERIFIED-1:1; one genuine divergence FIXED
(defender per-slot loot emission); one misleading comment corrected. The combat
test files compile clean; the only build blocker is a concurrent **world-cluster**
ODR clash (`MissionCompletionOutcome` doubly-defined in
`src/world/{mission_rules,history_mission}.h`), outside this cluster's ownership.

## RoundTier (combat_slots2) — confirmed, not redone
`combat_slots2.cpp::RoundTier` already uses `std::trunc` (ConvertX truncate toward
zero), with the correct provenance note (0x57eb64 call site + 0x5c6b08 ConvertX CW
high byte 0x1F = RC=11). Verified against the decompile — left as-is.

## Resolved wave-14 binary-diff queue (the 9 targets)

### 1 + 9 — ComputeAttacker/DefenderStrength (0x48d160 / 0x48d318)  FIXED + VERIFIED
Decompiled both. Command-emission order CONFIRMED:
- Attacker: base cash via **EnqueueCmd15** (`(int)((RandInt(50)*0.001+0.07)*cash)`,
  ConvertX-truncated); per-ware via **QueueRequest17** (`qty=max(1,(int)((RandInt(20)
  +80)*0.01*qty))`); market accum into v22; family credit `*(FamilyRecord+7) += v22`
  **once, after the loop**. Source matches exactly.
- Defender: per-ware **QueueRequest17** (`qty=(qty*rate>=1)?(int):1`, seller =
  commander id else -1); **DIVERGENCE FOUND + FIXED**: the no-commander bulk-cash
  command (0x48d44e, `*0.5` via dbl_61BA0C, EnqueueCmd15) **and** the family credit
  (0x48d496, `*(FamilyRecord+7) += v18`) sit INSIDE the per-production-slot loop
  (`do{...v3+=4;}while(v3!=a1+12)` = 3 slots, skipping -1), so with multiple
  populated stockpiles they fire **once per slot** against the running cumulative
  v18 — NOT once at the end. The reconstruction modeled them once-at-end. Restructured
  `ComputeDefenderStrength` to emit per-stockpile (single-slot tests unaffected; the
  difference only shows with >1 populated slot).
- **+7 family offset CONFIRMED**: `*((_DWORD*)FamilyRecord + 7)` = byte +0x1C, both
  sides. commanderCash = VIBE_Person_SumCurrencyHeld (0x48d19f). Confirmed.
- **.rdata constants confirmed via get_bytes**: dbl_61B9F4=`3f50624dd2f1a9fc`=0.001,
  dbl_61B9FC=`3fb1eb851eb851ec`=0.07, dbl_61BA04=`3f847ae147ae147b`=0.01,
  dbl_61BA0C=`3fe0000000000000`=0.5. All four match combat_strength.h.
- All qty/cash rounding is `(int)` truncation after ConvertX — matches source
  `static_cast<int>` (no nearbyint/round divergence).
- ADDED golden test `DefenderMultiSlotPerSlotEmission` pinning the two-slot
  per-slot emission (2 cash credits 10 then 15, family 100->110->125, cash 15).

### 2 — InitDefaultParameters (0x48b744)  VERIFIED-1:1 (+ comment fix)
180-float grid load + Pass A (×0.01 dbl_61B8BC, cumulative) + Pass B (dedup) match
the decompile loop-for-loop. **Return value 840** re-confirmed by tracing the
binary: `result = v11 + 120` runs each of 6 passes with v11 stepping
120→240→…→720, final result **840** (NOT 720). The test correctly asserts 840;
fixed the misleading "→720" comments in `.cpp`/`.h` to read 840. flt_B59C3C[-1]
non-read confirmed (the `+=prev` only runs for v4!=0, in-bounds).

### 3 — Physics_Update (0x5d8e80)  VERIFIED-1:1
`17*a1` stride, `if(bank)` gate, `LOWORD(v6)=HIWORD(a3)`, `HIWORD(v6)=velY`,
`HIWORD(v7)=velX`, gateB → Velocity_Apply((v6>>16)+6,(v7>>16)+6,...), then
Animation_Basic(v6>>16,v7>>16,...). The reconstruction's unsigned-assemble /
signed `>>16` recovery is byte-faithful (preserves negative velocity; no UB).
Matches exactly; goldens (-100/-7, +6, byte-offset 51) unchanged.

### 4 — BuildAttackPacket (0x488a4c)  VERIFIED-1:1
Full weapon-class switch confirmed:
- melee/default arm: ObjectDef null OR class ∈ {340,342,344,366,370,0} →
  target+4, kind=2, attacker+4 (no tile/a5).
- case 350|352: FindActiveTarget guard (hp `*((int*)t+8)<=0` → banner dword_8C6F20,
  return -1); target+4, kind=2, attacker+4, tile(v16), a5.
- case 372: guard → banner dword_8C6F24; target+4, **field4=0**, tile, kind=2, a5.
- case 374: guard → banner dword_8C6F28; target+4, kind=2, tile, **field4=0**, a5.
- secondary gate: `*(a2+452) ∈ {1,2,3}` → LOBYTE(v15[7])=1.
The reconstruction reproduces every arm incl. the exact 372/374 field-write order
and all three banner globals. Matches.

### 5 — RequestBuildOp80 (0x495874)  VERIFIED-1:1
`v3[0]=80` opcode; a1 @+0x10; staging `qmemcpy(v5,a2,44)` @+0x14; cut-target @+0x40
= `dword_6315C0 ? *dword_6315C0 : -1`; then EnqueuePacket. CONFIRMED EnqueuePacket
(0x49388c) `qmemcpy(v3,a1,0x99)` (153-byte record stride), then
`v5 = ComputePacketSize(...)` stamped at `*(rec+1)`, cmdId `*(rec+4)`, count
`*(rec+8)` — the source's descriptive note is accurate. (ComputePacketSize itself
is the command-apply cluster, not combat — out of ownership.)

### 6 — SetUnitFormationMode (0x48980c)  VERIFIED-1:1
Free-slot scan over the +128 16-slot array (`if base!=-1 walk ++ to first -1 else
-1`); per-mode opcodes CONFIRMED:
- mode 1: opcode **342** for BOTH resolved slots; delta-packet field byte -88 (0xA8).
- mode 2|3: opcode **344** (first slot) then **352** (second slot); field byte -46
  (0xD2).
`FormationModeOpcode`/`FindFirstFreeFormationSlot` in combat_drivers match (the
caller emits both opcodes of the pair). kFormOpLine=342, kFormOpA=344, kFormOpB=352.

### 7 — FindAvailableSquadSlot (0x57e76c)  VERIFIED-1:1
Byte-lane juggling re-derived field-by-field: HIBYTE(v11)=wantType, v10[4]=wantFlag;
stride 536, 768 slots. wantType!=0 accept: marker(@+0)!=-1, !(flags@+0x1C8 & 0x2000),
busyRank(@+2)<=1, reservedBy(@+0x165 via >>24)∈{wantType,0}, lock(@+0x16C)==0,
(double)fillCount>=requiredCap(@+0x20), ownerClassByte(@+0x164 via >>24)==wantFlag,
group gate IsTypeInGroup(wantType) (==2 || groupClass@+9), relation `*(a1+39)==0xFFFF
|| LookupMatrixEntry(rel,i) > -30`. wantType==0 branch: same minus the reservedBy /
fillCount gates, group via ClassifyTypeFlag(wantFlag). Claim sets flags|=0x2000,
returns &slot[v5]. **-30 relation threshold CONFIRMED.** All match the header offsets
and the reconstruction.

### 8 — ResolveTargetObjekt / PickActiveTargetEntry (0x57e92c / 0x57e9a4)  VERIFIED-1:1
- ResolveTargetObjekt: `typeCode = *(589*(signed char)entry[0] + typeTable)`;
  ==2 → QueryFind(handle,kind **19**) else kind 18; both fall back to kind **288**;
  handle @entry+93. Signed (char) multiply CONFIRMED.
- PickActiveTargetEntry: ring base 13CE298 stride 169, type table stride 589,
  cursor 642004; 256 tries (`v6=255; if(!--v6) return 0`), accept type ∈ {3,22,15},
  advance cursor v7+1, `*outEntityRef=-1`, `*outTypeId = *(entry+1)`. The
  objective path (a1+368→ResolveTargetObjekt) is correctly DEFERRED (scene-coupled).
  Ring path matches exactly.

## combat_packets builders — all VERIFIED-1:1
BuildMoveToPacket (kind 3), BuildTilePacket (kind 7), BuildSimplePacket (kind 8),
Plus4 two's-complement wrap, OrderStage offsets — all confirmed against the
respective decompiles (0x488c8c / 0x488edc / 0x488fa4) during the BuildAttackPacket
diff. No drift.

## Changes this wave
- `src/sim/combat_strength.cpp`: FIXED `ComputeDefenderStrength` to emit the
  no-commander bulk-cash command and the family credit **per production slot**
  (inside the loop), matching the binary's 0x48d318 loop structure. (Attacker
  family credit stays once-at-end, matching 0x48d160.)
- `tests/unit/sim_combat_strength_test.cpp`: ADDED `DefenderMultiSlotPerSlotEmission`
  golden pinning the per-slot behavior; existing 6 tests unchanged (single-slot,
  unaffected by the fix).
- `src/sim/combat_recon_balance.{cpp,h}` + `tests/unit/combat_recon_balance_test.cpp`:
  corrected the misleading "→720" comments to the verified return value **840**
  (the test already asserted 840 correctly — no behavior change, comment-only).

## Build status
Combat cluster sources + tests compile clean (verified by direct `g++ -fsyntax-only`
of every edited TU). The full `guild` lib link is blocked by a concurrent
**world-cluster** ODR clash (`MissionCompletionOutcome` defined in both
`src/world/mission_rules.h:74` and `src/world/history_mission.h:184`) — NOT this
cluster, NOT editable under ownership rules. A background watcher rebuilds and runs
the combat test binaries the moment that clash clears.
