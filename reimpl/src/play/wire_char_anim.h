#pragma once
// =============================================================================
// guild::play — WIRE REAL CHARACTER-ANIMATION PLAYBACK INTO THE LIVE RENDER
// (PLAYABLE_PLAN P2/P6).
//
// Wave 28 built render/agf_anim.{h,cpp}: it loads a REAL `.baf` morph clip out of
// Resources/animations.BIN and SamplePosedMesh(clip, t) returns the posed (morph-
// blended) vertex positions at continuous time `t`. But nothing DRIVES it at
// runtime — the live character render (play::object_mesh_render's MeshResolver
// hook, fed by play::real_mesh_source) always hands back the STATIC rest mesh, so
// an NPC draws as a frozen pose every frame.
//
// This module closes that gap. It is the runtime DRIVER that makes a character's
// drawn mesh be the POSED mesh at the entity's current animation time, advancing
// that time deterministically per tick — wired through the SAME public MeshResolver
// hook the static mesh path uses (object_mesh_render.h), so NO owned file is
// edited (object_mesh_render.cpp / agf_anim.cpp / character_render*.cpp untouched).
//
// IDA GROUNDING (the real anim-advance + posed-mesh leaves this driver mirrors)
// ---------------------------------------------------------------------------
//   0x5c953c  VIBE_Mesh_InterpolateMorphVertices  — the per-frame leaf that, when a
//             character carries an active morph track (record+380 set), BLENDS the
//             morph deltas into the OUTPUT vertex array (writing each posed vertex at
//             the 80-byte stride) before the world matrix is applied, via
//             VIBE_Anim_ComputeMorphWeights @0x5c9394. This is the exact point the
//             engine substitutes posed vertices for the static rest mesh. We do the
//             substitution one level up: the MeshResolver returns a MeshGeometry whose
//             vertices ARE the SamplePosedMesh(clip,t) positions (the same morph
//             blend agf_anim reconstructs), so the existing project/sort/raster path
//             draws the posed mesh with zero changes to the renderer.
//   0x5ccf18  VIBE_Anim_AdvanceFrameIndex  — the per-TICK anim-time advance leaf:
//             given the mode flags (a1), the current frame (a2), the loop start/end
//             (a4/a3) and the clip's last frame (a5), it returns the next frame.
//             Forward (frame+1) until the end, then loop / ping-pong / hold by flag.
//             AdvanceFrameIndex() below is a 1:1 port; the driver steps each entity's
//             frame through it per tick so playback is deterministic & reproducible.
//
// CLIP <-> ENTITY BINDING — NOTE / GAP
// ---------------------------------------------------------------------------
// The portable reimpl entity arrays (ObjectRec / Person / SceneNode in sim/types.h)
// carry only id + type — NOT the engine's per-object anim-track handle (record +380
// / the bound .baf), which lives in the full engine struct that was never modeled.
// So the entity->clip binding is NOT reconstructed. This driver therefore takes a
// DETERMINISTIC, TEST-SUPPLIED mapping (Bind(entityId, clip)); the inert default
// (nothing bound) leaves the static-mesh behaviour exactly as before. SAID SO.
//
// INERT DEFAULT = current behaviour: with no playback installed (or no clip bound
// for an entity), the resolver returns null -> object_mesh_render falls back to the
// static mesh / quad, i.e. nothing changes. Install + Bind to make it posed.
// =============================================================================
#include "guild/common/types.h"
#include "render/agf_anim.h"            // AnimClip / SamplePosedMesh
#include "render/geometry_types.h"      // MeshGeometry / Vertex / Polygon
#include "render/skeleton.h"            // render::AdvanceFrameIndex (0x5ccf18, reused)
#include "play/world_render.h"          // EntityRef / EntityKind
#include "play/object_mesh_render.h"    // MeshResolver signature

#include <map>
#include <memory>
#include <vector>

namespace guild::play {

// The per-tick anim-time advance leaf is gilde.exe 0x5ccf18 —
// VIBE_Anim_AdvanceFrameIndex — ALREADY reconstructed once as
// render::AdvanceFrameIndex(flags, cur, last/*=end*/, first/*=start*/, count/*=clip
// last*/). This driver REUSES that single definition (no ODR-duplicate port) to step
// each entity's frame cursor; see AdvanceBinding below.

// ---------------------------------------------------------------------------
// CharAnimPlayback — drives ONE entity's animation. Holds the bound clip, the
// optional static base mesh (for topology/UV pass-through), the playback mode, and
// the live frame cursor. Sample() returns a posed MeshGeometry* (owned) at the
// current time; Advance() steps the frame deterministically (AdvanceFrameIndex).
// ---------------------------------------------------------------------------
struct AnimBinding {
    const render::AnimClip*       clip = nullptr;   // bound morph clip (not owned)
    const render::MeshGeometry*   base = nullptr;   // static base mesh (topology/UV), optional
    u8    mode      = 0;        // AdvanceFrameIndex mode bits (0 = forward+hold)
    int   frame     = 0;        // current integer frame cursor
    float subPhase  = 0.0f;     // fractional phase in [0,1) within the current segment
    float stepPerTick = 1.0f;   // frames advanced per Advance() tick (deterministic)
};

// A posed mesh snapshot: posed vertices + (base or generated) polygons, plus a
// MeshGeometry view the renderer can consume directly.
struct PosedGeometry {
    std::vector<render::Vertex>  vertices;
    std::vector<render::Polygon> polygons;
    render::MeshGeometry         geom{};
    render::MeshGeometry*        View();   // refresh pointers/counts, return &geom
    bool valid = false;
};

// Build a posed MeshGeometry for `b` at continuous time `t` (frame units): sample
// the bound clip's morph mesh (SamplePosedMesh) into vertex positions; reuse the
// base mesh's polygons + UVs when the base vertex count matches the morph vertex
// count, otherwise synthesize a deterministic triangle-strip topology over the
// posed points so the posed mesh still has drawable geometry. Returns valid=false
// when no clip is bound or the sample is empty.
PosedGeometry BuildPosedGeometry(const AnimBinding& b, float t);

// The continuous time for a binding's current cursor: frame + subPhase.
inline float BindingTime(const AnimBinding& b) {
    return (float)b.frame + b.subPhase;
}

// Advance `b` one tick (deterministic): subPhase += stepPerTick; carry whole frames
// through AdvanceFrameIndex over the clip's loop range. No clip -> no-op.
void AdvanceBinding(AnimBinding& b);

// ---------------------------------------------------------------------------
// CharAnimDriver — the process-level playback registry. Maps entity id -> binding,
// advances every bound entity one tick, and (via the installed resolver) returns the
// posed geometry for the current frame of the entity the renderer is drawing.
// ---------------------------------------------------------------------------
class CharAnimDriver {
public:
    CharAnimDriver() = default;

    // Bind a real clip (and optional static base mesh) to an entity id. Resets the
    // entity's frame cursor to the clip's start frame. `mode`/`stepPerTick` control
    // the deterministic advance. Overwrites any prior binding for that id.
    void Bind(i32 entityId, const render::AnimClip* clip,
              const render::MeshGeometry* base = nullptr,
              u8 mode = 0, float stepPerTick = 1.0f);

    // Remove a binding (entity goes back to static-mesh behaviour).
    void Unbind(i32 entityId);

    // True if `entityId` has a bound clip.
    bool IsBound(i32 entityId) const;

    // The live binding for an entity, or null.
    const AnimBinding* BindingFor(i32 entityId) const;
    AnimBinding*       BindingFor(i32 entityId);

    // Advance EVERY bound entity one tick (deterministic). Returns ticks advanced.
    int Tick();

    // Resolve the posed geometry for `e` at its current cursor. Returns null when the
    // entity has no binding (-> static-mesh fallback). The returned pointer is owned
    // by the driver and stable until the next Resolve/Tick for that entity.
    const render::MeshGeometry* ResolvePosed(const EntityRef& e);

    std::size_t boundCount() const { return bindings_.size(); }

private:
    struct Slot {
        AnimBinding   binding;
        PosedGeometry posed;   // last posed snapshot (owned storage for the resolver)
    };
    std::map<i32, std::unique_ptr<Slot>> bindings_;
};

// ---------------------------------------------------------------------------
// Installable hook: the process-global active driver the resolver adapter reads.
// Inert default: null -> PosedMeshResolver returns null -> static behaviour.
// ---------------------------------------------------------------------------
void InstallCharAnimDriver(CharAnimDriver* d);
CharAnimDriver* ActiveCharAnimDriver();

// A play::MeshResolver (object_mesh_render.h) adapter: routes the EntityRef through
// the installed driver's ResolvePosed. Install this into
// ObjectMeshRenderer::Options::meshResolver to make the live render draw posed
// character meshes. Inert when no driver is installed (returns null). This is the
// PUBLIC wiring point — it touches no owned file.
const render::MeshGeometry* PosedMeshResolver(const EntityRef& e);

} // namespace guild::play
