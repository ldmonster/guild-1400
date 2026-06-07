#pragma once
// =============================================================================
// guild::play — REAL SCENE-GRAPH WALK / NODE-DISPATCH BRIDGE (PLAYABLE_PLAN P6).
//
// The "inert hook" gap on the LIVE FRAME PATH's scene-graph walk.
//
// The engine draws the universe each frame via (WAVE28 grounding pointers):
//   VIBE_Render_RenderMainViewFrame  0x5b6074
//     -> VIBE_Render_RenderUniverseFrame 0x5b3de8
//       -> VIBE_Render_BeginUniverseFrame 0x5b3900
//         -> VIBE_SceneGraph_WalkAndInvoke 0x5ac738  (the scene-tree walk)
//
// IDA grounding (disassembled 0x5b3a34 .. 0x5b3a5d in BeginUniverseFrame):
//   5b3a34  mov  ecx, offset VIBE_Render_ProcessSceneNode   ; the per-node callback
//   5b3a4b  mov  eax, off_649D64                             ; the universe root
//   5b3a5d  call VIBE_SceneGraph_WalkAndInvoke              ; walk(root, .., ecx, ..)
// i.e. the universe scene walk's PER-NODE DRAW-DISPATCH CALLBACK is
//   VIBE_Render_ProcessSceneNode @0x5add1c
// the project/cull/draw-list-APPEND leaf reconstructed in src/render/scene_node.cpp
// as render::ProcessSceneNodeAppend, driven over the generic walk reconstructed in
// src/render/scene_walk.cpp / src/render/scene.cpp as render::WalkAndInvoke.
//
// THE INERT HOOK
// ---------------------------------------------------------------------------
// render::WalkAndInvoke (render/scene.h) is the generic node walk; its per-node
// dispatch is the WalkVTable.invoke slot — exactly the engine's `ecx` callback.
// There is NO process-global setter for that slot (the walk takes the vtable by
// value, as the engine passed the callback in a register), so by default the live
// render path supplies an INERT invoke: visit every node but append NOTHING
// (return 1 == "descend, no side effect"), the engine's "subsystem not present"
// no-op. The real reconstructed dispatch leaf (ProcessSceneNodeAppend) is left
// disconnected.
//
// THE BRIDGE (additive, public-setter only)
// ---------------------------------------------------------------------------
// InstallRealSceneBridge() flips a process-static selector so the bridge's
// WalkVTable (SceneBridgeVTable()) routes its invoke slot to the REAL
// ProcessSceneNodeAppend dispatch instead of the inert no-op. Nothing here edits
// render_binder.cpp / frameloop.cpp / scene*.cpp or any hook-table .cpp — the
// bridge exposes its own WalkVTable + a WalkSceneTree() driver that a caller (the
// live path, or the tests) runs to walk a scene tree with the real per-node
// dispatch wired in. This is the same shape as wire_render_bridge (a self-owned
// installer over an inert-default dispatch table).
//
// FINDING (documented, not worked around): the inert dispatch lives in the
// WalkVTable VALUE that BeginUniverseFrame builds locally (the engine's `ecx`
// register), NOT behind a sim::SetXxxHooks process-global setter. So to make the
// REAL in-process BeginUniverseFrame call the real dispatch you would have to edit
// the owned render_binder.cpp / a frameloop that constructs that vtable. This
// bridge therefore exposes the wired WalkVTable + driver for an unowned caller to
// USE; the de-inert is proven by running render::WalkAndInvoke with the bridge's
// vtable vs. an inert one. See the module report.
// =============================================================================
#include "guild/common/types.h"
#include "render/scene.h"          // WalkVTable, WalkAndInvoke
#include "render/scene_walk.h"     // SceneNode (walk links + nodeType/flags)
#include "render/scene_node.h"     // ProcessSceneNodeAppend (the real dispatch leaf)
#include "render/geometry_types.h" // MeshGeometry / DrawListEntry / Polygon

namespace guild::play {

// ---------------------------------------------------------------------------
// SceneDrawNode — one renderable scene-graph node for the bridge walk. It carries
// the engine walk links + the per-node type/flags (render::SceneNode), PLUS the
// per-node draw payload the real dispatch (ProcessSceneNodeAppend) consumes: the
// node's projected mesh poly array and its cull byte. This is the minimal data the
// engine's ProcessSceneNode read off the full node struct (object +460 geometry,
// the CullNodeAgainstFrustum byte) to append the node's polys to the draw list.
//
// The walk links (firstChild / nextSibling / flags528 / nodeType) are the same
// fields render::WalkAndInvoke / TestNodeFlag traverse; nodeType is set so the
// node passes the walk mask (TestNodeFlag), and flags528 bit0 terminates a sibling
// chain (the engine's +528 list terminator).
// ---------------------------------------------------------------------------
struct SceneDrawNode {
    // -- walk topology (mirrors render::SceneNode offsets) -------------------
    SceneDrawNode* firstChild  = nullptr;  // +0x1FC (508) child list head
    SceneDrawNode* nextSibling = nullptr;  // +0x1F0 (496) next sibling
    u8 flags528 = 0;                       // +0x210 (528) bit0 = chain terminator
    u8 nodeType = 3;                       // +0x215 (533) walk dispatch type byte

    // -- per-node draw payload (the ProcessSceneNode inputs) ----------------
    u8 cullByte = 0;                       // CullNodeAgainstFrustum result (0 => visible)
    const render::Polygon* polys = nullptr;// projected mesh poly array
    i32 polyCount = 0;                     // polygon count
    const u32* texSortId = nullptr;        // per-poly software texture sort id (or null)
};

// ---------------------------------------------------------------------------
// SceneBridgeContext — the per-walk draw-list sink + append context the real
// dispatch writes through (the engine's global PolyList + NodeAppendContext). Also
// tallies what the walk did so a caller can prove real-vs-inert dispatch.
// ---------------------------------------------------------------------------
struct SceneBridgeContext {
    render::DrawList out{nullptr, 0, 0};       // draw-list sink (PolyList1 + cursor)
    render::NodeAppendContext appendCtx{};     // baseKey/frameStamp/depthScale
    u32* texFrameStamp = nullptr;              // per-poly frame-stamp scratch (or null)

    // -- tallies (read back by tests/report) --------------------------------
    int nodesVisited    = 0;   // nodes the walk reached
    int nodesDispatched = 0;   // nodes whose draw dispatch actually ran (TestNodeFlag pass)
    int polysAppended   = 0;   // draw-list entries the real dispatch appended
};

// Install the REAL scene-graph dispatch: route the bridge WalkVTable's invoke slot
// to render::ProcessSceneNodeAppend (the real per-node draw dispatch) instead of
// the inert no-op. Idempotent; process-static, like wire_render_bridge.
void InstallRealSceneBridge();

// Restore the inert default dispatch (visit but append nothing). For tests that
// observe the inert-vs-real difference in one process.
void UninstallRealSceneBridge();

// True while the real scene-graph dispatch is installed.
bool RealSceneBridgeInstalled();

// The bridge's WalkVTable: testFlag = render::TestNodeFlag, child/sibling/stop =
// the SceneDrawNode links, invoke = the real ProcessSceneNodeAppend dispatch when
// installed, else the inert no-op. Pass to render::WalkAndInvoke.
const render::WalkVTable& SceneBridgeVTable();

// Walk a scene tree rooted at `root` (a sibling-list head) with the bridge's
// vtable, dispatching each visited node through the currently-selected dispatch
// (real when installed, inert otherwise). `walkMask` gates node visibility
// (render::TestNodeFlag); use 0x1FF to admit every node type. `ctx` carries the
// draw-list sink + receives the visit/dispatch/append tallies. Returns the walk
// result byte (1 = completed, 0 = aborted), mirroring render::WalkAndInvoke.
char WalkSceneTree(SceneDrawNode* root, i16 walkMask, SceneBridgeContext& ctx);

// The real per-node dispatch leaf (exposed so tests can drive the exact same leaf
// the installed hook routes to): project/append `node`'s polys into `ctx.out`
// through render::ProcessSceneNodeAppend. Returns the polys appended.
int BridgeDispatchNode(const SceneDrawNode* node, SceneBridgeContext& ctx);

} // namespace guild::play
