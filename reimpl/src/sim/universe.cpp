// universe — scene-slot manager (gilde.exe). 1:1 translation of the Universe_*
// slot save/restore + camera bootstrap. The render/scene leaves route through
// UniverseRenderHooks; the slot bookkeeping and the byte-exact per-slot save/
// restore are translated from the Hex-Rays reference of record.
#include "sim/universe.h"

#include <cstring>

namespace guild::sim {

// --- module globals --------------------------------------------------------
UniverseRecord  g_universeSlots[kUniverseSlotCapacity] = {};  // byte_13ECEC8
RenderGlobals   g_render = {};                                 // the 64A0xx/13FCxx set
UniverseRecord* g_activeUniverseRecord = nullptr;             // off_649D64

u32 g_megaCam  = 0;   // dword_649EFC
u32 g_camOben  = 0;   // dword_649EF0
u32 g_camVorne = 0;   // dword_649EF4
u32 g_camSeite = 0;   // dword_649EF8
u8  g_extraCameras = 0; // byte_649D54

// dword_649D60 — owned by character_query.cpp (one definition in the tree).
// (declared extern in the header)

// Frustum projection weights (flt_62845C/628460/628464 @0x62845C).
static const float kFrustumW0 = 0.59f;
static const float kFrustumW1 = 0.30f;
static const float kFrustumW2 = 0.11f;

// ---------------------------------------------------------------------------
// Render hooks (inert default: camera handles are synthetic, walks are no-ops).
// ---------------------------------------------------------------------------
namespace {
u32  InertSpawn(int, const char*)                       { static u32 s = 0x1000; return ++s; }
void InertLink(u32)                                     {}
void InertFog(float, float, u32)                        {}
void InertTerrain(u32)                                  {}
void InertCamera(u32, const i32*)                       {}
void InertWalkRestore(UniverseRecord*, void*, int, u8)  {}
void InertWalkClear(UniverseRecord*)                    {}
const UniverseRenderHooks kInert = {
    &InertSpawn, &InertLink, &InertFog, &InertTerrain, &InertCamera,
    &InertWalkRestore, &InertWalkClear };
const UniverseRenderHooks* g_hooks = &kInert;
} // namespace

void SetUniverseRenderHooks(const UniverseRenderHooks* hooks) {
    g_hooks = hooks ? hooks : &kInert;
}
const UniverseRenderHooks& GetUniverseRenderHooks() { return *g_hooks; }

void ResetUniverse() {
    std::memset(g_universeSlots, 0, sizeof(g_universeSlots));
    g_render = RenderGlobals{};
    g_activeUniverseRecord = &g_universeSlots[0];
    g_megaCam = g_camOben = g_camVorne = g_camSeite = 0;
    g_activeUniverseId = 0;
}

// ===========================================================================
// gilde.exe 0x5b5f48 — VIBE_Universe_CreateDefaultCameras.
//   g_megaCam = Object_Spawn(3,"MegaCam"); set pos/world; LinkIntoScene.
//   if ( byte_649D54 ) spawn "Oben"/"Vorne"/"Seite" ortho cams, clearing bit 2 of
//   their +529 flag byte after linking. (The transform constants are camera setup
//   the render leaf owns; we record only the handles + the link calls.)
// ===========================================================================
void UniverseCreateDefaultCameras() {
    g_megaCam = GetUniverseRenderHooks().spawnObject(3, "MegaCam");
    GetUniverseRenderHooks().linkObject(g_megaCam);
    if (g_extraCameras) {
        g_camOben = GetUniverseRenderHooks().spawnObject(3, "Oben");
        GetUniverseRenderHooks().linkObject(g_camOben);
        g_camVorne = GetUniverseRenderHooks().spawnObject(3, "Vorne");
        GetUniverseRenderHooks().linkObject(g_camVorne);
        g_camSeite = GetUniverseRenderHooks().spawnObject(3, "Seite");
        GetUniverseRenderHooks().linkObject(g_camSeite);
    }
}

// ===========================================================================
// gilde.exe 0x5b46dc — VIBE_Universe_InitCameraNode(record).
//   if ( !rec || rec+128 || rec+164 ) return 0;   // already initialized
//   spawn a MegaCam; write the default frustum / near=4 / far=15 constants;
//   rec+972 = 4; rec+976 = 15; rec+128 = rec+132 = rec+136 = node.
// We model the observable record writes (the node body is render-owned).
// ===========================================================================
bool UniverseInitCameraNode(UniverseRecord* rec) {
    if (!rec || rec->objListHead || rec->renderNodeHead)
        return false;
    u32 node = GetUniverseRenderHooks().spawnObject(3, "MegaCam");
    rec->activeCamera = node;
    rec->objListHead = node;   // rec+128 = rec+132 = rec+136 = node
    rec->objListTail = node;
    rec->nearPlane = 4;        // rec+972 = 4 (dword_649D8C default)
    rec->farPlane  = 15;       // rec+976 = 15 (dword_649D90 default)
    rec->reloadFlags = 0;      // rec+982 = 0 (0x5b47b0 *(a1+982)=0)
    rec->visGuard  = static_cast<u8>(g_render.visGuard); // rec+983 = byte_649D70 (0x5b4854)
    return true;
}

// ===========================================================================
// gilde.exe 0x5b4a24 — VIBE_Universe_SwitchActiveSlot(slot, quiet).
//
//   if ( slot >= 64 ) return 0;
//   if ( slot != dword_649D60 ) {
//       // save the live globals into the active slot's record (cols at 246*active)
//       save(active);
//       // first visit to the target slot -> init its camera node
//       if ( !target.objListHead ) InitCameraNode(target);
//       // load the target slot's record into the live globals
//       load(target);
//       off_649D64 = &slots[slot]; dword_649D60 = slot;
//       // re-place the active camera
//       if ( dword_649EFC ) { SetPosition/SetWorldTranslation/SetActiveCamera }
//       if ( !quiet ) { rebuild terrain (present-vs-absent); ConfigureFog(...) }
//   }
//   if ( quiet ) return 1;
//   <present-frame DDraw tail — render-owned, omitted>
//   return 1;
// ===========================================================================
bool UniverseSwitchActiveSlot(int slot, bool quiet) {
    if (static_cast<unsigned>(slot) >= static_cast<unsigned>(kUniverseSlotCapacity))
        return false;

    if (slot != g_activeUniverseId) {
        UniverseRecord& active = g_universeSlots[g_activeUniverseId];

        // --- save live globals -> active slot record ------------------------
        active.objListHead    = g_render.objListHead;
        active.objListTail    = g_render.objListTail;
        active.activeCamera   = g_render.activeCamera;
        std::memcpy(active.camXform, g_render.camXform, sizeof(active.camXform));
        active.renderNodeHead = g_render.renderNodeHead;
        active.renderNodeTail = g_render.renderNodeTail;
        active.floor          = g_render.floor;
        active.sky            = g_render.sky;
        active.clipNear       = g_render.clipNear;
        active.fov0           = g_render.fov0;
        active.fov1           = g_render.fov1;
        active.ang0           = g_render.ang0;
        active.ang1           = g_render.ang1;
        active.ang2           = g_render.ang2;
        active.fogColor       = g_render.fogColor;
        active.fogNear        = g_render.fogNear;
        active.fogFar         = g_render.fogFar;
        active.nearPlane      = g_render.nearPlane;
        active.farPlane       = g_render.farPlane;
        active.reloadFlags    = g_render.reloadFlags;
        active.visGuard       = g_render.visGuard;

        // first visit -> seed the camera node.
        UniverseRecord& target = g_universeSlots[slot];
        if (!target.objListHead)
            UniverseInitCameraNode(&target);

        // --- load target slot record -> live globals ------------------------
        g_render.objListHead    = target.objListHead;
        g_render.objListTail    = target.objListTail;
        g_render.clipNear       = target.clipNear;
        g_render.fov0           = target.fov0;
        g_render.fov1           = target.fov1;
        g_render.ang0           = target.ang0;
        g_render.ang1           = target.ang1;
        g_render.ang2           = target.ang2;
        g_render.renderNodeHead = target.renderNodeHead;
        g_render.renderNodeTail = target.renderNodeTail;
        g_render.floor          = target.floor;
        g_render.sky            = target.sky;
        std::memcpy(g_render.camXform, target.camXform, sizeof(g_render.camXform));
        g_render.nearPlane      = target.nearPlane;
        g_render.farPlane       = target.farPlane;
        g_render.reloadFlags    = target.reloadFlags;

        // flt_64A070 = fov0*W0 + clipNear*W1 + fov1*W2  (frustum scalar).
        g_render.frustum = g_render.fov0 * kFrustumW0 +
                           g_render.clipNear * kFrustumW1 +
                           g_render.fov1 * kFrustumW2;

        // off_649D64 = &slots[slot]; dword_649D60 = slot.
        g_activeUniverseRecord = &target;
        g_activeUniverseId      = slot;

        g_render.activeCamera = target.activeCamera;
        g_megaCam = target.activeCamera;
        if (g_megaCam)
            GetUniverseRenderHooks().placeActiveCamera(g_megaCam, g_render.camXform);

        if (!quiet) {
            // terrain rebuild: present floor -> nothing to add here; absent ->
            // (re)build the terrain mesh. The heightmap leaf owns the mesh.
            if (!g_render.floor)
                GetUniverseRenderHooks().buildTerrain(g_render.floor);
            GetUniverseRenderHooks().configureFog(target.fogNear, target.fogFar,
                                                  target.fogColor);
        }
    }

    if (quiet)
        return true;
    // present-frame DDraw flip tail is render-owned (omitted).
    return true;
}

// ===========================================================================
// gilde.exe 0x5b44c4 — VIBE_Universe_ResetCurrentSlot.
//   dispose object lists (hook); clear floor/sky; reset fog/clip defaults
//   (200.0 base); rebuild the 7 camera arrays; respawn default cameras; zero the
//   active slot's record (+0,+972=4,+976=15,+980/+981/+982); return 984*activeId.
// We model the field resets + camera respawn (the dispose/free leaves are hooks).
// ===========================================================================
unsigned UniverseResetCurrentSlot() {
    // clear live floor/sky/fog/clip to the engine defaults.
    g_render.floor    = 0;
    g_render.sky      = 0;
    g_render.ang0     = 0.0f;   // flt_64A084
    g_render.ang1     = 0.0f;   // flt_64A088
    g_render.ang2     = 0.0f;   // flt_64A08C
    g_render.frustum  = 200.0f; // flt_64A070
    g_render.clipNear = 200.0f; // flt_64A074
    g_render.fov0     = 200.0f; // flt_64A078
    g_render.fov1     = 200.0f; // flt_64A07C

    g_megaCam = g_camOben = g_camVorne = g_camSeite = 0;
    UniverseCreateDefaultCameras();

    g_render.reloadFlags = 0;   // byte_649DD0
    g_render.nearPlane   = 4;   // dword_649D8C
    g_render.farPlane    = 15;  // dword_649D90

    unsigned off = static_cast<unsigned>(kUniverseRecordStride) *
                   static_cast<unsigned>(g_activeUniverseId);
    UniverseRecord& active = g_universeSlots[g_activeUniverseId];
    active.objListTail = 0;     // col 0x13ECF4C
    active.objListHead = 0;     // col 0x13ECF48
    active.head[0]     = 0;     // byte_13ECEC8[off]
    active.noReload    = 0;     // byte_13ED29D[off]
    active.field0x3D4  = 0;     // byte_13ED29C[off]
    return off;
}

// ===========================================================================
// gilde.exe 0x5b43f0 — VIBE_Universe_RestoreObjectStates(record, mode).
//   if ( !record ) return 0;
//   SceneGraph_WalkAndInvoke(off_649D64, record, Object_RestoreSuspendState,
//                            1023, mode);
//   return 1;
// ===========================================================================
bool UniverseRestoreObjectStates(UniverseRecord* rec, u8 mode) {
    if (!rec)
        return false;
    GetUniverseRenderHooks().walkRestoreStates(g_activeUniverseRecord, rec, 1023, mode);
    return true;
}

// ===========================================================================
// gilde.exe 0x5b2cd8 — VIBE_Universe_ClearActiveMeshes.
//   return SceneGraph_TraverseTree(off_649D64, 0, Object_AssignMeshData, 64);
// ===========================================================================
bool UniverseClearActiveMeshes() {
    GetUniverseRenderHooks().walkClearMeshes(g_activeUniverseRecord);
    return true;
}

} // namespace guild::sim
