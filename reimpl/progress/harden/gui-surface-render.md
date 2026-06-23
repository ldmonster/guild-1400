# GuiSurface — SHAPBANK shape-blit-to-surface primitive (rule-3 2D-GUI boundary)

**Status:** DONE, 1:1, no rule-8 gaps. 8 tests, all green (6 synthetic golden-vector
+ 2 real-asset, the latter guarded on `GUILD_GAME_DIR`). Build target
`gui_surface_render_test`.

## Files
- `src/gui/gui_surface_render.h` — `class GuiSurface : public guild::gui::IGuiSurface`.
- `src/gui/gui_surface_render.cpp` — implementation.
- `tests/unit/gui_surface_render_test.cpp` — tests.

## What it implements
A concrete `IGuiSurface` (the seam `RenderHSlider` in `src/gui/slider_render_1to1.cpp`
already draws against — wiring is closed: `RenderHSlider` is the caller, `GuiSurface`
the renderer). It blits SHAPBANK shapes + bitmap-font text into a 32bpp
`render::Surface`. Shape decode is delegated to the existing depth-2 codec via
`play::MenuAssets::SpriteByName` (which wraps `render::GfxArchive::DecodeShape` and
caches). Text is delegated to the existing 1:1 `play::MenuFont`.

Constructor: `GuiSurface(render::Surface* target, play::MenuAssets& assets)`.
`gfxId` is mapped to a gilde.gfx record NAME via `MapGfxId(id, name)` /
`SetDefaultRecord(name)` (default = `_SLIDER_GOLD_WAAGERECHT`). The options slider
registers its node gfx id -> `_SLIDER_GOLD_WAAGERECHT`.

## Addresses disassembled (reference of record)
| addr | name | finding |
|---|---|---|
| `0x5d8b00` | VIBE_Coord_Transform | shape ptr = `bank + u32@(bank + 4*n + 69)` (same +69 offset already used by MenuFont). |
| `0x5d85b8` | VIBE_Animation_Basic | `if(!bank) return 0; if(n>u16@bank+42) return 0;` else `FrameData_Process(x,y, shapeptr, ebx)`; returns 1. Plain opaque blit (draw-mode byte left at 0). |
| `0x5d89bc` | VIBE_Animation_Advanced | identical, but sets the shape's byte `@shape+0x0D = 3` before the draw and restores it after. |
| `0x5d883c` | VIBE_Velocity_Apply | identical, but sets byte `@shape+0x0D = 2`. |
| `0x5d8ae8` | VIBE_Coord_Push | clip globals: `64A1B4=eax(left)`, `64A1B8=edx(top)`, `64A1C0=ecx(bottom)`, `64A1BC=ebx(right)`. Confirmed from the Entity_InteractionLogic call site @0x41105a (`Coord_Push(0, 0, dword_69FFB8+2>>16, dword_69FFBC>>16)` = full screen). |
| `0x5d781c` | VIBE_FrameData_Process | dispatch on `byte@shape+0x0C` (frame-table selector, NOT the recolor byte) -> FrameTable_Index/Next/Validate/Bounds. X reject: `x>right || x+w<left`. |
| `0x5fbb24` | VIBE_FrameTable_Index | fully-in-bounds RLE blit, plain copy, ignores +0x0D. Y clip: `top<=Y<bottom`. |
| `0x5fbc10` | VIBE_FrameTable_Next | clip-edge RLE blit; `switch(byte@shape+0x0D)` cases 0..5. Per-pixel X clip `left<X<right` (strict). |
| `0x5fc200` | VIBE_FrameTable_Validate | right-edge RLE blit; same `switch(+0x0D)`. |
| `0x5d7420` | VIBE_FrameData_Interpolate | FULL-bitmap blit; same `if(+0x0D)` => mode 2 / mode 3. The clearest reading of the two recolor transforms. |
| `0x5d49a0` | VIBE_Shape_BuildLightTable | builds the mode-3 LUT: for each colour, unpack RGB, add `a1` to each channel saturating at 255, repack. |
| `0x5d4ad4` | VIBE_Shape_InitColorMasks | calls BuildLightTable(**0x30**). So the mode-3 brighten amount is **48**. |

## The three draw modes (1:1)
The draw-mode byte `shape+0x0D` selects a per-pixel transform that reads/WRITES the
**destination** pixel, keyed by the shape's opaque pixels (the shape colour is NOT used
in modes 2/3; only `if(src!=0)`):
- **kNormal (0)** — `dst = src` (opaque copy of the shape colour).
- **kVelocity (2)** — `dst = (dst >> 1) & mask` => 50% darken of the destination (the
  shadow pass). In 16bpp the `& word_1406944`/`& dword_1406930` masks clear the bit that
  would leak across channels after `>>1`; in byte-aligned 32bpp that is exactly per-channel
  `>>1`, so the reconstruction is byte-faithful (no gap).
- **kAdvanced (3)** — `dst = LightTable[dst]`, LightTable = per-channel saturating add of
  0x30. Reconstructed directly from BuildLightTable; the runtime LUT pointer
  (`dword_64A1C4`) is null in the static IDB but its *contents* are fully derivable from
  the builder, so this is a faithful reconstruction, not an analogue.

## Clip rect (1:1)
`SetClip(x0,y0,x1,y1)` == Coord_Push(left,top,right,bottom). The applied per-pixel test
matches FrameTable_Next/Validate: **strict X** (`left < X < right`) and **half-open Y**
(`top <= Y < bottom`), intersected with the surface bounds. `BlitShape` returns false when
the record/shape is absent (the engine's `==0`, used by the slider right-cap fallback).

## Rule-8 gaps
**None.** Both recolor transforms were fully recovered from the binary (the brighten
amount 0x30 from InitColorMasks, the darken from the mask math). No invented tints.

## Tests (8)
Synthetic (no assets; hand-built gilde.gfx with solid-colour RLE shapes):
`NormalBlitCopiesShapeColour`, `VelocityDarkensDestination`,
`AdvancedBrightensDestinationSaturating` (incl. saturation at 255),
`ClipRectStrictXHalfOpenY`, `ShapeSizeReportsMetrics`, `GfxIdMapsToRecordName`.
Real assets (guarded on `GUILD_GAME_DIR`): `RealSliderShapesNonBlankAndClipped`
(blits `_SLIDER_GOLD_WAAGERECHT` shapes 0/1/3/5, asserts non-blank + a clip band that
excludes them + an Advanced draw), `RealDrawTextSmoke` (TextWidth>0, DrawText returns
MeasureWidth, surface non-blank).

Run: `cmake --build build --target gui_surface_render_test -j && ./build/gui_surface_render_test`
=> `55 checks, 0 failures` (with `GUILD_GAME_DIR` pointing at the install). Without it the
two real-asset tests skip cleanly.
