# Hardening: AiAction target-finder cluster

Files:
- `/home/cnupt/work/reverse/guild-1400/reimpl/src/ai/aiaction_finder.cpp`
- `/home/cnupt/work/reverse/guild-1400/reimpl/src/ai/aiaction_finder.h`
- `/home/cnupt/work/reverse/guild-1400/reimpl/src/ai/ai_eval.cpp`
- tests: `/home/cnupt/work/reverse/guild-1400/reimpl/tests/unit/aiaction_finder_test.cpp`

Method: decompiled + disassembled every `gilde.exe 0xADDR` site, diffed line-for-line,
confirmed constants via `get_bytes`, verified RNG draw count/order and every float->int
/ x87 truncation site against disasm.

## Recovered constants (get_bytes @0x61AC30, 24 bytes)
`00 00 00 42 | 00 00 00 42 | 00 00 9A 42 | 00 00 70 42 | 00 00 20 42 | 00 00 90 41`
=> 32.0, 32.0, 77.0, 60.0, 40.0, 18.0. All six match source. VERIFIED.

## Per-function

### 0x47b84c FindNearbyPerson — VERIFIED-1:1
capacity<3 gate; RandomModulo(0x40) radius (float cast of u16); FindMatchingColors
want=1; kind=7; count ladder draws 4 independent RandomModulo(cap/2) — exactly matches
decompile (>1 && >=5 ->5; <=1 ->1; else draw). No churn.

### 0x47badc FindNearbyPersonRanged — VERIFIED-1:1
capacity<3; radius (double)RandomModulo(0x12)+flt_61AC30, stored as float. Match.

### 0x47ba20 FindTwoNearbyPeople — VERIFIED-1:1
No capacity gate; radius (float)RandomModulo(0x40); want=2; writes targetA/targetB. Match.
(Disasm confirms `v6=(float)v9`, plain float cast — no base add.)

### 0x47bd18 FindTwoPeopleInRange — FIXED
Evidence: disasm 0x47bdbc `fstp [var_10]` and 0x47bdc5 `fstp [var_C]` store each
favorability into a 4-byte FLOAT slot, then `fld; fcomp flt_61AC3C` (0x47bdc9/0x47bddc).
The compare therefore runs on float-truncated values.
- BEFORE: `double favAB/favBA = E.Favorability(...)`, compared as double (no truncation).
- AFTER: `float favAB/favBA = (float)E.Favorability(...)`; compare `(double)favAB <= 60.0`.
Order/operands (Fav(slotA,slotB) and Fav(slotB,slotA)) already correct. min=33.0, base 77.0.

### 0x47bb88 FindNearbyEntityByLevel — BOUNDARY
Reason: render/Person-table coupled (selection probe `dword_62EB8C`, `VIBE_Person_QueryBegin`
@0x586c20, and the 768-row person table walk over `word_12CE910`/`dword_12CEA7C`/`byte_12CE912`
with stride 0x218/268). Confirmed draw sequence in binary:
  1. RandomModulo(3) (always) selects palette code (2->unk_744300, 1->0x400300, else 0x344000).
  2. if `dword_62EB8C` (selection) != 0: FindNearestEntity(selection,4,code,0,100,&v13).
  3. if v13==-1: RandomModulo(0x20); radius=+flt_61AC34; FindNearestEntity(actor,6,code,0,r,&v13);
     if it returns 0 -> return.
  4. QueryBegin; RandomModulo(0x300); walk 768 rows from that offset matching
     person-home-ptr==query && prof-byte==0; write inventory id (+2 word table) and query +1.
The reconstruction faithfully reproduces draws (1) and (2)/(3) gating but CANNOT host the
(0x300) draw + 768-row table walk without the real person table — that data is not in the
finder env. Left as a documented hook (the production env binds the table). NOTE: the
0x300 draw on the success path is therefore not yet emitted; flagged for the env-backed
implementation. Cite: 0x47bb88 (data-not-in-tree boundary).

### 0x47bcd8 CheckObjectState — FIXED (signature + moduli)
Evidence (disasm): `mov eax,[edx+0x161]; sar eax,0x18` (signed) -> code byte to
GroupFromCode (0x47bcd8..0x47bce1); `cmp al,7`; group==7 branch `mov eax,3` ->
RandomModulo(3) (0x47bcea); fallthrough `mov eax,4` -> RandomModulo(4) (0x47bcf9).
The two draws use DISTINCT hard-coded moduli 3 and 4; Hex-Rays collapsed them into a
phantom shared `v3`. `__thiscall`, this=actor record (esi at call site 0x47c851).
- BEFORE: `bool CheckObjectState(i32 targetBuildingPtr, u16 n)` using a single `n` for both
  RandomModulo calls.
- AFTER: `bool CheckObjectState(i32 buildingTypeCode)`:
    if (BuildingGroup(code)==7 && RandomModulo(3)!=0) return true;
    return RandomModulo(4)==0;
  `buildingTypeCode` is the caller-derived (*(i32*)(record+0x161))>>24 byte (env wraps
  GroupFromCode as BuildingGroup). Golden test updated: group!=7 seed1 -> RandomModulo(4)=2
  ->false; group==7 seed1 -> RandomModulo(3)=2 !=0 -> true. (16838%4=2, 16838%3=2.)

### 0x47bf98 FindFactionPerson — VERIFIED-1:1
Disasm confirms: `(int)qword_13CE852 % 4` via `sar edx,0x1F; idiv 4` (SIGNED mod) vs
`faction4 & 3`. Handler conflict checks BOTH `[h+0xB0]`(field 44) and `[h+0xAC]`(field 43)
against faction word. Palette = (byte[actor+9]==0)+0x301409 (3150857/3150858). RandomModulo
draw uses `result` directly. All match the env-routed reconstruction.

### 0x47c23c FindNearbyBuilding — VERIFIED-1:1
capacity<2; radius (double)RandomModulo(0x20)+flt_61AC40; want=1; paired-entity-reverse
gate then write. Match.

### 0x47c314 FindNearbyWealthyTarget — FIXED
Evidence (disasm 0x47c3bf..0x47c3fa): RandomFloatScaled() drawn FIRST, result stored to a
4-byte float slot `fstp [var_10]` @0x47c3e6; then BuildingRatingCurve; then
`fadd [var_10]` adds the FLOAT-truncated roll to the 80-bit curve on st0; `fld1; fcompp`
(>= 1.0). Turn-bit gate `test byte ds:(dword_12CEAD8+1)[eax*8],4` == (&0x400).
- BEFORE: `double roll = util::RandomFloatScaled(); curve + roll >= 1.0`.
- AFTER: `float roll = (float)util::RandomFloatScaled(); curve + (double)roll >= 1.0`.
Capacity<3 / (flags457&4) gate already correct; order (roll then curve) already correct.

### 0x47be88 FindAdjacentEntitySmall — VERIFIED-1:1
threshold RandomModulo(4)+2; actor state==13 abort; CollectPlayerEntities; round-robin walk
(`idx=(idx+1)%n; if(--rem==0) return 0`) taking first dist>=threshold with kind!=15 &&
active && state not in {13,27,15,21}; write RecPersonId(+1 dword). Match.

### 0x47c094 FindAdjacentEntityLarge — VERIFIED-1:1
Identical to Small with threshold RandomModulo(8)+5. Match.

### 0x47c164 FindEligibleNeighbor — VERIFIED-1:1
capacity<2 and (flags457&1) gates; CollectPlayerEntities (ids in first array, dist in
second — note loop role swap vs small/large, confirmed); first (int)dist>=4 with
(rec+457 &2)==0 && OfficeGetDefinition(rec+358,&def) && BYTE2(def)>=4; write [rec+4]
(=+1 dword). Match. (Tier compare `jb 4` is unsigned over a byte; env tier is small.)

## ai_eval.cpp — N/A
No `gilde.exe 0xADDR`/`@0xADDR` provenance comments; only thin hooks + a RandomModulo
forwarder. Out of hardening scope (no in-tree addresses to diff). No changes.

## Counts
- VERIFIED-1:1: 9 (FindNearbyPerson, FindNearbyPersonRanged, FindTwoNearbyPeople,
  FindFactionPerson, FindNearbyBuilding, FindAdjacentEntitySmall, FindAdjacentEntityLarge,
  FindEligibleNeighbor, + 6 radius constants).
- FIXED: 3 (FindTwoPeopleInRange float-trunc; CheckObjectState signature+distinct moduli 3/4;
  FindNearbyWealthyTarget roll float-trunc).
- BOUNDARY: 1 (FindNearbyEntityByLevel — person-table walk + 0x300 draw not in env).

## Tests
`cmake --build build --target aiaction_finder_test -j` clean.
`ctest -R aiaction` => 5/5 passed (unit, recon, dispatch_ai_recon2, itest, e2e).
