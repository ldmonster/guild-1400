# Colour-key integration — wave 5 (W5-CKEY) — the black-tree-backdrop bug

Status: DONE (end-to-end, verified on real AUGSBURG). Owner agent: W5-CKEY.

## The bug
24-bit foliage / character textures with BLACK backgrounds rendered their black
backdrop OPAQUE (the "black tree backdrop"). The wave-3/4 code wired the colour
key to the WRONG flag bit (`flags & 8`, the `_NM` mip-bias flag) and then
hard-disabled it (`keyed = false`), so no texture was ever keyed.

## The 1:1 mechanism (decompile evidence, gilde.exe, imagebase 0x400000)

### What flag marks a colour-keyed texture — record +104 bit 2 (0x04)
`VIBE_Texture_LoadByName @0x5dad52` sets record +104 bit 2 on a FRESH load when
`!dword_140809C && bmpBpp > 8` — i.e. a >8bpp (24-bit) source with the global
gate off. `get_global_value dword_140809C == 0` (default) → the gate is OFF at
runtime → **every 24-bit source is colour-keyed**. 8-bit sources never set it
(bpp not > 8). bit 3 (0x08) is the `_NM` mip flag and only selects the mip-bias
arg of `VIBE_Render_LoadAndStretchTexture @0x5dea50` (consumer `@0x5db2f4`) — NOT
a colour key. `render::kTexFlagColourKey = 0x04`, `render::TextureIsColourKeyed`.

### What colour is the key — BLACK (0,0,0)
`VIBE_Render_LoadAndStretchTexture @0x5dea50`:
- The transparent-surface descriptor (record +104 bit 2 → `&unk_14080C4`,
  selected in `VIBE_Texture_UploadToSurface @0x5db319`) does a `DDBLT_KEYSRC`
  blit (`&unk_1000000`, `0x5df086`) keyed on `v111 = v85[0]|(v84<<8)|(v83<<16)`
  (`0x5df40a`) — i.e. **palette entry 0's RGB**.
- The 768-byte palette buffer (`v83/v84/v85…`) is **memset to 0** before
  `VIBE_Bmp_LoadBuffer`, and the **24-bit branch** of `VIBE_Bmp_LoadBuffer
  @0x5f0ce4` (v94 = `bpp==24 && !v76`, flags v31=9) takes the direct-RGB
  passthrough and **never builds a palette** (`VIBE_Quant_BuildPalette` runs only
  on the `!v94` 8-bit arm) → `pal[0]` stays 0 → **key = (0,0,0) = black**.

So the engine's OBSERVABLE transparency for a 24-bit colour-keyed texture is:
**every pixel whose colour is exactly (0,0,0) is transparent.**

### The software masked span — keys on INDEX 0
`byte_649D70 == 0` (default) → `VIBE_Texture_UploadToSurface @0x5db234` takes the
SOFTWARE arm (`VIBE_Texture_LoadSoftPalettize @0x5da34c`). The software masked
span `VIBE_Raster_FillSpanTexturedMasked @0x5F721A` keys on **source index 0**
(`test dl,dl / jz`). `LoadSoftPalettize` compacts the palette by usage and does
NOT reserve index 0 for the key; the octree quantizer
`VIBE_Quant_TreeCollectPalette @0x603180` (DFS child bit 7→0) places the all-zero
(black) leaf at a HIGH index, and biases pure (0,0,0) toward its bucket centre
(~(4,4,4) → 565 0x0020). So "skip index 0" does NOT coincide with black, and an
exact-565-black key would never match the quantized texel.

### The chosen 1:1 path — option (a): reserve black at index 0
Because (i) the engine's observable key is "source pixel == (0,0,0) → transparent"
(the DDraw KEYSRC on pal[0]==black) and (ii) the software masked span keys on
**index 0**, the faithful realisation is to make "source pixel was black" ==
"index 0": at palettize time, for a >8bpp (colour-keyed) source whose backdrop
contains exact (0,0,0), **reserve palette index 0 for black** — force every
source-(0,0,0) pixel to index 0, relocate the colour previously at 0 to the black
cell's old index (a label swap, no other pixel changes colour), and pin index 0
to exact (0,0,0). The masked span's original **index-0** rule then reproduces the
DDraw source-black key EXACTLY. Sources with no black backdrop are left untouched
(no spurious keying). This is `render::PalettizeDecodedBmp` (texture_palettize.cpp).

The masked span ALSO carries a resolved-value colour-key mode (option b) for
completeness — `SpanTexParams::useColorKey` / `colorKey565`; the bind sites pass
`colorKey565 = 0` and, with index 0 pinned to black, both rules agree. The
DEFAULT masked-span rule (index 0) is byte-identical to the binary for every
existing caller/test.

## What changed (files)
- `src/render/texture.{h,cpp}` — `ColourKeyEnabled()`/`SetColourKeyEnabled(bool)`
  (default TRUE = faithful; diagnostic toggle for the before/after e2e only; the
  engine has no such state). (`kTexFlagColourKey`/`TextureIsColourKeyed` already
  present from W5-TEX.)
- `src/render/raster.{h,cpp}` — `FillSpanTexturedMasked` gains the OPTIONAL
  resolved-value colour-key mode (`SpanTexParams::useColorKey`/`colorKey565`);
  the default index-0 rule is unchanged (byte-identical).
- `src/render/raster_textured.{h,cpp}` — `RasterizeTexturedTriangleRgbzMasked`
  gains `colorKey565` (< 0 = original index-0 rule; >= 0 = resolved-value key),
  threaded into `SpanTexParams`.
- `src/render/texture_palettize.cpp` — `PalettizeDecodedBmp` reserves index 0 for
  black on a >8bpp source with an exact-(0,0,0) backdrop (option a; the producer
  of the 1:1 transparency).
- `src/play/real_texture_source.cpp` — unchanged behaviour; the palettize wiring
  point already calls `PalettizeDecodedBmp` for every 24-bit source on decode.
- `src/play/universe_render.cpp` — PRODUCER: sets `kTexFlagColourKey` on a >8bpp
  source's record. BIND SITE: `keyed = ColourKeyEnabled() &&
  TextureIsColourKeyed(*bt->tex)` (was `false`); routes through the masked variant
  with `blackKey565 = 0`. Fixed the wrong `flags & 8` comment.
- `src/play/city_view3d.cpp` — same producer flag + bind-site change.

## Tests
- `tests/unit/render_colourkey_wave5_test.cpp` — 57 checks, all pass:
  * flag is bit 2 (not bit 3); producer rule (bit 2 only for bpp>8);
  * masked span default index-0 rule (byte-identical); plain span never skips;
  * resolved-value colour-key skips resolved-black at a NON-zero index;
  * colour key is light-row invariant (black ramp = black at every row);
  * RGBZ keyed triangle punches the black backdrop, opaque variant does not;
  * key-by-value (not index-0 fallback);
  * `PalettizeDecodedBmp` reserves black at index 0 for a black backdrop, and
    leaves a no-black source's quantized indices untouched.
- `tests/e2e/colourkey_augsburg_e2e_test.cpp` — GUARDED real-AUGSBURG e2e (skips
  cleanly without `GUILD_GAME_DIR`): renders a real 24-bit colour-keyed member
  through `UniverseFrameDriver` with the key OFF then ON over a magenta clear
  (so frame-black == texel-black) and asserts the black-backdrop pixel count
  drops materially. Dumps `/tmp/guild_ckey_before.ppm` + `/tmp/guild_ckey.ppm`.

## Before/after (real AUGSBURG, member `_DYNAMIC/Character/Sensenmann.bgf`)
black backdrop pixels: **BEFORE (key off) = 1543 → AFTER (key on) = 0 (100% drop)**.
Diagnostic probe over Textures.BIN confirmed every >8bpp member sets the flag and
that source-(0,0,0) pixels quantize to (4,4,4)→565 0x0020 (the reason exact-565
keying alone fails and index-0 reservation is required).

## Faithful verdict on "holes in opaque 24-bit art"
The reservation is keyed STRICTLY on the SOURCE pixel being exactly (0,0,0) — the
same exact key the DDraw path uses. A genuinely-dark-but-not-black texel
((1,1,1), (4,4,4), …) is NOT remapped and NOT keyed, so opaque 24-bit art that
merely contains dark colours is not punched. The before/after frame confirms ONLY
exact-black dropped (1543→0) while the character's non-black geometry stayed.
This matches the binary (an exact (0,0,0) colour-key), not a broad "dark =
transparent" heuristic.
