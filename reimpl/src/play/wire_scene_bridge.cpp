// =============================================================================
// guild::play — REAL SCENE-GRAPH WALK / NODE-DISPATCH BRIDGE implementation.
// See wire_scene_bridge.h. Additive: routes the generic scene walk's per-node
// dispatch (render::WalkVTable.invoke) to the REAL reconstructed draw leaf
// render::ProcessSceneNodeAppend. No owned file is edited.
// =============================================================================
#include "play/wire_scene_bridge.h"

#include <vector>

namespace guild::play {

namespace {

// -- process-static selector: real vs inert per-node dispatch -----------------
bool g_realDispatch = false;

// SceneDrawNode link accessors for render::WalkVTable (the engine's node[127] /
// node[124] / +528 bit0 reads, and the +533 type byte — HIBYTE of the dword
// at +530: `mov eax,[esi+212h]; sar eax,18h` @0x5ac7ba).
bool VtTestFlag(void* node, i16 walkMask) {
    auto* n = static_cast<const SceneDrawNode*>(node);
    // gilde.exe 0x5ac6d8 — VIBE_SceneGraph_TestNodeFlag (type byte -> mask bit).
    return render::TestNodeFlag(n->nodeType, walkMask);
}
void* VtChild(void* node) {
    return static_cast<SceneDrawNode*>(node)->firstChild;   // +0x1FC (508)
}
void* VtSibling(void* node) {
    return static_cast<SceneDrawNode*>(node)->nextSibling;  // +0x1F0 (496)
}
bool VtStopAtSibling(void* node) {
    return (static_cast<SceneDrawNode*>(node)->flags528 & 1) != 0; // +0x210 bit0
}

// Run the REAL per-node draw dispatch (ProcessSceneNodeAppend) for one node into
// the bridge context's draw-list sink. Returns the polys appended.
int DispatchReal(const SceneDrawNode* n, SceneBridgeContext& c) {
    if (!n->polys || n->polyCount <= 0)
        return 0;
    // ProcessSceneNodeAppend's SOFTWARE path unconditionally reads texSortId[i]
    // (the engine always supplied the per-poly texture-id array). When a node
    // carries no texSortId, present a zeroed array (== "untextured" sort key 0,
    // the engine's *(p+20)==0 branch) so the real leaf runs faithfully and safely.
    const u32* texSort = n->texSortId;
    std::vector<u32> zeroTex;
    if (!texSort && c.appendCtx.mode == render::NodeAppendMode::Software) {
        zeroTex.assign((size_t)n->polyCount, 0u);
        texSort = zeroTex.data();
    }
    // gilde.exe 0x5add1c — VIBE_Render_ProcessSceneNode draw-list append core.
    i32 added = render::ProcessSceneNodeAppend(
        n->cullByte, n->polys, n->polyCount, texSort, c.appendCtx,
        &c.out, c.texFrameStamp);
    c.polysAppended += (int)added;
    return (int)added;
}

// The per-node dispatch callback render::WalkAndInvoke invokes (the engine's `ecx`
// callback). Returns the engine control byte: 1 == visit + descend (no abort).
// REAL when installed (calls ProcessSceneNodeAppend), INERT otherwise (visit but
// append nothing — the "subsystem not present" no-op).
char VtInvoke(void* node, void* ctx, i32 /*userArg*/) {
    auto* n = static_cast<const SceneDrawNode*>(node);
    auto* c = static_cast<SceneBridgeContext*>(ctx);
    if (c) {
        ++c->nodesDispatched;
        if (g_realDispatch)
            DispatchReal(n, *c);
        // inert: no append (engine's no-op dispatch).
    }
    return 1;  // descend children, do not abort
}

// The bridge WalkVTable. The invoke slot's REAL/INERT behaviour is selected at
// call time by g_realDispatch (one vtable, faithful to the engine using one
// callback whose body is the real dispatch).
const render::WalkVTable g_vtable = {
    VtTestFlag,        // testFlag      -> TestNodeFlag(type, mask)
    VtInvoke,          // invoke        -> REAL ProcessSceneNodeAppend / inert no-op
    VtChild,           // child         -> firstChild
    VtSibling,         // sibling       -> nextSibling
    VtStopAtSibling,   // stopAtSibling -> flags528 bit0
};

} // namespace

// ---------------------------------------------------------------------------
void InstallRealSceneBridge()   { g_realDispatch = true; }
void UninstallRealSceneBridge() { g_realDispatch = false; }
bool RealSceneBridgeInstalled() { return g_realDispatch; }

const render::WalkVTable& SceneBridgeVTable() { return g_vtable; }

int BridgeDispatchNode(const SceneDrawNode* node, SceneBridgeContext& ctx) {
    if (!node)
        return 0;
    return DispatchReal(node, ctx);
}

char WalkSceneTree(SceneDrawNode* root, i16 walkMask, SceneBridgeContext& ctx) {
    if (!root)
        return 1;

    // Count nodes the walk REACHES via a topology pre-pass (so nodesVisited is the
    // true reached-node count independent of the walk mask / dispatch). The walk
    // itself (render::WalkAndInvoke) then dispatches every node whose type passes
    // TestNodeFlag, tallying nodesDispatched / polysAppended through VtInvoke.
    // Mirror render::WalkAndInvoke's traversal exactly: the START node of a sibling
    // chain is always reached; an advanced-to sibling that is a +528 bit0 terminator
    // is a SENTINEL and is NOT reached (the engine returns at the terminator without
    // invoking it). This keeps nodesVisited equal to the walk's true reached count.
    struct Counter {
        static void Reach(const SceneDrawNode* start, int& count) {
            const SceneDrawNode* s = start;
            while (s) {
                ++count;
                if (s->firstChild)
                    Reach(s->firstChild, count);
                const SceneDrawNode* nxt = s->nextSibling;
                if (!nxt)
                    break;
                if (nxt->flags528 & 1)   // sibling-chain terminator sentinel: stop
                    break;
                s = nxt;
            }
        }
    };
    Counter::Reach(root, ctx.nodesVisited);

    // gilde.exe 0x5ac738 — VIBE_SceneGraph_WalkAndInvoke over the node tree with
    // the bridge vtable. `ctx` is threaded to the callback as the engine's a-ctx;
    // userArg mirrors the engine's a5 (0 here, as BeginUniverseFrame passes).
    return render::WalkAndInvoke(/*root=*/nullptr, /*node=*/root, /*ctx=*/&ctx,
                                 walkMask, /*userArg=*/0, g_vtable);
}

} // namespace guild::play
