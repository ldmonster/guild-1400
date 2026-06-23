# Textured-raster edge-interpolator verification — wave 5 (W5-RTX)

**Scope:** re-verify the textured-path edge interpolators and the RGBZ leaf
residuals from the wave-4 UNVERIFIED queue against the **live IDA MCP**
(`gilde.exe`, imagebase 0x400000) — the decompile is the truth this pass.
**Owned files:** `src/render/raster_textured.{h,cpp}` + their unit tests +
this file. (Did NOT edit raster.cpp / meshlist / texture / any play file —
handoffs below.)
**IDA MCP:** LIVE — every verdict cites a fresh decompile/disasm/get_bytes.

---

## Verdict table

| Addr / claim | Decisive evidence (fresh) | Verdict | Action |
|---|---|---|---|
| **0x5F6930** InterpolateEdgeRgbz | decompile: `v3=vy[a2]-vy[a1]; if(v3>=0x10000){xStep=((vx[a2]-vx[a1])<<16)/v3; uStep=((vu..)<<16)/v3; v5=((vv..)<<16)/v3}else{v4=0x40000000/v3; step=(u64)(v4*(i64)num)>>14}; v7=((ya+0xFFFF)>>16<<16)-ya; xLeft=vx[a1]+((u64)(xStep*(i64)v7)>>16)` — channels: 13FC5E8/5CC/5D0 steps, 13FC5D8/5F0/5EC accumulators | **CONFIRMED** (line-for-line vs cpp 47-64) | none |
| **0x5F6A8C** InterpolateEdgeZ | decompile: X-only twin; `v4=...; xRightStep(13FC5C8); xRight(13FC5BC)=vx[a1]+((u64)(v4*(i64)sub)>>16)` | **CONFIRMED** (vs cpp 71-82) | none |
| **0x5F7840** InterpolateEdgeZTex | decompile: interpolates X + the **dword_13FC578** channel (shade), writes 13FC5C4 step / 13FC5F4 left — NOT U/V. xrefs_to → **only 0x5F7D58** (RasterizeTexturedTriangle, the shaded-affine path) | **CONFIRMED — wrong cluster (belongs to raster.cpp)** | not in raster_textured.cpp (correct); raster.cpp body re-verified CONFIRMED (handoff note) |
| **0x5F6C30 @0x5f6c64** UV scale (claim 5b residual) | `v53=(double)mipWidth*flt_62C3D4`; per-vertex `vu=(u+v52)*v53`; gradient `v18=(double)mipWidth*(flt_62C3D4/v44)` — i.e. original fuses `(u+off)*(mipWidth*65536)` and `mipWidth*(65536/area)` | **RESIDUAL CONFIRMED (documented)** | recon splits the mipWidth into the caller and applies *65536 here (`vu=u_texels*65536`, `v18=65536/area`); same value, one float-multiply association moved — unchanged, documented |
| **flt_62C3D4** | get_bytes 0x62C3D4 = `00 00 80 47` = 0x47800000 = **65536.0f** | **CONFIRMED** | dropped the "re-confirm" caveat in raster_textured.cpp |
| **0x5F6B34** FillSpanLoop | decompile: `for(i=0;i<rc;13FC5D4+=2*7626F8){v3=(xLeft+0xFFFF)>>16; spanLen=((xRight+0xFFFF)>>16)-v3; if>0 FillSpanTextured(13FC5D4+2*v3, (uGrad*((v3<<16)-xLeft)>>16)+uLeft, (vGrad*(..)>>16)+vLeft); advance all}` | **CONFIRMED** (vs cpp FillSpanLoopWith 135-173) | none |
| **claim 5c** flags38 bit-2 reverse-wound load | 0x5f6f16: `(*(a1+38)&4)!=0 && (x0-x2)*(y0-y1) > (x0-x1)*(y0-y2)` (x=+16,y=+20; a1[0]=v0,a1[4]=v1,a1[8]=v2); reverse loop walks `v23-=2; v22-=4` (slot i<-vertex 2-i) | **CONFIRMED** (vs cpp 264-267 + LoadVertices reverse) | none |
| **claim 5d** surface-clip clamp | 0x5f6b34 writes spans **unclipped** (no clamp in the original loop) | **CONFIRMED** (recon-only clamp; zero-init disables; pixel-neutral in-bounds) | kept, documented |
| **5AC540/5AC544** neighbour tables | get_bytes 0x5AC540 (24B) = dwords `1,2,2,0,0,1` → `[2*i]`={1,2,0}=kNext, `[2*i+? ]`(5AC544[2*i])={2,0,1}=kPrev | **CONFIRMED** | none |

## Combined-step packing note (Task 2 cross-check)

0x5f6c30 @0x5f70d7 builds the packed per-pixel texel delta
`dword_13DCE54[0] = (dV_int<<widthShift)+dU_int` and
`dword_13DCE50 = dword_1406A7C + 13DCE54[0]` (the +width-on-V-carry variant).
These are consumed by **FillSpanTextured @0x5F71AD (raster.cpp)**, not by the
edge interpolators or FillSpanLoop, which carry U/V as separate 16.16
accumulators. The reconstruction keeps U/V separate (proven equivalent — see
the equivalence note in raster.cpp / claim 2b). No change needed in my files.

## Code changes (W5-RTX)

* `src/render/raster_textured.cpp`: removed the stale "re-confirm flt_62C3D4"
  caveat — get_bytes 0x62C3D4 confirms 0x47800000 == 65536.0f. No logic change
  (all three interpolators + the leaf already matched the live decompile 1:1).
* `tests/unit/render_raster_textured_test.cpp`: +2 golden tests pinning the
  prologue sub-scanline accumulator advance (the `sub != 0` path that the
  pre-existing tests left at sub==0):
  - `InterpolateEdgeRgbzSubScanlineAdvance` (ya=1.5px, sub=0x8000): X/U/V
    step + accumulator goldens 61680/61680/38550, 227448/96376/19275.
  - `InterpolateEdgeZSubScanlineAdvance` (ya=1.25px, sub=0xC000): xRightStep
    126843, xRight 422812.
  Goldens transcribed directly from the verified 0x5F6930 / 0x5F6A8C decompile.

## Handoff (raster.cpp owner)

`InterpolateEdgeZTex` @0x5F7840 (raster.cpp:54) was re-verified against the
live decompile: interpolates X (13FC5E8/13FC5D8) + the shade channel
dword_13FC578 (step 13FC5C4 -> rs.uLeftStep, left 13FC5F4 -> rs.uLeft). Body is
1:1 — no change needed. It is the shaded-affine path's long-edge interpolator
(sole caller 0x5F7D58 RasterizeTexturedTriangle), NOT part of the RGBZ textured
cluster; the wave-4 queue mis-filed it under the textured interpolators.

## Residuals (carried, documented)

1. **UV-scale float-multiply association** (claim 5b): original fuses
   `(u+off)*(mipWidth*65536)` / `mipWidth*(65536/area)` in one multiply each;
   the reconstruction folds mipWidth into the caller and applies *65536 /
   /area here. Same value, association moved — a float-rounding-order residual,
   not a logic divergence. Documented in raster_textured.{h,cpp}.

## Test counts / results

* New unit tests: 2 (above) — render_raster_textured_test 1074 -> **1082
  checks, 0 failures**.
* Suites green: render_raster_textured_test 1082/0, render_raster_test 1004/0,
  render_raster_e2e_test 402/0, render_raster_textured_e2e_test 1/0,
  raster_clip_test 9/0, texraster_recon2_test 889/0,
  render_raster_textured_itest 594/0.
