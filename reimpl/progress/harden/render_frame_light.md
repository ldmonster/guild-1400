# Wave-H1 hardening: render/frame + light + heightmap + hicoltab

MCP-driven 1:1 diff of every provenance-tagged function in:
- src/render/frame.cpp
- src/render/light.cpp
- src/render/light_atmos.cpp
- src/render/hicoltab.cpp
- src/render/heightmap.cpp
- src/render/heightmap_create.cpp

Counts: VERIFIED-1:1 = 25, FIXED = 1 (+1 header-comment correction), BOUNDARY = (modeled hooks noted inline).

All constants re-confirmed with get_bytes (NOT trusting the existing literals):
| sym | addr | bytes | value |
|-----|------|-------|-------|
| flt_628CB8 | 0x628CB8 | 83 F9 22 3F | 0.63661975 (2/pi) |
| flt_628CB4 | 0x628CB4 | 00 00 80 3A | 1/1024 = 0.0009765625 |
| flt_628C28 | 0x628C28 | 00 C0 7F C4 | -1023.0 |
| flt_628C2C | 0x628C2C | 6F 12 83 3A | 0.001 |
| flt_628C30 | 0x628C30 | 0A D7 23 3C | 0.01 |
| flt_628C20 | 0x628C20 | 00 00 20 41 | 10.0 |
| flt_628C24 | 0x628C24 | 00 00 A0 41 | 20.0 |
| flt_628C88 | 0x628C88 | 3D 0A 17 3F | 0.58999997 (green) |
| flt_628C8C | 0x628C8C | 9A 99 99 3E | 0.30000001 (red) |
| flt_628C90 | 0x628C90 | AE 47 E1 3D | 0.10999999 (blue) |
| flt_628C94 | 0x628C94 | 00 00 7F 43 | 255.0 |
| flt_628C70 | 0x628C70 | 00 00 80 3B | 1/256 |
| flt_628C74 | 0x628C74 | 00 00 7F 43 | 255.0 |
| flt_628C64/68/6C | | | 0.30 / 0.59 / 0.11 |
| flt_628BFC | 0x628BFC | 00 00 00 3F | 0.5 |
| flt_611CE4 | 0x611CE4 | 00 00 40 40 | 3.0 |
| flt_611CE0 | 0x611CE0 | 00 00 00 40 | 2.0 |
| flt_6115B4 | 0x6115B4 | 00 00 80 3C | 1/64 |
| flt_628BA4 | 0x628BA4 | 00 00 E0 BF | -1.75 |
| flt_62838C | 0x62838C | F9 02 15 50 | 1.0e10 (== (float)1e10) |
| dbl_6295E8 | 0x6295E8 | ...E0 3F | 0.5 (double) |

ConvertX @0x5c6b08 verified via disasm: sets x87 RC=truncate-toward-zero, frndint,
restores CW. Callers' `fistp` stores the already-truncated value. util::ConvertX =
std::trunc — correct.

---

## frame.cpp

### RenderMainViewFrame @0x5B6074 — VERIFIED-1:1
Guard `byte_649D71 && dword_64A050 <= 0`; clear-select on `byte_649D70` (colorMode):
colorMode->ClearViewport, else conditional ClearRect (suppressed by byte_62D596),
then RenderUniverseFrame(64,1,1). Modeled via FrameHooks (clear funcs = SDL/GPU
boundary). useViewportClear == colorMode; clearSuppressed == byte_62D596.

### RenderUniverseFrame @0x5B3DE8 — VERIFIED-1:1
BeginUniverseFrame(a1,a2); DrawUniverseAndStats(a1,a2,1,a3). Arg mapping correct
(the literal `1` is the a3 param; the appendedPolys seed=64 lives in fs).

### BeginUniverseFrame @0x5B3900 — VERIFIED-1:1
Reentrancy clamp (`<0 -> 0`), gate, ++depth. Per-frame resets re-checked:
dword_64A060=0, flt_13FCF3C=0.0, dword_64A058=0, shadowPoly=0, runningNear=1e10,
framePoly=0. Sentinel collapse: binary compares against flt_62838C which IS
(float)1e10 == the 1e10 sentinel it just wrote -> .cpp `runningNear == 1.0e10f`
is bit-correct. Scene/terrain/particle/mirror walks = hooks (scene-graph boundary).

### DrawUniverseAndStats @0x5B3BBC — VERIFIED-1:1
Reentrancy clamp/gate. ScrollUvCoords(now); projectWalk flags `(i16)appendedPolys|0x181`;
a3-gated anim pose walk over 64 lists (dword_13ECF48[i*246], skip dword_649D60,
sentinel dword_13FCF4C, next node[124], pose arg `now|0x80000000`). FPS windows
verified asymmetric: window A uses snapshot `fpsFrameAccA+1`; window B uses the
POST-increment `fpsFrameAccB` (matches binary). Tail poly-counter resets match.

---

## light.cpp

### ComputeGrayShade @0x5c8218 (luma branch) — VERIFIED-1:1
g*0.59+r*0.30+b*0.11, (int) truncate, `(u32)i >= 0xFF -> 0xFF` (unsigned compare so
negatives saturate). Matches `(unsigned int)(int)v22 >= 0xFF` then LOBYTE=-1.

### ComputeColorShade @0x5c8218 (colour branch) — VERIFIED-1:1
max chain (b<=g?g:b then <=r?r:.), int-bit compare > 1132396544 (0x437EFFFF), scale
by 255.0/max, three (int) truncations to +68/+69/+70.

### ApplyVertexShade @0x5c7f04 (inner value) — VERIFIED-1:1
(r*0.30+g*0.59+b*0.11+ambientHi)*(1/256)*scale, clamp `255.0 >= v -> v else 255`,
(int) truncate. Weights flt_628C64/68/6C = 0.30/0.59/0.11 confirmed.

### BroadcastGrayDword @0x5c6af0 — VERIFIED-1:1
Replicates low byte into all 4 lanes. (FillDwordAlignedThunk = mem-fill boundary.)

### BuildShadeRamp — VERIFIED (helper; integer ramp scale)

### BuildFalloffLUT @0x5c88f8 — VERIFIED-1:1 (+ header comment FIXED)
out[k] = 1.0 - acos(k/1024)*(2/pi), k=0..1023. Confirmed via disasm: step
flt_628CB4=1/1024 (NOT -1/1023), index from `fild` of loop counter k. The builder
symbol flt_140510C writes (k+1)*4 but the consumer (VIBE_Light_ApplyToCachedVertices)
reads flt_1405110[k] = the same table shifted one slot — so out[k] is exactly the
consumer's value for index k. No off-by-one. Index scale flt_628C28 = -1023.0.
HEADER FIX: light.h BuildFalloffLUT doc said `acos(-k/1023)` step `-1/1023`
(WRONG); corrected to `acos(k/1024)` step `1/1024` with byte evidence. The .cpp
body was already correct.

### AccumulatePointLight / AccumulateDirectionalLight @0x5c6f90 — VERIFIED-1:1 (math)
Simplified standalone helpers reconstructing the per-vertex sub-branches of
VIBE_Light_ApplyToCachedVertices. Disasm confirms point math:
atten = intensity(v5[37])*10*objScale / (rangeParam(v5[38])*distSq);
factor = atten*LUT[(int)(NdotL*-1023)]; gate distSq<range^2 and NdotL<0. Directional
uses 0.001 (flt_628C2C) in the triangle-normal branch. Constants confirmed.

### ComputeRayFalloff @0x42dd4c — VERIFIED-1:1
t=(a3-a1)/(a2-a1); `t>0 && SLODWORD(t)>=0x3F800000` (== t>=1.0 for positive t) -> 1.0;
else recompute, `<=0 -> 0`; smoothstep t*t*(3 - 2t). flt_611CE4=3, flt_611CE0=2.

---

## light_atmos.cpp

### LightAtmosResetGlobalState @0x5c8964 — VERIFIED-1:1
dword_64A05C/60/58/64/54 = 0.

### LightAtmosBeginUniverseFrame @0x5b3982 — VERIFIED-1:1 (sub-store)
### LightAtmosEndUniverseFrame @0x5b3c19 — VERIFIED-1:1 (sub-store)

### LightAtmosStoreAmbient @0x5b85e4 — VERIFIED-1:1 (setter)
Models the four final stores flt_64A074/78/7C/70. The full band-blend math
(VIBE_SkyColor_BlendBandLighting with its ConvertX truncations) is render/sky.cpp's
responsibility — documented boundary.

### LightAtmosRemoveCacheEntry @0x5c7da4 — VERIFIED-1:1
Null-key full-list free; head-arm; scan-arm (find node whose NEXT matches key);
missing-key no-op. (FreeDebug = host-heap boundary.)

### LightAtmosBuildObjectCache @0x5c8218 — VERIFIED-1:1
geom null/hidden guards, ++walkDepth, PrepareObjectCache hook, ambient seed
(G,B,R order), collectFlag v27=(u8)(4*flags528)>>7, colour/luma reduce branches
(all truncations + publish dword v16=v17), ApplyVertexShading, tail (flags528|=4,
lastBuildFrame, cacheSerial, --walkDepth). Light-collect walk + RecomputeForObject
child walk = scene-graph hooks/gap (documented).

### LightAtmosApplyVertexShading @0x5c7f04 — VERIFIED-1:1
v21=(flags530&1)==0, v20=(i8)flags529>=0, hidden skip, poly loop stride 40,
material at +20, flag38 = (f&0xFB)|(4*((u8)(8*mat[104])>>7)), light word selection
(v20? mat+108 : frame+376), 0x10000 per-vertex recompute path (0.30/0.59/0.11,
1/256, scale, clamp 255, broadcast to +64/+68) vs the `(u8)!=0xFF` path (+67/+71).

### LightAtmosRefreshAllObjects @0x5c886c — VERIFIED-1:1
++rebuildSerial, ++walkDepth ALWAYS; gate (force||relightAlways);
force<=1 -> invalidate walk + invalidated=1; else eager rebuild; floor BuildTilePolys;
--walkDepth.

### LightAtmosEnsureNodeLit @0x5add1c — VERIFIED-1:1 (3 trigger arms)
The three VIBE_Light_BuildObjectCache trigger arms extracted from
VIBE_Render_ProcessSceneNode:
- fresh @0x5adfc3: type533==4 && !floor && (rebuildSerial>cacheSerial || invalidated)
- onscreen @0x5adf1b: cacheSerial<rebuildSerial && onscreenBudget>onscreenSpent;
  onscreenSpent += vertexCount(*(geom+8))
- culled @0x5ae25f: rebuildSerial>cacheSerial && offscreenBudget>offscreenSpent;
  offscreenSpent += vertexCount; flags528|=4
Strict/non-strict comparisons all match. Full ProcessSceneNode (cull/shadow/draw-
list) is a render boundary.

---

## hicoltab.cpp

### HiColTabDirect / HiColTabRamp — VERIFIED-1:1
Read offsets 0x7E00+2*idx and 512*light+2*idx into tab.data.

### HiColTabAddEntry @0x5d9db8 — VERIFIED-1:1
Full disasm trace (180 insns). Search loop guard (`256-freeCount`, return found
index, else append) behavior-identical to .cpp. Direct entry PackColor(r,g,b)=
PackColor(a1,a2,a4); rgb table bytes [a1,a2,a4]. Ramp loop 63 steps: each channel =
trunc(channel/62.0*step + 0.5) via ConvertX+fistp; bias dbl_6295E8 = 0.5 (confirmed);
62.0 = float 0x42780000; PackColor channel order (r_ch,g_ch,b_ch) matches. Per-step
truncation toward zero == `(int)(x+0.5)` for positive x. Offsets roff=512*step+2*i.
(.cpp computes the small intermediate in double vs x87-80bit; bit-identical for the
0..255 / 0..62 input range.)

---

## heightmap.cpp

### TileToWorld @0x5c65d4 — VERIFIED-1:1
out = {tx*scaleX+originX, h[tz*n+tx]*scaleY+originY, tz*scaleZ+originZ}; null/range
guards (tileX<0, >=n, tileY range).

### WorldToTileWithHeight @0x5c6644 — VERIFIED-1:1
fx/fz divisions, ConvertX (truncate toward zero) -> tx/tz, range/null guards,
bilinear: h00=h[tx+n*tz], hX=h[(n-1&tx+1)+n*tz], hZ=h[tx+n*(n-1&tz+1)], each
+0.5*scaleY+originY; result=(hZ-h00)*fracZ + fracX*(hX-h00) + h00. Formula matches
the binary expression exactly.

### FloodFillTileType @0x5c5530 — VERIFIED-1:1
Interior loop [1,n-1), 24-byte stride, type at +0; 4-neighbour test (up,down,left,
right where right uses `*(int*)(cell+21)>>24` == (signed)cell[24] — byte-equality
with from/to is preserved). Inner guard `n-1>1`: binary continues outer, .cpp
breaks — behavior-identical (guard is loop-invariant). returns n-1.

### AverageAreaHeight @0x427468 — **FIXED**
BEFORE: accumulated the 8x8 box sum in a `double sum`, returned `sum*(double)1/64`.
AFTER: accumulator is a `float sum` updated as `sum=(float)((double)expr + (double)sum)`
each iteration, with the final `sum = sum * kAreaAvgWeight` as a float*float, then
returned promoted to double.
EVIDENCE: decompile @0x427468 declares `float v15` (the running sum); each iteration
does `v7(double) = ... + v15; v15 = v7;` (stores back to FLOAT -> rounds every partial
sum), and `v15 = v15 * flt_6115B4` (0x427547) is a float*float multiply. A double
accumulator diverges bit-for-bit on large grids. Golden test (7.25 +/- 1e-4) still
passes (small magnitudes are exact in float).

### Free @0x5c6438 — VERIFIED-1:1 (boundary)
Nulls entries(+36) then heights(+40). The actual buffer/struct frees are the
debug-heap boundary (host heap owns the policy).

### DeriveGridScaleXZ (core of BuildTerrainMesh @0x5c5610) — VERIFIED-1:1
originX=minX; scaleX=(maxX-minX)/(size + -1.75); originZ=maxZworld; scaleZ=
(minZ-originZ)/(-1.75 + size). Matches @0x5c5b93..0x5c5c2a. originY/scaleY and the
full draw-list/raster builder are documented render boundaries.

---

## heightmap_create.cpp

### Create @0x5c63a0 — VERIFIED-1:1
size==0 -> null; alloc 0x30 record; flag2d=a3; size=abs(size); heights=alloc(sz*sz);
entries = (size<=0)?null:alloc(24*sz*sz); if !heights return; BuildTerrainMesh hook.
(AllocDebug = host-heap boundary; struct sized for 64-bit host pointers.)
