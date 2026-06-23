# Harden: gui/buy_dialog.cpp + gui/charcreate.cpp (1:1 line-for-line diff)

Scope: every function carrying the assigned `gilde.exe 0xADDR` provenance.
Reference of record: IDA decompile + disasm (disasm wins where Hex-Rays is loose).
Editable files: src/gui/buy_dialog.{cpp,h}, src/gui/charcreate.{cpp,h} + their tests.

## buy_dialog.cpp

### 0x54d8fc — VIBE_Form_PopulateObjectList -> BuyDialog_PopulateObjectList
VERIFIED-1:1.
- Loop bound `i < 2048` (kRowTableScanBound), row==0 terminates (`while(result)` on
  v15[]), parallel id/record binding (dword_1232080[i]=GetChildObjectId,
  dword_1232084[i]=row).  Side-effect order and termination match.
- The two parallel tables + null-terminated record list are modeled as ListRow rows.
  No constants disagree.

### 0x54d9d8 — VIBE_Panel_RunApBuy -> BuyDialog_BuildApBuy / BuyDialog_DispatchApBuy
VERIFIED-1:1.
- Form "Panel\Panel_AP", header text id 0x1AD4 (6868), `dword_75BF38 = -1` latch.
- Cancel: `dword_672230 -> dword_631614 = 1`.
- Select arm: `dword_75BF38 == 1210` (0x4BA = kBuyClickSelect).
- Stride-2 v4 scan, bound 512 (kRowMatchScanBound), null record terminates,
  `dword_62D22C == dword_1232080[v4]` match.  All branch conditions match disasm.

### 0x519b14 — VIBE_WineCellar_ShowBuyDialog -> BuyDialog_BuildWineCellar / DispatchWineCellar
VERIFIED-1:1 (disasm-confirmed display math + click ids).
- Display math (disasm 0x519bd7..0x519c2b, 0x519ccc):
  `ecx = ConvertToDisplayCoord(playerHeld) + ConvertToDisplayCoord(cellarMoney)`,
  `eax = ConvertToDisplayCoord(cellarMoney)`, `cmp ecx,eax; jg`.
  When sum > cellarDisp the label shows the sum (loc_519CCC recomputes
  playerDisp+cellarDisp), else just cellarDisp.  Source:
  `shownValue = (sum > cellarCashDisp) ? sum : cellarCashDisp`.  Exact.
- Cancel id `dword_75BF38 == 0x483` (1155 = kBuyClickCancel) confirmed at loc_519CF5.
- Buy id `0x4BA` (1210 = kBuyClickSelect); afford/leg reuse world::WineCellarBuy.
- Float->int: ConvertToDisplayCoord is the recovered world model (truncating ConvertX
  lives inside it); the GUI layer adds in int space — no extra float->int site.

Counts: buy_dialog_test PASS (no source change required).

## charcreate.cpp

### 0x527258 — kPreviewMaleNames  VERIFIED-1:1
get_bytes(0x527258, 9x32): patrizier_MANN2, wirt2_DICKER, ratsherr2_KUTTE,
offizier_SOLDAT, abt_KUTTE, buerger_MANN, dieb3_MANN2, handwerker2_MANN; 9th slot all-0
(terminator).  Byte-for-byte match (8 valid entries).

### 0x52739c — kPreviewFemaleNames  VERIFIED-1:1
get_bytes(0x52739c, 8x32): bauerin_FRAU, buergerin_FRAU, buergerin3_FRAU,
handwerkerin3_FRAU, magd_FRAU, patrizierin_FRAU, zigeunerin_FRAU; 8th slot all-0.
Byte-for-byte match (7 valid entries).

### 0x527378 — kPreviewMaleCommands  VERIFIED-1:1
get_bytes: 0x57,0x6b,0x63,0x55,0x00,0x17,0x29,0x3b, then 0xFFFFFFFF sentinel (dropped).
Match.

### 0x52749c — kPreviewFemaleCommands  VERIFIED-1:1
get_bytes: 0x10,0x1f,0x25,0x43,0x4d,0x5b,0x74, then 0xFFFFFFFF sentinel (dropped). Match.

### 0x52b088 — VIBE_Menu_ChooseCharacterTalent
- Talent_PaternalProfessionByte (v46[4] switch): 0->28,1->49,2->4,3->55,4->16, else prev.
  VERIFIED-1:1 (switch arms 0x52b485/0x52b5e4/0x52b5f0/0x52b5fc/0x52b608).
- Talent_MaternalProfessionByte (v46[5] switch): 0->28,1->37,2->4,3->0,4->73, else prev.
  VERIFIED-1:1 (0x52b49e/0x52b614/0x52b620/0x52b62e/0x52b638).
- Talent_CanDecrease (enable @0x52b386): enabled when value <= cap (jle loc_52B576, ebp=1).
  VERIFIED-1:1.
- Talent_CanIncrease (enable @0x52b3b0): **FIXED**.
  DISASM: `cmp edi(0),budget; jge loc_52B596` (enable if budget<=0) ; `fild value;
  fcomp dbl_622E18; jnb loc_52B596` (enable if value>=126.0); else +0x38/+0x4c=0 (disabled).
  => ENABLED when `budget <= 0 || value >= 126.0`; DISABLED only when budget>0 && value<126.
  Old source had the INVERSE `budget > 0 && value >= floor` with default floor 0.0 — wrong
  polarity AND wrong threshold.
  Evidence: dbl_622E18 = 0x405F800000000000 = 126.0 (also dbl_622E08=63.0,
  dbl_622E10=21.0 step, dbl_622E20=-21.0 step).
  Fix: source predicate -> `budget <= 0 || value >= floorThreshold`; header default
  floorThreshold 0.0 -> 126.0; golden test rewritten (both source AND golden corrected).
- Float->int / x87 accumulation inside the live frame-loop (ConvertX truncation,
  fild/fcomp) is in the heavy run path, not in the pure helpers; no helper does float->int.

### 0x52bcd4 — VIBE_Menu_RunChooseCharacter
- ChooseCharacter_ActorProfessionCode (v5 switch, disasm 0x52c061..0x52c265):
  dieb/zigeunerin->2, buerger3/buergerin2->0, offizier->3, priester/buergerin->4,
  handwerker/handwerkerin2->1.  VERIFIED-1:1.
- ChooseCharacter_ActorIsMale (parity branch 0x52c084..): even slot accepts the five male
  actors {dieb,buerger3,priester,handwerker,offizier}; odd slot accepts the four "Frau"
  actors.  VERIFIED-1:1.
- ChooseCharacter_ApplyActorClick: **1:1 DISCREPANCY FOUND, kept as documented BOUNDARY**
  (cross-module).  DISASM truth: the click writes ONLY the two PARENT slots —
  `dword_122F258[esi*4]` with esi forced 4 (male) / 5 (female); the free-slot walk
  @0x52bf14 starts at index 4; grandparent slots 0..3 are NEVER click-filled.
  xrefs_to(0x122F258): the sole click writer is 0x52c0b4 (+ the Talent qmemcpy).
  The current shared helper still fills 0..5 (parity-gated next-free) because
  gui/choosecharacter_run.* (the 0x52bcd4 RUN LOOP, OUT OF SCOPE) consumes the same helper
  and its tests (tests/unit/choosecharacter_run_test.cpp, tests/integration/
  gui_charcreate_itest.cpp — both out of my editable set) encode the 6-slot model.
  Changing the helper to the faithful 2-parent-slot behavior reds those out-of-scope tests
  which I may not edit.  ACTION: kept the existing model + added explicit 1:1 NOTE in
  charcreate.h + charcreate.cpp; needs a COORDINATED cross-module fix (charcreate +
  choosecharacter_run together).  FLAGGED FOR ORCHESTRATOR.

## Build / tests (all green)
buy_dialog_test, gui_charcreate_test, gui_charcreate_e2e_test, gui_charcreate_itest,
choosecharacter_run_test — 5/5 PASS.

## Summary
- VERIFIED-1:1: 0x54d8fc, 0x54d9d8, 0x519b14, 0x527258, 0x52739c, 0x527378, 0x52749c,
  0x52bcd4 (classification halves).
- FIXED: 0x52b088 Talent_CanIncrease (polarity + threshold; source + header default + golden).
- BOUNDARY/FLAGGED: 0x52bcd4 ApplyActorClick slot-range (2 parent slots vs current 6-slot
  model) — cross-module with gui/choosecharacter_run.*; documented, not silently changed.
