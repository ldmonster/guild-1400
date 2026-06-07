// character_mesh — the mesh-resolution cache/reload decision + the fade-out slot
// table management. Faithful 1:1 port of the gilde.exe data-rules; render leaves
// (heightmap build, reload bracket, transparency ramp) via CharMeshHooks.
#include "sim/character_mesh.h"

#include "sim/character_query.h"   // LiveActor, Universe, g_activeUniverse

#include <cstring>

namespace guild::sim {

// dword_66F010 @0x66F010 — 16 fade slots.
FadeSlot g_fadeSlots[kFadeSlotCapacity] = {};
void ResetFadeSlots() { std::memset(g_fadeSlots, 0, sizeof(g_fadeSlots)); }

// ---------------------------------------------------------------------------
// Mesh hooks (inert defaults).
// ---------------------------------------------------------------------------
namespace {
const CharMeshHooks* g_mhooks = nullptr;
void* DefCreateMesh(int) { return nullptr; }
void  DefBuildCollisionGrid(void*) {}
void  DefReloadBracket(int, int) {}
void  DefChangeTransparency(void*, int) {}
void  DefSetVisible(LiveActor*, int) {}
const CharMeshHooks g_mDefault = {
    DefCreateMesh, DefBuildCollisionGrid, DefReloadBracket,
    DefChangeTransparency, DefSetVisible,
};
} // namespace

void SetCharMeshHooks(const CharMeshHooks* h) { g_mhooks = h; }
const CharMeshHooks& GetCharMeshHooks() { return g_mhooks ? *g_mhooks : g_mDefault; }

// gilde.exe 0x4013fc — VIBE_Character_ResolveMesh.
//   v4 = *(char**)(u+176);                 // cached mesh
//   if (v4) return v4;
//   v6 = (u == off_649D64) ? byte_649DD0 : *(BYTE*)(u+982);   // reload flags
//   if (!*(BYTE*)(u+981) && v6) {           // not guarded & has reload flags
//     idx = IndexFromPointer(u);
//     v17 = v6 & 0xFD;                       // mask off bit 1
//     SwitchActiveSlot(idx, ...); InitLogAndInflate(v6 & 0xFD);
//   }
//   v11 = Heightmap_Create(dword_64A028, *(DWORD*)(u+180), 2);
//   *(DWORD*)(u+176) = v11; v4 = v11;
//   if (u != byte_13ECEC8 && u == off_649D64 && v11) BuildCollisionGrid(v11);
//   if (!v17) return v4;
//   DisplayLogAndCleanup(v17); SwitchActiveSlot(activeId);  // close bracket
//   return v4;
void* ResolveMesh(Universe* u, u8 activeReloadFlags) {
    if (!u)
        return nullptr;
    if (u->meshHandle)
        return u->meshHandle;

    const CharMeshHooks& hk = GetCharMeshHooks();

    // reload flags: active universe uses byte_649DD0, otherwise the universe's +982.
    u8 reloadFlags = (u == g_activeUniverse) ? activeReloadFlags : u->flags;

    u8 bracketFlags = 0;  // v17 (0 == no reload bracket opened)
    if (!u->noReload && reloadFlags) {
        bracketFlags = reloadFlags & 0xFD;   // mask bit 1
        int idx = IndexFromUniverse(u);
        hk.reloadBracket(idx, /*begin*/ 0);
    }

    void* mesh = hk.createMesh(u->assetHandle);
    u->meshHandle = mesh;

    // For the active universe (and not the special slot-0 array base) build grid.
    if (u != &g_universes[0] && u == g_activeUniverse && mesh)
        hk.buildCollisionGrid(mesh);

    if (bracketFlags) {
        hk.reloadBracket(g_activeUniverseId, /*end*/ 1);
    }
    return mesh;
}

// gilde.exe 0x4017d4 — VIBE_Character_RegisterFadeSlot.
//   scan dword_66F010 by 12-byte stride for the first free slot (cap 16);
//   if (slot < 16) {
//     dword_66F010[3*slot]   = actor;
//     byte_66F014[12*slot]   = kind;
//     dword_66F018[3*slot]   = tickNow - 1;
//     SetVisible(actor, 1);
//     ... build alpha (0xFF, clear bit 0x10000) ...
//     ChangeTransparency(actor.mesh, alpha);
//     if (actor.transport) ChangeTransparency(actor.transport, alpha);
//     *(BYTE*)(actor+140) |= 0x40;
//   }
//   return slot result.
int RegisterFadeSlot(LiveActor* actor, u8 kind, int tickNow) {
    int slot = 0;
    while (slot < kFadeSlotCapacity && g_fadeSlots[slot].actor != nullptr)
        ++slot;
    if (slot >= kFadeSlotCapacity)
        return -1;

    const CharMeshHooks& hk = GetCharMeshHooks();
    g_fadeSlots[slot].actor     = actor;
    g_fadeSlots[slot].kind      = kind;
    g_fadeSlots[slot].startTick = tickNow - 1;

    hk.setVisible(actor, 1);
    // alpha frame: full opacity marker with the fade bit cleared (engine packs a
    // color struct; the observable effect is a transparency ramp on the meshes).
    int alpha = 0xFF;
    hk.changeTransparency(actor->mesh, alpha);
    if (actor->transport)
        hk.changeTransparency(actor->transport, alpha);
    actor->flagsA |= kLaFadeSlot;   // +140 |= 0x40
    return slot;
}

// gilde.exe 0x426430 — VIBE_Character_ResetMeshThunk. Case-insensitive inequality.
int ResetMeshThunk(const char* a, const char* b) {
    if (!a || !b)
        return 1;
#if defined(_MSC_VER)
    return _stricmp(a, b) != 0 ? 1 : 0;
#else
    return strcasecmp(a, b) != 0 ? 1 : 0;
#endif
}

} // namespace guild::sim
