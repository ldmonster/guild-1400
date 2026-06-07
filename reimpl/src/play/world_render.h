#pragma once
// =============================================================================
// guild::play — REAL-WORLD RENDER (PLAYABLE_PLAN P2/M2).
//
// render_binder.{h,cpp} renders a SYNTHETIC LoadedWorld (a hand-built terrain +
// a few quads) through the REAL reconstructed render pipeline
// (ProjectVerticesToScreen -> RadixSortDrawList -> RasterizeMeshList ->
// PresentFrame) into a headless shim::IGraphicsDevice. This module does the same
// thing, but SOURCED FROM THE REAL LOADED WORLD: after a real `.cty` load
// (io::LoadWorld populates sim::g_persons / g_objects, sim::g_sceneNodes), it
// scans the live entity arrays, BUILDS one renderable WorldObject (a textured
// quad) per visible scene object plus a terrain ground quad, packs them into a
// play::LoadedWorld, and renders THAT through render_binder's RenderBinder — i.e.
// the exact same real pipeline, but fed by the real world rather than a synthetic
// one.
//
// THE NODE-WALK DRAW ORDER (grounding)
// ---------------------------------------------------------------------------
// The engine's per-frame draw walk is VIBE_Render_DrawUniverseAndStats @0x5b3bbc
// -> VIBE_SceneGraph_WalkAndInvoke @0x5ac738 over the active universe root
// (off_649D64): it descends the root's child list (root+128), invokes the
// per-node project/append callback for each VISIBLE node (TestNodeFlag on the
// node +530 high byte), recurses into a node's children (node[127] == +508) when
// the callback returns >= 0, then follows the sibling chain (node[124] == +496)
// to a terminator. Parent precedes its children; siblings keep chain order. The
// builder mirrors that order: it emits the scene-node tree in the SAME
// parent-then-children, sibling-chain traversal so the draw list back-to-front
// order matches the engine (radix sort by light index is then applied by the
// real pipeline, exactly as the engine).
//
// Everything here is additive (new file). The two policy leaves that the live
// game read from the full engine node struct (a node's world placement and its
// visibility/translucency) are exposed as INSTALLABLE HOOKS with inert,
// deterministic defaults — the engine's "subsystem not present" pattern — so a
// later agent can wire the real +76 world-transform / +460 mesh material bytes
// without editing this file. The default placement is a deterministic,
// id-derived layout so the build is reproducible (golden counts/coords).
// =============================================================================
#include "guild/common/types.h"
#include "play/object_transform.h"
#include "play/render_binder.h"
#include "sim/types.h"

namespace guild::shim { class IGraphicsDevice; }

namespace guild::play {

// ---------------------------------------------------------------------------
// Draw-list build result — what scanning the live entity arrays produced.
// ---------------------------------------------------------------------------
struct WorldDrawList {
    int objectQuads = 0;   // alive g_objects scanned -> quads
    int personQuads = 0;   // live g_persons scanned -> quads
    int sceneQuads  = 0;   // g_sceneNodes (tree-walk order) -> quads
    bool hasTerrain = false;
    // Total visible scene objects (excludes terrain): the "N scene objects drawn"
    // count the e2e reports.
    int sceneObjects() const { return objectQuads + personQuads + sceneQuads; }
    int quadCount() const { return sceneObjects() + (hasTerrain ? 1 : 0); }
};

// ---------------------------------------------------------------------------
// Placement / visibility policy hooks (installable, inert-default).
//
// In the live game the per-object world placement and the visibility/translucency
// come from the full engine node struct (the +76 transform block, the +460 mesh
// material, the +530 render-flags byte). The portable entity records (ObjectRec /
// Person / SceneNode in sim/types.h) only expose id + type + a few scalars; the
// transform bytes live in the records' TODO pad regions. Rather than guess at
// those byte offsets, the placement is computed by a hook with a DETERMINISTIC,
// id-derived default (a stable grid laid across the view) so the draw-list build
// is reproducible and golden-testable. A later agent can install a real hook that
// reads the recovered transform bytes WITHOUT editing this module.
// ---------------------------------------------------------------------------

// One entity the builder is about to place, tagged by which array it came from.
enum class EntityKind : u8 { Object = 0, Person = 1, Scene = 2 };

struct EntityRef {
    EntityKind kind;
    i32        id;        // the record id (ObjectRec::id / Person::id / SceneNode::id)
    i32        slot;      // array slot / tree-visit index (stable per build)
    i16        type;      // SceneNode::type / Person::kind / ObjectRec::alive
};

// The placement a hook returns for one entity (model-space quad + shade + flag).
// Coordinates are in the framebuffer's pixel space (x -> screenX, z -> screenY),
// matching render_binder's projection (unit scale, camera at origin).
struct EntityPlacement {
    WorldObject quad;     // the renderable quad (x0/z0..x1/z1, light, translucent)
    bool        visible = true;  // false -> the entity is skipped (culled)
};

// The placement hook signature. `fbW`/`fbH` are the target framebuffer geometry
// so the default can lay the grid inside the view. Returns the placement.
using PlacementHook = EntityPlacement (*)(const EntityRef& e, int fbW, int fbH);

// The default, inert placement hook: a deterministic id-derived grid quad, always
// visible, translucent for odd-id entities. Exposed so tests/callers can call it
// directly and so a real hook can fall back to it.
EntityPlacement DefaultPlacementHook(const EntityRef& e, int fbW, int fbH);

// ---------------------------------------------------------------------------
// REAL world-transform placement (replaces the synthetic grid hook).
//
// The live game's per-object placement comes from the engine render-node's world
// transform (object_transform.h: +76 world position, +396 frame-matrix yaw). To
// place an entity from that real transform we need its render-node pointer. The
// portable reimpl entity arrays don't carry the big node struct, so a NODE
// RESOLVER hook maps an EntityRef -> its engine node pointer (or null). Its inert
// default returns null (no node bound) — the build then falls back to the
// deterministic grid so a node-less load still renders. A caller that has the real
// nodes (the e2e, or the live game) installs a resolver that returns the actual
// node, and the placement becomes the REAL world layout.
// ---------------------------------------------------------------------------

// Map an entity to its engine render-node pointer (object_transform layout), or
// null when no node is bound. The default returns null.
using NodeResolver = const void* (*)(const EntityRef& e);

// The inert default node resolver: always null (no engine node bound).
const void* DefaultNodeResolver(const EntityRef& e);

// Project a decoded WorldPlacement to a framebuffer-space quad. `unitsPerCell` is
// the world->pixel scale; `eyeX`/`eyeZ` recenter the city on the framebuffer so
// the layout fits the view. Mirrors render_binder's x->screenX, z->screenY ground
// projection. The resulting quad's size/light derive from the placement so each
// object lands at its true (distinct) city position rather than a uniform grid.
EntityPlacement PlacementFromWorld(const WorldPlacement& wp, const EntityRef& e,
                                   int fbW, int fbH, float pixelsPerUnit,
                                   float eyeX, float eyeZ);

// The REAL placement hook factory: resolves the entity's engine node via
// `resolve`, decodes its world transform (ObjectWorldPlacement) and projects it.
// When the resolver returns null (or the node is not visible) it falls back to the
// deterministic grid so the build never produces an empty frame. Because a free
// function hook can't capture the resolver, the WorldRenderer wires the resolver
// through Options::nodeResolver and uses RealPlacement() internally.
EntityPlacement RealPlacement(const EntityRef& e, int fbW, int fbH,
                              NodeResolver resolve, float pixelsPerUnit,
                              float eyeX, float eyeZ);

// ---------------------------------------------------------------------------
// WorldRenderer — builds a draw list from the live entity arrays and renders it
// through the REAL render_binder pipeline into a headless device.
// ---------------------------------------------------------------------------
class WorldRenderer {
public:
    // Build options (framebuffer geometry + the clear colour + the cap on how
    // many scene objects to emit; the binder caps at 8 objects + 1 terrain).
    struct Options {
        int fbW = 96;
        int fbH = 72;
        u8  clearR = 0, clearG = 0, clearB = 64;   // dark-blue sky clear
        int maxObjects = 8;                         // render_binder object cap
        bool emitTerrain = true;                    // add a ground quad
        bool scanObjects = true;                    // include alive g_objects
        bool scanPersons = false;                   // include live g_persons
        bool scanScene   = true;                    // include g_sceneNodes (tree)
        PlacementHook placement = &DefaultPlacementHook;

        // --- REAL world-transform placement (replaces the synthetic grid) -----
        // When `useRealPlacement` is set, the build resolves each entity's engine
        // node via `nodeResolver` and places it at its REAL decoded world transform
        // (object_transform.h), projected with `pixelsPerUnit` and recentered on
        // (`eyeX`,`eyeZ`). Entities whose node the resolver can't supply fall back
        // to the deterministic grid (so a node-less load still renders).
        bool         useRealPlacement = false;
        NodeResolver nodeResolver     = &DefaultNodeResolver;
        float        pixelsPerUnit    = 1.0f;       // world unit -> pixels
        float        eyeX = 0.0f, eyeZ = 0.0f;      // city center -> frame center
    };

    WorldRenderer() = default;

    // Scan the LIVE sim entity arrays (g_objects / g_persons / g_sceneNodes) and
    // build a play::LoadedWorld (terrain + up to maxObjects quads) into `out`,
    // in the engine's node-walk draw order for the scene tree. Returns the build
    // summary (counts). Does NOT touch the GPU/device; pure data build, so it can
    // be unit-tested against synthetic entity records for golden counts/coords.
    WorldDrawList build(const Options& opt, LoadedWorld& out);

    // Build (as above) into the internal LoadedWorld, load the RenderBinder, and
    // render ONE frame through the real pipeline into `dev` (already init()'d to
    // opt.fbW x opt.fbH x 16bpp). Returns the binder's RenderStats; the build
    // summary is retrievable via lastBuild().
    RenderStats render(const Options& opt, shim::IGraphicsDevice& dev);

    const WorldDrawList& lastBuild() const { return build_; }
    const LoadedWorld&   world() const { return world_; }
    RenderBinder&        binder() { return binder_; }

private:
    LoadedWorld   world_{};
    WorldDrawList build_{};
    RenderBinder  binder_{};
};

} // namespace guild::play
