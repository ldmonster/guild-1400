#include "world/world_setup.h"

#include "world/city.h"                 // CityInitParameterTable (reused)
#include "sim/universe.h"               // UniverseResetCurrentSlot (reused)

namespace guild::world {

// World-bootstrap record-array base globals (heap ptrs in the original).
void* g_worldBuildingBase = nullptr;  // dword_13CE294
void* g_worldObjectBase   = nullptr;  // dword_13CE298
void* g_worldSceneBase    = nullptr;  // dword_13CE290
void* g_worldTypeBase     = nullptr;  // dword_13CE27C
bool  g_worldLoaded       = false;

// ===========================================================================
// gilde.exe 0x5839f0 — VIBE_World_CountActiveObjects.
//   v2 = 0; v3 = 0;
//   while ( StrCmpNoCase(name, typeBase + v3 + 1) ) {   // 0 == equal
//       v3 += 65; ++v2;
//       if ( v3 >= 47515 ) return 0;                    // 47515 = 65*731
//   }
//   return v2;
// The descriptor name lives at +1 of each 65-byte type-def record. We reproduce
// the case-insensitive (A-Z only) compare and the 731-entry scan bound.
// ===========================================================================
namespace {
// VIBE_Util_StrCmpNoCase (0x5cb8f0): A-Z lowercased; returns 0 on equal.
int StrCmpNoCase(const unsigned char* a, const unsigned char* b) {
    for (;;) {
        unsigned char v3 = *a;
        unsigned char v4 = *b;
        if (v3 >= 0x41 && v3 <= 0x5A) v3 += 32;
        if (v4 >= 0x41 && v4 <= 0x5A) v4 += 32;
        if (v3 != v4 || !v4)
            return static_cast<int>(v3) - static_cast<int>(v4);
        ++a;
        ++b;
    }
}
} // namespace

int WorldCountActiveObjects(const char* name, const void* typeBase, int typeStride,
                            int typeCount) {
    if (!typeBase)
        return 0;
    const unsigned char* base = static_cast<const unsigned char*>(typeBase);
    const int bound = typeStride * typeCount;   // 65 * 731 == 47515
    int index = 0;
    int byteOff = 0;
    while (StrCmpNoCase(reinterpret_cast<const unsigned char*>(name),
                        base + byteOff + 1) != 0) {
        byteOff += typeStride;
        ++index;
        if (byteOff >= bound)
            return 0;
    }
    return index;
}

// Convenience binding over the world's own type-def base (dword_13CE27C ==
// g_worldTypeBase). Matches the engine, which reads dword_13CE27C directly.
int WorldCountActiveObjects(const char* name) {
    if (!g_worldTypeBase)
        return 0;
    return WorldCountActiveObjects(name, g_worldTypeBase, /*typeStride=*/65,
                                   /*typeCount=*/731);
}

// ===========================================================================
// WorldSetupInit — world-state bootstrap.
// ===========================================================================
void WorldSetupInit(float capDivisor) {
    // 1. reset the active scene slot (cameras + slot record) to a clean state.
    guild::sim::UniverseResetCurrentSlot();

    // 2. seed the city economy parameter table (28-good drift/contrib/cap defaults).
    CityInitParameterTable(capDivisor);

    // 3. clear the world-bootstrap record-array globals to "unloaded".
    g_worldBuildingBase = nullptr;
    g_worldObjectBase   = nullptr;
    g_worldSceneBase    = nullptr;
    g_worldTypeBase     = nullptr;
    g_worldLoaded       = false;
}

} // namespace guild::world
