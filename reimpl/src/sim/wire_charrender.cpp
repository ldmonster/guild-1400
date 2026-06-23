// See wire_charrender.h. Binds the reconstructed action-queue pool allocator into
// CharRender4Hooks and the reconstructed VIBE_Util_StrCmp into CharRender5Hooks.
#include "sim/wire_charrender.h"

#include "sim/character_render4.h"  // CharRender4Hooks / SetCharRender4Hooks
#include "sim/character_render5.h"  // CharRender5Hooks / SetCharRender5Hooks
#include "sim/charaction.h"         // REAL QueueInsertEntry (0x40c15c) / UnlinkEntry (0x404370)
#include "sim/character.h"          // Character (QueueInsertEntry arg)

// Reused, already-reconstructed deterministic sibling (extern-declared, linked):
namespace guild::render { int AnimStrCmp(const char* a, const char* b); }  // 0x5d3f10 VIBE_Util_StrCmp

namespace guild::sim {

namespace {

// --- CharRender4 forwarders: delegate to the GENUINE reconstructed siblings. --------
// (Mirrors the live-wiring forwarders pinned by the character_render4 integration test.)
ActionNode* RealQueueInsert(Character* ch) { return guild::sim::QueueInsertEntry(ch); }
int         RealUnlink(ActionNode* n)      { return guild::sim::UnlinkEntry(n); }

// --- CharRender5: the one reconstructed leaf is the camera-dispatch comparator. -----
// VIBE_Util_StrCmp (0x5d3f10): faithful byte compare, 0 == equal, case-SENSITIVE.
int RealStrCmp(const char* a, const char* b) { return guild::render::AnimStrCmp(a, b); }

// Faithful inert defaults for CharRender5's remaining fields — all pure renderer / anim
// / object / heightmap LEAVES (rule 3, not reconstructed). These mirror the inert
// defaults in character_render5.cpp so a process-lifetime install never dereferences a
// null leaf (GetCharRender5Hooks returns the installed table verbatim).
void  InertTouchMeshFrames(void*) {}
void  InertPruneAttachments(void*) {}
void  InertSetupAttachCamera(void*, int) {}
void  InertScriptError(const char*) {}
void* InertFindByName(const char*) { return nullptr; }
void* InertObjectFindByHandle(void*, int, int, int, int) { return nullptr; }
void* InertResolveMesh(void*) { return nullptr; }
int   InertWorldToTileWithHeight(void*, const float*, int* t, float* h) {
    if (t) { t[0] = 0; t[1] = 0; }
    if (h) *h = 0.0f;
    return 1;
}
u8    InertTerrainCodeAt(void*, int, int) { return 1; }  // walkable (not 0, not 13)
int   InertTraceLineOfSight(void*, const float*, int toCol, int toRow,
                            int* oc, int* orr) {
    if (oc) *oc = toCol;
    if (orr) *orr = toRow;
    return 1;
}
void  InertFreeObjAnimData(void*) {}
void  InertLightUpdateDayCycle(void*) {}

}  // namespace

void InstallRealCharRenderWiring() {
    // --- CharRender4Hooks: the genuine action-node pool allocator + unlinker. --------
    // Default was a null-returning stub (builders early-out); now the BUILDERS thread
    // the REAL intrusive action queue.
    static CharRender4Hooks render4{};
    render4.queueInsertEntry = &RealQueueInsert;   // VIBE_CharAction_QueueInsertEntry 0x40c15c
    render4.unlinkEntry      = &RealUnlink;         // VIBE_ActionQueue_UnlinkEntry     0x404370
    SetCharRender4Hooks(&render4);

    // --- CharRender5Hooks: bind the reconstructed comparator; keep the render leaves --
    // inert. strCmp drives the SetCameraViewMode name dispatch.
    static CharRender5Hooks render5{};
    render5.touchMeshFrames       = &InertTouchMeshFrames;
    render5.pruneAttachments      = &InertPruneAttachments;
    render5.strCmp                = &RealStrCmp;            // VIBE_Util_StrCmp 0x5d3f10 (reconstructed)
    render5.setupAttachCamera     = &InertSetupAttachCamera;
    render5.scriptError           = &InertScriptError;
    render5.findByName            = &InertFindByName;
    render5.objectFindByHandle    = &InertObjectFindByHandle;
    render5.resolveMesh           = &InertResolveMesh;
    render5.worldToTileWithHeight = &InertWorldToTileWithHeight;
    render5.terrainCodeAt         = &InertTerrainCodeAt;
    render5.traceLineOfSight      = &InertTraceLineOfSight;
    render5.freeObjAnimData       = &InertFreeObjAnimData;
    render5.lightUpdateDayCycle   = &InertLightUpdateDayCycle;
    SetCharRender5Hooks(&render5);

    // CharRender2Hooks / CharRender3Hooks / CharMeshHooks / CharQueryHooks expose only
    // pure render/scene/object/anim/heightmap/sound leaves (rule 3) — no reconstructed
    // pure-logic field to bind — so they intentionally stay on their inert default
    // tables. See wire_charrender.h.
}

}  // namespace guild::sim
