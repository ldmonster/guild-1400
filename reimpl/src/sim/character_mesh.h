#pragma once
// character_mesh — the mesh/anim RESOLUTION data-rules of the Character cluster
// (gilde.exe). The functions here own the *bookkeeping* around mesh resolution:
//   * ResolveMesh: the universe-mesh cache + reload-guard decision (which universe
//     slot to (re)build a heightmap/collision mesh for, and whether a reload is
//     needed); the actual VIBE_Heightmap_Create / Map_BuildCollisionGrid /
//     Universe_SwitchActiveSlot LEAVES are render and are forward-declared/hooked.
//   * RegisterFadeSlot: the fade-out slot table (16 × 12-byte entries) management —
//     pure data (append slot, stamp the start tick, flip the +140 fade bit). The
//     transparency-ramp LEAF (VIBE_Object_ChangeTransparency) is hooked.
//   * ResetMeshThunk: the mesh-name compare used by the cache (string compare).
//
// Render/mesh/anim LEAVES (forward-declared, no body here — see report "deferred"):
//   VIBE_Character_CacheActiveMesh        0x4262c0  (CreateObjectAnim wrapper)
//   VIBE_Character_GetMeshPositionPair    0x428ed0  (bone position pair)
//   VIBE_Character_ComputeAnimBlendVectors 0x428f38 (anim blend vectors)
//   VIBE_Character_ScreenToWorldRay       0x426850  (camera ray)
//   VIBE_Character_ProjectRayDirection    0x426764  (camera ray)
//   VIBE_Character_PreloadAniSetByName    0x403f14  (asset preload)
//
// Translated functions (this TU):
//   VIBE_Character_IndexFromPointer  0x426724  (see character_query.h — reused)
//   VIBE_Character_ResolveMesh       0x4013fc   (universe-mesh resolve decision)
//   VIBE_Character_RegisterFadeSlot  0x4017d4   (fade-slot table append)
//   VIBE_Character_ResetMeshThunk    0x426430   (mesh-name compare)
#include "guild/common/types.h"

namespace guild::sim {

struct LiveActor;  // character_query.h
struct Universe;   // character_query.h

// ===========================================================================
// Mesh-resolution render LEAVES (forward-declared; bodies are render-cluster).
// The data-rule functions call these through the hook table below so the cache /
// reload decision is testable without a renderer.
// ===========================================================================
struct CharMeshHooks {
    // VIBE_Heightmap_Create(srcAsset): build the collision/heightmap mesh for a
    // universe's asset handle (+180); returns the new mesh handle (or null).
    void* (*createMesh)(int srcAsset);
    // VIBE_Map_BuildCollisionGrid(mesh): only for the active universe.
    void  (*buildCollisionGrid)(void* mesh);
    // VIBE_Universe_SwitchActiveSlot / InitLogAndInflate / DisplayLogAndCleanup:
    // the reload bracket. `slot` is the universe index; `mode` selects begin/end.
    void  (*reloadBracket)(int slot, int mode);
    // VIBE_Object_ChangeTransparency(mesh, alpha): the fade-slot transparency ramp.
    void  (*changeTransparency)(void* mesh, int alpha);
    // VIBE_Character_SetVisible(actor, visible): the fade-slot show.
    void  (*setVisible)(LiveActor* a, int visible);
};
void SetCharMeshHooks(const CharMeshHooks* hooks);
const CharMeshHooks& GetCharMeshHooks();

// ===========================================================================
// Fade-out slot table (gilde.exe dword_66F010 @0x66F010, 16 entries × 12 bytes:
// [+0] actor ptr (dword), [+4] kind byte, [+8] start-tick (dword)). RegisterFadeSlot
// appends a slot. Modeled as a small struct array.
// ===========================================================================
struct FadeSlot {
    LiveActor* actor;   // +0  actor being faded
    u8         kind;    // +4  fade kind byte (a2)
    int        startTick; // +8 start tick (dword_62D008 - 1)
};
constexpr int kFadeSlotCapacity = 16;
extern FadeSlot g_fadeSlots[kFadeSlotCapacity];  // dword_66F010
void ResetFadeSlots();

// gilde.exe 0x4013fc — VIBE_Character_ResolveMesh. Returns the universe's
// collision/heightmap mesh, building it on first use. Decision logic (1:1):
//   * if the universe already has a mesh (+176), return it.
//   * pick the reload flags: active universe -> byte_649DD0, else universe+982.
//   * if the universe is not reload-guarded (+981==0) and has reload flags, bracket
//     a reload (SwitchActiveSlot/InitLogAndInflate via the hook).
//   * build the mesh from the universe asset (+180), store it at +176; for the
//     active universe also build the collision grid.
//   * close the reload bracket if one was opened.
// `activeReloadFlags` == byte_649DD0 (the active-universe reload flag byte).
void* ResolveMesh(Universe* u, u8 activeReloadFlags);

// gilde.exe 0x4017d4 — VIBE_Character_RegisterFadeSlot. Appends `actor` to the
// first free fade slot (scan for the first null actor, cap 16), stamping the fade
// kind and start tick (`tickNow` == dword_62D008), shows the actor, ramps its mesh
// + transport-mesh transparency (via the hook), and sets the +140 fade bit (0x40).
// Returns the slot index used, or -1 if the table is full.
int RegisterFadeSlot(LiveActor* actor, u8 kind, int tickNow);

// gilde.exe 0x426430 — VIBE_Character_ResetMeshThunk. The mesh-cache name compare:
// returns 1 if the two names differ (case-insensitive), else 0 (and the engine
// records the matched handle). Modeled as a pure case-insensitive inequality test.
int ResetMeshThunk(const char* a, const char* b);

} // namespace guild::sim
