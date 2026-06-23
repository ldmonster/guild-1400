# Harden sweep — chunk gui_01

Full-tree 1:1 hardening of the 22 `src/gui/*.cpp` files in chunk `gui_01`, verified
function-by-function against `gilde.exe` via IDA MCP (decompile + disasm; `get_bytes` /
`get_global_value` on every constant). Disasm is the reference of record where Hex-Rays
collapses `__usercall` args or mislabels strides/registers.

No git commands run. `build/` directory never deleted/recreated — only the chunk's test
targets were built. `progress/INDEX.md` untouched.

## Totals

| Disposition | Count |
|---|---|
| VERIFIED-1:1 | 106 |
| FIXED (source / golden -> binary) | 33 |
| BOUNDARY (rule 3-5 tech swap or data-not-in-tree hook) | ~11 |

Wrong goldens corrected to the binary: ~9 (form_parse strides, dialogs6 worth-indices +
form names, groundplan hour-hand, dialogs5 usercall arg, dialogs4 hover return).

All chunk test suites GREEN: 30/30 (gui_dialogs3-7 + form + groundplan + drag/edge +
debug_windows, unit/itest/e2e incl. real-asset `real_forms_driver_e2e` over shipped
forms.BIN — 323 forms, 493 widgets, 0 failed) plus dialog/panel/event neighbor suites
(19/19). 0 failures.

---

## FIXED — divergences corrected to the binary (addr + evidence)

### gui_dialogs3.cpp
- `Gui_ClipRectToBuffers` @0x40e94c — was `x &= 0xFFFE` (32-bit); binary @0x40e9d4 does
  `LOWORD &= 0xFFFE` only, field is full DWORD -> `(x & ~0xFFFF) | (x & 0xFFFE)`.
- `Widget_BlitClippedRows` @0x412668 — zero-iteration return was `2*w`; corrected to the
  original `widgetIdx` (eax only reassigned inside the loop body @0x412702; on `jge` skip
  it is never reassigned).

### gui_dialogs4.cpp
- `Widget_SetFocus` @0x421370 — caret arg was `g_caretPenX`; disasm @0x421535/0x42155c
  reads `dword_75BEC4+2` -> `g_caretPenY>>16` (Hex-Rays mislabeled the register).
- `Widget_AddPersonRow` @0x518efc — full rewrite: `edx` is a packed dword (LOWORD=y,
  HIWORD=gfx base & item id), gfx ids hardcoded 0x4CA / (a2>>16)+0xCE / 0x6B8, price arg
  `a2>>16`, MoneyFormat rate verbatim, removed bogus `if(lbl>=0)` guard.
- `Widget_DrawScrollBar` @0x4121c4 — off-by-one: `(double)i < trackLen/step + 0.5`
  (dbl_610E44=0.5) -> inclusive segment count.
- `Widget_DrawScrollThumb` @0x40ecb0 — clip arg `g_screenClipW>>16` -> `(i16)g_screenClipExt`
  (misaligned `dword_69FFB8+2` @0x40ed49).
- `Widget_DrawCheckbox` @0x4137bc — added missing `flag&0x20` textured-quad branch; both
  animApply blob args `0` -> g_frameBlob (dword_62D210).
- `Window_RenderContent` @0x4186d8 — replaced placeholder with full reconstruction
  (phase-gated Result_Handler passes, 3 blit paths, +904 tiled bg w/ dbl_610F0C=1.5,
  +908 entity-tile double loop, +40 child Result_Finalize tail).
- `Widget_HandleKeyInput` @0x420360 — full tab(0x0F) group navigation, enter(0x1C) slot
  clear + early return, +344 overflow guard, shift arg to Input_CharToScancode.
- `Widget_ProcessMouseDrag` @0x420db4 — full mouse-down focus-grab FSM, disarm drag-origin
  restore, second flag-0x40 default, dword_75BEBC save/restore.
- `Widget_HoverUpdate` @0x41fd48 — head-reset reads dword_672220, byte-exact rolling-point
  buffer, 96-window State_Finalize sweep (stride 952), both wheel-scroll branches WIRED to
  real `Window_Scroll` @0x41a024, tail returns scroll/finalize result (was g_hitTestSlot4).

### gui_dialogs5.cpp
- `Panel_RunBuildingDetail` @0x551c2c — ComputeProductionWorth output is contiguous a2[]
  array; remapped to binary's non-linear stack-aliased indices, buffer 14->24; RunFrameLoop
  is __usercall (self-ptr = a2).
- `Panel_RunBuildingList` @0x552fc4 — called wrong sibling (RunBuildingDetail); binary
  dispatches RunBuildingRoundEnd @0x5530f6. Rewired + loop args.
- `Panel_BuildLawSeals` @0x552904 — Gesetz buffer 32B->36B (qmemcpy OOB); v7 read +0->+0x18.
- `BuildMoneyInfo`/`BuildMasterList`/`RunApBuy`/`RunUseObject` — sub-table offsets, label y,
  missing `widget+20=160`, conditional Populate, frame-loop args corrected to disasm.

### gui_dialogs6.cpp
- 3 wrong form names: RunBuildingRoundEnd `spielerr2`->`Spielerrunde_Ende_geb`;
  RunInventory `inventory`->`inventory_2` (+GameTick args -344/-264);
  RunThievesGuildTrain `diebe10`->`diebesgilde\diebesgilde_trainieren`.
- RunBuildingRoundEnd worth-index remap (base = &v11).

### dialog_checks.cpp
- `Window_CreateScrollButtons` @0x419ad8 — removed invented `if(down/up != -1)` guards;
  binary @0x419b42-0x419bf0 stores slot then writes widget+476=group, widget+444=3
  unconditionally.

### form_parse.cpp — `Form_ParseResourceFile` @0x41beb8 (5 sub-fixes)
- Per-object type/aux stride +8 -> +4 (`add esi,4`; type[+208], aux[+3472]).
- Slider range byte stride +8 -> +1; input flag byte `3472+4*o`.
- Type dispatch: `if(type<64) Object_AddToWindow` then independent switch 67/65/69
  (was mutually-exclusive chain dropping type 0, including type 64).
- Form-slot scan field dw[0] -> windowCount() dw[97] (dword_676E90 = base+268 dwords).
- Per-window `++windowCount()` + trailer flags dw[103]/dw[104]=1.

### groundplan.cpp
- `ComputeClockHands` @0x4af4a8 — hour hand uses WORD1 = `(lo>>16)&0xFFFF`, not WORD2;
  preserved x87 `*4.0*(1/60)`. All 7 FP constants get_bytes-verified.

### dragselect.cpp
- `UpdateUnitList` @0x4ba2bc — target(edi)=dword_12CEA8C and v32=67 set unconditionally
  before kind check (0x4ba327/0x4ba334); retry compares lastTarget not lastOwner
  (0x4ba4e4); removed stray Entity29-cell reassignment in retry block.

### event_panel.cpp
- `DestroySlot` high-water @0x4c562c — `dword_632268` written only inside the
  `!= -1` arm (no else); on empty table the binary leaves high-water unchanged.

---

## BOUNDARY — honest hooks (rule 3-5 tech swap or data not in tree)

- gui_dialogs3: `Gui_HitTestObject` +408 owner-state deref (GameObject not in module table);
  Form_RefreshIfVisible present/broadcast/anim edges (renderer).
- gui_dialogs4: dword_62D0Cx/75BExx pre-existing cross-module modeling fork with
  widget_interact.cpp (kept module-local for 1:1 + testability; unify later).
- gui_dialogs5/6: building-type/person interleaved tables not in tree, He-handler status
  enrichment, ComputeCurrentOutput thresholds, command-queue blobs, Win32
  Enable/Destroy/MessageBoxA, gildedlg.dll resource.
- form_cluster: ZOrder_RaiseWindow, Widget_DestroyByType, Surface_Destroy,
  Widget_LayoutBounds, RegisterGfxState, Form_PropertyValidate, Form_FindTextArrayIndex,
  512-vs-511 widget-array clamp.
- debug/cutscene: 3D scene/present/fade engine, GameTime baseline (qword_13CE852),
  dword_62EB4C music gate, owner-record back-refs, retained-mode GUI core edges.
- drag/edge/ground: `Hud_UpdateEdgeScroll` @0x4bc07c detached path depends on runtime
  frame-counters dword_631610/614/618/61C + aux node dword_631728 (coupled engine BSS);
  PickBlueprintName case-3 engine-string-derived `riss_<subtype>.bmp` primary path.

---

## Cross-module note for `src/play` owner
`dbl_61E208`/`dbl_61E210` (=0.125) are doubles -> drag-select centroid weight/quotient stay
double-precision in the binary. `play::DragUnitCentroidHit`'s local `kEighth` is a `float`;
verify it keeps double precision.

---

## Per-cluster detail
Full per-function dispositions are in the merged-from part files under
`progress/harden/_gui01_parts/` (gui_dialogs3, gui_dialogs4, gui_dialogs567, form_cluster,
dialog_cluster, debug_cutscene_cluster, drag_edge_ground_cluster).
