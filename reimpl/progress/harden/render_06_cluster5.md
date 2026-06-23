# Harden sweep — render cluster 5 (sky colour / fog / dome)

Files: src/render/skycolor_recon.cpp, src/render/sky.cpp, src/render/sky_dome.cpp
MCP module gilde.exe (live). Test targets: skycolor_recon_test, sky_render_test — both PASS.

## Constants re-verified with get_bytes (all confirmed correct)
| addr | bytes | value | use |
|------|-------|-------|-----|
| 0x62872c | 9a 99 99 3e | 0.30 | kSkyLumaR |
| 0x628728 | 3d 0a 17 3f | 0.59 | kSkyLumaG |
| 0x628730 | ae 47 e1 3d | 0.11 | kSkyLumaB |
| 0x64a018 | 00 00 80 3f | 1.0  | kFogRangeScale |
| 0x61752c | 0a d7 23 3c | 0.01 | kBrightScale |
| 0x628720 | 00 00 80 bf | -1.0f | kMinusOneF |
| 0x628718 | ...f0 bf | -1.0  | kMinusOneD |
| 0x628724 | 00 00 00 3f | 0.5  | kHalf / kSkyBandMid |
| 0x62c148 | 25 49 12 3e | 1/7  | kSkyColStep |
| 0x62c14c | cd cc 4c 3e | 0.2  | kSkyRowStep |

## Per-function results

### 0x5b80ac InterpolateBand — VERIFIED-1:1
Line-for-line match incl. branch conditions (band>=7, frac<0, SLODWORD(frac)>1.0f,
kind<5, v21>1.0/v21<-1.0 wrap with `(band±1)%7` unsigned mod), struct offsets
(+488 bands, +533 kind, +420 phase, out_148/144/152, out_92[]), the 3-iter
orientation/translation do-while, has_mag &0x7FFFFFFF || kind==8, SetPosition/
SetWorldTranslation order, return 1. (decompile + verified against logic.)

### 0x5b83b0 SetTimeOfDay — FIXED
`fistp [var_10]` writes only the LOW dword (signed-32 chop); `HIDWORD = ecx = 0`
zero-extends, so v6 is the UNSIGNED 32-bit truncation widened to 64-bit (NOT a
signed i64). Old code used `i64 v6 = TruncTowardZero(v2)` (signed) for both the
`(float)v6` frac and the band index, diverging for negative inputs.
Before: `i64 v6 = TruncTowardZero(v2); v8 = v7-(float)v6; band=(u32)v6`.
After: `u32 trunc32=(u32)(i32)TruncTowardZero(v2); i64 v6=(i64)(u64)trunc32;
v8=v7-(float)(double)v6; band=trunc32` (matches fistp-low-dword + HIDWORD=0).
Evidence: disasm 0x5b83c5 fistp / 0x5b83cb mov hi,ecx(=0) / 0x5b83cf mov lo.
In-domain (time ∈ [0,7)) behaviour unchanged; only the rejected negative path
now matches the binary's huge-positive widening.

### 0x5b83f0 StoreBandColors — VERIFIED-1:1
All 12 dword stores match exactly: dst+40←src[36], +36←[37], +44←[38], +0←[19],
+4←[20], +8←[21], +24←[23], +28←[24], +32←[25], +12←[33], +16←[34], +20←[35];
guard `band>6 || !bands`; 56*band stride; bands base = a1[122] (=+488).

### 0x5b85e4 BlendBandLighting (sky.cpp) — FIXED (x87 precision)
The disasm (0x5b8677..0x5b876f) has an asymmetric float-rounding pattern the old
all-float code did not model:
 * band DIFF (B-A) is `fsub` then `fstp` to a 4-byte stack float (var_28/1C/14) →
   float-rounded BEFORE the *t multiply.
 * t*(B-A) is kept in the x87 register (80-bit); rowA added at 80-bit.
 * R and B: lerp result is `fstp`'d to flt_64A074/A07C (float-rounded) then `fld`'d
   back to scale. G: gLerp is NEVER stored before the scale (`fmul arg_4`@0x5b872f
   on the register) → gLerp is NOT float-rounded. (Real asymmetry, preserved.)
 * the scale (*a3) `fst`s the float-rounded channel to flt_64A07x BUT keeps the
   80-bit product in the register for the luma weighting → luma uses the UNROUNDED
   scaled channels.
Before: `float r=(B-A)*t+A; r=r*scale; luma=g*0.59f+r*0.30f+b*0.11f` (everything
float-rounded). After: dr/dg/db = (float)(B-A); rLerp/bLerp float, gLerp double;
rScaled/gScaled/bScaled double; out.{r,g,b}=(float)scaled; luma computed in double
from the 80-bit scaled channels, single final float round. Order g*0.59 + r*0.30
+ b*0.11 confirmed (v16=v12*0.59+v14; +v18). Goldens (close()/exact bytes) still
pass — they encode correct behaviour at clean inputs; the fix only affects the
last-ULP cases the binary rounds differently.

### 0x5b8b04 ApplyAmbientBlend (sky.cpp BlendAmbientFog) — FIXED (fog precision)
Colour bytes: `(B-A)*frac + A` with UNSIGNED bytes (movzx), int diff `fild`'d,
truncated toward zero (ConvertX+fistp). Byte form already correct (NOT the
a*(1-t)+b*t form — confirmed at 0x5b8b9e). Fog near/far precision FIXED: disasm
0x5b8b49..0x5b8bdf does `fld B; fsub A; fmul frac; fadd A` ALL in the x87 register
(diff NOT stored to float, unlike the band diff above), then `fstp [var_14]`
float-rounds the lerp before the *flt_64A018 scale. Old code computed the lerp in
C++ float (rounding the (B-A)*frac intermediate). After: whole lerp in double with
one float round: `(float)(((double)B-A)*frac + A)`. Scale is exactly 1.0 so the
*kFogRangeScale is identity. Truncation/reassembly (B2<<16|B1<<8|B0) unchanged.

### 0x43f460 ApplyScaledBlend (sky.cpp ScaledBlendAlpha) — VERIFIED-1:1 (alpha math)
Threshold `SLODWORD(v6)<1.0f && v6<=0.0` ≡ `v6<=0` → 0; `v4>=1.0f` → 1; else v.
Constant flt_61752C=0.01 confirmed. The reconstruction models the alpha clamp;
the original's body forwards the clamped alpha to ApplyAmbientBlend (coupled
side-effect / scene-graph walk) — that call is the documented coupling, the alpha
computation itself is exact.

### 0x5b85e4 inner loop (skycolor_recon.cpp BuildFogScratch) — FIXED (1-t precision)
The (1-t) weight: `fld1; fsubrp` then `fstp [var_24]` STORES AS A 4-BYTE FLOAT
(0x5b8783) and is `fld`'d back (0x5b87bd) as the per-channel weight → (1-t) is
float-ROUNDED before the products. Old code used `double v47 = 1.0 - (double)t`
(full double). After: `float v47 = 1.0f - t; double v21 = (double)v47`. Channel
bytes are fild'd as signed-16 of the unsigned byte; near/far = A*(1-t)+B*t (A
first). Keyframe stride 12 bytes (add ecx/edx,0Ch) confirmed → src.fog[a/b][i].

### 0x5ef980 BuildDomeMesh (sky_dome.cpp) — VERIFIED-1:1
uStep=(rectXmax-rectXmin)*1/7, vStep=(rectYmax-rectYmin)*0.2, vAcc init
(float)rectYmin, uAcc reset (float)rectXmin per row. Loops 6 rows × 8 cols.
tRow=(float)row*0.2, tCol=(float)col*1/7 (SLODWORD of the bit-incremented float
counter = integer index — modelled with int col). VectorLerp(c30,c10,tRow) edge
start, (c20,c00,tRow) edge end, (start,end,tCol)→Normalize→Atan2(dir.x,dir.z)
longitude. Vertex writes: u@-32, v@-28, one0@-24=1.0, one1@-20=1.0, longitude@+1532.
uAcc+=uStep after write, vAcc+=vStep after row. Return: original clobbers eax to
LODWORD(v21) (garbage) and EVERY caller discards it (Sky_Create does `mov eax,ecx`
right after) — reimpl returns the written count (48) as a harmless testability
abstraction. The +2728=-1 cache-invalidate is a side-effect on the live object the
surrogate doesn't carry (documented BOUNDARY within the function).

## Boundaries (rules 3-5 tech swap)
### 0x5dd464 ClearViewport / 0x434728 ClearRect — BOUNDARY (DirectDraw)
ClearRect@0x434728 is a DirectDraw surface clear (memsets `ppvBits` to 0, or
LockSurfaceRegion for D3D backends, gated on byte_762721 device kind). This is the
DirectDraw/D3D rendering tech the project replaces with SDL/Vulkan (rules 3-4).
sky.cpp `RenderSky` is the faithful software/surface-fill replacement: it fills the
clip-rect of the surface with the time-of-day clear colour (dword_649DD4) in the
surface's native bpp. Not a 1:1 byte translation (no live DirectDraw surface state)
— this is the sanctioned boundary.

## Counts
- VERIFIED-1:1: 4 (InterpolateBand, StoreBandColors, ScaledBlendAlpha alpha, BuildDomeMesh)
- FIXED: 4 (SetTimeOfDay 64-bit widening; BlendBandLighting x87 luma/lerp asymmetry;
  BlendAmbientFog fog-lerp precision; BuildFogScratch (1-t) float-rounding)
- BOUNDARY: 1 (ClearViewport/ClearRect — DirectDraw → SDL/Vulkan surface fill)
- Tests: skycolor_recon_test PASS, sky_render_test PASS. All 3 source files compile clean.

NOTE: a pre-existing compile error in src/world/office_forms.cpp (kTortureCase2Button,
NOT in this cluster, owned by another agent) breaks the full `guild` lib build; it
does not affect these three files (verified compiling them in isolation) and the
test binaries built+passed from the last good link.
