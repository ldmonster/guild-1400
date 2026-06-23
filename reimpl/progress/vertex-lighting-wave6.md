# Wave-6 — Dynamic per-vertex LIGHTING (W6-LIGHT)

Owner: W6-LIGHT. Files: `src/render/vertex_lighting.{h,cpp}`,
`src/render/object_light_shade.{h,cpp}`, `src/render/env_map_walk.{h,cpp}`,
`src/render/light.{h,cpp}`, `tests/unit/object_light_shade_test.cpp`,
`tests/unit/env_map_walk_test.cpp`.

Goal: make the dynamic per-vertex lighting 1:1 — objects lit by the sun + scene
point lights (not a flat ambient). The original applies day/night brightness by
*rebuilding the per-object vertex lighting table*, then the 16bpp rasterizer
Gouraud-interpolates the per-vertex shade.

## The reconstructed call tree (the real one, rule 7)

```
VIBE_Light_BuildObjectCache            0x5c8218   the per-object REBUILD
  ├─ seed every vertex RGB accumulator (+12/+13/+14 of the 20-float cache entry,
  │  == vertex +48/+52/+56) from the GLOBAL AMBIENT flt_64A074/78/7C (= 200 each)
  ├─ VIBE_SceneGraph_WalkAndInvoke -> VIBE_Light_CollectAffectedObject  0x5c80a0
  │     collect the lights whose range reaches this object; the sun (type 7) is
  │     always collected when its intensity (+148) != 0 (0x5c80f8 early-out)
  ├─ VIBE_Light_ApplyToCachedVertices  0x5c6f90   THE LIGHT-LIST WALK (3 arms):
  │     · POINT lights (obj type != 7): per vertex
  │         d   = vpos - light(+118/+119/+120) ; dist2 = |d|^2
  │         if dist2 < light[+38]*?  (range^2 gate v105) :
  │         NdotL = vnormal . normalize(d)         (d points AWAY from the light)
  │         if NdotL < 0 :
  │            atten  = 1/(light[+38]*dist2) * (light[+37]*flt_628C20(10.0)*objI)
  │            factor = atten * flt_1405110[(int)(NdotL*flt_628C28(-1023))]
  │            accum += light[+23/+24/+25] * factor
  │       (there is a normal-present sub-branch (v28=+106+12) and a no-normal
  │        sub-branch that omits the NdotL gate — both same atten.)
  │     · DIRECTIONAL sun (obj type == 7), per-vertex branch (obj +535==1, 0x5c7129):
  │         dir = sun(+488 -> +392/+396/+400) ; NdotL = vnormal . normalize(dir)
  │         if NdotL < 0 :
  │            factor = sunI*flt_628C30(0.01)*objI * flt_1405110[(int)(NdotL*-1023)]
  │            accum += sunColor(+92/+96/+100) * factor
  │     · DIRECTIONAL sun, per-POLY branch (else, 0x5c7607): TriangleNormal . dir,
  │         intensity scale flt_628C2C(0.001), accumulated to all 3 poly corners.
  ├─ FINALIZE per vertex (byte_649D70 selects):
  │     · software/colour branch (0x5c8346): m=max(R,G,B); if m>254.99998 scale by
  │       255/m; store truncated B/G/R at +68/+69/+70  (FinalizeVertexShadeSoftware)
  │     · hardware/luma branch (0x5c84bc): G*0.59+R*0.30+B*0.11, clamp 255 -> +70
  │       (FinalizeVertexShadeLuma)
  ├─ VIBE_Light_ApplyVertexShading     0x5c7f04   per-material modulate
  │     shade = (R*0.30+G*0.59+B*0.11 + texHi)*(1/256)*texLo, clamp 255
  │       (ApplyMaterialVertexShade)
  └─ publish the shade dword (+68..+71 over +64..+67); Vertex::lightIdx(+66)=byte+70

VIBE_Light_IlluminateObject            0x5c7804   the single-light variant
      (same point-light inner loop; emits a per-object light list; uses
       flt_628C4C(10.0) / flt_628C50(-1023) / flt_628C54/58/5C luma / flt_628C60)

VIBE_Mesh_ComputeVertexLighting        0x5c9054   ENV-MAP reflection UV (NOT diffuse)
      reflect the position about the bone-rotated normal, map to spherical env UV
      (+32/+36).  Reconstructed in vertex_lighting.cpp + env_map_walk.cpp.

VIBE_Light_InitFalloffTable            0x5c88f8   the 1024-entry angular falloff LUT
      out[k] = 1 - (2/pi)*acos(k * flt_628CB4) ; consumer indexes by NdotL*-1023.
```

## Exact constants (get_bytes — little-endian float)

| symbol | bytes | value | role |
|---|---|---|---|
| flt_628C20 | `00 00 20 41` | 10.0 | point-light intensity scale (ApplyToCachedVertices) |
| flt_628C24 | `00 00 a0 41` | 20.0 | bone-space sun normalize term |
| flt_628C28 | `00 c0 7f c4` | -1023.0 | falloff LUT index scale |
| flt_628C2C | `6f 12 83 3a` | 0.001 | sun per-POLY intensity scale |
| flt_628C30 | `0a d7 23 3c` | 0.01  | sun per-VERTEX intensity scale |
| flt_628C4C | `00 00 20 41` | 10.0 | point intensity scale (IlluminateObject) |
| flt_628C50 | `00 c0 7f c4` | -1023.0 | falloff index (IlluminateObject) |
| flt_628C54/58/5C | — | 0.59 / 0.30 / 0.11 | IlluminateObject luma weights |
| flt_628C60 | `00 00 7f 43` | 255.0 | IlluminateObject normalize cap |
| flt_628C88/8C/90 | — | 0.59 / 0.30 / 0.11 | BuildObjectCache luma weights |
| flt_628C94 | `00 00 7f 43` | 255.0 | BuildObjectCache normalize cap |
| flt_64A074/78/7C | `00 00 48 43` (x3) | 200.0 | global ambient seed (per channel) |

## What is reconstructed 1:1 here

- **Per-light accumulation kernels** (`light.cpp`): `AccumulatePointLight`,
  `AccumulateDirectionalLight`, `BuildFalloffLUT`, plus the finalize/material
  kernels in `light.h`/`object_light_shade.cpp`
  (`ComputeGrayShade`/`ComputeColorShade`/`ApplyVertexShade`,
  `FinalizeVertexShadeSoftware`/`FinalizeVertexShadeLuma`/`ApplyMaterialVertexShade`,
  `PointLightDiffuse`). Pre-existing; verified against the decompile this wave.
- **NEW — the consolidated per-mesh entry** `LightMeshVertices`
  (`object_light_shade.{h,cpp}`): the clean entry the brief asks for. It runs the
  exact `BuildObjectCache` flow as ONE unit — seed ambient -> walk the sun +
  point lights (`ApplyToCachedVertices`) -> finalize to per-vertex shade bytes.
  It consumes **W6-SUN's sun vector** directly (`sunDir`), uses the correct
  per-vertex sun scale `flt_628C30 = 0.01` (exposed as `kSunVertexIntensityScale`),
  and disables the sun when `sunIntensity <= 0` (the `0.0 == light+148` early-out).
- **Env-map reflection UV** (`vertex_lighting.cpp`, `env_map_walk.cpp`): the
  VIBE_Mesh_ComputeVertexLighting @0x5c9054 spherical reflect map + the skinned /
  non-skinned object walk. (This is reflection, not diffuse; kept here because the
  original named it "ComputeVertexLighting".)

## Boundaries (rule 8, NAMED, not faked)

- **flt_1405110 shade-ramp LUT** is all-zero in the static image (a runtime-built
  .bss table; no writer among its xref sites). With the live engine ramp the diffuse
  *ramp* term evaluates per the LUT; the angular falloff LUT
  (`BuildFalloffLUT` @0x5c88f8) IS reconstructed and is the table `LightMeshVertices`
  / the kernels index. The point/sun kernels are parameterised on the LUT so they
  are golden-testable and faithful regardless of which table the caller supplies.
- The **scene-graph collect walk** (`VIBE_Light_CollectAffectedObject` @0x5c80a0 +
  `VIBE_SceneGraph_WalkAndInvoke`) that *gathers* which lights reach an object is the
  caller's responsibility — `LightMeshVertices` takes the already-collected light
  list (the engine's two-pass alloc-then-fill produces exactly this list).

## THE EXACT BIND-SITE HANDOFF (CityView3D / scene_view)

CityView3D lights objects per frame from the sun + scene lights **before the object
draw**, inside the universe frame:

```
render::BeginUniverseFrame  (0x5b3900)
  -> 0x5b3a2f terrain arm
  -> for each drawable object (type 1/4, hasMesh):
       build world-space vertex positions + smoothed world normals
       *** render::LightMeshVertices(verts, n, ambient,
                                     sunDir, sunColor, sunIntensity,
                                     pointLights, m, objScale, falloffLUT,
                                     outShade) ***          <-- THE HANDOFF
       feed outShade[i] as the per-vertex Gouraud shade into RasterizeMeshList
  -> RasterizeMeshList (0x5aec88) -> RasterizeTexturedTriangleRgbz (0x5F6C30)
  -> PresentToDevice
```

Inputs at the bind site:
- `ambient[3]`  : `play::ComputeSceneAmbient` (the day/night band rig -> the global
  ambient store flt_64A074/78/7C; 200,200,200 default).
- `sunDir/sunColor/sunIntensity` : **W6-SUN** (the sun direction transition state
  dword_62D568/56C/570; sun_state.h). `sunIntensity <= 0` => sun off.
- `pointLights[]` : the scene's `hasLight && type != 7` instances (world position
  from the parent-composed xform; colour +92/+96/+100; range +144; intensity +148;
  rangeParam +152/+38).
- `objScale` : the per-object marker scale (the engine's *(obj+492)+2296 object
  intensity, folded into every light's attenuation).
- `falloffLUT` : `render::BuildFalloffLUT` once per frame (or cached).

GATE: only runs when the object-lighting path is enabled (the engine always runs it;
the portable build gates on `Options::bakedLighting`). **scene_view.cpp currently
inlines this exact walk** (lines ~640-678) using the same `light.cpp` kernels;
`LightMeshVertices` consolidates it into the faithful one-call entry — the
orchestrator should point that bind site (and city_view3d's object arm) at
`render::LightMeshVertices`. (scene_view.cpp / city_view3d.cpp are bind sites owned
by other agents; NOT edited here.)

## Tests

`tests/unit/object_light_shade_test.cpp` (8 tests, 43 checks, all pass):
- finalize kernels (a-e, pre-existing): ambient pass-through, software normalise,
  luma reduce/clamp, material modulation, point-light diffuse gates.
- **(f) MeshAmbientOnly** — no lights -> ambient shade verbatim.
- **(g) MeshSunBranchMatchesKernel** — back-facing vertex (NdotL>=0) gets no sun;
  facing vertex matches a hand-rolled accumulate+finalize and is brighter.
- **(h) MeshPointPlusSun** — point light + sun summed into the accumulator before
  finalize, byte-equal to the hand-rolled AccumulatePointLight + sun term.

`tests/unit/env_map_walk_test.cpp` (pre-existing) covers the reflection-UV walk.

Build note: the full-tree build had transient errors in OTHER agents' concurrent
files (sprite_scale.h, water_render.cpp, city_view3d.cpp); the four owned files
compile clean standalone and the test suite passes (verified by direct compile +
link of object_light_shade.o + light.o against the test framework).
