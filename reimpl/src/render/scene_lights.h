#pragma once
#include "guild/common/types.h"
#include "render/object_light_shade.h"  // MeshPointLight (the LightMeshVertices light record)

#include <vector>

// =============================================================================
// guild::render — the SCENE-LIGHT COLLECTION + per-object affected-light CULL.
//
// This is the SOURCE/driver layer that feeds the wave-7 per-vertex lighting core
// (object_light_shade.h LightMeshVertices). The render kernels already exist; what
// was missing is the engine's "which lights are in the scene, and which of them
// affect THIS object" walk. That is reconstructed here, 1:1, from:
//
//   0x5c8218  VIBE_Light_BuildObjectCache        (the per-object collect driver:
//                                                  walk scene graph, build affected
//                                                  light list, apply to vertices)
//   0x5c80a0  VIBE_Light_CollectAffectedObject   (the per-light cull callback the
//                                                  walk invokes — count pass + fill
//                                                  pass; the type==7 sun vs point cut)
//   0x5ac738  VIBE_SceneGraph_WalkAndInvoke      (the child/sibling scene-graph walk
//                                                  that drives the callback)
//   0x5c6f90  VIBE_Light_ApplyToCachedVertices   (the consumer — wave-7 already owns
//                                                  the per-vertex math; see below)
//
// THE ENGINE FLOW (BuildObjectCache @0x5c8218, exact)
// ---------------------------------------------------------------------------
//   1. seed every vertex's RGB accumulator to the global ambient (flt_64A074/78/7C);
//   2. PASS A (count): VIBE_SceneGraph_WalkAndInvoke(root, CollectAffectedObject,
//      mask=10, ctx) with ctx.list == NULL — counts how many scene light nodes
//      affect this object (ctx.count), marking each affected light's +424 bit;
//   3. allocate ctx.list = count * 4 bytes ("d3_light:cache");
//   4. PASS B (fill): the SAME walk with ctx.list != NULL — append each affected
//      light pointer to ctx.list (the marked-bit short-circuit, no recull);
//   5. VIBE_Light_ApplyToCachedVertices(obj, ctx) — accumulate each listed light's
//      diffuse term into the vertex accumulators (wave-7 owns this math);
//   6. finalize the accumulators to the per-vertex shade bytes.
//
// THE CULL (CollectAffectedObject @0x5c80a0, exact)
// ---------------------------------------------------------------------------
// Per scene node `L` (a candidate light) against the lit object `O`:
//   * COUNT pass (ctx.list == NULL):
//       L.affected = 0
//       transform L's world point into O's bone-local space (PointToBoneLocalSpace)
//       if L.type == 7 (the DIRECTIONAL "sun"):
//           if L.intensity (+148) == 0.0  OR (L.flags(+529) & 0x10)  -> not affected
//           else: L.affected = 1 ; ctx.count++
//       else (POINT light):
//           L.cullRadius (+484) = L.range (+144)              <-- NOTE: NOT squared
//           dist = |L.pos(+472..) - O.pos(+472..)|            (euclidean, sqrt)
//           if L.intensity (+148) == 0.0  OR  L.cullRadius + O.cullRadius <= dist
//               -> not affected
//           else: L.affected = 1 ; ctx.count++
//   * FILL pass (ctx.list != NULL):
//       if L.affected (+424 bit): ctx.list[ctx.count++] = &L   (no recull)
//
// ENGINE QUIRK (reproduced 1:1, rule 1): the point cull compares
//   L.cullRadius (a LINEAR range, +144) + O.cullRadius (a SQUARED bound, set in
//   PrepareObjectCache @0x5c7e58 as radius*radius into O+484) against the LINEAR
//   distance. The two operands are in different units; this is exactly what the
//   binary does and we keep it. CullParams below carries both so a faithful caller
//   passes the object's SQUARED bound for O.cullRadius and the light's LINEAR range
//   for L.range, matching the binary's stores.
//
// 1:1 NOTE on PASS B: pass B does NOT re-run the geometric cull — it appends every
// node whose +424 "affected" bit pass A set. We model that with the same callback
// run twice over the same nodes (the affected[] state persists between passes), so
// the result is byte-identical to the engine's two-pass walk.
// =============================================================================
namespace guild::render {

// The DIRECTIONAL "sun" type tag — object byte +533 == 7 (the type==7 branch in
// CollectAffectedObject / ApplyToCachedVertices). Any other type is a POINT light.
inline constexpr int kSunLightType = 7;

// One scene light node, as the collector sees it (the lighting-relevant fields of
// the engine object record the walk visits). Field offsets in the engine record:
//   type       : +533 (7 == sun/directional)
//   pos        : +472/+476/+480 (world position; the +472 block in CollectAffected)
//   range      : +144 (the LINEAR cull range; stored into +484 by the cull)
//   intensity  : +148 (0.0 == light OFF -> never affects; the engine early-out)
//   color      : +92  (RGB; v5[23..25] in ApplyToCachedVertices)
//   dir        : +132 (the sun direction, type 7) -- *(*(L+488)+392/396/400)
//   rangeParam : +152 (the stored falloff denom field, +38 float == v104)
//   flags      : +529 (bit 0x10 disables a type-7 sun in the cull)
struct SceneLightNode {
    int   type = -1;
    float pos[3] = {0, 0, 0};
    float color[3] = {0, 0, 0};
    float dir[3] = {0, 0, 0};
    float range = 0.0f;       // +144
    float intensity = 0.0f;   // +148
    float rangeParam = 0.0f;  // +152
    u32   flags = 0;          // +529 (low byte; bit 0x10 used)
};

// The object being lit, as the cull reads it (PrepareObjectCache @0x5c7e58 state).
//   pos        : +472/+476/+480 (the object's world point — its bone-local origin)
//   cullRadius : +484 (the object's SQUARED bound, set as radius*radius)
struct LitObjectCullParams {
    float pos[3] = {0, 0, 0};
    float cullRadius = 0.0f;  // +484 (radius^2 — see ENGINE QUIRK above)
};

// gilde.exe 0x5c80a0 — VIBE_Light_CollectAffectedObject (count-pass predicate).
// Returns true when light `L` affects object `O` (i.e. the engine would set L's
// +424 affected bit and increment the count). 1:1 with the count-pass branch:
//   * sun  (type==7): intensity!=0 AND !(flags & 0x10)
//   * point         : intensity!=0 AND (L.range + O.cullRadius > dist)
// `dist` is the euclidean distance between L.pos and O.pos.
bool LightAffectsObject(const SceneLightNode& L, const LitObjectCullParams& O);

// The result of collecting a scene's lights for one object: the point lights (in
// LightMeshVertices' MeshPointLight form) and the single directional sun (if any).
struct CollectedObjectLights {
    std::vector<MeshPointLight> pointLights;  // type != 7 affected lights
    bool  hasSun = false;                     // a type==7 sun affected the object
    float sunDir[3] = {0, 0, 0};              // the affected sun's direction (+132)
    float sunColor[3] = {0, 0, 0};            // the affected sun's colour (+92)
    float sunIntensity = 0.0f;                // the affected sun's intensity (+148)
};

// gilde.exe 0x5c8218 (the two-pass collect driver) — for object `O`, walk the
// scene's light list `scene` and gather every light that affects it, in the form
// LightMeshVertices consumes. Reproduces the engine's count+fill walk: a light is
// included iff LightAffectsObject(L, O). The list ORDER matches the walk order
// (the order lights appear in `scene` — the engine's child/sibling pre-order).
//
// At most ONE directional sun is reported (CollectedObjectLights.hasSun): the
// ApplyToCachedVertices sun arm @0x5c704d locates the first type==7 affected light
// (peek slot[0] @0x5c7044, else scan forward), and the wave-7 consumer it feeds
// (LightMeshVertices) takes a single sun, so only the first affected sun in walk
// order is surfaced here.
CollectedObjectLights CollectObjectLights(const std::vector<SceneLightNode>& scene,
                                          const LitObjectCullParams& O);

// =============================================================================
// THE CLEAN ENTRY (the brief's handoff): collect the scene's lights ONCE, then
// cull per object before LightMeshVertices.
//
// SceneLightSet is the per-FRAME collection: the scene's full light list, gathered
// once from the parsed scene LightSource records (the engine builds this from the
// scene graph; the universe/city driver parses it from the .ed3/.cty into the
// SceneObjectInst light fields — see CityView3D handoff in scene-lights-wave8.md).
// CullForObject then runs the per-object affected-light cut for each drawn object.
// =============================================================================
struct SceneLightSet {
    std::vector<SceneLightNode> lights;   // every scene light node, in walk order
};

// Cull `set`'s lights for object `O` -> the affected point lights + sun for
// LightMeshVertices. Thin wrapper over CollectObjectLights (the per-object cull).
inline CollectedObjectLights CullForObject(const SceneLightSet& set,
                                           const LitObjectCullParams& O) {
    return CollectObjectLights(set.lights, O);
}

} // namespace guild::render
