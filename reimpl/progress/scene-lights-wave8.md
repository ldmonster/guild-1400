# Scene-Light Collection + Per-Object Cull (wave-8, W8-SCENELIGHTS)

The DATA-PLUMBING that feeds wave-7's per-vertex dynamic lighting
(`render::LightMeshVertices`, `object_light_shade.h`). The render kernels existed;
what was missing was the engine's **scene-light collection** (which lights are in
the scene) and the **per-object affected-light cull** (which of them light THIS
object). Both are reconstructed 1:1 here.

Module: `src/render/scene_lights.{h,cpp}` (namespace `guild::render`).
Tests:  `tests/unit/scene_lights_test.cpp` (10 tests / 30 checks, all pass).

## Engine functions reconstructed (addresses)

| Address    | Engine symbol                         | What it does                                   | Status |
|------------|---------------------------------------|------------------------------------------------|--------|
| 0x5c80a0   | `VIBE_Light_CollectAffectedObject`    | per-light cull callback (count + fill passes)  | DONE — `LightAffectsObject` |
| 0x5c8218   | `VIBE_Light_BuildObjectCache`         | the two-pass collect DRIVER (count/alloc/fill) | DONE (collect payload) — `CollectObjectLights` |
| 0x5ac738   | `VIBE_SceneGraph_WalkAndInvoke`       | child/sibling pre-order walk driving the cb    | MODELLED as iteration over the walk-order light list |
| 0x5c6f90   | `VIBE_Light_ApplyToCachedVertices`    | the CONSUMER (per-vertex accumulate)           | wave-7 (`LightMeshVertices`) — CALLED, not redone |
| 0x5c7e58   | `VIBE_Light_PrepareObjectCache`       | sets object +484 = radius^2 (the O.cullRadius) | referenced (the squared bound the cull reads)  |
| 0x5c8c40   | `VIBE_Transform_PointToBoneLocalSpace`| L.pos -> O bone-local space before the delta   | referenced (reduces to world delta for the city/universe single-bone object) |

## The cull (CollectAffectedObject @0x5c80a0), exact

Per scene light node `L` against lit object `O` (count pass, `ctx.list == NULL`):

```
L.affected = 0
transform L's world point into O's bone-local space (PointToBoneLocalSpace)
if L.type(+533) == 7 (DIRECTIONAL "sun"):
    if L.intensity(+148) == 0.0  OR (L.flags(+529) & 0x10)  -> NOT affected
    else: L.affected = 1 ; ctx.count++
else (POINT light):
    L.cullRadius(+484) = L.range(+144)          <-- LINEAR range, NOT squared
    dist = |L.pos(+472..480) - O.pos(+472..480)|     (euclidean sqrt)
    if L.intensity(+148) == 0.0  OR  L.cullRadius + O.cullRadius <= dist
        -> NOT affected
    else: L.affected = 1 ; ctx.count++
```

Fill pass (`ctx.list != NULL`): append every node whose +424 affected bit the count
pass set — **no recull**. We collapse the two passes into one walk over the same
nodes (the affected state persists), which is byte-identical to the engine's result.

### ENGINE QUIRK preserved (rule 1)
The point cull adds `L.cullRadius` (a **linear** range from +144) to `O.cullRadius`
(a **squared** bound — PrepareObjectCache @0x5c7e58 stores `radius*radius` into
O+484) and compares against the **linear** distance. The operands are in mixed units.
This is exactly what `gilde.exe` does; `LitObjectCullParams.cullRadius` is documented
to carry the object's **squared** bound and `SceneLightNode.range` the light's
**linear** range, so a faithful caller reproduces the binary's stores.

### Sun arm has no distance term
The type-7 branch never reads position — a sun with non-zero intensity and the 0x10
flag clear affects the object regardless of distance (test `SunIgnoresDistance`).

## Constants recovered (get_bytes)

| Addr       | Bytes (LE)      | Value  | Role |
|------------|-----------------|--------|------|
| 0x628c20   | 00 00 20 41     | 10.0   | flt_628C20 point-light intensity scale (ApplyToCachedVertices) |
| 0x628c30   | 0a d7 23 3c     | 0.01   | flt_628C30 per-vertex SUN intensity scale (type-7 vertex arm) |
| 0x628c2c   | 6f 12 83 3a     | 0.001  | flt_628C2C per-poly sun scale (the OTHER, non-vertex sun arm) |
| 0x628c28   | 00 c0 7f c4     | -1023  | flt_628C28 LUT index = NdotL * this |
| 0x5ca2b0   | 00 00 00 00     | 0.0    | flt_5CA2B0 sun reference plane (RotateVectorWithFrame; not in affect test) |

(The luma/ambient/falloff constants live in `object_light_shade.h` / `light.h`,
already recovered in wave-7; this module only adds the cull/collect layer.)

## Field offsets (engine object record), from the decompile

```
+92   light colour RGB        (v5[23..25] / SceneLightNode.color)
+132  sun direction           (type 7; SceneLightNode.dir)  -- *(*(L+488)+392/396/400)
+144  light range (LINEAR)    (-> +484 in the cull; SceneLightNode.range)
+148  light intensity         (0.0 == OFF; SceneLightNode.intensity)
+152  rangeParam / falloff    (SceneLightNode.rangeParam)
+472/476/480  world position  (SceneLightNode.pos / LitObjectCullParams.pos)
+484  cull radius             (light: LINEAR range; object: radius^2)
+529  flags (low byte)        (bit 0x10 disables a sun; SceneLightNode.flags)
+533  type byte               (7 == sun; SceneLightNode.type)
+488 +424  per-frame "affected" bit (count pass sets; fill pass reads)
```

## Clean entry (the brief's handoff API)

```cpp
struct SceneLightNode { int type; float pos[3], color[3], dir[3], range, intensity, rangeParam; u32 flags; };
struct LitObjectCullParams { float pos[3]; float cullRadius; /* radius^2 */ };
struct CollectedObjectLights { std::vector<MeshPointLight> pointLights; bool hasSun; float sunDir[3], sunColor[3], sunIntensity; };
struct SceneLightSet { std::vector<SceneLightNode> lights; };  // collected ONCE per frame

bool LightAffectsObject(const SceneLightNode&, const LitObjectCullParams&);          // the cull
CollectedObjectLights CollectObjectLights(const std::vector<SceneLightNode>&, const LitObjectCullParams&);
CollectedObjectLights CullForObject(const SceneLightSet&, const LitObjectCullParams&); // per-object
```

`MeshPointLight` is wave-7's `object_light_shade.h` record — `CollectObjectLights`
emits the exact form `LightMeshVertices` consumes (no adapter needed at the bind site).

Only ONE sun is reported (`hasSun`): `ApplyToCachedVertices`' sun arm @0x5c704d
BREAKS at the first type-7 affected light, so the first affected sun in walk order
wins (`FirstSunWins` test).

## EXACT CityView3D handoff (for the orchestrator to wire)

`CityView3D` owns the object arm (`doSceneWalk`, `src/play/city_view3d.cpp` ~1609).
Today each city instance gets a flat `ambientShade` (line ~1793) — no per-vertex
NdotL, because the simplified instance pipeline carried no scene light list. This
module supplies that list + cull. The wiring (orchestrator edits city_view3d, NOT
this agent):

1. **Collect ONCE per frame** (before the instance loop, alongside the ambient
   computation ~line 1637). Build a `render::SceneLightSet` from the parsed scene
   light nodes — `scene_view.cpp` already parses each light's fields into
   `SceneObjectInst` (`hasLight`, `lightColor` +92, `lightDir` +132,
   `lightParam` = {+144 range, +148 intensity, +152 rangeParam}, `type` +533).
   For each `hasLight` object push a `SceneLightNode`:
   ```cpp
   render::SceneLightNode n;
   n.type = inst.type;                                   // +533 (7 == sun)
   n.pos[0..2]  = worldPos(inst);                        // +472 world point
   n.color[0..2]= inst.lightColor;                       // +92
   n.dir[0..2]  = inst.lightDir;                         // +132
   n.range      = inst.lightParam[0];                    // +144
   n.intensity  = inst.lightParam[1];                    // +148
   n.rangeParam = inst.lightParam[2];                    // +152
   // n.flags from the object flag byte if available (+529); 0 otherwise.
   set.lights.push_back(n);
   ```
   This mirrors the `ptLights`/`dirLights` build already in `scene_view.cpp`
   (~840-852) — same record layout, now centralized for per-object culling.

2. **Cull per object + light it** (replace the flat-ambient line ~1793). Per drawn
   instance, before/at the vertex shade step:
   ```cpp
   render::LitObjectCullParams O;
   O.pos[0..2]  = inst.pos;             // object world point (+472)
   O.cullRadius = objRadiusSq;          // +484 == radius^2 (PrepareObjectCache)
   render::CollectedObjectLights cl = render::CullForObject(set, O);

   // Need WORLD positions + smoothed WORLD normals per vertex -> MeshLightVertex[].
   // (Named gap, wave-6/7: the simplified instance pipeline does not yet carry
   //  per-vertex world normals; until it does, pass the ambient-only path. Once it
   //  does, this is the full per-vertex dynamic light.)
   render::LightMeshVertices(meshVerts, vc, ambient,
                             cl.sunDir, cl.sunColor, cl.hasSun ? cl.sunIntensity : 0.0f,
                             cl.pointLights.data(), (int)cl.pointLights.size(),
                             objScale, falloffLut, outShade /* per-vertex */);
   // then write outShade[i] -> fm.verts[i].lightIdx (B/G/R or luma per byte_649D70).
   ```

This is the genuine engine sequence: `BuildObjectCache` collects the affected light
list ONCE per object via the scene-graph walk, then `ApplyToCachedVertices` lights
its vertices. The collect-once `SceneLightSet` hoists the scene walk out of the
per-object loop (the engine re-walks per object; hoisting is a behavior-identical
optimization since the light set is frame-constant).

### Standing named gap (rule 8, unchanged from wave-7)
- **Per-vertex world normals**: the city instance pipeline does not yet carry the
  smoothed world normal per vertex that `LightMeshVertices`' NdotL needs. Until that
  is plumbed (wave-6/7 frame-integration gap), the object stays ambient-dominant —
  which is what the universe driver renders today. The cull/collect layer is complete
  and ready; the consumer is wired and tested end-to-end in `FeedsLightMeshVertices`.
- **Shade-ramp LUT (flt_1405110)**: all-zero in the static image (runtime-built; no
  writer recovered). With the live ramp the per-light NdotL term is zero, so shading
  is ambient-dominant. Documented in wave-7; not this module's boundary.

## Tests (tests/unit/scene_lights_test.cpp)

| Test | Covers |
|------|--------|
| PointInRangeAffects     | the `<=` boundary of `range + objRadius` vs dist |
| PointOutOfRangeCulled   | distance cull |
| ZeroIntensityCulled     | the `0.0 == +148` early-out (point + sun) |
| SunFlagCull             | the 0x10 disable bit; other bits ignored |
| SunIgnoresDistance      | sun arm has no distance term (quirk) |
| CollectPartitionsAndOrders | point/sun split, far-light drop, walk-order preserved, field mapping |
| FirstSunWins            | only the first affected sun (ApplyToCachedVertices break) |
| EmptyAndAllCulled       | empty + all-culled scenes |
| FeedsLightMeshVertices  | the full handoff: SceneLightSet -> CullForObject -> LightMeshVertices |
| DisabledSunNotFed       | disabled sun never feeds the consumer |

10 tests, 30 checks, 0 failures. No assets required.
