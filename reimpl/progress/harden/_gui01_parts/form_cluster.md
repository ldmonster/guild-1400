# Form subsystem — 1:1 hardening sweep (MCP live)

Scope: `src/gui/form.cpp`, `form_asset.cpp`, `form_lifecycle.cpp`, `form_loader.cpp`,
`form_parse.cpp`. Every function carrying a `gilde.exe 0xADDR` provenance was
decompiled + disassembled and diffed line-for-line against the binary.

Totals: **20 provenance functions** — 15 VERIFIED-1:1, 5 FIXED (in 1 function:
`Form_ParseResourceFile`, multiple distinct divergences), several documented BOUNDARYs.

Test status: all suites green.
- unit: `gui_form_parse_test`, `gui_form_loader_test`, `gui_form_asset_test`,
  `real_forms_driver_test` (PASS)
- integration: `real_forms_driver_itest` (PASS, real assets)
- e2e: `real_forms_driver_e2e_test`, `gui_form_real_e2e_test`,
  `real_assets_forms_e2e_test`, `gui_form_loader_e2e_test`, `gui_form_asset_e2e_test`,
  `gui_core_e2e_test` (PASS); dependent screens
  (`gui_options_screens_e2e_test`, `gui_charcreate_e2e_test`,
  `gui_window_mgmt_e2e_test`, `gui_message_box_e2e_test`, `session_hud_e2e_test`) PASS.

---

## form.cpp

| addr | name | status |
|------|------|--------|
| 0x41e4cc | Form_SelectWindow | VERIFIED-1:1 — bound `winSlot > windowCount() \|\| <0` (jg/jl signed), 676BE4[171*id] count, 676A64 windowId, caches dword_62D230/62D298. |
| 0x41db20 | Form_GetObjectPtr | VERIFIED-1:1 — bound `localId <= sar(dword@+26,16)` = word@+28 = objCount(); returns child INDEX (documented faithful deviation; original returns `dword_69FFB4 + 740*idx`). |
| 0x41dc10 | Form_GetObjectDataPtr | VERIFIED-1:1 — same bound, calls Object_GetDataPtr on resolved child. |
| 0x41dd14 | Form_GetObjectAnimPtr | VERIFIED-1:1 — type==65 ('A') → returns +12, else 0. |
| 0x41dea8 | Form_GetChildObjectId | VERIFIED-1:1 — form==-1 → slot=group (`v3=8*group; 119*v3/4=238*group`), else windowId(group); bound, returns child idx or -1. |

## form_asset.cpp

| addr | name | status |
|------|------|--------|
| 0x41b888 | Gui_LoadGfxFile (open-by-name half) | VERIFIED-1:1 — path `"%sgfx\\%s"` (Gfx_BuildPath), VFS open + slurp + Form_LoadFromBuffer(initTables=true). Surface/palette/property setup = BOUNDARY (renderer cluster). |
| — | Form_LoadByName | VERIFIED-1:1 — alias of Gui_LoadGfxFile. |

## form_lifecycle.cpp

| addr | name | status |
|------|------|--------|
| 0x41e544 | Form_GetWindowId | VERIFIED-1:1 — bound `slot > windowCount() \|\| <0` → -1, else windowId(slot). |
| 0x41d634 | Form_SetObjectsVisible | VERIFIED-1:1* — sweeps widgets; type@+24 set, parentClip@+60 == form-record key → renderPtr@+52 = (hide==0); caches dw[104]. *Original sweeps 512 slots (`+=0x2E4` to `0x5C800`); reimpl sweeps kMaxWidgets=511 — BOUNDARY (g_widgets sized 511; slot 511 cannot exist). |
| 0x41d568 | Form_SetChildrenVisible | VERIFIED-1:1 — `if windowCount()>0`, per window walk objCount() children, disabledA@+56 = (hide==0); caches dw[103]. |
| 0x41be6c | Form_RaiseWindows | VERIFIED-1:1 — `xor ecx,ecx; inc; cmp count; jl` → visits 0..count-1 windows; ZOrder_RaiseWindow = BOUNDARY (host-pointer identity, deferred). |
| 0x41da04 | Form_Destroy | VERIFIED-1:1 — back-to-front window loop, enabled@67EE00, backWidget@67EDEC, surfaces dw[105/106]; Widget_DestroyByType/Surface_Destroy/Light_SetGrayColorThunk = BOUNDARY; faithful effect = valid()=0. |
| 0x41d6ac | Form_CenterChildWindows | VERIFIED-1:1 — top-level (groupLink==0) windows centered: `screenCenterX - w/2`, `screenCenterY - h/2`; Widget_LayoutBounds = BOUNDARY. |
| 0x41d990 | Form_PositionChildWindows | VERIFIED-1:1 — top-level windows → Window_PositionCentered(slot, mode). |

## form_loader.cpp

| addr | name | status |
|------|------|--------|
| 0x41b888 | Form_InitTables (init half) | VERIFIED-1:1* — 48 forms (dw[0]=i, 684 stride), 96 windows (dword[0]=i, 238 stride), 512 widgets (marker=i, 740 stride). *Widget loop clamped to kMaxWidgets=511 (BOUNDARY, same array-size constraint). |
| 0x41b888 | Form_LoadFromBuffer (parse half) | VERIFIED-1:1 — u32 count@+0; `count > 2048` → reject; 84-byte (0x54) records into g_gfxObjects; state walk `(+68 & 1) && +56 != 0` → RegisterGfxState (BOUNDARY: VIBE_State_Helper). |
| 0x41b888 | Form_LoadFromFile | VERIFIED-1:1 — VFS open + chunked slurp + Form_LoadFromBuffer. |

## form_parse.cpp

| addr | name | status |
|------|------|--------|
| 0x41beb8 | Form_ParseResourceFile | **FIXED (5 divergences)** + BuildChildWindow (0x41a598) VERIFIED-1:1. |
| — | Form_SniffFormat | VERIFIED-1:1 — `byte[3]-'0'`. |

### FIXES in Form_ParseResourceFile (gilde.exe 0x41beb8)

All evidence from disasm @0x41c383..0x41c4f9 (object loop) and @0x41c037 (form scan).

1. **Per-object type/aux stride: +8 → +4.** Disasm @0x41c4d6 `add esi, 4`; type read
   `[esi+0D0h]`(+208), aux `[esi+0D90h]`(+3472). Was `208+8*o`/`3472+8*o`; fixed to
   `208+4*o`/`3472+4*o`. (x/y stride +2 and name stride +64 were already correct.)

2. **Slider range byte stride: +8 → +1.** Disasm: `v113` base `inc` per iter (0x41c4eb),
   read `v113[3868]`. Was `rec[3868+8*o]`; fixed to `rec[3868+1*o]`.
   Input-field flag byte: `*(BYTE*)(v40+3472)` = `3472+4*o` (uses corrected aux stride).

3. **Type dispatch restructured.** The binary is NOT a mutually-exclusive type switch.
   Disasm @0x41c3ea/0x41c490/0x41c713:
   - `if (type < 64)` → Object_AddToWindow (covers sprite=5 AND **type 0 and all
     types <64**; the earlier code skipped type 0 and wrongly included type 64).
   - then, independently: `type==67`→label, `else if type==65`→input,
     `else if type==69`→slider. Rewrote to two independent decisions.
   - Button flags: original uses TWO separate `if`s (aux==18; aux==8), not else-if.
   - Label guard adds `StrCmp(&dword_610ECC[=""], name)` ⇒ only non-empty names.

4. **Form-slot scan field: dw[0] → windowCount() (dw[97]).** Disasm @0x41c037:
   `dword_676E90` = `dword_676A60 + 268 dwords` = the windowCount field; the scan
   walks `windowCount()` (stride 171) from form 1, returning the first with
   windowCount()==0. The old code scanned dw[0], which Form_InitTables stamps =index
   (never 0 for forms 1..47) — so it could never find a free slot.

5. **Per-window windowCount increment + trailer flags.** Original increments
   `dword_676BE4[171*formId]` (= windowCount, dw[97]) once per window in the loop
   (0x41c3bd) and at the end sets dw[100]=dw[103]=dw[104]=1 (676BF0/676BFC/676C00).
   Added the per-window `++windowCount()` and the dw[103]/dw[104]=1 stamps (only
   dw[100] was set before). This makes the parsed form actually usable by
   Form_SelectWindow / Destroy / SetChildrenVisible.

### BuildChildWindow (reconstruction of VIBE_Window_AddChildWindow 0x41a598)
VERIFIED-1:1 against 0x41a598: enabled guard, Window_Create at parent-adjusted coords
(margins 0), append backing to parent child list at objCount() before bump,
groupLink/clip fields (+44/+32/+34/+28/+30), inherit parentClip from parent backing,
++objCount, grow contentHeight from `child.h() + dy`. The objCount>=384 guard is added
safety (original trusts the file). Object_AddToWindow / Object_AddTextLabel /
Input_AddFieldToWindow / Widget_AddSliderToWindow are REUSED leaves (verified
signatures: AddToWindow(win,y,x,gfx); AddTextLabel(x,y,win,text); AddSlider(x,y,a3,
range,100,gfx,auxLowByte,win)).

### BOUNDARY notes
- `Form_PropertyValidate` (0x40dfd4) / `Form_FindTextArrayIndex` (0x44e0d8): renderer /
  text-cluster edges (weak, neutral -1 default; strong inert hooks in the driver).
  The original calls FindTextArrayIndex twice per built label (guard + +8 field store);
  the reconstruction calls it once (the +8 text-id store is inside the deferred text
  cluster). The label text passed to AddTextLabel is `dword_8C36B0[idx]` (the text
  array) in the binary; the reconstruction passes the object name — a text-cluster edge.

### Tests fixed to the binary
- `gui_form_parse_test.cpp`: makeRecord type/aux writes +8 → +4; inert (type-0) object
  now expected to build a widget (`widgetIdx >= 0`).
- `real_forms_driver_test.cpp`: WriteWindow type/aux +8 → +4; widgetCount 6 → 7
  (type-0 spacer now builds), propertyValidate 3 → 4.
- `real_forms_driver_e2e_test.cpp` (real assets): `totalWidgets > 500` → `> 450`
  (corrected strides yield 493 widgets [sprite=404 label=13 input=5 slider=45]; the
  old +8 stride mis-read type bytes and inflated to ~609).
