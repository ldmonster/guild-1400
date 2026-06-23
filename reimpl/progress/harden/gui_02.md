# Hardening sweep — chunk gui_02 (GUI: HUD / menus / input / panels)

MCP-live 1:1 verification of every provenance'd function in the 22 chunk files. For each
function the original was decompiled (and, where Hex-Rays collapsed args/strides, the
disasm read directly) and diffed line-for-line against the reconstruction. Constants and
table bytes were confirmed with `get_bytes` / `get_global_value`.

## Summary
- **FIXED (source + golden to the binary):** 3 divergences (Hud_FindModeIndex return,
  PlayerBar_ResetDragSlots index range, slider-label +112 offset) + 1 latent
  signed/unsigned compare (status-banner expiry).
- **VERIFIED-1:1:** the remaining ~60 provenance'd functions.
- **BOUNDARY / documented model imprecision:** 3 (scroll-arrow axis grouping, action-panel
  centre/right gfx parameterization, hotkey assign-button id pass-through).
- Built + ran (green): `gui_hud_test`, `hud_menu2_test`, `hud_menu2_e2e_test`,
  `gui_hud_labels_test`, `gui_hud_label_draw_test`, and the `*_itest` siblings.

---

## FIXED

### hud.cpp — Hud_FindModeIndex @0x4bea2c
The not-found exit (`0x4bea4f`, `if (result < 0) return result*4`) returns `result*4`
where `result` is the **negative loop counter** at exit (6→4→2→0→−2 ⇒ −8), NOT `4*modeId`.
- Before: `return modeId * 4;` (and golden expected `Hud_FindModeIndex(2,3,table)==8`).
- After: `return result * 4;` (golden updated to `-8`).
- Disasm evidence: `0x4bea49 result-=2; 0x4bea4f jl ...; return result*4`. The `curIndex<0`
  path *does* return `4*modeId` (eax still holds modeId) — that golden (`==20`) kept.
- Files: src/gui/hud.cpp, tests/unit/gui_hud_test.cpp.

### hud_menu2.cpp — PlayerBar_ResetDragSlots @0x4b1d17
`for ( i=0; i != 320; byte_11BB714[i*4]=0 ) { i += 10; arr[i]=... }`. The `i += 10` runs at
the **top of the body before the writes**, and the `i != 320` cond is checked before each
body ⇒ the writes land on indices **10, 20, …, 320** (slot 0 is never reset; slot 320 IS).
- Before: loop wrote indices 0,10,…,310 (size 320 array, slot 0 reset, slot 320 missing).
- After: `for(i=0;i!=320;){ i+=kStep; arr[i]=...; }`, arrays sized `kDragSlotLoopEnd+1`.
- Goldens updated (unit + e2e) to assert slot 0 stays 0 and slot 10/320 are reset.
- Files: src/gui/hud_menu2.cpp, tests/unit/hud_menu2_test.cpp, tests/e2e/hud_menu2_e2e_test.cpp.

### hud_menu2.cpp — Hud_BuildSliderPanel @0x4bd388 (label text-color offset)
`0x4bd564: *(WORD*)(widget + 112) = 67` writes the **text-color word at +112** (the same
slot BuildButtonRow writes at 0x4bcfac), NOT editFlags(+132).
- Before: `widgets[labelId].editFlags() = 67;` (+132).
- After: `widgets[labelId].at<i16>(112) = 67;`.
- Goldens updated (unit + e2e) to assert `at<i16>(112) == 67`.
- Files: src/gui/hud_menu2.cpp, tests/unit/hud_menu2_test.cpp, tests/e2e/hud_menu2_e2e_test.cpp.

### hud_labels.h — Hud_StatusBannerExpired @0x4bcc0c (signed→unsigned compare)
`dword_631678 + 350 <= (unsigned int)dword_62EB38` is an UNSIGNED compare (cmp/jbe). The
reconstruction used a signed `<=`, which diverges on tick wraparound.
- After: both operands cast to `unsigned` before the `<=`.
- File: src/gui/hud_labels.h.

---

## VERIFIED-1:1 (no churn — confirmed identical to the binary)

### input_state.cpp
- Input_ResetMouseButtonState @0x40c87c — 21 dwords, exact order.
- Input_ClearMouseButtonFlags @0x40c900 — 12 dwords, exact order.
- Input_SetWheelBase @0x40c870 — `wheel + 256`.
- Input_ClearMouseButtonsByMask @0x40dca8 — both raw/cooked branches, exact pair mapping.
- Input_SwapCursorClampState @0x40dc2c — park/restore order, ±32000, returns clampY1.
- Input_SaveMouseButtonSnapshot @0x40d338 — v0[3]/v0[6] (6721D0/6721DC) rotation.

### gui_object_state.cpp (84-byte stride; +48/+52/+60/+64/+68/+72/+76 confirmed)
- StateUpdate @0x40e9e8, ResolveObjectState @0x40eaf0, MarkObjectUsed @0x412ea4.

### input.cpp (behavioral model of slices of VIBE_Widget_DispatchMouseClick @0x421594)
- ResolveClickedSlot / RouteClick / ScrollbarDragValue — offsets (+36/+40/+68/+72), the
  byte-xor-1 ≡ dword-xor-1 toggle, the `thumbMax*(m-w-12)/(W-24)+4` signed-div scroll math
  and the active band all match.

### hud.cpp
- Hud_FindSlotForWidget @0x4bc280 (slot-scan slice), Hud_FindFreeSlot @0x4c5460.
- Hud_ButtonRowLayout @0x4bcdfc — cell/maxW+8/spread math, `maxW/-2+v26`, 16-bit pitch wrap.
- StatusText_Register @0x4bcc80, DamageLabel_Register @0x4bad5c (de-dup + last-older eviction).
- Clock_ComputeTimeOfDay @0x527778 — `((acc*0.00625f)+0.5)*100` x87→int truncate; h/m/s split.
  Consts confirmed: flt_622958=0.00625f, dbl_622960=0.5, dword_63CC60=100.

### hud_drag.cpp
- Hud_EnableDragMode @0x595b8c, Hud_DisableDragMode @0x595b80,
  Hud_ToggleObjectHighlight @0x4bd008 (form stride 171, widget stride 740, +52 flag).

### hud_labels.cpp
- Hud_CenteredLabelPlacement @0x4bbaec, DamageLabel_ExpireSweep @0x4bb7a0 (unsigned +300<now),
  Hud_NameInputCaptionPlacement @0x4bcafc (−54/+52), Hud_MarkOwnedObjects @0x4bacb4.

### hud_draw.cpp
- Hud_ScrollArrowPlacements @0x4bbd34 — gfx 0/1/2/3/4/5/6/7 & insets confirmed (but axis
  grouping is a documented BOUNDARY, see below).
- Hud_BuildObjectActionPanel @0x4bd0d4 — button block positions/gfx (0/400/77/583, +1/+2/+3,
  frame 1221, subject+1010) confirmed via disasm (centre/right gfx is a BOUNDARY).
- Hud_ProcessDragClick — state machine model.
- Hud_SelectedUnitCaptionBase @0x4bae88 — all 19 caption-base values + branch boundaries
  (0x18/0x1A/0x3C/0x3F/0x43/0x48/0x61/0x64/0x65/117/98) match exactly.

### hud_grid.cpp (Coord_ConvertX truncate + idiv /2,%2 confirmed)
- Hud_BuildTiledRow @0x4bd688, Hud_BuildScaledTiledBar @0x4bd758 — every gfx offset
  (+0/+1, +2/+3, +4/+5) read from disasm; consts flt_61E1F0=flt_61E200=1/21,
  dbl_61E1F8=0.01.
- Hud_BuildPersonGridCells @0x553154 — switch table (cols/rows/baseY) exact.
- Hud_BuildPersonColumnSlots @0x55375c, Hud_BuildBuildingChoiceRows @0x5529f8 (i<<7, (W−cW)>>1),
  Hud_BuildBuildingPriceRows @0x552b80 (i<<6, +27, code+1010),
  Hud_BuildTrainingRow @0x550a0c (backdrop x0/gfx1726, trainee x13/gfx205, child x64/w87/h205).

### hud_label_draw.cpp (drawer-pass models; layers/charH/width/insets confirmed)
- DrawObjectNameLabels @0x4bbaec (67/160/40), DrawAnimalLabels @0x4bbbbc,
  DrawCharacterLabels @0x4bbc8c (anchor>>16, −20, charH 32), DrawDamageLabels @0x4bb7a0 (−64),
  DrawStatusBanner @0x4bcb74 (layer 66, x=R−300, w 600, charH 40, scale 2047/505, expire 350),
  DrawNameInputCaption @0x4bcafc (charH 168). flt_61E190=2047f, flt_61E194=505f confirmed.

### hud_menu2.cpp
- MapMarker_ClassifyTree @0x543d1c (23/24, cap 48), ClassifyTower @0x544048 (22, cap 128).
- MapMarker_BridgeAngleKind / ClassifyTerrainFeature @0x543da0 — all **9 bucket doubles**
  (dbl_623FF0..624030) byte-verified; the b[5]-overlap 8th bucket reproduced exactly.
- Hud_SyncWindowColors @0x4bd5dc (+8/+10 word copy, quad 24/24/27/11),
  Hud_EnableObjectList @0x555f64 (stride 56, +8 id), PlayerBar_Destroy @0x4b1dc4 (order),
  Menu_FormatMissionBuildingName @0x59b8cc (hdr0=856692811 ⇒ type 51, hdr4=1342, hdr528=1555).

### hud_actionsn.cpp
- Hud_IsTradeContactType (30/31/32), Hud_IsEnterableContactClass (2/6),
  RunMeisterPersonalBookLoop @0x50ff00, RunSoeldnerBookLoop @0x50ff7c,
  UpdateSelectedObjectContact @0x50ee60 (GUI-owned tail).

### gui_widgetn.cpp
- Widget_RefreshTextN @0x412480, Widget_SetTextColor @0x412530 (+20 then refresh),
  StatusText_ClearTable @0x4bcc30 (50-stride, returns 6400), Scroll_RunAnimationLoop @0x537318,
  InfoPanel_Destroy @0x4b8438.

### gui_dialogs8.cpp (pure helpers all exact; consts byte/offset-verified)
- Person_TitleTextId @0x4f854b (+353>>24 + 370/294), Person_SpouseLayout @0x4f8588 (+358/+361,
  560/525, none 0x20D), Person_TalentTextId @0x4f85e8 (+13 + 279/272),
  Person_GuildTextId @0x4f8663 ((+9>>24)+1070), Person_StatBarRow @0x4f86be (4810/2/15/100/1162),
  Person_DetailGateOpen @0x4f8840 (signed char +2 < 10). The RenderPersonBody / BuildObject /
  BuildPersonDetailed text-id sequences match the decompile (hook-driven model).

### hotkey_assign.cpp
- Hotkey_BuildRows / OpenAssignWindow @0x4ff070/0x4ff1d1 — 11 rows, slot stride 3
  (dword_122DC14[3i]/dword_122DC18[3i]), colors 67/66, clear sets +4/+8 := −1,
  assign delegates, *Enabled gates (clear: row≥0 && buildingId≠−1; assign: row≥0).

### loadgame.cpp / loadgame_run.cpp @0x56a270/0x56a392/0x56a3f5
- Slot scan (stride 544, present/objId/widgetId), path "Gamedata\\Saves\\%s.SAV",
  session word_63C740=10, confirm gate byte_63CC40→RunMessageBox(257). Form "menu\loadgame_new",
  title 6244, slider 532×360 rows 130.

### loading.cpp @0x52ee84/0x52effc/0x52f0bc
- net/single form select (word_63C740 & 4), slider 582/114/66, proc 7, bar 582*pct/100,
  fade BLACK/30.

### main_menu_run.cpp @0x529d08
- Full faithful model: build order 10,53,96,139,268,311 + dword_63C7CC trio/pair, x=32 gfx=174
  color=300, RadioGroup(8), CD-track RandNext()%3 (Rittersleut/MauerUndTor/Kraeuter), the whole
  click-dispatch chain & session flags (|1, =137, |8 then |1, =10, =0, Quit 63CC30/63CC48/631614,
  Esc 67225C), and the volume sequence (2000…1000/1000…1000…5000).
- main_menu_wire.cpp: composition of the real sub-screen RunXxx — verified consistent.

### infopanel.cpp @0x4b84c0 / infopanel_build.cpp @0x4b6454/0x4b6930/0x4b64b0/0x4b6c80/0x4b6db8/0x4b7104/0x4b7468
- InfoPanel_SelectionChanged OR-chain (8 fields, 631E2C/28/24/30/34/38/3C/40 + 631680) verified
  exact against the binary. AddIconSprite (color 67, +88, text 96). The builder dispatch +
  per-builder form/icon/slider/sprite spine are hook/host-driven models faithful to the decompile.

---

## BOUNDARY / documented model imprecision (not raw-constant bugs)

1. **hud_draw.cpp Hud_ScrollArrowPlacements @0x4bbd34** — the model groups the 4 arrows as
   {up/down}=vScroll, {left/right}=hScroll. The binary pairs them per camera-delta global:
   dword_6316CC ⇒ gfx 4/5 (right) + 2/3 (down); dword_6316D0 ⇒ gfx 0/1 (left) + 6/7 (up).
   The per-arrow gfx/positions are correct; the *which-pair-lights-together* mapping differs
   because the model's vScroll/hScroll abstraction doesn't carry the camera sign convention
   (lives in another module). Left as a documented divergence rather than a guessed remap.

2. **hud_draw.cpp Hud_BuildObjectActionPanel @0x4bd0d4 centre/right gfx** — the model's
   `hasSubject` returns kActionFrameGfx(1221) for the a3≠0 case, but the binary uses the
   caller's a3 value there (and subjectKind+1010 when a3==0 & subject present). The function
   signature doesn't take a3, so full fidelity needs an API change. Button block is exact.

3. **hotkey_assign.cpp assign-button id** — the original calls AssignFromSelection(row, v24)
   with the assign-button widget id; the GUI hook layer doesn't surface it, so the model passes
   0. v24 only affects a `%G` status-banner number in the no-selection fallback path (text only).

## Notes
- No build/ recreation; built only the listed test targets. No git commands run.
- ConvertX @0x5c6b08 (truncate-toward-zero) verified at every float→int site in this chunk
  (clock, tiled bars, banner scale, person favorability) — all reconstructions use truncation.
