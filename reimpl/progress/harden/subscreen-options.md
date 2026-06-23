# Sub-screen: OPTIONS (Game / Gfx / Sfx) — 1:1 VIEW reconstruction

Status: DONE. The native options pages now render the REAL game view (the
`_OPTIONEN_PIC` background, FRM2-geometry windows, `_TOOL_TIP_BIG` parchment,
localized `_OPTIONEN_*` labels, gold `_SLIDER_GOLD_WAAGERECHT` tracks with the
engine's value->thumb mapping, the centered title, and the `_BUTTON_RED`
OK/Cancel row) instead of the previous guessed flat dark-blue panel.

File: `src/play/sdl_options_screen.cpp` (render only). Header unchanged.

## Disassembled (IDA MCP, module gilde.exe, imagebase 0x400000)

- `0x56c21c` VIBE_Menu_RunOptionsGfx — title rich-string `0x1867`; 9 children in
  build order; row bindings + the inverted gamma confirmed (matches the pinned
  OptionsRowsFor table).
- `0x56c808` VIBE_Menu_RunOptionsSfx — title rich-string `0x1868`; 4 volume
  sliders (range 127) + msx_freq cycle (`_OPTIONEN_STUFEN_FREQ`, 5 lines via
  AppendWideLines).
- `0x56cc44` VIBE_Menu_RunOptionsGame — title rich-string `0x1866` (Game).
- `0x4208ac` VIBE_Scrollbar_DragThumb — the value->pixel mapping:
  field `+32` (thumb pixel x) `= 4 * (value - min) / step`.
- `0x420270` VIBE_Scrollbar_SetThumbPosition — the inverse (drag->value) mapping;
  same `4 / step` quantum.
- `0x41df08` VIBE_Slider_ComputeStep — the integer step table by range
  (1/10/50/100/.../10000). Reproduced 1:1 in `SliderComputeStep`.
- `0x41dfec` VIBE_Object_SetValueOrText — confirms the 25-step clamp
  (`v19/v15 > 25` => thumb travel max 100px), driving the slider track width.

Title resolution: the originals draw a rich-string id; the textbin headline keys
`_OPTIONEN_UEBERSCHRIFT+0/+1/+2` resolve to the localized titles (verified
on-screen: "ПАРАМЕТРЫ ЗВУКА" for Sfx). English fallback when absent.

## What changed (render)

- Background: `_OPTIONEN_PIC` (`_1024`/`_1152` by framebuffer width) full-screen,
  NOT `_MENUE_BACKGROUND`.
- Geometry: real FRM2 window x/y/w/h per page (WIN0 outer, WIN1 body); sliders at
  x=208 relative to WIN1 at the per-page y-lists (Gfx/Sfx/Game). NATIVE size,
  CENTER-TRANSLATE only (`ox=(fbW-800)/2, oy=(fbH-600)/2`) — never scaled.
- Parchment: `_TOOL_TIP_BIG` shape0 (475x301) in the body window + the two
  `_TOOL_TIP_BIG+1` trim sprites at the form's type-5 positions.
- Rows: real `_OPTIONEN_GFX/SFX/GAME+N` label (real `_FONT`, CP1251) + gold
  `_SLIDER_GOLD_WAAGERECHT` track (3-slice fill + thumb at
  `4*(value-min)/ComputeStep(range)`, 0..100px) + value text on the right.
- Title: centered real-`_FONT` headline (resolved `_OPTIONEN_UEBERSCHRIFT+N`).
- OK/Cancel: `_BUTTON_RED` 3-slice buttons, labels `_OPTIONEN_BUTTONS+0/+1`, in
  the outer panel's bottom button band (form window 3 / Hud_BuildButtonRow).
- Asset-less fallback: flat panel + slider lines so headless tests still pass.
- Debug aid: env-gated one-shot BMP dump via `GUILD_OPTIONS_DUMP=path`.

## Preserved exactly (NOT changed)

- `OptionsRowsFor` value/range/step/hidden table (pinned by
  `tests/unit/oneone_screens_wave14_test.cpp` — still GREEN).
- `RowsToConfig` save-back, `SaveSettings` persistence, the apply sinks
  (`applyVolumeSettings`/`applyGfxSettings`/`applyCameraScroll`), the two-step
  Gfx resolution path, and the `OptionsResult` contract.
- Input semantics: ESC = cancel (discard), OK = apply, Cancel button = discard,
  value-actuation-on-click. The hit-test rects were re-laid to the new view
  geometry (slider track = the actuation area; OK/Cancel rects derived from WIN0).

## Tests

- `tests/unit/sdl_options_screen_test.cpp` — 118 checks, 0 failures. Geometry
  mirror updated to the new FRM2 layout; added `CancelButtonDiscardsLikeEsc`.
- `tests/integration/sdl_options_screen_itest.cpp` — 22 checks, 0 failures.
- `tests/e2e/sdl_options_screen_e2e_test.cpp` — 18 checks, 0 failures (real
  assets present): Sfx page toggles+persists the real INI value over the real
  `_OPTIONEN_PIC` background; Game page renders 478706 lit px over the real bg.
- `oneone_screens_wave14_test` — Passed (OptionsRowsFor semantics intact).

Build: only the three options targets + the wave14 target were built (per the
no-full-tree rule); all build clean (pre-existing warnings only).

---

## UPDATE — bool "square switch" for Game options (user: "make bool square switch... integrate to bool options")

Re-disassembled how the Game-options rows are built (`Menu_RunOptionsGame @0x56cc44`):
- `VIBE_Widget_SetScrollLimit @0x41207c` is mislabeled — it sets widget `+132` = the
  slider FLAGS. Value sliders get `0x882`; the bool/cycle pickers get `0x82` (the
  `0x800` "continuous track" bit cleared) + `AppendWideLines(2, _OPTIONEN_STUFEN_AN_AUS)`
  (Вкл/Выкл). So bools are NOT gold sliders.
- Two widget draws: `VIBE_Widget_DrawScrollBar @0x4121c4` (continuous slider — uses
  `_SLIDER_GOLD_WAAGERECHT` shapes 0,1,2 normal / 3,4,5 pressed via Coord_Transform) and
  `VIBE_Widget_DrawCheckbox @0x4137bc` (the discrete on/off "square switch").
- The slider bank `_SLIDER_GOLD_WAAGERECHT` has 8 shapes; the continuous slider consumes
  0–5, leaving shapes **6 (35×18) = OFF** and **7 (34×18) = ON** — the square switch states.
  (No dedicated _CHECKBOX gfx exists; confirmed by enumerating gilde.gfx.)

Implemented:
- `MenuAssets::BoolSwitch(bool on)` (menu_assets.h) → `_SLIDER_GOLD_WAAGERECHT` shape 7/6.
- `sdl_options_screen.cpp`: rows with `OptionKind::kToggle` now render the square switch
  (native size, no resolution scale) at the control column instead of a gold slider+thumb;
  click toggles the value (ActuateValue) → switch flips 6↔7. Continuous (kStep) rows keep
  the gold slider; asset-less build draws a square checkbox fallback.
- Verified over real assets (Game page dump): the 4 speed/mouse/scroll/camera rows are gold
  sliders; the 6 bool rows (cursor text / building info / help events / hints / panel help /
  invert-mouse[hidden]) are gold square switches.

Suite 1553/1553 green. (Cycle rows like panel_mode are also flags-0x82 discrete pickers
that the original renders as option text — left as a slider for now; the user's request was
the bool square switch.)

---

## CORRECTION — bool is a 2-position slider, NOT a checkbox (shapes 6/7 are +/- buttons)

My previous "square switch = shapes 6/7" was WRONG (those are the slider's +/- end
buttons). Disassembled the real widget draw `VIBE_Entity_InteractionLogic @0x41078c`
(the type-69 'E' slider, reached from the GUI dispatch `VIBE_GameLogic_Interactions
@0x4139a8`; `DrawCheckbox @0x4137bc` is actually the hover-tooltip text, not the toggle):

`_SLIDER_GOLD_WAAGERECHT` shapes, via `Coord_Transform(gfx, n)`:
- shape 0 = LEFT end cap (`Animation_Basic ...,0`), shape 5 = RIGHT end cap (`...,5`)
- shape 6 / 7 = the left/right **+/- button hover-pressed overlays** (`Animation_Advanced
  ...,6 / ...,7`) — what I had mistaken for OFF/ON
- shape 1 = track fill (100x6), shape 3 = the thumb/knob (66x19), placed at
  `(value-min)*range/(max-min)`
- discrete pickers (option list at widget+216 from `AppendWideLines`) draw the current
  OPTION TEXT at the thumb; value sliders draw `%i` / `%i%%`. Text is drawn ON the thumb.

So a **bool is the SAME gold slider with the thumb snapped to 2 positions** (off=left,
on=right) and the localized `_OPTIONEN_STUFEN_AN_AUS` ("Выкл"/"Вкл") drawn on the thumb —
there is no separate checkbox graphic.

Fixed `sdl_options_screen.cpp`: removed the bogus shapes-6/7 toggle; ALL option rows now
render the gold slider with end caps (0/5) + track (1) + thumb (3) at the value position
and the value/option text centred on the thumb; bools snap off/on and show AN_AUS. Removed
`MenuAssets::BoolSwitch`; added `SliderCapLeft/Right/Track/Thumb` accessors with the
@0x41078c shape provenance. Suite 1553/1553.

---

## TRUE 1:1 — the real engine GUI render pipeline (user: "reimplement completely, ALL THE SAME")

The native slider approximation is replaced by a 1:1 reconstruction of the engine's
retained-mode GUI render-to-surface pipeline, reconstructed from RAW X86 (Hex-Rays dropped
the __usercall FPU coord args; the raw FPU sequences ARE the source of record).

New modules (both built into the lib, tested):
- `src/gui/gui_surface_render.{h,cpp}` — `GuiSurface : IGuiSurface`, the SHAPBANK
  shape-blit-to-surface primitive (rule-3 pixel boundary). 1:1 with `Animation_Basic
  @0x5d85b8` (kNormal), `Animation_Advanced @0x5d89bc` (kAdvanced = per-channel saturating
  +0x30 brighten of the DEST, via Shape_BuildLightTable @0x5d49a0), `Velocity_Apply @0x5d883c`
  (kVelocity = 50% darken), `Coord_Push @0x5d8ae8` clip, `Coord_Transform @0x5d8b00` shape
  lookup (decode reuses GfxArchive::DecodeShape), text via the 1:1 MenuFont (Property_Set/Get).
  Recolor transforms fully recovered from the binary (no invented tints). 55-check test.
- `src/gui/gui_render_iface.h` + `src/gui/slider_render_1to1.cpp` — `RenderHSlider`, the
  horizontal path of `VIBE_Entity_InteractionLogic @0x41078c` (the 1466-insn FPU slider draw),
  reconstructed instruction-by-instruction: filled/target span = (value-min)*range/(max-min)
  snapped to 1px; FILL = the gold (shape 1) band over the filled span + empty (shape 2) band
  over the remainder (these ARE the bands `Widget_CreateSlider @0x410180` tiles into the
  fill-source surface node+0x94, which `Result_Finalize @0x423648` Blts — same pixels);
  left cap shape 0 @ (x, y+(H3-H0)/2); right cap shape 5 @ (x+W0+range,..) with shape-0
  fallback; +/- hover shapes 6/7; thumb shape 3 @ trunc(x+filled+W0-W3/2); value/option text
  on the thumb; min/max numbers (flags&4, cleared for the 0x82/0x882 options sliders).
  43-check test.

Wired into `sdl_options_screen.cpp`: every options control is now drawn by
`gui::RenderHSlider` over a `gui::GuiSurface` (gfxId -> `_SLIDER_GOLD_WAAGERECHT`), value
rows (kStep) as flags 0x882 with the number on the thumb, bool/cycle (kToggle/kCycle) as
flags 0x82 with the option text (AN_AUS "Выкл"/"Вкл") on the thumb. Verified over real assets
(Game page dump): gold fill-bars, end caps, thumb at the fill edge, value/option on the thumb.

`form_parse` now exposes the slider `range` byte (record +3868+o) on FormObjectRecord for the
geometry. Remaining (documented, not faked — rule 8): the text vertical-centring term
(-dword_69FFB0/2+1, a runtime font-global the seam doesn't expose; X is exact) and the
optional caps recolor pass (node+0x4C, a visual-only shadow). Suite 1555/1555.

---

## Positions now 1:1 too — driven by the real form geometry

The slider POSITIONS were the last approximation (uniform Layout rows). Extracted the exact
geometry from the real forms via Form_ParseResourceFile (FormObjectRecord now exposes the
slider `range` byte @+3868+o): every options control is a `_SLIDER_GOLD_WAAGERECHT` type-69
object at objX=208 in the body window WIN1, each with its own objY and track length:
- OPTIONS_GAME: WIN1(136,168), range 140, yByRow {8,72,96,120,144,208,232,168,256,280,304}
- OPTIONS_GFX:  WIN1(146,171), range 160, yByRow {16,48,96,160,128,192,240,272,304}
- OPTIONS_SFX:  WIN1(144,168), range 140, yByRow {32,120,160,200,280}
(yByRow is in OptionsRowsFor / GetChildObjectId child order incl. the hidden invert row;
the y values are non-uniform — the real screen groups the sliders, e.g. panel_mode sits at
y=168 between the two groups.) Slider screen pos = center-translate(ox,oy) + WIN1.xy + obj.xy
(PositionAtCoord mode-2 + AddChildWindow). The row label is right-aligned ending 24px left of
the slider at the slider Y (the engine's flags&0x80 label path, 0x41148..0x41115f).

Also guarded Widget_CreateSlider's tile-count divide against tile==0 (only reachable with an
unresolved gfxBase in a headless form parse; behaviour-identical for every real input) so the
form can be parsed without a fault.

Verified over real assets (Game + Sfx dumps): sliders at the real x=208 with the real
non-uniform spacing, right-aligned labels, gold fill-bars + thumb at value, range 140/160.
Suite 1555/1555.

---

## Bool params -> square toggle from the _AUSWAHL asset (user request)

The boolean option rows now render as a SQUARE TOGGLE built from the real gilde.gfx
`_AUSWAHL` record (#1210, the selection/spinner bank): shape 0 = the empty 18x18 square box
(gold-bordered blue), shape 3 = the small filled square drawn centred inside when ON; the
localized on/off caption (`_OPTIONEN_STUFEN_AN_AUS` "Выкл"/"Вкл") sits to its right. (I
scanned gilde.gfx for the toggle asset — `_AUSWAHL` is the square selection box; shapes 1/2
are up/down arrows, 4/5 the −/+ boxes, 6 the "Ok" button.) Value rows (kStep) keep the gold
fill-bar slider (RenderHSlider) and panel-mode (kCycle) keeps the slider picker; only
kToggle rows become the square box. Asset-less builds draw a square outline + fill fallback.
Verified over real assets (Game page): the 5 bool rows show the blue square toggle, the 4
value rows the gold slider. Suite 1555/1555.

---

## Slider text = original + slider interaction (+/- buttons, drag)

Two fixes:
1. **Text on sliders matches the original.** Added `OptionRow::optList` = the textbin
   list each row's `VIBE_Text_AppendWideLines` appends (disasm @0x56cc44/56c21c/56c808):
   cycles show option `value` of their localized list — resolution `_OPTIONEN_STUFEN_RES`
   ("800x600"...), detail/etc `_OPTIONEN_STUFEN` (низкий/средний/высокий), panel-mode
   `_OPTIONEN_STUFEN_PANEL`, music-quality `_OPTIONEN_STUFEN_FREQ`; true on/off bools
   `_OPTIONEN_STUFEN_AN_AUS`; numeric rows (speeds/volumes/gamma) show "%i". The lists are
   resolved once from the textbin and indexed by the row value (re-displayed each frame).
   The square-toggle (bool) is now gated on the AN_AUS list (so the 2-option STUFEN
   `lod_handling` is a slider, not a checkbox).
2. **Interaction.** Every slider now moves by the +/- end-cap buttons and by dragging the
   thumb: left cap = MINUS (-step, clamp), right cap = PLUS (+step, clamp), track click =
   set value by position + begin a drag (held-cursor tracks; Slider_UpdateFromMouse
   @0x420a04). Bool rows toggle on click. Hit-testing + the +/- hover overlays (shapes 6/7)
   are driven from the real form geometry (OptGeomFor), so clicks land exactly on what is
   drawn. The +/- buttons CLAMP (the engine's slider buttons do not wrap).

Tests updated to the new interaction model (click the plus/minus cap to step; clamp not
wrap): sdl_options_screen_test (118 checks) + sdl_options_screen_itest (23). Suite 1555/1555.
