// =============================================================================
// guild::play — REAL-WORLD RENDER implementation. See world_render.h.
//
// The build step scans the live sim entity arrays and emits one WorldObject quad
// per visible entity into a play::LoadedWorld, then the render step drives the
// reconstructed render_binder pipeline (which itself runs ProjectVerticesToScreen
// -> RadixSortDrawList -> RasterizeMeshList -> PresentFrame). Scanning order:
//
//   1. terrain ground quad (if enabled),
//   2. scene-node tree in the engine's WalkAndInvoke order (parent before its
//      children via childPtr, then the sibling chain) — @0x5ac738 draw order,
//   3. alive g_objects (linear slot scan),
//   4. live g_persons (linear slot scan, real persons only).
//
// The terrain-first / scene-then-objects order mirrors BeginUniverseFrame
// (terrain, then the scene-graph project/append walk). The final back-to-front
// ordering is produced by the real RadixSortDrawList inside the binder.
// =============================================================================
#include "play/world_render.h"

#include "play/object_transform.h"
#include "sim/entity.h"

#include <cmath>
#include <cstdint>

namespace guild::play {

// ---------------------------------------------------------------------------
// Default placement hook: a deterministic id-derived grid quad.
//
// Lays a stable grid across the lower 60% of the view (leaving the top strip for
// the HUD). The cell is chosen from a hash of (slot, id) so the layout is
// reproducible and golden-testable yet spreads entities over distinct screen
// positions and depths. Light/shade is id-derived (so the radix sort has a
// non-trivial key spread); odd ids are translucent (exercise dispatch slot 3).
// ---------------------------------------------------------------------------
EntityPlacement DefaultPlacementHook(const EntityRef& e, int fbW, int fbH) {
    EntityPlacement out;
    out.visible = true;

    // A 6-wide grid; quad ~ 1/8 of the frame, inset so quads stay on-screen.
    const int cols = 6;
    // Stable cell index from the slot (primary) folded with the id (secondary),
    // so two entities never collapse to the same cell for small worlds.
    std::uint32_t h = (std::uint32_t)e.slot * 2654435761u
                    + (std::uint32_t)e.id * 40503u;
    int cell = (int)(h % 48u);                  // up to 8 rows x 6 cols
    int cx = cell % cols;
    int cy = cell / cols;

    float cellW = (float)fbW / (float)(cols + 1);
    float cellH = (float)fbH * 0.6f / 8.0f;
    float ox = cellW * 0.5f;                     // left inset
    float oy = (float)fbH * 0.35f;               // start below the HUD strip

    float x0 = ox + (float)cx * cellW;
    float z0 = oy + (float)cy * cellH;
    float qw = cellW * 0.85f;
    float qh = cellH * 0.85f;

    out.quad.x0 = x0;
    out.quad.z0 = z0;
    out.quad.x1 = x0 + qw;
    out.quad.z1 = z0 + qh;
    // Light index in [80, 240], id-derived so the sort key spreads.
    out.quad.light = (u8)(80 + (h % 161u));
    out.quad.translucent = (e.id & 1) != 0;
    return out;
}

// ---------------------------------------------------------------------------
// REAL world-transform placement.
// ---------------------------------------------------------------------------

const void* DefaultNodeResolver(const EntityRef&) {
    return nullptr;   // inert: no engine node bound
}

EntityPlacement PlacementFromWorld(const WorldPlacement& wp, const EntityRef& e,
                                   int fbW, int fbH, float pixelsPerUnit,
                                   float eyeX, float eyeZ) {
    EntityPlacement out;
    out.visible = wp.visible;

    // Ground projection (render_binder convention): world X -> screenX, world Z ->
    // screenY, recentered so the city eye lands at the framebuffer center.
    const float cx = (float)fbW * 0.5f + (wp.x - eyeX) * pixelsPerUnit;
    const float cy = (float)fbH * 0.5f + (wp.z - eyeZ) * pixelsPerUnit;

    // Quad half-extent: a small footprint scaled by the projection so distinct
    // world positions yield distinct, non-overlapping screen quads.
    float half = pixelsPerUnit * 1.5f;
    if (half < 1.0f) half = 1.0f;
    if (half > (float)fbW * 0.1f) half = (float)fbW * 0.1f;

    out.quad.x0 = cx - half;
    out.quad.z0 = cy - half;
    out.quad.x1 = cx + half;
    out.quad.z1 = cy + half;

    // Light/shade seeded from the heading + id so the radix sort key still spreads
    // and the object's facing influences its shade (id-stable, deterministic).
    std::uint32_t h = (std::uint32_t)e.id * 2654435761u;
    int shade = 96 + (int)(h % 128u) + (int)(wp.yaw * 24.0f);
    if (shade < 16) shade = 16;
    if (shade > 254) shade = 254;
    out.quad.light = (u8)shade;
    out.quad.translucent = (e.id & 1) != 0;
    return out;
}

EntityPlacement RealPlacement(const EntityRef& e, int fbW, int fbH,
                              NodeResolver resolve, float pixelsPerUnit,
                              float eyeX, float eyeZ) {
    const void* node = resolve ? resolve(e) : nullptr;
    if (node) {
        // ObjectWorldPlacement delegates to SceneNodeWorldPlacement(node) — the REAL
        // engine layout read (+76 world pos, +396 frame-matrix yaw, +533 visibility).
        WorldPlacement wp = ObjectWorldPlacement(nullptr, node);
        if (wp.visible)
            return PlacementFromWorld(wp, e, fbW, fbH, pixelsPerUnit, eyeX, eyeZ);
    }
    // No node bound (or not visible): fall back to the deterministic grid so the
    // build still produces a frame.
    return DefaultPlacementHook(e, fbW, fbH);
}

// ---------------------------------------------------------------------------
// Build the draw list from the live entity arrays.
// ---------------------------------------------------------------------------
namespace {

// Resolve one entity's placement: the REAL world-transform read when the build
// opted into it (and the resolver supplies an engine node), else the configured
// placement hook (deterministic grid by default).
EntityPlacement ResolvePlacement(const WorldRenderer::Options& opt,
                                 const EntityRef& ref, int fbW, int fbH) {
    if (opt.useRealPlacement)
        return RealPlacement(ref, fbW, fbH, opt.nodeResolver, opt.pixelsPerUnit,
                             opt.eyeX, opt.eyeZ);
    return opt.placement ? opt.placement(ref, fbW, fbH)
                         : DefaultPlacementHook(ref, fbW, fbH);
}

// Emit one entity into the LoadedWorld object array (capped at world cap). Returns
// true if a quad was actually appended (visible + room remained).
bool EmitEntity(LoadedWorld& w, int cap, const EntityRef& ref,
                const WorldRenderer::Options& opt, int fbW, int fbH) {
    if (w.objectCount >= cap || w.objectCount >= 8)
        return false;
    EntityPlacement p = ResolvePlacement(opt, ref, fbW, fbH);
    if (!p.visible)
        return false;
    w.objects[w.objectCount++] = p.quad;
    return true;
}

// Walk the scene-node tree in VIBE_SceneGraph_WalkAndInvoke @0x5ac738 order:
// visit a node, recurse into its childPtr subtree (parent before children), then
// follow the sibling chain. The flat reimpl models child/sibling as indices in
// childPtr / (the next live slot acts as the sibling chain when childPtr links a
// real tree). To stay robust for the common flat `.cty` case (no tree links) the
// walk falls back to a linear live-slot scan for any node not reached via links.
void WalkSceneNodes(LoadedWorld& w, int cap, const WorldRenderer::Options& opt,
                    int fbW, int fbH, WorldDrawList& dl) {
    using namespace guild::sim;
    int count = g_sceneNodeCount;
    if (count <= 0 || count > kSceneNodeCapacity)
        count = 0;

    // visited[] guards against cycles / double-visits when childPtr links a tree.
    static bool visited[kSceneNodeCapacity];
    for (int i = 0; i < kSceneNodeCapacity; ++i) visited[i] = false;

    // Iterative DFS over childPtr (parent pushed first, then descended): mirrors
    // the recursive WalkAndInvoke parent-then-children order. We seed the DFS from
    // every live root slot in array order (the sibling chain at the top level).
    static int stack[kSceneNodeCapacity + 8];

    auto visit = [&](int idx) {
        if (idx < 0 || idx >= count || visited[idx])
            return;
        if (g_sceneNodes[idx].type == 0)        // empty slot
            return;
        // DFS this subtree.
        int sp = 0;
        stack[sp++] = idx;
        while (sp > 0) {
            int n = stack[--sp];
            if (n < 0 || n >= count || visited[n])
                continue;
            visited[n] = true;
            if (g_sceneNodes[n].type == 0)
                continue;
            EntityRef ref;
            ref.kind = EntityKind::Scene;
            ref.id   = g_sceneNodes[n].id;
            ref.slot = n;
            ref.type = g_sceneNodes[n].type;
            if (EmitEntity(w, cap, ref, opt, fbW, fbH))
                ++dl.sceneQuads;
            // Push the child subtree (visited after the parent -> parent-first).
            int c = g_sceneNodes[n].childPtr;
            if (c >= 0 && c < count && !visited[c])
                stack[sp++] = c;
        }
    };

    for (int i = 0; i < count; ++i)
        visit(i);
}

} // namespace

WorldDrawList WorldRenderer::build(const Options& opt, LoadedWorld& out) {
    using namespace guild::sim;
    build_ = WorldDrawList{};

    out = LoadedWorld{};
    out.fbW = opt.fbW;
    out.fbH = opt.fbH;
    out.clearR = opt.clearR;
    out.clearG = opt.clearG;
    out.clearB = opt.clearB;
    out.objectCount = 0;
    out.hudEnabled = false;            // real-world render: no synthetic HUD bar

    int cap = opt.maxObjects;
    if (cap > 8) cap = 8;
    if (cap < 0) cap = 0;

    // 1. Terrain ground quad across the lower half (BeginUniverseFrame: terrain
    //    is rendered before the scene-graph walk).
    if (opt.emitTerrain) {
        out.hasTerrain = true;
        out.terrain.x0 = opt.fbW * 0.04f;
        out.terrain.z0 = opt.fbH * 0.42f;
        out.terrain.x1 = opt.fbW * 0.96f;
        out.terrain.z1 = opt.fbH * 0.97f;
        out.terrain.light = 120;
        out.terrain.translucent = false;
        build_.hasTerrain = true;
    }

    // 2. Scene-node tree in engine WalkAndInvoke draw order.
    if (opt.scanScene)
        WalkSceneNodes(out, cap, opt, opt.fbW, opt.fbH, build_);

    // 3. Alive objects / buildings (linear slot scan over g_objects).
    if (opt.scanObjects) {
        for (int i = 0; i < kObjectCapacity; ++i) {
            if (g_objects[i].alive == 0)
                continue;
            EntityRef ref;
            ref.kind = EntityKind::Object;
            ref.id   = g_objects[i].id;
            ref.slot = i;
            ref.type = g_objects[i].alive;
            if (EmitEntity(out, cap, ref, opt, opt.fbW, opt.fbH))
                ++build_.objectQuads;
            else if (out.objectCount >= cap || out.objectCount >= 8)
                break;
        }
    }

    // 4. Live persons (real persons only: marker live + kind < 10).
    if (opt.scanPersons) {
        for (int i = 0; i < kPersonCapacity; ++i) {
            if (g_persons[i].marker == -1)
                continue;
            EntityRef ref;
            ref.kind = EntityKind::Person;
            ref.id   = g_persons[i].id;
            ref.slot = i;
            ref.type = g_persons[i].kind;
            if (EmitEntity(out, cap, ref, opt, opt.fbW, opt.fbH))
                ++build_.personQuads;
            else if (out.objectCount >= cap || out.objectCount >= 8)
                break;
        }
    }

    world_ = out;
    return build_;
}

RenderStats WorldRenderer::render(const Options& opt, shim::IGraphicsDevice& dev) {
    build(opt, world_);
    if (!binder_.load(world_))
        return RenderStats{};
    return binder_.renderFrame(dev);
}

} // namespace guild::play
