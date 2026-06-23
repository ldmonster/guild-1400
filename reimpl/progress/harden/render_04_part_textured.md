# Hardening: render part 04 — textured + blend rasterizer (1:1 verification)

Scope (edit boundary): `src/render/raster_textured.cpp` (+ `.h`),
`src/render/raster_blend.cpp` (+ `.h`), and their tests only.
IDA module `gilde.exe`, imagebase 0x400000.

Method: for every `// gilde.exe 0xADDR` provenance, `decompile` + `disasm` the
original and diff the C++ line-for-line (control flow, constants/tables via
`get_bytes`/`get_global_value`, float->int conversion, fixed-point shift signed-
ness, wraparound, RNG, struct offsets, side-effect order, return value).

## Constants / tables verified (get_bytes)

- `flt_62C3D4` @0x62C3D4 = `00 00 80 47` = 0x47800000 = **65536.0f** (kFixedScale). CONFIRMED.
- `dword_5AC540`/`dword_5AC544` (stride-2 interleaved) @0x5AC540 =
  `{1,2,_,2,_,0,_,0,_,...}` -> next = {1,2,0}, prev = {2,0,1} (kNext/kPrev). CONFIRMED.
- `dword_13FC5C0` @0x5af70a = `fistp(ConvertX(view arg_14))` in
  VIBE_Render_SetupViewTransform — a runtime viewport Y-clip bound (NOT 0x7FFFFFFF).

## raster_blend.cpp — 4 functions

All four share the affine texel fetch + per-pixel U/V step of FillSpanTextured
(raster.cpp), differing only in the destination combine. Verified against disasm
(`mov cx, pal[idx]; shr cx,1; shr ax,1; and ecx,mask; and eax,mask; add cx,ax`):

- 0x5F728A `FillSpanTexturedBlend` — VERIFIED-1:1.
  `dst = (u16)((src>>1)&mask) + (u16)((cur>>1)&mask)`; 16-bit shifts/add match
  `shr/shr/and/and/add cx,ax`. Address = `(uInt) + (vInt<<widthShift) & texelMask`
  matches `v7=(a2>>16)+(a3>>16<<shift)`. Return = U accumulator. The combined-
  index/`adc` step is modelled by separate u/v 16.16 accumulators (documented
  equivalence shared with FillSpanTextured @0x5F71AD).
- 0x5F7310 `FillSpanTexturedBlendMasked` — VERIFIED-1:1. Adds `if (idx!=0)` colour
  key (`test dl,dl / jz`) around the blend; otherwise identical.
- 0x5F739C `FillSpanTexturedOr` — VERIFIED-1:1. `dst[i] |= pal[idx]`.
- 0x5F740A `FillSpanTexturedOrMasked` — VERIFIED-1:1. OR + `if (idx!=0)` key.

## raster_textured.cpp

- 0x5F6930 `InterpolateEdgeRgbz` — VERIFIED-1:1. Field map vy=13FC59C, vx=13FC5B0,
  vu=13FC55C, vv=13FC550; steps 5E8/5CC/5D0; starts 5D8/5F0/5EC. Two-path slope
  (`dy>=0x10000`: `(dx<<16)/dy`; else `recip=0x40000000/dy`, `(recip*dx)>>14`)
  and `sub = ((ya+0xFFFF)>>16<<16)-ya`, `start = v[a] + (step*sub)>>16` all match.
  The unsigned-domain `<<16` casts are wave-10 UBSAN hardening — bit-identical to
  the binary's signed shifts. (The `fLeft`/fog 4th channel has no original; it is
  the deliberate wave-7 vertex-fog feature, gated at the call sites so non-fog
  runs are byte-identical.)
- 0x5F6A8C `InterpolateEdgeZ` — VERIFIED-1:1. X-only; step 5C8, start 5BC. Matches.
- 0x5F6B34 `FillSpanLoop` — VERIFIED-1:1 (after FIX, below). `xL=ceil(xLeft)`,
  `spanLen=ceil(xRight)-xL`, `sub=(xL<<16)-xLeft`, `U=(uGrad*sub>>16)+uLeft`,
  `V=(vGrad*sub>>16)+vLeft` (uGrad=13FC5E4, vGrad=13FC598), per-row advance of all
  edge accumulators and `dword_13FC5D4 += 2*dword_7626F8` all match. The recon-
  only surface clip is gated (`clip` flag) and off for direct callers.
  - **FIXED — return value for rowCount<=0.** The original seeds `result`(eax)
    with the rowCount argument and only overwrites it to `2*dword_7626F8` INSIDE
    the loop; so for rowCount<=0 it returns the input rowCount, not `2*pitch`. The
    C++ initialised `result = 2*fbPitchPx` before the loop -> wrong for the empty
    case. Changed to `int result = rowCount;` (the in-loop `result = 2*fbPitchPx`
    still applies for >=1 row). The real call tree discards this return; fixed for
    Rule-1 fidelity. Tests still green (the return is untested).
- 0x5F6C30 `RasterizeMirrorTriangle` (`RasterizeTexturedTriangleRgbzWith`) —
  VERIFIED-1:1 with two documented residuals + one KNOWN DIVERGENCE:
  - Winding/reverse test `(x0-x2)*(y0-y1) > (x0-x1)*(y0-y2)` gated by
    `(polyFlags38 & 4)` — matches `*(a1+38)&4` and the a1[0/4/8] pointer order.
  - Reverse vs forward vertex-load slot order (slot i <- vertex 2-i / i) — matches
    the v22/v23 down-walk vs the v3/v4 up-walk.
  - Apex / edge selection (3 branches: flat-top-next / flat-top-prev / general)
    decoded from `dword_5AC540`/`5AC544` indexing — all three map exactly to the
    C++ v7/v19/v20/v21 assignments (using next[next]=prev, prev[prev]=next).
  - Gradients: `v47=y0-y1`, `v46=y2-y1`, `v44=(x0-x1)*v46-(x2-x1)*v47`, early-out
    on `v44==0`, `dU=(v46*(u0-u1)-(u2-u1)*v47)*v18`, `dV=v18*((v0-v1)*v46-v47*(v2-v1))`
    with v49[] = (u0,v0,u1,v1,u2,v2) — match.
  - Top + bottom sub-triangle row counts (`rc1`, `rc2`), the `rightShorter`
    (vy[longEnd]<vy[shortEnd]) branch, the re-walk edge selection
    (`InterpolateEdgeRgbz(v20,v21)` vs `InterpolateEdgeZ(v21,v20)`), and the
    bottom guard `vy[v20]!=vy[v21]` — all match (fbRow0 recompute == original's
    continued cursor absent the recon clip).
  - lightRow8 = `(l0+l1+l2)/3 << 8` of the +66 bytes — matches `13FC5E0`
    (addition order 1+0+2 vs 0+1+2 is commutative).
  - **RESIDUAL (pre-existing, documented):** UV scale association. Original
    computes `(u_norm + offset) * (mipWidth*65536)` as one float multiply; the
    C++ takes caller-supplied texel-unit UVs and applies `*65536`. Same value,
    different float-multiply association -> possible <=1-ulp pre-truncation diff
    (progress/raster-verify-wave4.md). Caller-contract layering, not introduced here.
  - **RESIDUAL (pre-existing, documented):** PatchSpanConstants self-modifying
    immediates (0x5F7500/0x5F76CD) modelled by BuildSpanTexParams; combined
    texel-index step (`dword_13DCE54`) modelled by separate u/v accumulators —
    proven bit-identical addressing (equivalence note in raster.cpp).
  - **KNOWN DIVERGENCE (edge-only) — apex min-Y seed.** Original seeds the
    LoadVertices min-Y compare from the runtime viewport constant `dword_13FC5C0`
    (set in VIBE_Render_SetupViewTransform @0x5af70a), not 0x7FFFFFFF. Because the
    test is `if (seed >= vy[i])`, the apex is the global-min vy WHENEVER that min
    <= the viewport bound — true for every on-screen triangle, so the result is
    bit-identical to the 0x7FFFFFFF seed for all valid input. They differ ONLY for
    a degenerate triangle whose ENTIRE vy set exceeds the bound (original ->
    apex=-1 -> culls; recon -> proceeds, then clamps to zero rows via clampRows).
    Faithful reproduction needs `dword_13FC5C0` plumbed from the view-transform
    module (cross-module wiring, outside this file's edit boundary) — NOT faked
    here per Rule 8. **HARDENED:** exposed as a settable field `RgbzRasterState::
    minYSeed` (the rasterize wrappers set 0x7FFFFFFF) so the real value can be
    wired in later; LoadVertices now reads `rs.minYSeed`. Comments at both the
    field and the use site cite the address + evidence.
- 0x5F6C30 wrappers `RasterizeTexturedTriangleRgbz` / `...Masked`,
  `BuildSpanTexParams`, `LoadVertices`, `InterpolateEdge*` re-statements — all
  consistent with the above.

## BOUNDARY notes

- GPU/DDraw blit: not present in these files; the textured/blend spans write the
  16bpp software framebuffer directly (the Vulkan/SDL boundary is the surface
  flip elsewhere). Raster MATH verified 1:1.
- VIBE_Coord_ConvertX @0x5C6B08 truncates toward zero (chop control word);
  modelled by `std::trunc()->(i32)` and bare `(int)` of already-truncated values.
  Confirmed against SetupViewTransform's ConvertX use and the per-vertex/per-
  gradient `(int)` stores.

## Counts

- raster_blend.cpp: 4 functions — 4 VERIFIED-1:1, 0 FIXED.
- raster_textured.cpp: provenance functions reviewed —
  InterpolateEdgeRgbz, InterpolateEdgeZ (VERIFIED-1:1);
  FillSpanLoop (FIXED: rowCount<=0 return);
  RasterizeMirrorTriangle (VERIFIED-1:1 + 1 KNOWN DIVERGENCE hardened: minYSeed);
  plus 2 documented pre-existing residuals (UV float association, SMC modelling).
- Edits: 1 behavioral FIX (FillSpanLoop return), 1 DIVERGENCE hardening
  (minYSeed field + seed set), comments. No golden was wrong.

## Build / test

Targets built (test targets only; build dir untouched):
render_raster_textured_test, texraster_recon2_test, render_raster_test,
oneone_raster_wave13_test, render_picture_test, render_raster_textured_itest,
render_raster_textured_e2e_test — all link clean (1 pre-existing unused-var warning).

`ctest` (with GUILD_GAME_DIR set): all 7 above **PASS**.
