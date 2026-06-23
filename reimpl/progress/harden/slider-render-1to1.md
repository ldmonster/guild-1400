# Slider render 1:1 — `VIBE_Entity_InteractionLogic @0x41078c` (horizontal branch)

Status: **implemented** (shape + text elements) with one documented **RULE-8 GAP**
(the gold fill, a DirectDraw surface→surface Blt, is not expressible through the
current shape/text seam). 5 unit tests, 43 checks, green.

## Files
- `src/gui/slider_render_1to1.cpp` — `guild::gui::RenderHSlider(IGuiSurface&, const HSliderWidget&)`
- `tests/unit/slider_render_1to1_test.cpp` — recording-fake `IGuiSurface` golden vectors
- declarations: `src/gui/gui_render_iface.h` (unchanged, as required)

## Scope
The horizontal `(v149 & 2)` path of `0x41078c`, the path the Game-options sliders
take (`VIBE_Menu_RunOptionsGame @0x56cc44`: flags `0x882` value sliders and `0x82`
discrete pickers). The vertical `(v149 & 1)` branch and the `0x400`/`0x10` fill
sub-modes (not taken by options sliders) are intentionally out of scope.

Flag decode for the canonical `0x882` = bits `0x2 | 0x80 | 0x800`:
`&2` horiz=1, `&8`=0, `&0x10`=0, `&0x20`=0, `&0x40`=0, `&0x400`=0, `&4`=0, `&0x80`=1, `&0x800`=1.

## Recovered coordinate formulas (raw-disasm addresses)
Widget record (esi), 740-byte stride, base `dword_69FFB4`. `node.x = *(i32*)(esi+0x0E)>>16`
(== sign-extended low word of `esi+0x10`); `node.y = *(i32*)(esi+0x10)>>16`;
`node.w = *(i32*)(esi+0x12)>>16`. Shapes via `VIBE_Coord_Transform @0x5d8b00`
(fields +6 width, +0x0A height): shape0 leftcap(var_28/v143), shape1 track(var_2C/v142),
shape3 thumb(var_30/v141), shape6 +/- btn(var_5C/v130).

| element | formula | addr |
|---|---|---|
| `v5` (px/unit) | `(double)range / (double)(max-min)` | 0x41089e |
| `filled` (var_34) | `(value-min)*v5`; if `0<filled<1` → `1.0` | 0x4108c8 / 0x4108f8 |
| `target` (var_48) | `(target-min)*v5`; if `0<target<1` → `1.0` | 0x4108d8 / 0x41091e |
| caps/hover Y (`capY`) | `node.y + (H3-H0)/2` | 0x410bd4 |
| leftcap (shape 0, Basic) | `(node.x, capY)` | 0x410bd4..0x410c12 |
| left hover (shape 6, Advanced) | `(node.x, capY)` if hoverLeft | 0x410c17..0x410c59 |
| rightcap (shape 5, Basic) | `(node.x + W0 + range, capY)` | 0x410c5e..0x410cb4 |
| rightcap fallback (shape 0) | same pos, only if shape 5 returns 0 | 0x410d12 |
| right hover (shape 7, Advanced) | `(node.x + node.w - W6, capY)` if hoverRight | 0x410d17..0x410d78 |
| caps recolor (shape 0/5, Velocity) | same caps pos, only if `node+0x4C != 0` | 0x410d7d..0x410e83 |
| thumb (shape 3, Basic) | `(ConvertX(node.x + filled + W0 - W3/2), node.y)` | 0x410e9a..0x410f0d |
| value/option text (mode 8) | x = `ConvertX(node.x + filled + W0 - W3/2)`; y = `(H3/2 + node.y) - dword_69FFB0/2 + 1` | option 0x410f1f..0x410ff5 / numeric 0x4117ad..0x4118c3 |
| min "%i" (Property_Set) | x = `node.x - 4 - TextWidth(min)`; y = `node.y - v38` | 0x411121..0x41115f |
| max "%i" (Property_Set) | x = `node.x + node.w + 4`; y = `node.y - v38` | 0x4111a5..0x4111c7 |
| `v38` | `(dword_69FFB0 - H0)/2 - 1` | 0x4110ff..0x41111e |

- `W0 = *(u16)(leftcap+6)`, `H0 = *(u16)(leftcap+0x0A)`, `W3/H3` = thumb, `W6 = *(u16)(shape6+6)`.
- `ConvertX` = truncate toward zero (`util::ConvertX`, `VIBE_Coord_ConvertX @0x5c6b08`).
- `(H3-H0)/2`, `W3/2`, `v38` halving reproduced as `(d-(d>>31))>>1` (the emitted `sar;sub;sar`).
- Text X for the thumb is identical in the option and numeric branches (both
  `ConvertX(node.x + filled + W0 - W3/2)`); both call the font pen with mode 8 (advanced).
- The min/max numbers are in the `(v149 & 4)` branch, which is CLEAR for `0x82/0x882`,
  so they are correctly skipped for the actual options sliders (matches
  `if ((v149 & 4)==0) goto LABEL_49` at 0x41101c). Implemented + tested with an opt-in
  flag-4 vector.

## Seam mapping (rule-3 pixel boundary)
- `VIBE_Animation_Basic @0x5d85b8` → `BlitShape(kNormal)` (returns drawn flag, used by the right-cap fallback)
- `VIBE_Animation_Advanced @0x5d89bc` → `BlitShape(kAdvanced)` (hover btns / option text mode 8)
- `VIBE_Velocity_Apply @0x5d883c` → `BlitShape(kVelocity)` (caps recolor)
- `VIBE_Coord_Push @0x5d8ae8` → `SetClip`
- `VIBE_Property_Set @0x4159dc` / `VIBE_Animation_Apply @0x415b78` → `DrawText`
- `VIBE_Property_Get @0x4152cc` → `TextWidth`
- `VIBE_Coord_Transform @0x5d8b00` → `ShapeSize`

## RULE-8 GAP — the gold fill (NOT emitted, documented)
The three fill draws — remainder `(range-filled)>0 && !(flags&8)` (0x410942..0x410a25),
target band `(flags&0x10) && target>0` (0x410a38..0x410abf), filled `filled>0`
(0x410ac4..0x410b4a) — all call `VIBE_Result_Handler_Interaction @0x42395c`, a thin
thunk to `VIBE_Result_Finalize @0x423648` (verified: it forwards a1..a8 and appends
a9=0).

`Result_Finalize` is **not a shape blit**. It is a DirectDraw surface→surface Blt:
it clips a rectangle and either invokes `IDirectDrawSurface::Blt`
(`(**(*(a8+0x20))+0x14)(...)`) or, on the software fallback, `qmemcpy`s scanlines
from the **fill-source surface** `a5 = *(esi+0x94)` (node+0x94) into the destination
surface `a8 = a2`. Call-site register mapping (raw disasm 0x41097e..0x410a25): the
src surface pointer is passed in edx (`push edx` at 0x410a01 → a5), preserved across
`ConvertX`; a8 = the context/surface a2 (`push edi` at 0x4109a7).

The `IGuiSurface` seam exposes only `BlitShape`(by shapeNr)/`DrawText`/`SetClip`/
`ShapeSize` — there is **no** surface→surface Blt of an arbitrary source surface, so
the gold fill cannot be reproduced faithfully through it. The shape-1 **tiling** the
brief mentions is the `(flags & 0x400)` sub-mode (loc_411415..0x4116e2, `Animation_Basic`
over shape 1) which the options sliders (`0x400` clear) **do not take**; emitting a
shape-1 tile in this path would invent behavior the binary never performs for these
widgets. Per rule 8 the fill is therefore left out and documented rather than faked.

Recovered geometry for an eventual surface-blit seam extension (carrying node+0x94):
- filled span: dst `(node.x + W0,               capY, w=(int)filled,          h=track H)`
- remainder:   dst `(node.x + W0 + (int)filled, capY, w=(int)(range-filled+1))`

### Other small gaps (documented, not faked)
- **Caps recolor (`node+0x4C`)**: `HSliderWidget` has no field for node+0x4C, so the
  optional Velocity recolor/shadow pass over the caps is documented and not driven
  (absence == node+0x4C == 0, the common case). Code path is described in-source.
- **Text Y line-height term** `- dword_69FFB0/2 + 1` (and `v38`): `dword_69FFB0` is a
  runtime font line-height global the seam does not expose (the seam's `DrawText` *is*
  the font pen, which owns line-height). The recoverable anchor `(H3/2 + node.y)` for
  text and `node.y` for min/max is emitted; the line-height vertical-centering term is
  font-runtime state, documented in-source. (Text X — the load-bearing position — is exact.)
- **Text color**: the engine sets the pen color via `State_Finalize(font) @0x41e57c`
  (ambient state), not a per-`Property_Set` argument; the seam's `DrawText` takes r,g,b,
  so a neutral `0,0,0` placeholder is passed (color is ambient font state, not a
  call-site constant in the disasm).

## Tests (5, 43 checks, green)
`tests/unit/slider_render_1to1_test.cpp` — recording fake `IGuiSurface` with fixed
shape metrics (leftcap 68x18, track 100x6, thumb 66x19, rightcap 67x18, btns 35x18/34x18):
1. `OptionsValueSlider_882` — x=200,y=100,w=120,range=100,min=0,max=160,value=80,flags=0x882:
   leftcap @200, rightcap @368, thumb @285 (=200+50+68-33) y=100, option text "ON" @285 y=109
   mode advanced, no min/max (flag 4 clear), no hover shapes.
2. `RightCapFallbackToShape0` — shape 5 absent → shape 0 redrawn at the rightcap x (368).
3. `HoverButtons` — shape 6 @200 advanced, shape 7 @ x+w-W6=285 advanced; numeric text "80".
4. `MinMaxNumbers` — flags 0x2|0x4|0x8|0x20: min "0" @ x-4-width=190, max "160" @ x+w+4=324; no caps/thumb.
5. `FilledSnapToOne` — filled 0.01 snaps to 1.0 → thumb @236 (=200+1+68-33).

Build/run: `cmake --build build --target slider_render_1to1_test -j && ./build/slider_render_1to1_test`.

## Wiring (rule 13)
`RenderHSlider` is the implementation of the `gui_render_iface.h` declaration — it is
bound to its declaration in the shared seam header. The seam itself
(`IGuiSurface`/`GuiSurface` in `gui_surface_render.*`) is the new options-render
infrastructure and its dispatch into the live options form (via
`VIBE_GameLogic_Interactions @0x4139a8` routing widget type 69 to `0x41078c`) is a
separate, larger harden step owning files outside this task; that routing change is
out of scope here (assigned files only).
