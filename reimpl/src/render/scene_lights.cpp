#include "render/scene_lights.h"

#include <cmath>

// =============================================================================
// guild::render — scene-light collection + per-object affected-light cull.
//
// 1:1 reconstruction of the cull predicate and the two-pass collect driver. See
// scene_lights.h for the engine flow and the exact field offsets / addresses.
//
// The cull is VIBE_Light_CollectAffectedObject @0x5c80a0; the driver that runs it
// (count pass + alloc + fill pass + ApplyToCachedVertices) is the head of
// VIBE_Light_BuildObjectCache @0x5c8218. The per-vertex accumulation the list
// feeds is wave-7's LightMeshVertices (object_light_shade.h) — NOT re-done here.
// =============================================================================
namespace guild::render {

// gilde.exe 0x5c80a0 (count-pass branch). The flags-bit constant 0x10 is the
// `*(_BYTE *)(a1 + 529) & 0x10` test in the type-7 arm; flt_5CA2B0 (== 0.0) is the
// sun's reference plane, not part of the affect test.
bool LightAffectsObject(const SceneLightNode& L, const LitObjectCullParams& O) {
    // The engine first transforms L's world point into O's bone-local space
    // (VIBE_Transform_PointToBoneLocalSpace) then takes the difference against O.
    // For the un-skinned, single-bone object the bone-local transform reduces to
    // the same world-space delta the cull subtracts (L.pos - O.pos); we operate
    // directly on the world positions the caller supplies, which is the value the
    // engine's PointToBoneLocalSpace yields for the city/universe object case.

    if (L.type == kSunLightType) {
        // sun arm @0x5c80f8:
        //   if (0.0 == intensity || (flags & 0x10)) return (not affected).
        if (L.intensity == 0.0f) return false;
        if (L.flags & 0x10u) return false;
        return true;
    }

    // point arm @0x5c8150:
    //   L.cullRadius(+484) = L.range(+144)
    //   v7/v8/v9 = L.pos - O.pos ; v10 = sqrt(v7^2 + v8^2 + v9^2)
    //   if (0.0 == intensity || L.cullRadius + O.cullRadius <= v10) return (not affected)
    const float cullRadius = L.range;  // +484 <- +144 (LINEAR; see header QUIRK)
    const float dx = L.pos[0] - O.pos[0];
    const float dy = L.pos[1] - O.pos[1];
    const float dz = L.pos[2] - O.pos[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (L.intensity == 0.0f) return false;
    if (cullRadius + O.cullRadius <= dist) return false;  // +484(L) + +484(O) <= dist
    return true;
}

// gilde.exe 0x5c8218 (the two-pass collect driver, lighting-relevant payload).
CollectedObjectLights CollectObjectLights(const std::vector<SceneLightNode>& scene,
                                          const LitObjectCullParams& O) {
    CollectedObjectLights out;

    // PASS A (count) + PASS B (fill) collapse to one walk here: the engine's pass B
    // appends every node whose +424 bit pass A set, with no recull, so iterating the
    // affected nodes in walk order reproduces the fill list byte-identically.
    //
    // ApplyToCachedVertices @0x5c6f90 processes the list in two arms:
    //   - point arm: every non-type-7 entry (walk order),
    //   - sun arm @0x5c704d: LOCATES the first type-7 entry (peek slot[0] @0x5c7044,
    //     else scan forward) and shades from it. The wave-7 consumer that this collect
    //     feeds (LightMeshVertices) takes a SINGLE sun dir/color/intensity, so we
    //     surface only the first affected sun here; multi-sun per-vertex accumulation
    //     stays inside ApplyToCachedVertices (not reconstructed in this module).
    for (const SceneLightNode& L : scene) {
        if (!LightAffectsObject(L, O))
            continue;

        if (L.type == kSunLightType) {
            // Only the first affected sun is surfaced (LightMeshVertices is single-sun).
            if (!out.hasSun) {
                out.hasSun = true;
                out.sunDir[0] = L.dir[0];
                out.sunDir[1] = L.dir[1];
                out.sunDir[2] = L.dir[2];
                out.sunColor[0] = L.color[0];
                out.sunColor[1] = L.color[1];
                out.sunColor[2] = L.color[2];
                out.sunIntensity = L.intensity;
            }
            continue;
        }

        // POINT light -> MeshPointLight (the LightMeshVertices record). The mapping
        // mirrors scene_view.cpp's PtLight build (+92 colour, +144 range,
        // +148 intensity, +152 rangeParam) and the +472 world position.
        MeshPointLight p;
        p.pos[0] = L.pos[0]; p.pos[1] = L.pos[1]; p.pos[2] = L.pos[2];
        p.color[0] = L.color[0]; p.color[1] = L.color[1]; p.color[2] = L.color[2];
        p.range = L.range;
        p.intensity = L.intensity;
        p.rangeParam = L.rangeParam;
        out.pointLights.push_back(p);
    }

    return out;
}

} // namespace guild::render
