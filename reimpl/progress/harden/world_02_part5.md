# Full-tree 1:1 hardening sweep — world chunk 02 part 5

Scope (owned files): `src/world/location4.cpp/.h`, `src/world/location.cpp/.h`,
`src/world/location_church.cpp/.h`, `src/world/location_recon3_dialogs.cpp/.h`,
`src/world/location_residence.cpp/.h` + their tests.

MCP: gilde.exe @ imagebase 0x400000. Every provenance-tagged function decompiled and
disasm-diffed; every FP/table constant re-read with get_bytes.

## Constants re-verified (get_bytes) — all MATCH
| sym | addr | bytes | value |
|---|---|---|---|
| flt_62240C | 0x62240c | 0a d7 23 3c | 0.01f |
| flt_622410 | 0x622410 | cd cc 4c 3e | 0.2f |
| flt_622430 | 0x622430 | 0a d7 a3 3b | 0.005f |
| flt_622434 | 0x622434 | 00 00 48 43 | 200.0f |
| flt_622438 | 0x622438 | 0a d7 23 3c | 0.01f |
| dbl_622440 | 0x622440 | ...b9 3f | 0.1 |
| dbl_622448 | 0x622448 | ...a9 40 | 3200.0 |
| dbl_622450 | 0x622450 | ...13 41 | 320000.0 |
| flt_621B90 | 0x621b90 | 00 00 c8 42 | 100.0f |
| dbl_621918 | 0x621918 | a6 e2 ec c3 67 d8 55 3f | 0.0013333… (1/750) |
| dbl_621920 | 0x621920 | …d0 3f | 0.25 |
| dword_507F90 | export goods | c4/c5/c6 01 | {452,453,454} |
| dword_507F9C | import goods | c1/c2/c3 01 | {449,450,451} |

## Per-function status

### location_church.cpp
- **ChurchComputeDonation 0x521674 — FIXED (reputSpread float-source bug).**
  Disasm 0x521939 `fst [var_18]` (FLOAT slot) stores the 80-bit quotient BEFORE the
  truncating `fistp [var_14]` at 0x521945; 0x521958/0x52195f `fld var_18; fmul flt_622410`
  builds the per-peer spread (v45) from the **un-truncated float quotient**, not the (int)
  reputDelta. Before: `reputSpread = (float)reputDelta * 0.2` (used the truncated int).
  After: `reputSpread = (float)quotient * 0.2`. Golden (500000,300000,4000): spread was
  asserted ~0.2 (from int 1), now 0.255 (= 1.275*0.2). suggestedCost path verified: the
  `fild;fmul;ConvertX;fistp` at 0x521737-0x521756 keeps the product in 80-bit (NO float
  narrow) → `(int)(combined*0.01)` unchanged (7999). reputDelta `(int)q` truncation OK.
- **ChurchComputeIndulgence 0x521bac — FIXED (missing handlerSum multiply + base float-narrow).**
  Two divergences:
  1. ecx leftover multiply. At 0x521c16 `mov ecx, eax` (ecx = He_SumPlayerHandlerValues).
     ComputeTotalWealth (0x591f7c push/pop ecx) and Favorability (0x594330 push/pop ecx)
     both PRESERVE ecx, so at 0x521d84 `mov var_18, ecx; fild; fmul var_2C` multiplies the
     favorability factor by `(double)handlerSum`. The recon omitted this entirely.
     Signature changed `bool hasCrime` → `int crimeHandlerSum` (0 = not offered, nonzero =
     multiplier). Header doc + struct comment updated.
  2. base float narrow. 0x521d35 `fild;fmul flt_622430;fstp [var_20]` stores base to a
     **32-bit float** (stack frame: var_20 size 0x4 type float); 0x521da0 `fld var_20`
     reloads the narrowed float. float(1e6*0.005)=5000.0f, so raw=(int)(5000*factor).
     The recon kept base in double (4999.9998…). Goldens corrected to binary-true:
     raw 9999→**10000**, 7499→**7500**, 9999999→**10000000** (the old goldens encoded the
     un-narrowed value = a divergence; both source and golden fixed, addr cited). Clamp band
     (>3200 && >=320000 → 320000 else floor 3200) verified vs 0x521ddc-0x521e6e; costs
     unchanged (3200/3200/320000). Added IndulgenceHandlerSumScalesCost (sum=3 → raw 30000).
- **ChurchComputeConfession 0x522ae8 — VERIFIED-1:1.**
  eligible=(person+39!=0xFFFF); confess offered only when CountActiveByTarget!=0; reputGain
  = `DataPtr/2` where DataPtr is reloaded from GetDataPtr (the paid slider) at 0x522c70,
  not activeCrimes — recon already passes `paid/2`. Integer division truncation matches.

### location_residence.cpp
- **ResidenceComputeMistress 0x5150f4 — FIXED (cooldown 64→32-bit compare).**
  0x5152df `mov ebx, dword ptr qword_13CE852` (LOW 32 bits), 0x5152e5 `mov eax,[a3+188]`
  (32-bit), `cmp eax,ebx; jge`. The compare is **32-bit signed** of the low dwords.
  Before: full i64 `lastAffairTime < nowTime`. After: `(i32)lastAffairTime < (i32)nowTime`.
  Added MistressCooldownIsLow32SignedCompare (high-dword-set & sign-bit cases). Existing
  small-positive goldens unaffected. affairEnabled / StartAffair emit verified.
- **ResidenceComputeMasterExam 0x5159fc — VERIFIED-1:1.**
  eligible=!pending && gauge>=1.0 && rank<=6 (0x515ac1 `||` chain). gaugeMax: 0x515d34
  `fmul flt_621B90(100.0); ConvertX; fistp` → `(int)(gauge*100)` truncation. staffOk=
  journeymen>=4 (0x515bb8 `>= 4`). Emit PromoteMaster + GameTime_Advance(+2) gated by
  takeExam(1210) && gauge>=1.0 (re-checked 0x515ba3) && staffOk.

### location4.cpp (hook-routed dialog cores)
All five reconstructions are the recovered control flow with GUI/command/law callees behind
hooks; no float→int sites. Branch chains diffed against the decompile:
- **ThiefKidnapDialog 0x52554c — VERIFIED-1:1** (gate chain !target / !skill(3) / captive
  (v2+91 && +39==*v2) / blocked(+433→5577); confirm: collect ALL occupied (j!=102912 step
  134, no cap) → SlotReset28 + BuildOp90(-3) + Gesetz(25,1,perp,firstId,-1) + favor voice).
- **GuardCustomsDialog 0x5268a8 — VERIFIED-1:1** (!a1; full→8C905C; confirm: selection cap 6;
  no commit flag in orig — committed flag is test observability, documented).
- **GuardDetainDialog 0x526b34 — VERIFIED-1:1** (!a1; full; already-detained(+91&8); confirm:
  FIRST active selection batch, then recount-all and warn when >1).
- **ThiefSpyBuildingDialog 0x524380 — VERIFIED-1:1** (!target||+39==0xFFFF; !busy→5791;
  v13=handlers+occupied < 2*cap else 5641; confirm: occupied cap 8 (v28<8); v39=count).
- **DungeonBribeDialog 0x523d1c — VERIFIED-1:1**, **ThiefInformationDialog 0x52481c —
  VERIFIED-1:1**, **ThiefTrainingDialog 0x5253c0 — VERIFIED-1:1** (gate + radio/drag-grid
  commit models match the recovered flow).

### location.cpp
- **ClassifyLocationKind / ContactRegisterMenu / ContactDispatch — VERIFIED-1:1**
  (slot-id-in-registration-order dispatch; type codes 84/229,230/247/288 per dispatcher).
- **ProductionContactMenu 0x52388c — VERIFIED-1:1** (registration order PRODUKTION_SCHREIBEN,
  LAGER, TRANSPORT, PERSONALBUCH, MEISTERBRIEF, all gated by word_631758&0x200; dispatch by
  clicked slot id lines up).
- **ThiefGuildContactMenu 0x525070 — VERIFIED-1:1** (shop 0x200 / back-room 0x400 gates,
  attack gated rank>=2; registration order preserved).

### location_recon3_dialogs.cpp (loc3)
- **Bribery_Gate 0x512a6c — VERIFIED-1:1** (TargetValid / AnimalTargetBusy / total>=2*cap).
- **Transport_BarFill 0x513568 — VERIFIED-1:1** (`(double)(unsigned)delta * dbl_621918`,
  clamp at 0.25, stored float). Transport_RebuildFor/RowEnable verified (sub 475/476, text
  0x1854/0x1855, +56/+76 = dir / dir==0).
- **Search_ExportRowCount / Search_Aggregate / Search_RowMessageId 0x513c60/0x5142ec —
  VERIFIED-1:1** (typeByte==8 || (unsigned)cap<2 → 2 rows; LOBYTE-XOR accumulate reduces to
  `out[i]+=weightByte`; msg 1/“%s”/many; good-type triples byte-verified).
- **Stammtisch_CardGeometry / Stammtisch_JoinState 0x517a58 — VERIFIED-1:1** (w=win-2*card,
  cw=w/3 sar>>16, c=(w%3)/2, x/y terms; v38 state machine 1/2/3/4).
- **DarkCorner_HireOffer / DarkCorner_RowTextArg 0x51816c — VERIFIED-1:1**
  (`blocked → 0x14B7; else (int)wealth>price → 0x14B6`; 5*(byte>>24)+4145).

## Counts
- Functions audited: 22 (church 3, residence 2, location4 7, location 5, loc3 ~10 cores).
- FIXED: 3  (ChurchComputeDonation reputSpread; ChurchComputeIndulgence handlerSum +
  base-float-narrow + 3 corrected goldens; ResidenceComputeMistress 32-bit cooldown).
- VERIFIED-1:1: rest.
- BOUNDARY: 0 (all owned cores are pure logic; GUI/command/law callees route through the
  documented LocationDialog(4)Hooks / loc3 deps — those are the pre-existing modeled seams,
  not new tech swaps).

## Tests
- tests/unit/world_location_test.cpp: indulgence goldens migrated to int handlerSum +
  binary-true raw (10000/7500/10000000/30000); donation reputSpread 0.255; added
  IndulgenceHandlerSumScalesCost + MistressCooldownIsLow32SignedCompare.
- All owned source files + every location/recon3/location4 test file `-fsyntax-only` clean.
- Standalone golden harness (church + residence) run: ALL PASS.

## Handoffs / notes
- No files outside the owned chunk were edited.
- Pre-existing UNRELATED build break: `src/gui/widget_layout.cpp` (Widget has no member
  `ld`) prevents linking the monolithic `libguild.a`; not in this chunk. Verification was
  done via `-fsyntax-only` on owned TUs/tests + a standalone golden harness.
- The `committed` flag in several location4 dialog outcomes is test-observability (the
  original GuardCustoms/Detain leave v35/v30 == 0); already documented in source comments.
