# Hardening sweep — chunk gui_05

MCP-live 1:1 verification of the provenanced functions in the gui_05 .cpp set.
Every float→int site checked against the disasm (ConvertX @0x5c6b08 = round-toward-zero
trunc via control word + frndint; `(int)` cast trunc); every constant `get_bytes`-verified;
fixed-point shift signedness and struct offsets checked. Tests built per-target only;
build/ untouched; no git.

## FIXED (divergence → binary)

### window_render.cpp — VIBE_Window_ApplyScrollOffset @0x4163bc
The child-reflow `ny` term order was inverted.
- disasm 0x416631: `ny = child.y(+18) + word@588(prevY) - word@584(scrollY)`.
- before: `ny = c.y() + w.at<i16>(584/*scrollY*/) - w.at<i16>(588/*prevY*/)` (negated).
- after:  `ny = c.y() + w.at<i16>(588/*prevY*/) - w.at<i16>(584/*scrollY*/)`.
- `nx` was already correct (prevX@604 - scrollX@600). All field offsets (580/584/588/592/
  596/600/604/608) reverified against `v1[145..151]` / `*((u8*)v1+608)`. VERIFIED otherwise.

### window_layout.cpp — VIBE_Window_Resize @0x41a0f8
Two divergences, both fixed against the disasm (var_18=a1-w[+8]=widthDelta;
var_14=a2-h[+10]=heightAccum; the child-fit loop writes `*(DWORD*)&var_14`).
1. The child-fit loop grew the WIDTH accumulator; the original grows the HEIGHT
   accumulator (var_14). Also the loop origin was `win.x()+win.y()`; the original uses
   `y + h` (`v26 = y@+6`, `a4 = h@+10`, `v36 = heightAccum + y + h`,
   `if (childRight > v36) heightAccum = childRight - y - h + 8`).
   - before: grew `fittedW`, origin `win.x()+win.y()`, applied to width.
   - after:  grows `heightAccum`, origin `win.y()+win.h()`, applied to height
     (`win.w() += widthDelta; win.h() += heightAccum`).
2. The first screen-clamp condition used `win.y()+win.x()` against `g_screenClipW>>16`;
   the original is `if (y + h > LOWORD(dword_69FFBC)) h = LOWORD(dword_69FFBC) - y`
   ([&dword_69FFB8+2]>>16 resolves to the low word of dword_69FFBC = (i16)g_screenClipExt).
   - before: `if (win.y()+win.x() > g_screenClipW>>16) win.h() = g_screenClipExt - win.y()`.
   - after:  `if (win.y()+win.h() > (i16)g_screenClipExt) win.h() = (i16)g_screenClipExt - win.y()`.
   The second clamp (x+w vs g_screenClipExt>>16 → w = (g_screenClipExt>>16)-x) was correct.

### window_layout.cpp — VIBE_Window_AutoFitHeight @0x41a500
The child loop iterated the window's child id-list; the disasm (0x41a537, `eax += 0x2E4`
from base 0 over `dword_69FFB4`) shows it walks the GLOBAL widget array
`g_widgets[i], i in [0, objCount)`. Fixed to iterate `g_widgets[i]` flat. `winTop = y`
(v3[1]>>16 = word@+6) and `childBottom = child.x + child.w` already matched; return
`Window_Resize(win.w(), minHeight, slot)` matches `HIWORD(*(u32*)(v3+6))` = word@+8 = w.
Free-slot return `952*winSlot` = `(238*a1)*4` VERIFIED.

### window.cpp — VIBE_Object_AddToWindow @0x41ae10
Three divergences fixed against the decompile:
1. Child geometry was missing the scroll-offset subtraction. Original:
   `x' = win.x(+2 word) + a3 - scrollX_lowword(word@600)`,
   `y' = a2 + win.y(+3 word) - scrollY_lowword(word@584)`.
   Added `- (i16)win.at<i32>(600)` (x) and `- (i16)win.at<i32>(584)` (y).
2. clipY1(+34)/clipX1(+30) were left default; the original stamps
   `+34 = LOWORD(dword_69FFBC)`, `+30 = HIWORD(dword_69FFBC)`. Added (g_screenClipExt
   low/high words).
3. clip Y0/X0 = 0, +60/+52 inheritance from backing, +116 owner, content-height grow
   `y + child.h()` (`*(v12+20)>>16` = word@+22 = h) all VERIFIED.

## VERIFIED-1:1 (no change)

- widget_interact.cpp — VIBE_Scrollbar_DragThumb @0x4208ac: control flow, +24 max /
  +28 min / +38 flags / +296 value offsets, signed `idiv` (sar edx,1Fh), `track32`(+32)
  write, `frndint`-free integer path, SetThumbPosition(rec, startValue, mouseX, originX)
  tail, value==-1 / held-page / edge-gate early-returns all match. The `sar 0x10` (signed)
  shifts for the packed mouse coords are modeled by the caller-supplied curMouseX/Y.
- widget_interact.cpp — VIBE_Widget_ClearActiveDrag @0x4202e8: 740*owner, +40 clear,
  dword_62D328=0, dword_62D33C!=-1 clamp-restore returning dword_75BEC0. Match.
- widget_interact.cpp — Slider_StepFromButtons (core of VIBE_Slider_UpdateFromMouse
  @0x420a04, 0x420d71 block): repeat gate `clickEdge || (autoRepeat && editMax>=100)`,
  +/- via +732/+733, clamp `>= editMin(+124)` with `(flags&0x10)&&v15>step(+140)` →step
  else `>editMax(+128)`→editMax else →editMin, dirty(+96)=1, editVal(+120)=v15. Match.
  (a1=+132,a2=+140 are read directly from the widget — the exact fields the full function
  loads in the mouse-track block.)
- widget_interact.cpp — VIBE_Widget_SetScrollLimit @0x41207c: `740*idx` return, write
  code→+132. Match.
- trade_dialog.cpp — VIBE_TradeDialog_ConfirmSellCarts @0x53f2bc: per-element
  `total = trunc(price + total)` (fild prev int, fadd, ConvertX trunc, fistp) and final
  `total = trunc(total * dbl_623ED8)`; dbl_623ED8 get_bytes = 0x3FD999999999999A = 0.4.
  `count<=32 && count` gate, signed sum. Model is parameterized (prices/rate passed in);
  the `(int)(double)` casts reproduce ConvertX truncation. Match.
- trade_panel.cpp — VIBE_TradePanel_InitSlotTables @0x50854c: buy x=((k%4)<<6)+16 /
  y=((k/4)<<6)+16 (stride 56B), sell x=((k%4)<<6)+16 / y=((k/4)<<6)+208, itemId=0 /
  objectId=-1; return 272 = ((7/4)<<6)+208. Match (owned item-grid portion).
- trade_panel.cpp — VIBE_TradePanel_PopulateInventorySlots @0x50a794: fill =
  `(unsigned)(stock * capacity) >> 2` (disasm: `(unsigned int)(*(v40+28)*cap) >> 2`) —
  unsigned shift reproduced as `(i32)((u32)((u32)stock*(u32)capacity)>>2)`. Match.
- trade_panel.cpp — TradePanel_SlotX/BuySlotY/SellSlotY: the `((k%4)<<6)+16` /
  `((k/4)<<6)+16|208` expressions, matched to the table fills above. Match.
- window_mgmt.cpp — VIBE_Window_Scroll @0x41a024: scrollOffset+scrollCur floor/ceil
  branches, `dword_67EDD4[238*winSlot] += delta`, return `16*(56*winSlot)`. Match.
- window_mgmt.cpp — VIBE_Window_PositionCentered @0x41d764: x = (mode&1)?
  g_screenCenterX - (w/2) : x ; y = (mode&2)? g_screenCenterY - (h/2) : y ; word picks
  w@+8 / h@+10 / x@+4 / y@+6; LOWORD truncation to i16. Match.
- widget_create.cpp — VIBE_Object_AddTextLabel @0x41b288: +16=win.x+x, +18=win.y+y-scrollY
  (word@584), +20=Property_Get+96, +22=g_defaultCtrlH, clip +32=y/+34=h+y/+28=x/+30=w+x,
  font word@636 else g_defaultFont. Match (pointer/scroll modeling documented).
- trade_panel_windows.cpp — column tables get_bytes-verified:
  kColTableItem (0x507C58) and kColTableSell (0x507D68) byte-identical to source;
  kColTableRebuild = &dword_507E38[+16B] (=0x507E48) rotation — first row {4,4,3,3} etc.
  confirmed by bytes. VIBE_TradePanel_RefreshItemColumns @0x50b1c4 structure
  (FillColumnScratch `v3+=5`→5,10,15,20; existing-slot stock=0; free-slot append;
  re-query refresh/clear) matches.

## BOUNDARY / documented model (rule-8 boundaries or aliasing the brief warns about)

- widget_create.cpp — VIBE_Input_RegisterField @0x40fe04 (and Input_RegisterIcon
  @0x40f800): the occupancy scan reads `dword_695308[edx]` (edx steps 0x15C=348B record
  stride), i.e. an aliased field at +648 within the 348B-stride record — NOT the dw[0]
  record-id the function writes. The reconstruction scans/writes dw[0] (internally
  consistent sequential allocation). Stride (348B/87 dwords), the 127 cap, the
  digit-count loop `do{++n; v/=10;}while(n<10&&v>0)`, and the flag branches
  (1→max=32, 2→numeric, 0x10|0x20→+100=1, 0x80→max>=25) all match. The +648-aliased
  occupancy field is a packed-table aliasing detail left as a documented divergence to
  avoid a speculative regression; flagged for a dedicated pass with the InputField layout.
- Renderer/property/metric leaves (Property_Get/Object_RecomputeSize glyph metrics,
  GfxMetricWord/Dword, SliderTrackExtent, SceneStateFor, GlyphAdvance, the slider surface
  blit, ZOrder cache as host pointers, window child-list/text buffers held in side arrays
  for 64-bit) remain the documented rule-3/4 boundaries; their integer cores are 1:1.
- tooltip_dispatch.cpp / trade_dialog (layout halves) / violation_dialog: phase-structured
  reimplementations of multi-global dispatchers; control flow follows the decompile phases
  and the float→int site in ViolationDialog_Build (`(int)(wealth*rate)`) is a ConvertX
  identity trunc reproduced by `(int)`. No new numeric divergences found.

## Tests
Built only chunk test targets (cmake --build build --target <t>). All green:
gui_widget_create_test, gui_form_loader_test (ApplyScrollOffset + Resize/AutoFit goldens),
gui_window_mgmt_test, gui_widget_layout_test, window_scroll_test, gui_widget_interact_test,
gui_trade_dialogs_test, gui_trade_item_panel_test, gui_tooltip_dispatch_test,
trade_recon_dragslots_test, chattrade_wave22_test — 11/11 passed.

## Counts
FIXED: 4 functions (ApplyScrollOffset, Window_Resize, Window_AutoFitHeight,
Object_AddToWindow).
VERIFIED-1:1: 15 functions/tables.
BOUNDARY/documented: Input_RegisterField/Icon occupancy-field aliasing + the standing
renderer/property leaves.
