# Harden sweep — src/gui/gui_dialogs4.cpp

MCP-verified against gilde.exe (imagebase 0x400000). Disasm is the reference of record
on __usercall arg/stride/register disputes. Test target: `gui_dialogs4_test`
(+ `gui_dialogs4_itest`, `gui_dialogs4_e2e_test`). All three GREEN (61 unit checks).

## Per-function status

### VERIFIED-1:1
- **Form_SetObjectValueOrText** @0x41e1c4 — window resolve `676A60[171*form+1+a2]`,
  `a1 > *(win+26)>>16` guard, type 'A'(65)/'E'(69) branches, flag&1 2-byte-stride copy
  (strlen<=0xFE), flag&2 (+28/+24/+296), 'E' (+124/+128/+132&0x10/+120/+140), button
  mirror (+68||+72 -> +36/+40). Diffed every offset against the decompile. The `(char)v6`
  return is a 32-bit-pointer-truncated-to-byte artifact (callers ignore it); the model
  returns the slot byte — cosmetically different, behaviorally inert.
- **Form_MarkDirtyAndRender** @0x41cae0 — `GameTick_Finalize`, dirty-mark loop
  (`backWidget(+620)+52=1`, child `ids[k]+52=1`, re-read windowCount each iteration),
  `shownFlag(+102)=1`, two `Surface_Create` (lightSetGray(0,64) before each, surfaceA/B
  at +105/+106). The Surface_Create 3rd arg comes from an UNRESOLVED register in the
  binary (Hex-Rays `v13/v15` uninit; dead `var_50/var_4C` width/height writes) — modeled
  as 0 (BOUNDARY: un-resolvable source-widget register; the leaf is a render hook).
- **Window_MainWndProc** @0x5279dc — full message dispatch ladder verified: WM_PAINT(0xF)
  ValidateRect+0; WM_DESTROY(2) PostQuit+0; 0x10 -> DefWindowProc; 0x14 ValidateRect+1;
  0x209 ValidateRect+1; 0x218 !wParam -> 1112363332 (0x42490004); 2023 audio reacquire;
  WM_ACTIVATE(28) activate/deactivate latch (byte_63CC14) with the full reacquire block
  (`Render_IsSurfaceLost` spin, DecompressState_Blob, two Result_Handler_Interaction with
  `*(blob+8)/*(blob+4)`, frameBlob2, audio reacquire, mouse acquire) and the deactivate
  path (SetWindowPos(hWnd,1,0,0,0,0,3)). All arms + magic constant match.

### FIXED (addr + before/after + evidence)
- **Widget_SetFocus** @0x421370 — caret-creation call args. Binary 0x421532/0x421559:
  `CreateObject_Thunk(eax=dword_75BEC4>>16, dx=(i16)dword_75BEC4-7)` and
  `LayoutBounds(eax=dword_75BEC4>>16, dx=(i16)caretY-7, ebx=g_caretWidget)`.
  Hex-Rays mislabeled `eax` as `dword_75BEC8` (g_caretPenX); the disasm reads
  `dword_75BEC4+2` (g_caretPenY high word). BEFORE passed `g_caretPenX`; AFTER passes
  `g_caretPenY>>16`. (Evidence: disasm 0x421518 `mov ax,word dword_75BEC4`; 0x421535/
  0x42155c `mov eax,dword_75BEC4+2; sar eax,10h`.)
- **Widget_AddPersonRow** @0x518efc — full body rewrite to match disasm 0x518efc-0x519018.
  The `edx` arg is a PACKED dword: LOWORD = base row y, HIWORD(>>16) = gfx base AND market
  item id (was wrongly treated as a single `gfxId` used as `gfx`, `gfx+2`, `gfx+53`).
  Corrected to the binary's: cell1 gfx **0x4CA(1226)** at (x=a1, y=a2>>16); cell2 gfx
  **(a2>>16)+0xCE** at (x=a1+2, y=(i16)a2+2); cell3 gfx **0x6B8(1720)** at (x=a1+3,
  y=(i16)a2+53). Price: `LookupCachedMarketPrice(a2>>16, a5)` (was `gfxId,rate`);
  `ConvertX` then `fistp` = `(int)trunc(price)`; `MoneyFormatWithSeparators(amount, a5)`
  (removed bogus `rate?rate:1`). Label at (a1+7, (i16)a2+58, dword_62D230); the +20/+112/
  +92 writes are UNCONDITIONAL in the binary (removed the recon's `if(lbl>=0)` guard).
- **Widget_DrawScrollBar** @0x4121c4 — track-segment loop count. Binary 0x412361 loop is
  `(double)i < (double)(trackLen/step) + dbl_610E44`, dbl_610E44 = 0x3FE0000000000000 =
  **0.5** -> iterates i = 0..floor(trackLen/step) INCLUSIVE (one more than the int quotient).
  BEFORE `for(i; i < trackLen/step)` (off by one); AFTER float-bounded loop. New unit test
  `DrawScrollBarTrackSegmentCountInclusive` (35/10 -> 4 segments). Rest of the function
  (glyph state 8/9/10, focus branch, coord caps, animApply tail w/ g_defaultCtrlH/2,
  g_drawClipTop/Bottom) verified 1:1.
- **Widget_DrawScrollThumb** @0x40ecb0 — Coord_Push 3rd/4th args. Binary 0x40ed49
  `mov ecx,dword_69FFB8+2; sar ecx,16` is a MISALIGNED int read spanning g_screenClipW/
  g_screenClipExt -> `(i16)g_screenClipExt`; 4th arg `g_screenClipExt>>16`. BEFORE used
  `g_screenClipW>>16`. Rest (state_update(thumbStateId), 4x coordTransform, y/xc, 3x
  animBasic, stateGetCurrent, ty/tx halving, animBasic(3), scrollThumbX/Y, stateFinalize,
  animStateUpdate(+296), animApply) verified 1:1.
- **Widget_DrawCheckbox** @0x4137bc — (a) added the missing `flag & 0x20` textured-quad
  branch (0x413860): `Decompression_Finalize(g_frameBlob)`, `inv=1.0/(double)*(u32)
  (g_renderContext+116)`, DrawTexturedQuad(g_renderContext, +448/2+(*(+14)>>16),
  +452/2+(*(+4)>>16), 0, inv*+448, inv*+452, 0.5), `DecompressState_Blob(g_frameBlob,0)`.
  (b) both `animApply` calls' 4th (blob) arg = **g_frameBlob (dword_62D210)**, was `0`.
  State byte (8/9|0x10|4|0x20), gx/gy off/span (-1 sentinels), st(+440 or g_defaultFont+1),
  and the flag&4 vs default animApply x/y verified 1:1. New global `g_renderContext`.

### FIXED (full reconstruction of a prior simplified "minimal model")
- **Window_RenderContent** @0x4186d8 — the prior version was an explicit fake ("tiled fill
  loop bound preserved structurally; body draws via hook" + `coordPush(0,0,0,0)`),
  dropping ~8 branches. Fully reconstructed from the decompile + disasm: the ColorFill
  (+13&4), two early Result_Handler_Interaction passes gated on g_renderPhaseFlag(byte_62D25C)
  / +624 / +13&0x10, the +920/924/928/932 inner-rect math, the three textured-quad blit
  paths (+624==2, phase==1, +912) each branching ctx==g_frameBlob -> blit vs
  Render_WithSurfaceContext, the outline (+12&2), the +904 tiled-background fill (clip
  save/restore of all four g_drawClip{Left,Top,Right,Bottom}, anim table base
  dword_62D204 84-stride, outer loop bias dbl_610F0C = 0x3FF8000000000000 = **1.5**,
  inner integer `j < cW/frameW`), the +908 entity tile-animation double loop
  (v34=cH/v11+2, v41=cW/v36+2) bracketed by Entity_AnimationUpdate, and the +40 child
  Result_Finalize tail. New globals: g_renderPhaseFlag, g_tileAnimRec, g_animTableBase,
  g_drawClipLeft, g_drawClipRight.

### FIXED — full reconstruction of the three prior "minimal models" (now 1:1)
All three were authored as deliberate "minimal models" (Rule-8 violations). They are
now fully reconstructed line-for-line from the decompile + disasm. Build + all three
test suites green; regression cases added for each newly recovered path.

- **Widget_HandleKeyInput** @0x420360 — FIXED. BEFORE: enter cleared focus but fell
  through to the caret recompute (binary returns early after zeroing the backing slot
  +40); tab (0x0F) was entirely absent; printable cap path had no `+344`/`+16` overflow
  guard; numeric path ignored shift state. AFTER (disasm 0x420360-0x42088b): enter
  (0x1C) now zeroes `*(740*record[304] + pool + 40)` and returns early; tab (0x0F)
  performs the forward (no-shift) / backward (byte_671D96|byte_671D8A held) group walk
  over the child-id list `*(group+24)` bounded by `*(group+26)>>16`, skipping non-'A'
  / destroyed(+52) children, transferring focus (`dword_62D328 = *(pool+740*child+12)`),
  toggling the +40 highlights, seeding `dword_75BEBC`(index)/`75BECC`(value from +296)/
  `75BED0`(=0)/`75BED4`(=dword_62D0C8 - v20), then LABEL_48 caret teardown
  (DestroyByType) + Property_Get caret recompute (`75BEC8`/`75BEC4`); the insert path
  now honours the `+344 && +344<=+16` overflow guard and the shift arg into
  Input_CharToScancode. Evidence: 0x420590 (740*[304]), 0x420745 (shift gate), 0x4205dc
  (back walk), 0x420772 (fwd walk), 0x420877 (overflow guard), 0x4204cd (shift arg).
- **Widget_ProcessMouseDrag** @0x420db4 — FIXED. BEFORE: the entire mouse-down focus-GRAB
  block was missing, drag-origin bookkeeping (dword_62D0C8/0D0 restore from 75BEB8/EC0)
  was absent, and the focus-index preservation across the early disarm block was dropped.
  AFTER (disasm 0x420db4-0x42135d): the disarm block restores `dword_62D0C8=dword_75BEB8`
  / `dword_62D0D0=dword_75BEC0` and clears `dword_62D33C` when an owner was set; the GRAB
  block (0x420eb2) hit-tests `dword_62D22C` -> type-'A' widget, resolves its data record
  (+12), gates on `slot==*(rec+304) && mouseDown && rec!=focus && !focusFlag`, transfers
  focus with the +40 highlight swap, seeds `75BED0/75BED4/75BECC/62D328`, walks the
  owning group (+44) for the index, applies the flag-0x40 no-value auto -1 default, and
  lays out / tears down the caret on the +38&1 edit bit; the second flag-0x40 default
  (0x421142) and `dword_75BEBC` save/restore across both blocks are reproduced. Evidence:
  0x420de7 (origin restore), 0x420edd ('A' tag), 0x420f1a (grab gate), 0x420fc5 (group
  walk), 0x420ffa (auto -1), 0x421041 (+38&1 caret).
- **Widget_HoverUpdate** @0x41fd48 — FIXED. BEFORE: the State_Finalize window-table sweep
  and the wheel-scroll forwarding were absent, the head used a phantom `g_mouseMovedFlag`
  instead of `dword_672220`, and the tail returned the wrong value (`g_hitTestSlot4`).
  AFTER (disasm 0x41fd48-0x4200f6): the head reset condition now reads `dword_672220`;
  the cursor "rolling point" buffer (word_75BF44|dword_75BF46|word_75BF4A) is modelled
  byte-exact so the misaligned X(`+4>>16`)/Y(`+2>>16`) reads and the post-update shift
  translate 1:1; the State_Finalize sweep walks all 96 windows (stride 952 == sizeof
  Window) gating on `+13&2 && +640==1 && word+636`, forwarding `*(win+634)>>16`; both
  wheel-scroll branches (hovered-scroll window dword_75BF08 and wheel window dword_62D294)
  are WIRED to the REAL `Window_Scroll` sibling (@0x41a024, gui/window_mgmt.cpp) with the
  +592(scrollOffset)/+608/+612 record reads; the tail returns
  `State_Finalize(dword_62D248)` when set, else the scroll-result pointer (null -> 0) —
  it never returns the hit-test slot. The e2e golden that encoded the old wrong
  `return g_hitTestSlot4` was corrected to the binary (returns 0 in the no-scroll case).
  Evidence: 0x42003d (672220 gate), 0x41fe52 (point shift), 0x41fe84 (sweep gate),
  0x41ff5f (+476 window index), 0x41ff82/0x41ffe4 (Window_Scroll calls), 0x4200eb (tail
  State_Finalize).

## Globals added this pass (module-owned, BSS, reset in ResetGuiDialogs4)
g_renderContext(dword_62D268), g_renderPhaseFlag(byte_62D25C), g_tileAnimRec(dword_62D21C),
g_animTableBase(dword_62D204), g_drawClipLeft(dword_64A1B4), g_drawClipRight(dword_64A1BC).

## Globals added in the 3-function full-reconstruction pass (module-owned, reset in ResetGuiDialogs4)
g_dragOriginX(dword_62D0C8), g_dragOriginY(dword_62D0D0), g_savedClampY0(dword_75BEB8),
g_savedClampY1(dword_75BEC0), g_caretCreateBase(dword_62D2C8), g_shiftHeldL(byte_671D96),
g_shiftHeldR(byte_671D8A), g_hoverScrollWin(dword_75BF08), g_scrollWheelWin(dword_62D294),
g_stateFinalizeReq(dword_62D248), g_wheelUp(dword_672254), g_wheelDn(dword_672250),
g_cursorPtBuf[8] (word_75BF44 | dword_75BF46 | word_75BF4A — the live cursor rolling-point
buffer; replaces the prior g_mouseX/g_mouseY/g_mouseMovedFlag, which were unreferenced
elsewhere and modelled the misaligned dual-coord read incorrectly).

NOTE (cross-module modeling fork, BOUNDARY): dword_62D0C8/0D0/33C/34C/75BEB8/EC0 are the
SAME engine dwords that widget_interact.cpp already models under its own C++ names with a
different type (ScrollDragRecord*/i32). gui_dialogs4 keeps its own copies (matching the
rest of this module's interaction-state cluster) so it is internally 1:1 and testable;
unifying the two modules' view of these globals is a pre-existing architectural decision
outside this file's edit scope.

WIRING (Rule 13): Widget_HoverUpdate now calls the REAL Window_Scroll sibling
(gui/window_mgmt.cpp @0x41a024) directly via #include "gui/window_mgmt.h" — no hook.

## Constants recovered via get_bytes
- dbl_610E44 = 0x3FE0000000000000 = 0.5  (DrawScrollBar track loop bias)
- dbl_610F0C = 0x3FF8000000000000 = 1.5  (Window_RenderContent tiled-fill outer bias)
- DrawCheckbox cell gfx / AddPersonRow gfx ids: 0x4CA(1226), +0xCE(206), 0x6B8(1720)
- ConvertX @0x5c6b08 sets x87 RC=truncate then frndint => trunc-toward-zero (== (int) cast).

## Counts
VERIFIED-1:1: 3 (Form_SetObjectValueOrText, Form_MarkDirtyAndRender, Window_MainWndProc)
FIXED: 10 (Widget_SetFocus, Widget_AddPersonRow, Widget_DrawScrollBar, Widget_DrawScrollThumb,
           Widget_DrawCheckbox, Window_RenderContent, + AddPersonRow ConvertX/Money args;
           Widget_HandleKeyInput, Widget_ProcessMouseDrag, Widget_HoverUpdate — the three
           prior minimal-models, now fully reconstructed 1:1)
DIVERGENT (follow-up): 0
BOUNDARY notes:
 - Form_MarkDirtyAndRender Surface_Create source-widget arg (unresolved reg).
 - Caret CreateObject_Thunk ebx=dword_62D2C8+8 third arg dropped (the thunk hook is 2-arg,
   matching the established Widget_SetFocus convention; the thunk is a render-cluster leaf).
 - Cross-module modeling fork for the dword_62D0xx/75BExx drag-origin cluster (see above).
Tests: gui_dialogs4_test 76 checks / 0 fail (added EnterClearsFocus, TabNavigatesGroupForward,
ProcessMouseDragFocusGrab, HoverUpdateWheelScrollForward); itest GREEN; e2e GREEN (its
HoverUpdate golden corrected to the binary's return). 3/3 ctest passed.
