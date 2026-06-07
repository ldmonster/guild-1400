// =============================================================================
// guild::play — WIRE REAL CHARACTER-ANIMATION PLAYBACK implementation.
// See wire_char_anim.h. Additive integration glue: drives render::SamplePosedMesh
// (real `.baf` morph blend, Wave 28) into the live render through the PUBLIC
// play::MeshResolver hook (object_mesh_render.h). No owned file is edited.
// =============================================================================
#include "play/wire_char_anim.h"

#include <cmath>

namespace guild::play {

// ---------------------------------------------------------------------------
render::MeshGeometry* PosedGeometry::View() {
    geom.vertices    = vertices.empty() ? nullptr : vertices.data();
    geom.polygons    = polygons.empty() ? nullptr : polygons.data();
    geom.polyCount   = (i32)polygons.size();
    geom.polyCap     = (i32)polygons.size();
    geom.vertexCount = (i32)vertices.size();
    return &geom;
}

// ---------------------------------------------------------------------------
// Build a posed MeshGeometry at time `t`.
//   * Vertex positions come from SamplePosedMesh(clip, t) — the REAL morph blend.
//   * When a static base mesh is supplied AND its vertex count matches the morph
//     vertex count, the base polygons + UVs are reused (rebound to the posed verts),
//     so the posed mesh keeps the engine's exact topology — exactly what
//     VIBE_Mesh_InterpolateMorphVertices does (it overwrites positions in place and
//     leaves the topology / UVs untouched).
//   * Otherwise we synthesize a deterministic sequential-triangle topology over the
//     posed points (so a clip with no matching base still draws as geometry).
// ---------------------------------------------------------------------------
PosedGeometry BuildPosedGeometry(const AnimBinding& b, float t) {
    PosedGeometry out;
    if (!b.clip || !b.clip->valid) return out;

    render::PosedMesh pm = render::SamplePosedMesh(*b.clip, t);
    if (!pm.valid || pm.vertexCount <= 0) return out;

    const int nv = pm.vertexCount;
    out.vertices.assign((size_t)nv, render::Vertex{});
    for (int i = 0; i < nv; ++i) {
        render::Vertex& dst = out.vertices[(size_t)i];
        dst.x = pm.points[(size_t)i * 3 + 0];
        dst.y = pm.points[(size_t)i * 3 + 1];
        dst.z = pm.points[(size_t)i * 3 + 2];
    }

    // Topology / UV pass-through from the base mesh when the counts line up.
    bool reusedBase = false;
    if (b.base && b.base->vertices && b.base->polygons &&
        b.base->vertexCount == nv && b.base->polyCount > 0) {
        // Copy UVs/light from the base verts (same index space).
        for (int i = 0; i < nv; ++i) {
            const render::Vertex& src = b.base->vertices[i];
            render::Vertex& dst = out.vertices[(size_t)i];
            dst.u        = src.u;
            dst.v        = src.v;
            dst.color0   = src.color0;
            dst.lightIdx = src.lightIdx;
        }
        // Rebind each base polygon's vertex pointers into our posed vertex array,
        // preserving winding / UVs / flags. Skip polys that index out of range.
        out.polygons.reserve((size_t)b.base->polyCount);
        const render::Vertex* base0 = b.base->vertices;
        for (int p = 0; p < b.base->polyCount; ++p) {
            const render::Polygon& sp = b.base->polygons[p];
            long i0 = sp.v0 ? (sp.v0 - base0) : -1;
            long i1 = sp.v1 ? (sp.v1 - base0) : -1;
            long i2 = sp.v2 ? (sp.v2 - base0) : -1;
            if (i0 < 0 || i0 >= nv || i1 < 0 || i1 >= nv || i2 < 0 || i2 >= nv)
                continue;
            render::Polygon np = sp;
            np.v0 = &out.vertices[(size_t)i0];
            np.v1 = &out.vertices[(size_t)i1];
            np.v2 = &out.vertices[(size_t)i2];
            out.polygons.push_back(np);
        }
        reusedBase = !out.polygons.empty();
    }

    if (!reusedBase) {
        // Deterministic synthetic topology: sequential triangles over the posed
        // points (i, i+1, i+2). Enough to give the posed point cloud drawable
        // geometry that moves with the morph. Winding is fixed; UVs zero.
        out.polygons.clear();
        for (int i = 0; i + 2 < nv; ++i) {
            render::Polygon np{};
            np.v0 = &out.vertices[(size_t)i];
            np.v1 = &out.vertices[(size_t)i + 1];
            np.v2 = &out.vertices[(size_t)i + 2];
            out.polygons.push_back(np);
        }
    }

    out.valid = true;
    out.View();
    return out;
}

// ---------------------------------------------------------------------------
// Advance one binding one tick. The continuous time is (frame + subPhase); we add
// stepPerTick to subPhase, carry whole frames through AdvanceFrameIndex over the
// clip's [start..end] loop range, and keep subPhase in [0,1). No clip -> no-op.
// ---------------------------------------------------------------------------
void AdvanceBinding(AnimBinding& b) {
    if (!b.clip || !b.clip->valid) return;
    int fc = b.clip->FrameCount();
    if (fc <= 0) return;
    int start = b.clip->StartFrame();
    int end   = b.clip->EndFrame();
    int last  = fc - 1;
    if (end <= start || end > last) { start = 0; end = last; }

    b.subPhase += b.stepPerTick;
    // Carry whole frames; render::AdvanceFrameIndex (0x5ccf18) enforces the
    // loop/clamp/ping-pong policy (params: flags, cur, last=end, first=start, count).
    while (b.subPhase >= 1.0f) {
        b.subPhase -= 1.0f;
        b.frame = (int)render::AdvanceFrameIndex(b.mode, b.frame, end, start, last);
        if (b.frame < 0) b.frame = 0;
        if (b.frame > last) b.frame = last;
    }
}

// ---------------------------------------------------------------------------
// CharAnimDriver
// ---------------------------------------------------------------------------
void CharAnimDriver::Bind(i32 entityId, const render::AnimClip* clip,
                          const render::MeshGeometry* base, u8 mode, float stepPerTick) {
    auto slot = std::make_unique<Slot>();
    slot->binding.clip        = clip;
    slot->binding.base        = base;
    slot->binding.mode        = mode;
    slot->binding.stepPerTick = stepPerTick;
    slot->binding.subPhase    = 0.0f;
    slot->binding.frame       = (clip && clip->valid) ? clip->StartFrame() : 0;
    bindings_[entityId] = std::move(slot);
}

void CharAnimDriver::Unbind(i32 entityId) { bindings_.erase(entityId); }

bool CharAnimDriver::IsBound(i32 entityId) const {
    return bindings_.find(entityId) != bindings_.end();
}

const AnimBinding* CharAnimDriver::BindingFor(i32 entityId) const {
    auto it = bindings_.find(entityId);
    return it == bindings_.end() ? nullptr : &it->second->binding;
}
AnimBinding* CharAnimDriver::BindingFor(i32 entityId) {
    auto it = bindings_.find(entityId);
    return it == bindings_.end() ? nullptr : &it->second->binding;
}

int CharAnimDriver::Tick() {
    int n = 0;
    for (auto& kv : bindings_) { AdvanceBinding(kv.second->binding); ++n; }
    return n;
}

const render::MeshGeometry* CharAnimDriver::ResolvePosed(const EntityRef& e) {
    auto it = bindings_.find(e.id);
    if (it == bindings_.end()) return nullptr;
    Slot* s = it->second.get();
    s->posed = BuildPosedGeometry(s->binding, BindingTime(s->binding));
    if (!s->posed.valid) return nullptr;
    return s->posed.View();
}

// ---------------------------------------------------------------------------
// Installable hook + inert default (defined here so the unified build links).
// ---------------------------------------------------------------------------
namespace {
CharAnimDriver* g_activeDriver = nullptr;
}

void InstallCharAnimDriver(CharAnimDriver* d) { g_activeDriver = d; }
CharAnimDriver* ActiveCharAnimDriver() { return g_activeDriver; }

const render::MeshGeometry* PosedMeshResolver(const EntityRef& e) {
    CharAnimDriver* d = g_activeDriver;
    if (!d) return nullptr;          // inert: no playback installed -> static mesh
    return d->ResolvePosed(e);
}

} // namespace guild::play
