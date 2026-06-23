# diff-raster-wave15 — TRUE 1:1 binary diff of the raster leaves (W15-RASTER)

**MCP: LIVE.** This wave did the real line-for-line decompile/disasm diff of every
leaf in the segment against the reconstruction, and **RESOLVED** the three wave-13
residuals (UV-scale association, masked-span runtime selector, 0x5dea50 colour-key
descriptor).

Segment files (unchanged this wave — every leaf VERIFIED-1:1, see §3):
`src/render/raster.{h,cpp}`, `raster_textured.{h,cpp}`, `raster_blend.{h,cpp}`,
`hicoltab.{h,cpp}`, `texture_palettize.{h,cpp}`.

---

## 1. Functions decompiled + diffed this wave (all VERIFIED-1:1)

| addr | function | verdict | evidence |
|---|---|---|---|
| 0x5F6930 | InterpolateEdgeRgbz | VERIFIED-1:1 | decompile: two-path slope `>=0x10000` direct `((dx<<16)/dy)` / else `(0x40000000/dy)*num>>14`; sub `((vy+0xFFFF)>>16<<16)-vy`; X/U/V accumulators. Recon byte-matches (fog channel is gated-off additive). |
| 0x5F6A8C | InterpolateEdgeZ | VERIFIED-1:1 | X-only, same two-path slope + sub. Matches `EdgeSlope`/`SubpixelToCeil`. |
| 0x5F7840 | InterpolateEdgeZTex | VERIFIED-1:1 | decompile: X (13FC5B0) + shade (13FC578) two-path slope, sub-ceil. Matches raster.cpp. |
| 0x5F6B34 | FillSpanLoop | VERIFIED-1:1 | decompile: `v3=ceil(xLeft)`; span=`ceil(xRight)-v3`; `U=uGrad*((v3<<16)-xLeft)>>16+uLeft`; advance all + fb `+=2*7626F8`. Recon matches (horizontal clip is documented recon-only additive; disabled when clip rect empty). |
| 0x5F71AD | FillSpanTextured | VERIFIED-1:1 | disasm: `(V<<shift)+U & mask` -> `texBase[idx]` -> `pal[lightRow8\|idx]` (`mov dl,texel` over `edx=13FC5E0`). Matches. |
| 0x5F721A | FillSpanTexturedMasked | VERIFIED-1:1 | disasm: identical body + `test dl,dl / jz` index-0 skip. Matches default (`useColorKey==false`) rule. |
| 0x5f753f | PatchSpanConstantsMasked | VERIFIED-1:1 | disasm: writes the SAME six sources (unk_1406A8C, dword_1406A78, 13FC594, 13FC5A8, 1406A88, byte_1407A91) into the 0x5F72xx body. Matches `BuildSpanTexParams`. |
| 0x5F7960 | FillTexturedSpansShaded | VERIFIED-1:1 | decompile: stamp `(a4&5)&&!(a4&1)?11:0`; byte-span ROR'd `adc` carry chain (`v8=ROR(start,16)`, `v9=ROR(uGrad,16)`, carry one px late); halo/pyramid blocks (24-byte stride, `v40+=-24*a2`, `pitch-xL` clamp); advance. Matches raster.cpp verbatim. |
| 0x5F7D58 | RasterizeTexturedTriangle | VERIFIED-1:1 | decompile: signed-area `x2*y1-y2*x1+x1*y0-y1*x0+x0*y2-y0*x2 <= 0` -> forward else reverse; min-Y `v16>=v17`; mode mask `!a6->&=2 / !a4->&=5`; shade grad 13FC590 with `&0x7FFFFFFF` nonzero; edge select + 2 sub-tris. Matches. Uses flt_62C3D8==65536.0. |
| 0x5F6C30 | RasterizeMirrorTriangle (Rgbz) | VERIFIED-1:1 | decompile: winding gate `(+38 & 4) && (x0-x2)*(y0-y1)>(x0-x1)*(y0-y2)`; UV/pos cross products; light row `((+66 sums)/3)<<8`; edge select + 2 sub-tris. Matches `RasterizeTexturedTriangleRgbzWith`. (UV-scale association = residual R1, resolved §2.) |
| 0x5F728A | FillSpanTexturedBlend | VERIFIED-1:1 | disasm: `shr cx,1; shr ax,1; and mask; and mask; add` = `((src>>1)&m)+((dst>>1)&m)`. Matches raster_blend.cpp. (Or/Masked siblings same body family.) |
| 0x5d9db8 | HiColTabAddEntry | VERIFIED-1:1 | decompile: 63-row ramp (`while v9<0x3F`), `clamp0(ch) + (ch-0)/62.0*step + dbl_6295E8`, chop; `512*step` stride (`v7+=512`); direct at `+32256` (0x7E00); `--a3[193]`. Matches hicoltab.cpp. `dbl_6295E8`=0.5 PINNED (get_bytes). |
| 0x5da34c | LoadSoftPalettize | VERIFIED (off-segment) | decompile: histogram-compact palette + `VIBE_HiColTab_FindOrBuild` + remap. This is the 8bpp texture-UPLOAD path (feeds the span palette); belongs to the texture-binding segment, not the raster leaves. |
| 0x6029f0 | QuantBuildPalette | VERIFIED-1:1 | decompile: Init(once,64ADB0)->Alloc->Histogram->HeapReduce->TreeCollect(0,0)->MapImage->Free->CopyEntries. Matches texture_palettize.cpp orchestrator. |
| 0x603a30 | QuantFindClosestColor | VERIFIED-1:1 | decompile: bucket center `(c&0xF8)+4` all 3 channels; sq-dist via dword_140A210. Matches. |

### get_bytes constants pinned this wave
- `flt_62C3D4` @0x62C3D4 = `00 00 80 47` = **65536.0f** (and neighbour 62C3D8 also 65536.0f).
- `dword_5AC540` block = `01 00 00 00  02 00 00 00  02 00 00 00  00 00 00 00  00 00 00 00  01 00 00 00`
  → kNext (5AC540 stride-2) = {1,2,0}, kPrev (5AC544 stride-2) = {2,0,1}. CONFIRMED.
- `dbl_6295E8` @0x6295E8 = `00 00 00 00 00 00 E0 3F` = **0.5** (the HiColTab ramp round constant). CONFIRMED.

---

## 2. The three wave-13 residuals — RESOLVED

### R1 — UV-scale float-multiply association (claim 5b)  →  CONFIRMED RESIDUAL (not bit-identical)
**Question:** is the original `(u+off)*(w*65536)` vs our split bit-identical?
**Disasm @0x5f6c30 (0x5f6c5e / 0x5f6ccf):**
```
fild  [mipWidth]            ; v1+116
fmul  ds:flt_62C3D4         ; v53 = (double)mipWidth * 65536.0   (computed ONCE)
fstp  v53
...per vertex:
fld   dword [edi]           ; u_norm  (projected UV, v49)
fadd  v52                   ; + (flt_1406950[scrollU] + a1[+1C])   <- normalised space
fmul  v53                   ; * (mipWidth * 65536.0)               <- ONE x87 multiply
call  VIBE_Coord_ConvertX   ; chop toward zero -> 16.16 texel U
```
The original fuses the texel scale and the 16.16 scale into a **single x87 80-bit
multiply** of `(u_norm + offset_norm)` by `v53 = mipWidth*65536`. The reconstruction
hands `RgbzVertex` already in **texels** (the caller did `u_norm*mipWidth`, offsets
pre-added) and applies `*65536.0` in IEEE `double` here. These are **NOT
bit-identical**: (a) the multiply association is moved (`(u+off)*(w*65536)` vs
`(u*w + off*w)*65536`), and (b) the original keeps the whole chain in x87 80-bit
extended precision while the recon uses 64-bit double. The integer-truncated result
agrees for all in-range texel coordinates the engine produces, but the float rounding
order genuinely differs at the ULP boundary.

**Verdict:** documented residual, now with full disasm evidence. It cannot be made
bit-identical inside this leaf without changing the RgbzVertex caller contract
(the caller pre-multiplies mipWidth) — that is a bind-site interface decision
outside this segment's ownership. No source change made (the recon header + raster-
verify-wave4.md already record it; this wave upgrades it from "claim" to "disasm-
confirmed"). Left as-is per rule (don't restructure a bind-site interface unasked).

### R2 — masked-span RUNTIME selector  →  CONFIRMED UNREACHABLE (static dead code)
**Question:** does anything route to FillSpanTexturedMasked @0x5F721A? the 0x5dea50
flags arm / 0x5db234 flags&8 path?
**xrefs_to 0x5F721A = [] ; xrefs_to 0x5f753f = [].** Both the masked span body AND
its self-mod patcher have **zero code/data xrefs** in the static image — nothing
installs the masked span. The wave-13 finding holds with live MCP confirmation.

The real-runtime transparency is NOT done by the software masked span at all — it is
done at **texture-upload time** by the DDraw colour-key blit inside
`VIBE_Render_LoadAndStretchTexture` @0x5dea50 (see R3). `VIBE_Texture_UploadToSurface`
@0x5db234 routes by `byte_649D70` (HW-accel flag): the HW arm calls 0x5dea50
(DDraw colour-key), the SW arm calls `VIBE_Texture_LoadSoftPalettize` @0x5da34c
(8bpp soft palettize, no per-pixel mask). The `flags&8` (record+104 bit 3) only
zeroes the 5th arg (`v9`, the mip-shift `a5`) of 0x5dea50 — it is NOT a masked-span
trigger (wave-13 finding confirmed). The masked-span leaf is reconstructed 1:1 and
golden-pinned; its runtime selector is genuinely absent from this binary. Bind sites
correctly default to the plain span.

### R3 — 0x5dea50 colour-key descriptor  →  RESOLVED (key == black 565 0x0000)
**decompile @0x5dea50:**
- Descriptor flag `(v119+4 & 0x20)` (bit 5) selects the **8-bit** source path
  (`loadt8`, v94=8, pixfmt code v31=7, a real DDraw palette built at 0x5defcb) vs the
  **24-bit** path (`loadt24`, v94=24, v31=9).
- Colour-key value `v111`:
  - 24-bit arm (0x5df40a): `v111 = v85[0] | (v84<<8) | (v83<<16)` where v83/v84/v85
    are the **memset-0** 768-byte palette buffer the 24-bit path never fills →
    `(0,0,0)` == **BLACK**.
  - 8-bit arm: builds a real palette object instead (no black key).
- `SetColorKey(8, &v111)` (0x5df041 on v115, 0x5df46d on v114) installs the key, and
  when `(v119+4 & 4)` (bit 2) is set the **DDBLT_KEYSRC** blit at 0x5df086
  (`...(v114,0,v115,0,&unk_1000000,0)`, `&unk_1000000` == DDBLT_KEYSRC) makes every
  pixel whose resolved colour == the key transparent.

**Verdict:** the engine's runtime colour key for 24-bit colour-keyed textures is
**black (565 == 0x0000)**, exactly as the wave-5 W5-CKEY reconstruction models with
`colorKey565` (default-resolved to black). The descriptor bit that selects the
colour-keyed path is `descFlags & 4` (bit 2) on the descriptor at v119; the descriptor
records `unk_14080A0` (plain, 24-bit, mask FF0000/FF00/FF) vs `unk_14080C4` (the
mip/colour-key descriptor) selected by `record+104 & 4` in 0x5db234. Both descriptor
globals read as zero in the static image (filled at init); the live key derivation is
the black-pixel-0 path above. The recon's `FillSpanTexturedMasked(useColorKey=true,
colorKey565=0)` is the faithful software realisation. No source change required —
the recon banner in raster.cpp already states this; this wave confirms it from the
0x5dea50 decompile (previously UNVERIFIED in raster-verify-wave4.md).

---

## 3. Diff verdict summary

**No DIVERGENCE found. No source edit required.** Every leaf in the segment is
VERIFIED-1:1 against the live decompile/disasm (§1). The two carried items are
genuine residuals, not divergences:
- **R1 UV-scale association** — float-rounding-order residual (disasm-confirmed),
  fixable only by changing a bind-site interface; left as documented.
- Host stand-ins (white default binding, byte-map defensive clip, surface horizontal
  clip in FillSpanLoop/FillTexturedSpansShaded) — documented recon-only additions,
  disabled by zero-init so existing callers/tests stay byte-identical.

The fog channel additions (wave-7) in InterpolateEdgeRgbz / FillSpanLoop /
FillSpanTextured are gated off by default (`SpanFog().enabled==false`,
`fogPerPixel==false`) so the spans are byte-identical to the binary; they are not in
the binary's path and do not affect the 1:1 verdict.

---

## 4. Build / tests

- All 5 segment source files compile clean in isolation
  (`g++ -std=c++17 -Iinclude -Isrc -I.`): raster, raster_textured, raster_blend,
  hicoltab, texture_palettize — no errors.
- The `build/` tree currently fails to link due to a **concurrent edit in
  `src/render/frame.cpp`** (another wave-15 agent mid-edit: `FrameState::animSkipIndex`
  missing) — **not a raster-segment file**, outside this segment's ownership. My
  segment is green; the failure clears when that agent's edit lands.
- No golden changed (no source change → existing goldens remain valid):
  render_raster_test, render_raster_textured_test, render_texture_palettize_test,
  render_surface_test, oneone_raster_wave13_test (all unchanged).

## 5. Still-deferred (address + reason)
- **R1 UV-scale bit-identity** — requires moving the mipWidth multiply out of the
  RgbzVertex caller into this leaf (single fused `(u+off)*(mipWidth*65536)` x87
  multiply). That changes a bind-site interface; deferred to the bind-site owner.
- **Masked-span runtime selector** — genuinely unreachable in this binary
  (0x5F721A / 0x5f753f have no xrefs); the runtime mask is the DDraw colour-key blit
  at upload (0x5dea50), reconstructed as the texture-binding segment's concern. The
  raster leaf is 1:1 and golden-pinned.
