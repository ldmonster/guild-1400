# Hardening sweep — chunk gui_03 (src/gui/ map/menu/options/object/recruit cluster)

MCP-driven 1:1 verification of every provenance'd function in the 22 assigned files
against `gilde.exe` (imagebase 0x400000). Each function decompiled **and** disassembled,
diffed line-for-line; constants/tables confirmed via `get_bytes`/`get_global_value`;
every float->int site checked for ConvertX-truncate vs fistp-round vs (int)-truncate.

Discipline: no git; build/ never recreated; only named test targets built; INDEX.md untouched.

## Summary counts

| Verdict | Count |
|---|---|
| VERIFIED-1:1 | ~48 functions/ranges |
| FIXED (source and/or golden) | 11 |
| BOUNDARY (rule 3-5 tech swap / data-not-in-tree / non-deterministic return) | ~15 |
| NOT-1:1 flagged (rule 8, dedicated task needed) | 1 (markup) |

All assigned test targets GREEN.

---

## FIXED (addr + before/after + evidence)

### mapview.cpp
- **0x543bd0 VIBE_MapView_StepScrollOffset** — original writes two globals
  **unconditionally** at the tail (outside the change-detect `if`):
  `0x543c82 mov dword_12334F0,edx` (clamped X) and `0x543c7c mov dword_12334F4,ecx`
  (clamped Y). Reconstruction omitted them. Added `g_mapLastClampedX/Y` + the two
  tail stores in original Y-then-X order. (`xrefs_to` confirms write-only scratch,
  but it is a real per-step side effect.)

### mapview_markers.cpp
- **0x519448 VIBE_CityMap_CreateCityPointMarker** — missing
  `VIBE_Light_BuildObjectCache @0x51952f` call **between** the flag writes and
  `LoadObjectAnimation @0x51953b`. Added `BuildLightCache()` edge in correct order.
  Flag offsets/order (+529/+530/+535/+536) re-verified 1:1.

### menu.cpp
- **0x56dccc Menu_OptionEnabled** — branch was **inverted**. Binary (0x56de10):
  `if(word_63C740 & 4){ SetEnabled(Load,0); if(!(0x10)) SetEnabled(Save,0); }`
  i.e. network disables **Load unconditionally**, **Save** only when host bit 0x10
  clear. C++ had Load gated on `!0x10` and never disabled Save. Rewrote to match.
  Golden `gui_hud_test.cpp::OptionEnabledFlags` encoded the wrong behavior — fixed
  both source and test.

### menu_frame_leaves.cpp
- **0x41e614 VIBE_Object_SetColor** — return register `al` is the **type byte**
  (`mov al,[edx+18h]`), overwritten only in type==0x40 (`al=16*v4`, v4=56*owner)
  and type==0x41 (`al=LOBYTE(348*owner)`) branches. C++ returned `color & 0xFF`
  always. Rewrote return to compute `al` per branch. (Caller discards eax;
  now bit-exact.)

### message_box.cpp (all 5 dialogs)
- **Palette mask** — decompile `… & 0xC7` is actually `and byte ptr [v],0C7h`
  (0x4ad72b, 0x4acc0b, 0x569a63, 0x4adc93, 0x4ada2c): masks only the **low byte**,
  upper 3 bytes preserved. C++ used `0xC7` (masked whole dword). Fixed to
  `0xFFFFFFC7`. Golden only tested low-byte vectors — added a high-byte vector
  (`0x12A3FF -> 0x12A3C7`).
- **Close-flag / slider** — original sets `dword_631614` mid-frame but does **not**
  break; it finishes the frame body (incl. `SetSliderValue`) and exits on the next
  `RunFrameLoop` re-read. C++ did `close=true; break;` mid-body, skipping the slider
  update on the terminal frame. Restructured all four loop bodies to break at the
  bottom. Added `SliderUpdatesOnClosingFrame` test.
- (Provenance-only) Green header/body render into windows 1/2 (0x4ada4e), not
  window 0 as the comment claimed — comments corrected.

### netfile_run.cpp
- **0x569668 VIBE_Menu_RunFileSelector** — source stripped the file extension via a
  `BareName()` helper for both the row label and the commit. Original does NOT strip:
  the enumerator (0x569530) writes the row-name field at `a3+9` as a raw 2-byte-stride
  copy of `*v6` (extension intact); only the separate full-path buffer at `a3+265`
  is stripped. Selector uses `a3+9` for both label and commit. Removed `BareName`.
  Golden `gui_netfile_run_test.cpp` (`rows[0].name=="BERLIN"`) encoded wrong behavior
  — fixed to `"BERLIN.INI"`/`"AUGSBURG.INI"`.

### radiogroup.cpp
- **0x41dfec Object_SetButtonValue (button branch)** — added the original's
  type-'A'(0x41)/type-'E'(0x45) early-skip (`jnz` at 0x41e02f / 0x41e04a) so the
  value/mirror writes (+36/+40) only happen when type is neither. No-op for real
  radio buttons (always type 'F') but now 1:1 for arbitrary input.

### playerbar.cpp / playerbar.h
- **0x4b11e4 sub-window geometry scrambled** — `AddChildWindow @0x41a598` proto is
  `(x@ax, y@dx, w@cx, h@bx)`. Body call `AddChildWindow(6, 78*i+63, 15, 80)` @0x4b19a5
  ⇒ **x=6, y=78*i+63, w=15, h=80**. Source had `subWinX=15, subWinW=80, subWinH=6`.
  Corrected in playerbar.h. Cascaded golden fixes: `hud_binder_test.cpp` (subWin
  asserts), `hud_render_itest.cpp` (sample point moved on-bar), comment in
  `play/hud_render.h`. **pct float->int VERIFIED**: 0x4b1542 `fmul dbl_61DD00(=100.0)`
  -> ConvertX(0x5c6b08, RC=truncate + frndint) -> fistp = `(int)(ratio*100.0)`.

### recruit_office.cpp (0x55de00 RunRecruitmentOfferWindow) — 3 real bugs
- **RNG desync (load-bearing)** — original ALWAYS draws `RandomModulo(3)` first
  (0x55e241) then overwrites count in the tier>0 arm; the first draw is still
  consumed. Old `OfferBonusRoll` skipped it for tier>0, consuming 1 RNG value vs
  the original's 2 → desynced the shared RNG for everything after. Now always draws.
- **Wrong branch comparand** — roll branch is `tier=(goodType[slot]-5-candRank)/2+1`
  (signed /2 trunc; 0x55e246–0x55e25d), not `candRank`. Corrected.
- **Wrong rank-cost delta bytes** — reads dwords at handler+0xB6/+0xB5 with `sar,18h`
  (>>24) ⇒ `(i8)byte185 - (i8)byte184` (top byte), not byte182-byte181. Mode 2 clamps
  ≥0; mode 4 header un-clamped. Both sites corrected via `OfferRankCostDeltaRaw`.
- Added golden `RecruitOffer.Mode4BribeRngOrderAlwaysDrawsThrice` pinning 2 rnd(3)
  draws + clamped count.

---

## NOT-1:1 — flagged per Rule 8 (dedicated task needed)

### markup_build.cpp — 0x416720 VIBE_Window_ParseMarkupAndBuild
1961-instruction / 298-BB monolith with a **fully inlined markup state machine**
(pen globals dword_69FFA8/dword_69FFAC; inline handlers for
`$R/$L/$B/$Y/$T/$M/$F/$[/$]/$A/$C/$<…`) and 23 callees including
`Object_AddButtonLabel(0x41b598)`, `Object_SetEditText(0x41b75c)`,
`Input_SetIconTextById(0x40fb4c)`, `Window_Resize(0x41a0f8)`,
`Window_NormalizeSpriteWidths(0x416658)`, `Window_LayoutScrollContent(0x41536c)`.

The current ~150-line reconstruction is a **structural approximation**, not a
translation. Confirmed divergences vs the binary:
- Depends on `guild::gui::text::TokenizeMarkup` — no such separable tokenizer exists
  in the original; the parse is inlined into 0x416720.
- The `$t`/`$tt` `Input_AddFieldToWindow` params here (8,0x10 / 16,0x82) do NOT match
  the three real call sites (0x4176ff / 0x417810 / 0x417a25), which compute field
  geometry from pen positions and object metrics (`[obj+4]>>16`, `[obj+2]>>16`) and
  pass different args (window, 1, ebx=0).
- The layout-only tokens (`$R/$L/$B/$Y/$T/$M/$F/$</$=`/literal text/%-codes) are
  stubbed as no-ops; the original mutates pen/columns and blits glyphs.

**Action taken:** did NOT make superficial edits (would violate Rule 8 "no cheap
analogues"). Annotated the source with a precise NOT-1:1 banner listing the gaps.
A faithful 1:1 requires reconstructing the inlined parser at 0x416720 together with
`text/markup.*` (the tokenizer is outside this chunk's file set). **Recommend a
dedicated reconstruction task spanning 0x416720 + the markup tokenizer.**
(Its accessory `g_buttonRedGfx`/`ButtonRedGfx` _BUTTON_RED caching is correct.)

---

## BOUNDARY (legitimate; documented, unchanged)

- **mapview / mapview_markers** — 0x5c8b38 ProjectThroughBoneChain, 0x5b3e30
  AttachToUniverseNode, 0x5c8218 Light_BuildObjectCache, 0x426488
  LoadObjectAnimation, 0x5b7be4 FindByHandle: 3D/render edges via `CityMarkerSink`.
  0x41aa50 ZOrder_RemoveObject return (eax) unused by caller.
- **menu_render.cpp** — 0x423c70 ColorFillRect: the DDraw vtable Blt color-fill is
  the rendering-tech path (Rule 3 Vulkan swap). Clamp/reject logic itself VERIFIED-1:1.
- **menu_frame_leaves.cpp** — InitStateReader type==9 ENTER return leaves `eax` = raw
  widget record address (32-bit pointer); not reproducible/consumed numerically.
  All side effects (clickFlag, selectedWidget, hover 1155/1210) bit-exact.
- **object_add_animated.cpp** — VIBE_GameLogic_Objects (0x412fa0) factory + ApplyAnimScale
  (0x41e388): full ResolveObjectState/Coord_Transform/ShapeAnim clusters modeled as
  inert weak edges (coupled-leaf design). AddAnimatedToWindow itself VERIFIED-1:1.
- **radiogroup.cpp** — early/no-op returns are raw stack-address bytes (ASLR-dependent,
  unobservable); SetEnabled int return unused.
- **message_box.cpp** — Modeless flag-0x04 auto-close reads an **uninitialized
  register** (`cmp ecx,dword_62EB38` @0x4ade0a; unlike its siblings it never computes
  v16+500); trigger non-deterministic in the binary, modeled with start+500.
- **recruit_office.cpp** — object-table side effects (dword_69FFB4[...+92], +20=200,
  56-byte descriptor stride), card objId click-binding written by the engine
  allocator (hook returns void), kind!=6 always-32 return (needs entity +532/+8 not
  in the marker model), byte_6477A1 resource-rate modeled 0. Geometry/timer/RNG/
  control-flow all VERIFIED-1:1.
- **options screens** — Sfx live-preview `*master/100` truncation pairing behind
  `AudioPreviewVolume` (Rule 5 audio). Gfx res-reset camera globals via `ReloadResolution`.

---

## Test targets run (all GREEN)

mapview_test, gui_hud_test, gui_hud2_test, smallleaves_wave22_test, hud_render_test,
gui_hud_e2e_test, gui_hud2_e2e_test, menu_frame_leaves_test, gui_menu_render_test,
gui_main_menu_test (+32 menu-regex, +11 gui_hud-regex), gui_message_box_test(+itest+e2e),
gui_mission_load_run_test, gui_netfile_run_test, newgame_apply_test,
newgame_builders_w13_test, newgame_diff_w15_test, gui_choosecity_run_test,
gui_charcreate_test, sdl_charcreate_screen_test, gui_main_menu_run/wire_test,
gui_options_run_test(+itest+e2e), gui_options_screens_test, sdl_options_screen_test,
object_value_test, object_recon3_add_animated_test, anim_object_test,
object_lifecycle2-10_test, menu_widgets_test, charintro_markup_test,
recruit_office_estate_test, personnel_recruit2_test, person_personnel2_test,
sim_personnel_test, personnel_gui_test, office_law3_test, privilege_law_test,
playerbar/quickchat/pamphlet via gui_hud / hud_binder / hud_render / session_hud /
wire_hud_bridge / hud_menu2.

## Files touched

src/gui/mapview.cpp, mapview.h, mapview_markers.cpp, mapview_markers.h,
menu.cpp, menu.h, menu_frame_leaves.cpp, message_box.cpp, message_box.h,
netfile_run.cpp, radiogroup.cpp, playerbar.cpp, playerbar.h, recruit_office.cpp,
markup_build.cpp (NOT-1:1 banner), src/play/hud_render.h (stale comment);
tests: gui_hud_test.cpp, gui_message_box_test.cpp, gui_netfile_run_test.cpp,
hud_binder_test.cpp, recruit_office_estate_test.cpp, hud_render_itest.cpp.
