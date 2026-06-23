# Harden pass — render_08 WATER (water_anim / water_vertices / water_render)

1:1 hardening of the water animation + render arm against gilde.exe (imagebase
0x400000). DISASM is authority; Hex-Rays is cross-check only. Files touched:
`src/render/water_anim.cpp`, `water_vertices.cpp`, `water_render.cpp`
(+ `water_render.h` zero-vector comment) and `tests/unit/water_render_test.cpp`.

## Counts
- VERIFIED-1:1: 11
- FIXED: 2
- BOUNDARY (rule-3 GPU/DDraw or documented wave-7 design reconstruction): 4

## Constants (get_bytes verified)
| symbol | addr | bytes | value | status |
|---|---|---|---|---|
| kWaterAnimSpeedTable | 0x5d93c8 | `0f,0d,0b,09,08,07,05,03,02,01` (dwords) | {_,15,13,11,9,8,7,5,3,2,1} | VERIFIED. Index 0 = the preceding dword 0x3ab78034 (a float, unused — selector guarded nonzero). |
| dbl_628AF4 (2π) | 0x628af4 | `ea 2e 44 54 fb 21 19 40` | 6.283185307179586 | VERIFIED |
| flt_628B2C | 0x628b2c | `00 00 00 3f` | 0.5 | VERIFIED |
| flt_628B30 | 0x628b30 | `00 00 00 40` | 2.0 | VERIFIED |
| flt_5CA2B0/B4/B8 | 0x5ca2b0 | `(0,0,1.0)` | sun dir (0,0,1) | VERIFIED |

## water_anim.cpp
- **WaterTextureFrameIndex (0x5be47a..0x5be492)** — VERIFIED-1:1.
  `div dword_5D93C8[esi*4]` (unsigned, eax=time) then `div esi` (unsigned,
  esi=memberCount byte) → edx = (time/speed) % memberCount, `and edx,0FFh`
  before the FindGroupMember arg. Both divisions UNSIGNED, full 32-bit; result
  byte-masked. Source `(time/divisor) % memberCount` + caller `(u8)frame` ==
  the `&0xFF`. Time threaded as signed `v22` but used in `div` (unsigned) — src
  uses `(u32)time`. ✓ Goldens: sel 1→div 15 `(3/15)%4=0`; sel 2→div 13
  `(100/13)%5=2`.

## water_vertices.cpp
- **PropagatePhases / phase loop (loc_5BE50B..0x5be52a)** — VERIFIED-1:1.
  edx starts at a3, `add edx,4` each iter, `fstp [edx+134h]` AFTER the add →
  store target = (edx+4)+0x134 = a3+0x138+4k == the SAME phase[k] slot read at
  `fadd [edx+138h]`. In-place update of all four: `phase[k] =
  Fmod(waveSpeed[k]*dt + phase[k], 2π)`, k=0..3 (reads +0x18+4k speed, +0x138+4k
  phase). Loop exit `cmp edx,ecx` with ecx=a3+0x10 → exactly 4 iters. Matches
  source + existing comment.
- **texAccum A/B (0x5be4b9 / 0x5be4d7)** — VERIFIED-1:1. BOTH accumulators are
  identical structure: `fld rate; fmul dt; fadd accum; fld1; fxch st(1); call
  Fmod` → st0=(rate*dt+accum)=dividend, st1=1.0=divisor → `(rate*dt+accum) mod
  1.0`. The Hex-Rays `Fmod(1.0, v5)` for the second accumulator is a
  MISRENDERING (confirmed at 0x5be4e8 — same opcode sequence as the first). Then
  `fxch st(1); fstp [148h]=A; fstp [14Ch]=B`. Source `util::Fmod(tA,1.0)` /
  `util::Fmod(tB,1.0)` correct. Golden DriverTexAccumIndependentGolden locks
  distinct rates + wraparound.
- **Fmod @0x5d3fb2** — VERIFIED. `fprem` (st0 mod st1, st0=dividend), partial-
  remainder `jp` loop, `fstp st(1)` pops divisor leaving result. Confirms the
  dividend/divisor stack order used above.
- **AnimateWaterVertices driver** — VERIFIED-1:1: texture gate
  `texPtr && [+0x70] && ([+0x72]&0xF)`; time gate `(v22 - [+0x150]) > 0` (signed
  `jle`); `var_14 = (float)dt` (fild→fstp); wave grid call; latch `[+0x150]=v22`.
  Strides a3+=0x158 (86 floats). Matches source.

## water_render.cpp (VIBE_Floor_TransformTileGeometry @0x5be668, water arm)
- **ComputeWaterShade (0x5be73a)** — VERIFIED-1:1. `flt_628B2C - fabs(v74*
  flt_5CA2B0 + v75*flt_5CA2B4 + v76*flt_5CA2B8)`, v74/75/76 = dword_13FD520/524/
  528 after VectorNormalize. = 0.5 - |normalize(axisH)·(0,0,1)|.
- **VectorNormalize3 (0x5cb148)** — FIXED (fidelity).
  - Evidence: 0x5cb167 `fsqrt`; 0x5cb169 `fstp [var_C]`; 0x5cb16c `test
    [var_C],7FFFFFFFh; jz loc_5CB1A0` → on zero MAGNITUDE the engine WRITES
    x=y=z=0 (loc_5CB1A0), and the guard tests the float-rounded magnitude (not
    `n<=0`), with `inv = fld1 / mag`.
  - Before: `if (n <= 0.0f) return;` (left vector untouched) using `1/sqrt(n)`.
  - After: `mag = sqrt(...); if (mag==0) {x=y=z=0; return;} inv = 1.0f/mag;`.
  - Behaviorally identical for the 3-vector use (mag==0 ⟺ input already zero), but
    now matches the binary's zeroing branch + `1/mag` form exactly.
- **ampBase / v18 / v95 (0x5be833 / 0x5be849 / 0x5be854)** — FIXED (MATH
  divergence).
  - Evidence (disasm): `xor eax,eax; mov al,[esi+8]` (esi=v12=mesh ptr) →
    `mov [var_C],eax; fild word ptr [var_C]; faddp` → ampBase =
    `mesh_float[5]*waterShade + (i16)(u8 *)(mesh + 8)`. The +8 term is a RAW
    BYTE at mesh+8 (low byte of float[2]), zero-extended to u8 then loaded as
    i16, NOT the float[2] value.
  - Before: `meshF[5]*waterShade + (float)(i16)(i32)meshF[2]` (read mesh+8 as a
    FLOAT and truncated — wrong whenever the byte is nonzero).
  - After: `ampByte = ((const u8*)meshF)[8]; meshF[5]*waterShade +
    (float)(i16)(u8)ampByte`.
  - Note: in the headless reconstruction ampBase only feeds the engine's +67
    per-vertex depth/alpha byte (v98/v99/v100 ConvertX block) which is part of
    the wave-7 documented buffer reconstruction and is not carried to screen xyz
    (BuildWaterVertexPos `(void)ampBase`); the value is now computed faithfully
    regardless.
- **Per-vertex light (LABEL_42 == BuildTileVertex, 0x5bec71..0x5bed64)** —
  VERIFIED-1:1. `l = (typeByte & 0x7F) * flt_628B30(2.0)`; `*v46>=0` (high bit
  clear) → ambient branch (flt_13FD510/514/518 * l + flt_13FD4F0/4F4/4F8); else
  shadow branch adds flt_13FD530/534/538. Channels: R=+66, G=+65, B=+64; +67=
  depth byte; +68 dword = +64. Clamp = `(int)x; if(>255)0xFF; else low byte`
  (only high-side clamp, no negative clamp) — exactly shared `ClampLightByte`
  (truncate via the same semantics as VIBE_Coord_ConvertX). Reuses ground
  pass logic.
- **Reproject — no-fog branch (0x5bee13..0x5bee7c)** — VERIFIED-1:1.
  `invZ = 1.0/[j+8]; [j+28]=invZ; [j+16]=flt_13FCD0C*x*invZ + flt_13FCD18;
  [j+20]=flt_13FCAF8*y*invZ + flt_13FCD10`. Source screenX/screenY/_pad18 with
  projXScale=13FCD0C, projXOff=13FCD18, projYScale=13FCAF8, projYOff=13FCD10.
  Math matches; the engine's per-vertex guard is `[j+76]<0` (clip flag from
  ComputeVertexClipFlags @0x5ad614) — the reconstruction approximates with
  `z>0` (BOUNDARY, see below).
- **Sort key (0x5beebb / 0x5bef03)** — VERIFIED-1:1 (math). `v68 = mesh+4`
  (texture handle); if nonzero `((v68 - dword_1406A84) >> 7) + 1`, else 0.
  Source `((tex-texBase)/128)+1` else 0. The handle/texBase is the texture-bank
  boundary (below).
- **ConvertX vs bare-fistp** — CHECKED. The light-byte clamps use plain `(int)`
  truncation casts in the binary (cvtt-style), matched by ClampLightByte. The
  depth byte (v106) and fog-shade byte (v73) go through VIBE_Coord_ConvertX
  (frndint truncate); those bytes belong to the wave-7 / fog reconstruction
  boundary, not the modeled screen xyz/light path.

## BOUNDARY (rule-3 / documented design reconstruction — not churned)
1. **Texture bank** (dword_1406A84 texBase, FindGroupMember @0x5daec0, the
   mesh+4 handle, dword_649D58 stamp) — data-coupled global texture array;
   injected/headless. The sort-key arithmetic is reproduced exactly when a real
   texBase is supplied.
2. **Per-tile point buffer + corner threading (wave-7)** — the engine's per-tile
   strips/points/polys buffers (tile+28/+44/+52, FindRegionOffset linkage). The
   reconstruction restores per-tile grouping via WaterStripSpan.tile /
   WaterPoly.tile; documented design, math (base/wave/wh offsets) verified.
3. **+67 depth/alpha byte + fog-shade branch** (v98/v99/v100/v73, byte_649DD8
   fog path, ConvertX) — render detail consumed by the DDraw blit; not modeled
   in the headless screen-xyz path. The no-fog reproject (the city path) IS
   reconstructed 1:1.
4. **ComputeVertexClipFlags @0x5ad614 + backface/no-cull** — clip computed by an
   out-of-arm helper; the reconstruction gates reproject on `z>0` and drops
   degenerate (zero-area) tris with the documented water no-cull flag.

## Tests
`ctest -R water` (GUILD_GAME_DIR set): 7/7 PASS — floorwater_regions_test,
water_render_test, water_vertices_test, water_vertices_itest,
floorwater_regions_e2e_test, water_render_e2e_test, water_vertices_e2e_test.
Phase in-place, both texAccum accumulators (not Fmod(1.0,A)), 2π modulus, and the
unsigned frame-index/speed-table goldens are locked in water_vertices_test.
