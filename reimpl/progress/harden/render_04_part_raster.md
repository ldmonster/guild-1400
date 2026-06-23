# Hardening audit — src/render/raster.cpp (1:1 vs gilde.exe)

File: /home/cnupt/work/reverse/guild-1400/reimpl/src/render/raster.cpp
Header: /home/cnupt/work/reverse/guild-1400/reimpl/src/render/raster.h
Scope: every `// gilde.exe 0xADDR` provenance function + the referenced leaves.
Method: mcp decompile + disasm per function; get_bytes for every table/const.

## Constants / tables verified by get_bytes
- flt_62C3D8 @0x62C3D8 = `0x47800000` = 65536.0f (16.16 scale, textured path). VERIFIED.
- flt_62C6C0 @0x62C6C0 = `0x47800000` = 65536.0f (flat/shadow path). VERIFIED — identical
  to flt_62C3D8, so the shared `kFixedScale` is correct for both paths.
- dword_5AC540 (stride 8) @0x5AC540 bytes -> {1,2,0} = kNext. VERIFIED.
- dword_5AC544 (stride 8) @0x5AC544 bytes -> {2,0,1} = kPrev. VERIFIED.

## Float->int sites
- VIBE_Coord_ConvertX @0x5C6B08 (disasm): `fstcw` save, set CW high byte = 0x1F
  (RC bits 11:10 = 11 => round toward zero), `frndint`, restore CW. => TRUNCATE
  toward zero. The triangle setups call ConvertX then `fistp` an already-integral
  value, so the store rounding is moot. The C++ `(i32)(x*kFixedScale)` truncates.
  VERIFIED-1:1.

## Per-function results

### 0x5F6A8C VIBE_Raster_InterpolateEdgeZ — VERIFIED-1:1
Two-path slope (dy>=0x10000 64-bit divide; else 0x40000000/dy reciprocal *num >>14),
sub = ((vy+0xFFFF)>>16<<16)-vy, xRight = vx[a] + (step*sub)>>16. Matches EdgeSlope +
InterpolateEdgeZ verbatim (incl. unsigned-domain <<16 hardening, bits identical).

### 0x5F7840 VIBE_Raster_InterpolateEdgeZTex — VERIFIED-1:1
Same two-path for both xLeftStep and uLeftStep (light), with the reciprocal `v4`
reused for both channels in the short-edge arm. xLeft/uLeft seeded with the same
`sub`. Matches decompile field-for-field (13FC5E8/5C4/5D8/5F4).

### 0x5F71AD VIBE_Raster_FillSpanTextured — VERIFIED-1:1 (SMC reconstruction)
The original is self-modifying (immediates shown as 0x12345678 / 305419896 /
word_1234567 in the decompile). The combined-index inner loop
`ebx=(Vint<<shift)+Uint, &mask` advanced by the patched delta + adc/sbb carries is
proven equal to the separate-accumulator reconstruction (documented EQUIVALENCE
NOTE in source). Masked test is on the SOURCE INDEX byte. The `lightRow8|idx`
fetch == `mov dl,texel` byte-replace because lightRow8 = avg<<8 (low byte 0).

### 0x5F721A VIBE_Raster_FillSpanTexturedMasked — VERIFIED-1:1
Identical span loop; the only added op is `if ((_BYTE)v10) store` i.e. skip when the
TEXEL INDEX == 0 (`test dl,dl / jz`). The default `useColorKey==false` path tests
`idx != 0`. Exact. (useColorKey is the wave-5 documented DDraw-key realization.)

### 0x5F7960 VIBE_Raster_FillTexturedSpansShaded — VERIFIED-1:1 (math)
- byte-span (modeMask&2): the ROR'd adc accumulator chain (0x5f7a53..0x5f7a60)
  reproduced exactly: r=ROR(uLeft+(uGrad*((xL<<16)-xLeft))>>16,16), s=ROR(uGrad,16),
  per pixel `t=(u64)r+s+cf; cf=t>>32; r=(u32)t` == the two-step CFADD adc (carry out
  of fraction half lands one pixel late). VERIFIED.
- tile-stamp (modeMask&5): stamp value 11 when (modeMask&1)==0 else 0; current-row
  halo, once-per-firstBatch top pyramid (v46 flag), core span, trailing bottom
  pyramid (v34/v31/v33 saved state). All clamps (x0<=0->0, n>=pitch-xL->pitch-xL,
  row gate v35<=pitch) match. VERIFIED.
- Byte writes are clipped to the surface rect / row gated — documented
  reconstruction-only host-safety guard (original writes unclipped); MATH 1:1.

### 0x5F7D58 VIBE_Raster_RasterizeTexturedTriangle — FIXED (min-Y seed)
- Signed-area expr, forward/reverse load, a5 masking (!a6->&2, !a4->&5), uGrad
  gradient (always ARG order), long/short edge pick, top+bottom sub-triangles: all
  VERIFIED against the decompile.
- FIXED: the min-Y tracker seed. disasm 0x5f7d9f `shl ecx,10h` seeds the running
  min with `pitch<<16` (a3), NOT INT_MAX. A vertex whose 16.16 Y exceeds pitch<<16
  is not a valid top candidate; if none qualify, top stays -1 and nothing draws.
  - before: `i32 minY = 0x7FFFFFFF;` in LoadVertices.
  - after:  `i32 minY = (i32)((u32)rs.fbPitch << 16);`  (evidence: 0x5f7d9f; flat
    path uses the same seed at 0x603f6f). The min update test `vy[i] <= minY`
    (cmp ecx,eax; jl skip => update on ecx>=vy) is unchanged/correct.

### 0x603ED4 VIBE_Shadow_RasterizeTriangle (flat) — FIXED (bounds reject) + min-Y seed
- Back-wound cull / reverse-load on (poly+38 & 4), winding cross product, edge pick,
  top+bottom fills: VERIFIED against decompile.
- min-Y seed: same `shl edx? /a3<<16` seed — now covered by the shared LoadVertices
  fix above (0x603f6f).
- FIXED: the whole-triangle BOUNDS REJECTION loop (disasm 0x603f72..0x603f8d) was
  MISSING. edx=(pitch<<16)-1; for each loaded vertex, reject (return, draw nothing)
  if `vx<0 || vx>limit || vy<0 || vy>limit` (signed `jl`). The textured path
  @0x5F7D58 has NO such loop — flat only.
  - after: added a 3-vertex bounds loop right after LoadVertices in
    RasterizeFlatTriangle, returning 0 on any out-of-range coordinate. Evidence:
    0x603f74 `dec edx` (limit=(a3<<16)-1), 0x603f79/7f/87/8d compares.
  - Existing tests stay green: on-screen flat goldens are inside the limit; the
    off-screen overhang test now (correctly) rejects (its assertion only requires
    pixels in {0,color}).

### 0x603D00 VIBE_Raster_ComputeEdgeSlope (flat LEFT-edge) — VERIFIED-1:1
Identical two-path divide as InterpolateEdgeZ but writes xLeft/xLeftStep
(13FC5D8/5E8). Reconstructed as ComputeEdgeSlopeLeft via the shared EdgeSlope.

### 0x603DA8 VIBE_Raster_FillSpans (flat per-row fill) — BOUNDARY (documented)
Two arms gated by byte_140A220 = (bpp<=8): 8bpp `memset(..., dword_13FC5E0, len)`;
16bpp word-stores of dword_13FC5E0 (=0xFFFF stencil). The reconstruction's
`fillRange` implements the byte path with a host-supplied `color` fill index (per
the header's documented host adaptation: the flat leaf's value is the fixed shadow
stencil 1/0xFFFF, generalized to a `color` parameter). The 16bpp word-stencil arm
is not reproduced. Pre-existing documented reconstruction choice; per-pixel x clamp
+ row gate are host-safety guards. MATH of the span extents [ceil(xLeft),
ceil(xRight)) is 1:1.

### 0x5F76E2 VIBE_Raster_BilinearBlendBlockMmx — BOUNDARY (documented, off-path)
Texture-block magnifier, NOT on the triangle span path (header lines 46-59 / 332).
Scalar fast path: `1404660[byte1]+1404260[byte0]+LOWORD(1404A60[HIWORD(dword)])`;
MMX path: pmaddwd of 4 corner texels by the mm6 weight vector, >>8/channel,
recombined via the 3 channel LUTs. The portable scalar reconstruction reproduces
the per-channel LUT recombination (rLut/gLut/bLut) for the texture-upload use; it
is documented + unit-tested and never reached by RasterizeTexturedTriangle. Left
as the documented scalar boundary (rewriting the MMX magnifier is outside the
raster/projection-math scope and would churn an off-path leaf).

## Counts
- VERIFIED-1:1: 7  (0x5F6A8C, 0x5F7840, 0x5F71AD, 0x5F721A, 0x5F7960, 0x603D00,
  plus ConvertX 0x5C6B08 and the constant/table set)
- FIXED: 2 sites across 2 functions
  - min-Y seed INT_MAX -> pitch<<16 in LoadVertices (affects 0x5F7D58 and 0x603ED4)
  - flat bounds-rejection loop added (0x603ED4 / 0x603f72)
- BOUNDARY (documented): 2  (0x603DA8 16bpp arm; 0x5F76E2 bilinear magnifier)

## Tests (built + run, all pass)
render_raster_test, render_raster_textured_test, oneone_raster_wave13_test,
raster_clip_test, render_clip_test, render_shadow_test, shadow_ground_test,
shadow_object_render_test, scene_recon5_raster_test  => 9/9 PASS.
No golden vectors required changes (both fixes only alter behavior for inputs the
existing goldens do not exercise: out-of-(pitch<<16) coordinates).
