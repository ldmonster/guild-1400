# Wave-7 — per-vertex sun NORMAL transform + the shade-ramp LUT (W7-LIGHTNRM)

Owner: W7-LIGHTNRM. Closes the two rule-8 named gaps left by
`progress/vertex-lighting-wave6.md`:

1. **The directional NdotL normal source/transform** — wave-6 used the smoothed
   WORLD normal directly; the engine's type-7 per-vertex sun arm @0x5c7129 instead
   fetches the OBJECT-space normal, Y-rebiases it by the bone translation/scale,
   rotates it by the per-object bone-world matrix, renormalizes, and only THEN dots
   with the sun direction.
2. **The shade-ramp LUT `flt_1405110`** — wave-6 called it "all-zero / no writer /
   runtime-empty / ambient-dominant". **That is wrong.** The writer was found this
   wave: it is `VIBE_Light_InitFalloffTable` @0x5c88f8, and `flt_1405110` is the SAME
   1024-entry angular falloff LUT that wave-6 already reconstructs as
   `render::BuildFalloffLUT`. (Corrected below.)

Files owned/edited: `src/render/object_light_shade.{h,cpp}` (Gap-1 kernel +
bone-matrix `LightMeshVertices` overload), `tests/unit/object_light_shade_test.cpp`
(+5 tests). Read-only: `light.{h,cpp}`, `light_atmos.*`, `anim_normals.*`,
`vertex_lighting.{h,cpp}`.

---

## Gap 1 — the per-vertex sun normal transform (1:1)

`VIBE_Light_ApplyToCachedVertices` @0x5c6f90, type-7 (sun) per-vertex arm
@0x5c7129..0x5c7297. Disasm-exact recovery (verified by `disasm` @0x5c7078,
0x5c70a4, 0x5c71b5):

```
// per object, once, when *(obj+535)==1 and (*(obj+528) flag bit 0x80) :
M[16] = VIBE_Transform_ComputeBoneWorldMatrix(obj, ..., 1)   // out @ var_E0 (lea ecx)
bone  = *(obj+16)                                            // [edi+10h]
sy    = flt_628C24(20.0) / bone[+0x1D4]   (= +468)           // v99  (fld 628C24; fdiv [eax+1D4h])
boneY = bone[+0x6C]   (= +108)                               // v98  ([eax+6Ch])

// per cache vertex (stride 20 floats):
n  = *(float(*)[3])(vertex+0x48)          // mov edx,[ecx+48h]; a POINTER to the normal
ny = (n.y - boneY) * sy                    // fld var_9C; fsub var_38; fmul var_34
// 4-stride 3x3 rotate (the @0x5c71b5 fmul chain; cols 0..2 of rows 0..2):
t.x = n.x*M[0] + ny*M[4] + n.z*M[8]        // [ebx], [ebx+10h], [ebx+20h]
t.y = n.x*M[1] + ny*M[5] + n.z*M[9]        // [ebx+4], [ebx+14h], [ebx+24h]
t.z = n.x*M[2] + ny*M[6] + n.z*M[10]       // [ebx+8], [ebx+18h], [ebx+28h]
VIBE_Math_VectorNormalize(&t)              // @0x5cb148 (zero-length -> 0,0,0)
sun = *(*(light+488)+392/396/400)          // v78/v79/v80 (the stored sun dir, NOT renormalized)
NdotL = t.x*sun.x + t.y*sun.y + t.z*sun.z  // v19
if (NdotL < 0.0):
   f = sunI(+148) * flt_628C30(0.01) * objScale(*(obj+492)+2296)
       * flt_1405110[(int)(NdotL * flt_628C28(-1023))]
   accum += sunColor(+92/96/100) * f
```

Key facts that make this the *named gap*, not a re-derivation:
- the normal is **object-space**, fetched via a **pointer** at `vertex+0x48` (the
  cache's normal-source slot), NOT the cache's own accumulator;
- its **Y is pre-biased**: `(n.y - boneY) * (20.0/boneScale)` — `boneY`/`boneScale`
  come from the bone/transform record `*(obj+16)` at +0x6C / +0x1D4;
- the rotation uses the **bone-world matrix** in the 4-stride 16-float layout (cols
  0,4,8 / 1,5,9 / 2,6,10 — identical to `render::TransformPointByWorldMatrix`'s
  `world`, no translation row for a normal);
- the transformed normal is **renormalized** (the original `VectorNormalize` on the
  3-float temp) before the dot;
- the sun direction is used **verbatim** (the vertex arm does NOT renormalize it —
  only the per-point/per-poly arms normalize their direction vectors).

Reconstructed as:
- `TransformVertexLightingNormal(n, m16, boneY, boneScale, outN)` — the exact
  normal source → Y pre-bias → 4-stride rotate → renormalize.
- a new `LightMeshVertices(... , const float boneMatrix[16], float boneY,
  float boneScale, ...)` **overload** — identical to the wave-6 entry except the
  sun NdotL is taken against the transformed object-space normal. The ambient seed,
  the point-light arm (which dots the cache normal directly, unchanged), and the
  finalize are byte-for-byte the same. `sunIntensity <= 0` OR `boneMatrix == nullptr`
  disables the sun → the ambient-only path is unchanged. The wave-6 world-normal
  overload is **kept** for the bind site that already supplies world normals.

### Constants (get_bytes, little-endian)
| symbol | bytes | value | role |
|---|---|---|---|
| flt_628C24 | `00 00 a0 41` | 20.0 | per-vertex sun-normal Y pre-bias scale (`kSunNormalYScale`) |
| flt_628C30 | `0a d7 23 3c` | 0.01 | per-vertex sun intensity scale (`kSunVertexIntensityScale`) |
| flt_628C28 | `00 c0 7f c4` | -1023.0 | falloff LUT index scale |

---

## Gap 2 — the shade-ramp LUT `flt_1405110` (CORRECTED: it IS BuildFalloffLUT)

Wave-6 reported `flt_1405110` as "all-zero in the static image, no writer among its
xref sites, runtime-empty, ambient-dominant". Re-investigated this wave:

- All 5 occurrences of the literal address `0x01405110` in the image are **reads**
  (`fmul ds:flt_1405110[...]`): the 3 sun/point/poly arms in
  `VIBE_Light_ApplyToCachedVertices`, the point arm in `VIBE_Light_IlluminateObject`
  @0x5c7c3a, and the floor tile-light-circle arm in `VIBE_Floor_BuildTilePolys`
  @0x5bc7a8. (`find_bytes "10 51 40 01"` → exactly those 5; confirmed it's a `.bss`
  table, all-zero statically.)
- **The writer was missed because it addresses the table as `flt_140510C + 4`.**
  `VIBE_Light_InitFalloffTable` @0x5c88f8 fills it:
  ```
  ecx=0; for edx in [0,1024): ecx += 4 (BEFORE store);
         st = 1.0 - acos(edx * flt_628CB4) * flt_628CB8;
         fstp flt_140510C[ecx]        // == flt_1405110[edx]
  ```
  i.e. `flt_140510C + 4 == 0x140510C + 4 == 0x1405110`. So **`flt_1405110` IS the
  output of `VIBE_Light_InitFalloffTable`** — the 1024-entry angular falloff LUT.

Therefore the LUT the kernels index (`flt_1405110[(int)(NdotL*-1023)]`) is exactly
the table `render::BuildFalloffLUT` (light.cpp, @0x5c88f8) already reconstructs, and
wave-6 was correct to pass that LUT as the kernels' `lut` argument — the two are the
**same table**. The "ambient-dominant because the ramp is zero" conclusion does **not**
hold for the live engine: `VIBE_Light_InitFalloffTable` runs at init and the diffuse
terms are non-zero.

### The falloff LUT formula (exact)
```
flt_1405110[k] = 1.0 - (2/pi) * acos(k / 1024)          for k in [0, 1023]
```
Constants (get_bytes):
| symbol | bytes | value | role |
|---|---|---|---|
| flt_628CB8 | `83 f9 22 3f` | 0.63661975 (2/π) | outer factor |
| flt_628CB4 | `00 00 80 3a` | 0.0009765625 (1/1024) | per-index argument step |

Note: the per-index step is `+1/1024` and the acos argument is `+k/1024` (NOT the
`-1/1023` wave-6's first draft used). `light.cpp::BuildFalloffLUT` is already on the
correct `+1/1024` form (its inline comment notes the earlier negated draft was
fixed), so no edit is needed there — it is not owned by this wave and is correct.

### What DOES map accumulated light → final shade
Per the brief: the light→shade mapping itself is the **finalize** kernels (already
reconstructed in `object_light_shade.cpp`, verified this wave):
- `FinalizeVertexShadeSoftware` (software colour branch @0x5c8346): hue-preserving
  max-channel normalise to 255, truncate to B/G/R bytes;
- `FinalizeVertexShadeLuma` (hardware luma branch @0x5c84bc): `G*0.59+R*0.30+B*0.11`,
  clamp 255.
These are the functions that turn the accumulated RGB into the rasterizer-facing
shade byte; the falloff LUT only shapes the per-light diffuse *weight* feeding the
accumulator.

---

## CityView3D / bind-site note (handoff — NOT edited here)

Objects are now lit by the **real per-vertex NdotL from the sun**: the object's
object-space normal is bone-world-transformed (and Y-rebiased) per vertex, dotted
with the sun direction, weighted by the falloff LUT (`flt_1405110` ==
`BuildFalloffLUT`), and summed into the ambient before finalize. The faithful entry
is the new bone-matrix overload:

```
render::LightMeshVertices(verts, n, ambient,
                          sunDir, sunColor, sunIntensity,
                          pointLights, m, objScale, falloffLUT,
                          boneMatrix /*ComputeBoneWorldMatrix output, 16 floats*/,
                          boneY      /*bone[+0x6C]*/,
                          boneScale  /*bone[+0x1D4]*/,
                          outShade)
```

The bind site (CityView3D object arm / scene_view, owned by other agents) should
prefer this overload when it has the object's bone-world matrix + bone Y/scale (the
type-7 vertex-lit objects, `*(obj+535)==1`). When it only has world-space normals
+ a world-space sun it may keep the wave-6 world-normal overload (the NdotL is the
same dot, just in a different frame). No bind-site file was edited (rule:
city_view3d.*/scene_view.* are owned elsewhere).

---

## Tests

`tests/unit/object_light_shade_test.cpp` — 13 tests, 63 checks, all pass
(8 pre-existing + 5 new):
- **(i) NormalTransformIdentity** — identity matrix, sy=1: pass-through + normalize.
- **(j) NormalTransformYBiasAndRotate** — Y pre-bias `(n.y-boneY)*(20/boneScale)`
  then a 90° X-rotation in the 4-stride layout (verifies the matrix indexing).
- **(k) NormalTransformZeroCollapses** — zero-length transformed normal → (0,0,0)
  (VIBE_Math_VectorNormalize behaviour).
- **(l) MeshSunBoneMatrixLitVsUnlit** — a transformed-normal-facing vertex gets the
  verbatim sun term (byte-equal to a hand-rolled transform+accumulate+finalize) and
  is brighter than a back-facing vertex (ambient only).
- **(m) MeshSunBoneMatrixNullDisablesSun** — no bone matrix → sun off → ambient-only,
  identical to the no-sun path.

Build: the four owned/read render TUs + util/math + coord + math_trig compile clean
and link; the full-tree `cmake --build` had transient errors only in OTHER agents'
concurrent files (mirror_project.h, particle_integrate.cpp) — none in owned files.

## Addresses touched (reference)
- `VIBE_Light_ApplyToCachedVertices` @0x5c6f90 (sun vertex arm @0x5c7129; normal
  transform @0x5c71b5; matrix build @0x5c709f→var_E0; Y terms @0x5c70a7/0x5c70b3)
- `VIBE_Transform_ComputeBoneWorldMatrix` @0x5c8fac (the matrix source)
- `VIBE_Math_VectorNormalize` @0x5cb148
- `VIBE_Light_InitFalloffTable` @0x5c88f8 (the `flt_1405110` writer, via `flt_140510C+4`)
- `VIBE_Floor_BuildTilePolys` @0x5bc45c (a reader of `flt_1405110` @0x5bc7a8)
- constants: flt_628C24/28/30 @0x628c24/28/30; flt_628CB4/B8 @0x628cb4/b8
