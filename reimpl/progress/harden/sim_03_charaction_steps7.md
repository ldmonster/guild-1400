# Harden sweep — sim_03 charaction_steps7 (he_Transport cluster)

File: `src/sim/charaction_steps7.cpp` (+ `.h`), tests
`tests/{unit,integration,e2e}/charaction_steps7_*`.
MCP: gilde.exe, imagebase 0x400000. All functions decompiled AND disassembled where
the divergence class demanded it.

## Per-function status

| addr | function | status |
|------|----------|--------|
| 0x4de3b0 | CollectTransporterCb | VERIFIED-1:1 |
| 0x4de3fc | PickRandomTransporter | VERIFIED-1:1 (buffer-zero memset modelled as count=0; bounds guards are extra-safe but unreachable) |
| 0x4ddfcc | AllocTransport | FIXED |
| 0x4de534 | RunTransportLocal | FIXED |
| 0x4de884 | RunTransportDoor | FIXED |
| 0x4df258 | RunTransportEntry | FIXED |
| 0x4deca4 | RunTransportStadtStadt | FIXED |
| 0x4df7d8 | RunTransport | FIXED |

Counts: **VERIFIED 2 · FIXED 6 · BOUNDARY items documented below.**

## Constants (`.h`) — all ten re-confirmed via get_bytes

(These had already been corrected in the header by a concurrent pass; values
re-verified against .rdata bytes here. The original literals were all wrong.)

| sym | bytes | value |
|-----|-------|-------|
| flt_61F244 | 0x3C23D70A | 0.01f |
| dbl_61F248 | 0x3FE0000000000000 | 0.5 |
| dbl_61F250 | 0x3FECCCCCCCCCCCCD | 0.9 |
| dbl_61F258 | 0x3FD3333333333333 | 0.3 |
| dbl_61F260 | 0xBFD999999999999A | -0.4 |
| dbl_61F268 | 0x3F847AE147AE147B | 0.01 |
| flt_61F3A8 | 0x42C80000 | 100.0f |
| flt_61F3AC | 0xC1200000 | -10.0f |
| flt_61F3B0 | 0xC1700000 | -15.0f |
| dbl_61F3B8 | 0x3F847AE147AE147B | 0.01 |

## Fixes (addr + evidence)

1. **Person/object id offset +1 vs +4 (systematic, every function).** Records from
   VIBE_Person_QueryBegin / Object_FindObjectById / ResolveEntityById carry their id
   at byte **+1** (misaligned dword), NOT the He_* `+4`. Disasm: `mov eax,[edi+1]`
   @0x4de0ae (AllocTransport OriginId); `cmp ...,[Begin+1]`/`[v9+1]`/`[v38+1]`
   @0x4df8a1/0x4df8aa/0x4df8ba (RunTransport state -2); `*(v35+1)` @0x4dfc93 (BuildOp74);
   `*(v5+1)`/`*(Begin+1)` cmd19 args across Local/Door/Entry/StadtStadt. Recon used
   `He_Id()`(+4) everywhere. Added a `PersonId(rec)` helper (`*(rec+1)`) and replaced
   all such reads — load-bearing (OriginId comparisons drive states -2 and variant 3).

2. **RunTransport null-check dropped the query-69 result (0x4df84e/0x4df8e7).** Binary
   forces state=-1 if Begin(68) OR startp OR goalp OR the query-69 result is null
   (4 tests). Recon checked only 3 and discarded the 69 query. Captured `begin69`,
   added it to the gate.

3. **RunTransportLocal arrived else-branch fabricated arrival (0x4de677).** Binary:
   `arrived = veh && veh[+296]==0 && ((veh[+136]==&byte_13ECEC8 && tol) || lap>6)`. When
   veh==null or veh[+296]!=0 there is no arrival path; recon's `else arrived=lap>6`
   invented one. Removed it.

4. **StadtStadt arm-leg waypoint source startp→goalp (0x4ded41).** `PickRandomTransporter
   (*(v5+97))`, v5 = GoalId query (goalp). Recon read startp+97. Fixed.

5. **StadtStadt running-leg storage key & cmd19 arg1 (0x4df01b/0x4df046).** IsStorageType
   keys on goalp (recon used startp); cmd19 arg1 = `*(v5+1)` = goalp id (recon passed
   raw +176 StartId). Both fixed.

6. **StadtStadt anim target-coord copy was a no-op (0x4defa2).** Binary copies
   anim[+308/+312/+316] -> h[+216/+220/+224] when `anim && *(anim+9)==45`. Implemented.

7. **cmdRequestNamedObject53 literal "trans"→"Trans"** (aTrans @0x61F298 = "Trans"),
   all 4 sites.

8. **Float→int / mixed-precision sites verified** (already corrected by a concurrent
   pass; re-verified): AllocTransport cart-speed `(double)(__int16)cls*flt*dbl+dbl`
   @0x4de12d; case-1 ambush threshold `(float)((double)(__int16)cls*v40)` @0x4dfaa9
   and `(double)roll <= (float)thr` @0x4dfad2 compute in double then store/compare as
   the binary does. No bare-fistp / ConvertX sites in this cluster.

   Also re-verified by a concurrent pass: case -2 now models VIBE_NpcAction_
   SetTargetCityRef @0x4c9484 exactly — `*(h+8)=city`, `*(h+12)= city==0xFFFF ? -1 :
   dword_12CE914[134*city]` (cityRecipientId). Confirmed against decompile.

## BOUNDARY (rule 8 — cross-cluster opaque data / tech, left as documented hooks)

- `byte_13ECEC8` sentinel vs veh[+136] in Local arrived-gate (render pointer vs a
  global). Modelled as matching so the tolerance gate is preserved. (0x4de677)
- Narrative render/quickjump blocks (Door/Entry/StadtStadt `*v==71`/30/31/32 +
  6216/6218/6220/6224/6225 templates; panel-dispatcher gating dword_11BBC30/631610/
  631744/631748/63174C) — opaque text/UI emits via sendQuickjump/sendEntityMessage;
  the control effect (always `+132=QueueRequestEntity29(-1)` at LABEL_13/62) is kept.
- AllocTransport start==goal: binary logs via ErrorLog_ReportMessage before free
  (0x4de350); recon frees without the log (opaque logging).
- AllocTransport escort cost (ComputeCartCost @0x592220 + cmd16) — opaque; the escort
  byte stamp (cart+40) is reproduced. (0x4de29b)
- StadtStadt heightmap world-to-tile snap (Heightmap_WorldToTileWithHeight,
  Transform_PointThroughBoneChain) — render-domain; arm-leg target coords set to 0 as
  a documented simplification (feed only the tolerance gate).
- personQueryBegin self arg: binary passes a1/a2 register; recon passes 0. The
  load-bearing query key (4th arg) is exact; inert/mock hooks ignore self.

## Tests
Goldens were keying person ids on He_Id(+4); fixed BOTH source and goldens to the
binary's +1 layout (added Pid/SetPid; mocks match on +1). State-2 city assertions
updated to SetTargetCityRef semantics (He_CityIndex=city, He_CityId=cityRecipientId(city)).

- unit  `charaction_steps7_test`:      53 checks, 0 failures
- itest `charaction_steps7_itest`:     12 checks, 0 failures
- e2e   `charaction_steps7_e2e_test`:  28 checks, 0 failures
