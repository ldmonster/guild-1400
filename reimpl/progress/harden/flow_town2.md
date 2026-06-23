# Harden sweep — TOWN-2 (town-view HUD + panels)

Agent: GUI-flow hardening TOWN-2. Scope = the town-view HUD GUI (on-screen
panels / credit / gift / selection-action + HUD render bridge + per-frame driver).
Every provenance'd function in the owned files was decompiled (MCP live) and diffed
line-for-line against gilde.exe (imagebase 0x400000); disasm used as reference of
record at every float->int site and gate.

Owned files:
src/play/hud_binder.cpp, hud_render.cpp, hud_recon_credit.cpp, hud_recon_giftpanel.cpp,
hud_recon_selaction.cpp, ui_recon4_hud_surface.cpp, ui_recon3_widget.cpp,
ui_recon5_panels.cpp, wire_hud_bridge.cpp, session_hud.cpp, session_panels.cpp,
session_select.cpp (+ headers + tests).

## Key cross-cutting finding (ConvertX truncation, confirmed at disasm)
VIBE_Coord_ConvertX @0x5c6b08: `fstcw; fldcw(RC=11 truncate-toward-zero via the 0x1F
high byte); frndint; fldcw(restore)`. It rounds st0 to an integral value TOWARD ZERO,
then the caller's `fistp` stores that already-integral value (round-to-nearest is a
no-op on an integer). Net = truncation. So every `(int)ConvertX(...)` site in the gift
dialog / person card is correctly modeled by `(int)` truncation. CONFIRMED, no fix.

## FIXED (source + golden corrected to the binary)

### 0x4243d8 VIBE_Surface_DrawText colour packing — ui_recon4_hud_surface.cpp
- Disasm 0x424430..0x42444c: `color = a3 | (a4<<8) | (a5<<16)` with bl=a3 (bit0=R),
  cl=a4 (bit8=G), stack=a5 (bit16=B). COLORREF 0x00BBGGRR.
- BEFORE: `SurfaceTextColorRef` returned `(a3<<8) | a4 | (a5<<16)` — a3 and a4 swapped
  into the wrong bit positions (a3 at bit8, a4 at bit0). Params mis-named green/red.
- AFTER: `a3 | (a4<<8) | (a5<<16)`; params renamed a3_red/a4_green/a5_blue;
  `SurfaceColorRefRGB(r,g,b)` -> `(r,g,b)` (was the swapped `(g,r,b)`).
- Golden (tests/unit/ui_recon4_hud_surface_test.cpp) encoded the swapped behavior;
  corrected: f(0xFF,0,0)=0x000000FF (R), f(0,0xFF,0)=0x0000FF00 (G),
  f(0,0,0xFF)=0x00FF0000 (B), f(0x34,0x12,0x56)=0x00561234.

### 0x51b0fe.. LenderSumBuildingValues (inner v22 aggregation) — ui_recon4_hud_surface.cpp
- Binary accumulates in a 32-bit signed `int v22` (wraps mod 2^32) and passes it to
  VIBE_Text_RenderRichString as a 32-bit int.
- BEFORE: accumulated in `i64` (no wrap) -> diverges on overflow.
- AFTER: accumulate in i32 with explicit u32 wrap, sign-extend into the i64 return.
  Existing goldens (425/0/42) all fit 32-bit -> unchanged.

## VERIFIED-1:1 (decompiled + diffed, no change needed)

### hud_recon_credit.cpp / .h
- 0x519dbc NewLoan_CompactOffers — packs {amount,b,c,childId=0,index} for amount>0;
  the original's stride-5 base-offset-5 layout is an impl detail, row VALUES identical.
- 0x51ab48 LoanRelease_ComputeRepay / RemainingPreview — signed `held>=repayAmt`
  payer/transfer selection; remaining = held-repay. Matches v5/v21 paths exactly.
- 0x51d64c AccountInfo_HandleClick — price-child -> tab1, choice-child(&&tab!=2) -> tab2,
  dirty=1. Matches the `if/else if` and the `v2!=N` guards.
- 0x51a868 LoanList_InitRowTable — the `for(i=0;i!=27; v24[i+2]=0){i+=3; v24[i]=-1;
  v24[i+1]=-1;}` net writes tbl[3..29] (9 rows {-1,-1,0}); tbl[0..2] untouched. Match.

### hud_recon_giftpanel.cpp / .h  (0x55d03c)
- Gift_ComputeSliderRange — upper v37 = max(ConvertX(wealth*0.005),1600); lower v38 =
  ConvertX(min(held,wealth*0.05)) gated >=3200 (else 3200; if >=3200 branch yields <=0
  -> abort). All float->int via ConvertX = truncate (confirmed). Constants
  flt_624A3C=0.005, flt_624A40=0.05 match.
- Gift_EvaluateConfirm — v41 (player-controlled) suppresses amount; else amt =
  MultiplyByRate(slider, rate); emit gated on CheckResourceAmount. Match.
- NOTE (modeling): the original rounds the 0.05 cap to 32-bit float (`fstp`) BEFORE the
  `(double)held <= cap` compare; the reimpl compares against a full-double cap. A
  float-vs-double edge case at the exact boundary only; values away from the knee are
  identical. Documented; not a behavioral divergence for integer wealth/held inputs in
  the representable range.

### hud_recon_selaction.cpp / .h  (0x54e7b4)
- Gate dword_631724 -> dword_6317B0 && slot && (rank>1||dword_63C7B8) && dword_67221C
  -> free-act over 411648/536=768 entities; else slot rank<=1 -> generic tooltip, else
  resource-name index `(slot+8)>>16` (signed), table stride 2. Match.

### ui_recon3_widget.cpp  (0x410178)
- VIBE_Widget_Free_Thunk: `call 0x414f98; retn` pass-through. Match.

### ui_recon4_hud_surface.cpp  (remaining)
- 0x553f30 BuildPersonCardLayout / 0x55433c simple — v24/v14 = (cardDims>>16)/2 + baseX
  (signed sar then signed /2); portrait x = center - (dims>>16)/2 - 3; slider gate
  kind!=6, full kind==7, icon row kind in {5,6,7}; no-record sprite 1190. Match.
- 0x5540d7 PersonCardNameTextId — labelCount!=0 && withLabels -> (genderFlag?279:272)+
  labelCount; else -1. Match.
- 0x41eee8 PaintboxClearAlt — v5[160]==0 invalidate; v5[10]==0 no-paintbox; else filled.
- 0x51adb4 LenderInitRowSlots — do/while v3+=3 over v34[-2..] = 16 slots {-1,-1,0}.
- 0x51aea3 LenderSliderPanelValues — {14,24,14,14}. Match.
- 0x51d9a4 AssetOverviewSetup — mode 6, capacity 1024, textId 5371, grayShade 40. Match.

### ui_recon5_panels.cpp / .h
- 0x4b1ba8 PlayerBarResetSlots — 32 slots {objA/B/C=-1, handle=0xFFFF, objD/E=-1,
  flag=0} (i=10..320 step 10). Match.
- 0x548c54 AbductEvalGate — skill2 / null-target / office-pick / office-confirm /
  count<=0 (signed) / skill3 / show-picker, in that order. Match.
- 0x55bff8 InfoPanelTraitRows — 9 rows gated by word11; masks/text-ids/advance:
  (w22&0xF)4657 adv, (w22&0xF0)4658 adv, (w45&0xF)4659 adv, (w45&0x30)4660 no-adv,
  (w11&0x1C000)4661 adv, (w23&0xE)4662 adv, (w23&0x70)4663 no-adv, (w23&0x180)4664 adv,
  (w47&0x1E)4665 no-adv. Byte-exact to the v90/++v90 source shape.
- 0x55a9f8 CityTowerPennantPos — corner interpolation r = corner*scale*delta + base.
- 0x5441d0 MapView radio tables — Y {88,166,218,270,354,406,458,536}, sprite
  {1334,1338,1340,1337,1335,1339,1336,1341}. Match (8 AddToWindow calls @432,Y).
- MapViewSortMarkersByScreenY — selection sort, compare +3 screenY `<`, swap 6-dword
  record. Match.
- MapViewScrollDelta — up/down -> +/-4 in v121, left/right -> +/-4 in v123; values and
  up/down=vertical, left/right=horizontal pairing preserved (Window_Scroll(v123,v121)
  arg-order is the deferred shell).
- MapViewClampFocus — x clamp [0, mapW-512], y clamp [0, mapH-360]. Match.
- 0x4ba614 HudShadowSelection (LABEL_9 loop) — prev[i]=cur[i] shadow + selected count.
  MODELING NOTE: the reimpl's single `cur[]` collapses the binary's two inputs
  (record-ptr presence dword_12CEA94 AND selection byte byte_12CEA98); the count gates
  on the selection byte only. The shadow store is unconditional in both. Documented in
  the header as a logical bool table (selection implies a record). Faithful for the
  modeled subset.

### hud_binder.cpp / hud_render.cpp
- No own 0xADDR provenance: orchestration/compositor over reconstructed gui::/render::
  leaves (PlayerBar_*, MenuFillRect, DrawText, MapView_ComputeMarkerScreenPos,
  MoneyFormatWithSeparators). HudBarFillPixels truncation matches the (int) scaled fill.

### wire_hud_bridge.cpp / session_hud.cpp / session_panels.cpp / session_select.cpp
- session_select.cpp HAS provenance, all VERIFIED-1:1:
  * 0x4b94d8 Selection_ClearAll — zero 11BC270/631740, sweep byte_12CE880[536..411648]
    (768), zero 6317B0, return 411648.
  * 0x4b950c Selection_CommitContact — full control flow + gate
    (67221C && !62D4E8 && (cursorX>>16) in (63CC4C,63CC54) && (cursorY>>16) in
    (63CC50,63CC58) && 75BF08==-1 && 62D31C==-1 || v0; all signed sar/compare),
    quickjump resolve, hover latch, tp_TUER/type==6/door gate, highlight pulse, worker
    (v6&8) path, anchor stores, type==29 clear. Every store cites its addr; matches.
  * 0x4bc280 tail Selection_UpdateStatusTextLatch — v5=631744?:(631748?:0); if changed
    -> ComputeSelectionFlags(63CC5C,v5,0,63174C), StatusText_Reset, latch stores. Match.

## Rule-13 wiring (verified)
- HUD is driven PER-FRAME by the live SDL frame loop: src/play/sdl_session.cpp
  instantiates `SessionHud hud`, fills `SessionHud::Inputs` each frame, and calls
  `hud.Render(...)` inside the render loop (sdl_session.cpp ~L1253 / ~L1313).
- session_hud.cpp installs the REAL HUD sprite bridge (InstallRealHudBridge ->
  render::ShapeShowFromBank 0x5d861c) and composites via reconstructed leaves.
- Panels open on the REAL dispatch: SessionPanels::RunTooltip -> gui::Tooltip_Dispatch
  (@0x4f7424); SessionPanels::RunPanel -> gui::InfoPanel_Update (@0x4b84c0); both fed by
  `hi.panels` in the live loop. session_select drives Selection_CommitContact (0x4b950c)
  on pick.

## Boundaries (rule 8 honored, not faked)
- VIBE_Shape_ConvertRgbTo16 @0x5d7c0c (24bpp->16bpp shape decode) — render leaf, not in
  this chunk; documented gap; session_hud uses DefaultHudSpriteBank fallback.
- Rich-string renderer @0x59d6e8 — gui leaf; session_panels uses a documented
  plain-glyph projection, not invented content.
- Coupled engine leaves (amt slot table, entity bytes, command queue, voice bank,
  mesh records) surfaced as inert/default hooks per the pure-logic units' design.

## Tests
Built only the owned test targets (never touched build/). All green:
ui_recon4_hud_surface_test, ui_recon5_panels_test, ui_recon3_widget_test,
hud_recon_credit_panel_test, hud_binder_test(+e2e), hud_render_test(+i/e2e),
session_select_test, session_hud_test(+e2e), session_panels_test,
wire_hud_bridge_test(+i/e2e) — 16/16 pass (GUILD_GAME_DIR set; asset-guarded suites
skip cleanly when absent).

## Counts
- Functions diffed: ~22 provenance'd (+ wiring layers with no own addr).
- FIXED: 2 (0x4243d8 colour packing src+golden; 0x51b108 sum 32-bit wrap).
- VERIFIED-1:1: ~20.
- Documented modeling notes: 2 (gift float-vs-double cap compare; HudShadowSelection
  single-input collapse).
- Boundaries: 3 (named gaps / coupled leaves).
- No git commands run. build/ dir untouched (test targets only).
