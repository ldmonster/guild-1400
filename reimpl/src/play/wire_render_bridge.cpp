// =============================================================================
// guild::play — REAL RENDER / CHARACTER-MESH BRIDGE implementation.
// See wire_render_bridge.h. Additive: wires sim::CharRenderHooks (a public
// installable table) to real reconstructed leaves via sim::SetCharRenderHooks.
// No owned file is edited.
// =============================================================================
#include "play/wire_render_bridge.h"

#include "sim/character_render.h"       // CharRenderHooks + SetCharRenderHooks
#include "sim/object_lifecycle8.h"      // ObjectSelectTextureSet (0x5b3f54) + SceneNode8
#include "util/transform.h"             // PointThroughBoneChainPivot (0x5c8d0c)

namespace guild::play {

// ---------------------------------------------------------------------------
// Real leaf trampolines (match the sim::CharRenderHooks ABI).
// ---------------------------------------------------------------------------
namespace {

// gilde.exe 0x4048c1 — the call the original ComputeAttachOffset makes:
//   VIBE_Transform_PointThroughBoneChainPivot(*(mesh@actor+52), a4, a4);
// REAL leaf (util::PointThroughBoneChainPivot). The mesh handle is the frame base;
// a null handle keeps the inert identity so a meshless actor stays safe.
void HookPointThroughPivot(void* mesh, const float in[3], float out[3]) {
    if (!mesh) {
        out[0] = in[0]; out[1] = in[1]; out[2] = in[2];
        return;
    }
    util::PointThroughBoneChainPivot(static_cast<float*>(mesh), in, out);
}

// gilde.exe 0x4048d1 — the add the original ComputeAttachOffset makes right after:
//   *v5 += *(float*)(mesh+132);  v5[1] += *(float*)(mesh+136);  v5[2] += *(float*)(mesh+140);
// i.e. the mesh ROOT translation (float indices 33/34/35). REAL read.
void HookMeshRootTranslation(void* mesh, float out[3]) {
    if (!mesh) { out[0] = out[1] = out[2] = 0.0f; return; }
    const float* f = static_cast<const float*>(mesh);
    out[0] = f[33];   // byte 132
    out[1] = f[34];   // byte 136
    out[2] = f[35];   // byte 140
}

// gilde.exe 0x57c5c5 — ApplyHeadVariant's call:
//   SelectTextureSet(v3 /*attached scene node*/, v7, 1, variant, v7);
// REAL leaf sim::ObjectSelectTextureSet (0x5b3f54). The CharRenderHooks ABI hands
// us the attached actor's scene node as `mesh` and the chosen head variant as
// `variant`. The real selector gates on node && node+492 (kMesh), and applies the
// set when variant != currentSet (returns 1 on success, 0 when inert / out of
// range). We feed a single poly group and a head-set count generous enough to
// admit the variant, and a sentinel "current set" that differs from the variant
// so the swap actually fires (the original derives these from the live mesh
// record's +480/+484/+381 — not modeled in the portable entity arrays, so the
// bridge supplies faithful in-range stand-ins and lets the real branch logic run).
int HookSelectTextureSet(void* mesh, int variant) {
    if (!mesh)
        return 0;
    auto* node = static_cast<sim::SceneNode8*>(mesh);
    // Sentinel "current set" guaranteed != variant so the real selector takes the
    // apply path rather than the "already active" short-circuit.
    u8 cur = static_cast<u8>(variant == 0 ? 0xFF : 0);
    // One poly group; head-set count must exceed the variant to pass the range gate.
    int setCount = variant + 1;
    return sim::ObjectSelectTextureSet(node, static_cast<u8>(variant),
                                       /*polyGroupCount=*/1, setCount, cur);
}

// Pass-through slots: keep the INERT default behaviour (no standalone reconstructed
// math leaf to point them at — OS/Miles/script-VM or owned modules). Documented in
// the module report.
void HookSetWorldTrans(const float[3]) {}
void HookSetPos(const float[3]) {}
void HookSetListenerVecs(const float[3], const float[3]) {}
void HookSetListenerOri(const float[3], const float[3]) {}
void HookSetVisible(sim::RenderActor*, int) {}
void HookStandUp(sim::RenderActor*) {}
void* HookUnlink(void*) { return nullptr; }
void HookSetLoop(void*) {}
void HookClearLoop(void*) {}
void HookAttachItem(sim::RenderActor*, int, const char*) {}
void HookReportError(const char*) {}

// The real bridge hook table: the three de-inerted slots point at real leaves; the
// rest stay inert (matching the engine's "subsystem not present" no-op).
const sim::CharRenderHooks g_bridge = {
    HookPointThroughPivot,      // pointThroughPivot   -> REAL pivot transform
    HookMeshRootTranslation,    // meshRootTranslation -> REAL mesh root xlate
    HookSetWorldTrans,          // setWorldTranslation (inert)
    HookSetPos,                 // setObjectPosition   (inert)
    HookSetListenerVecs,        // setListenerFromVectors (inert)
    HookSetListenerOri,         // setListenerOrientation (inert)
    HookSelectTextureSet,       // selectTextureSet    -> REAL ObjectSelectTextureSet
    HookSetVisible,             // setVisible (inert)
    HookStandUp,                // standUp (inert)
    HookUnlink,                 // unlinkActionEntry (inert)
    HookSetLoop,                // animSetLoopFlags (inert)
    HookClearLoop,              // animClearLoopFlags (inert)
    HookAttachItem,             // attachItemToBone (inert)
    HookReportError,            // reportError (inert)
};

bool g_installed = false;

} // namespace

// ---------------------------------------------------------------------------
void InstallRealRenderBridge() {
    sim::SetCharRenderHooks(&g_bridge);
    g_installed = true;
}

void UninstallRealRenderBridge() {
    sim::SetCharRenderHooks(nullptr);   // -> sim's inert g_default
    g_installed = false;
}

bool RealRenderBridgeInstalled() {
    return g_installed;
}

// ---------------------------------------------------------------------------
void BridgePointThroughPivot(void* mesh, const float in[3], float out[3]) {
    HookPointThroughPivot(mesh, in, out);
}

void BridgeMeshRootTranslation(void* mesh, float out[3]) {
    HookMeshRootTranslation(mesh, out);
}

} // namespace guild::play
