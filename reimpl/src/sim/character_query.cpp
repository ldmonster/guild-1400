// character_query — owner/universe collection + count data-rules. Faithful 1:1
// port of the gilde.exe Character iteration code; render/heightmap/rng leaves via
// CharQueryHooks. See character_query.h for the function/address map.
#include "sim/character_query.h"

#include <cmath>
#include <cstring>

namespace guild::sim {

// gilde.exe 0x5caa4c — VIBE_Math_VectorWithinTolerance (axis-aligned box test).
// (Also defined in character_social.cpp for the social scan; this is a local
// internal copy so the query pass does not drag in the action-system TU. The
// canonical declaration lives in character_social.h.)
namespace {
bool BoxWithin(const float a[3], const float b[3], float tol) {
    return std::fabs(b[0] - a[0]) <= tol
        && std::fabs(b[1] - a[1]) <= tol
        && std::fabs(b[2] - a[2]) <= tol;
}
} // namespace

// dword_66F0D0 @0x66F0D0 — 512 live-actor slots (null == empty).
LiveActor* g_live[kLiveCapacity] = {};
// byte_13ECEC8 @0x13ECEC8 — 64 universe slots (stride 984).
Universe   g_universes[kUniverseSlotCount] = {};
// off_649D64 / dword_649D60 — active scene ptr + id.
Universe*  g_activeUniverse   = nullptr;
int        g_activeUniverseId = 0;

void ResetCharacterQuery() {
    std::memset(g_live, 0, sizeof(g_live));
    for (auto& u : g_universes) u = Universe{};
    g_activeUniverse   = nullptr;
    g_activeUniverseId = 0;
}

// ---------------------------------------------------------------------------
// Query hooks (inert defaults).
// ---------------------------------------------------------------------------
namespace {
const CharQueryHooks* g_qhooks = nullptr;
void DefSetVisible(LiveActor*, int) {}
int  DefWorldToTile(LiveActor*, const float*, int* c, int* r) { if (c) *c = 0; if (r) *r = 0; return 1; }
u8   DefTerrainAt(LiveActor*, int, int) { return 1; }  // 1 != 0 && != 13 -> walkable
const CharQueryHooks g_qDefault = { DefSetVisible, DefWorldToTile, DefTerrainAt };
} // namespace

void SetCharQueryHooks(const CharQueryHooks* h) { g_qhooks = h; }
const CharQueryHooks& GetCharQueryHooks() { return g_qhooks ? *g_qhooks : g_qDefault; }

// ---------------------------------------------------------------------------
// Mesh cull-gate test, shared by the owner counts. The engine reads
// *(BYTE*)(mesh+533) != 1 (1 == culled). A null mesh is treated as "gate open".
// ---------------------------------------------------------------------------
namespace {
inline bool MeshGateOpen(const MeshHandle* m) { return !m || m->cullGate != 1; }
} // namespace

// gilde.exe 0x401a9c — VIBE_Character_CountActiveUniverse.
//   v0 = 0;
//   for (i = 0; i != 517120; i += 404)
//     if (*(DWORD*)(i + dword_62CEFC)) ++v0;
//   return v0;
// The pool is 1280 nodes of stride 404; a node is live when its step-fn slot
// (node+0) is nonzero. We pass that column in as `poolStep0`.
int CountActiveUniverse(const int* poolStep0, int nodeCount) {
    int v0 = 0;
    for (int i = 0; i < nodeCount; ++i) {
        if (poolStep0[i])
            ++v0;
    }
    return v0;
}

// gilde.exe 0x401ad4 — VIBE_Character_CountByOwner.
//   v5 = &universes[ownerUniverse];          // 984 * a1 + base
//   for (i = 0; i != 512; ++i) {
//     v7 = live[i];
//     if (v7 && (a1==-1 || v5 == *(char**)(v7+136))
//            && (a2 || *(BYTE*)(*(DWORD*)(v7+52)+533) != 1)) ++v4;
//   }
int CountByOwner(int ownerUniverse, int anyMesh) {
    Universe* key = (ownerUniverse >= 0 && ownerUniverse < kUniverseSlotCount)
                        ? &g_universes[ownerUniverse]
                        : nullptr;
    int v4 = 0;
    for (int i = 0; i < kLiveCapacity; ++i) {
        LiveActor* a = g_live[i];
        if (!a)
            continue;
        if (ownerUniverse != -1 && a->universe != key)
            continue;
        if (!anyMesh && !MeshGateOpen(a->mesh))
            continue;
        ++v4;
    }
    return v4;
}

// gilde.exe 0x401b40 — VIBE_Character_CountByOwnerInRange. Adds the box test.
int CountByOwnerInRange(int ownerUniverse, int anyMesh, const float center[3],
                        float tol) {
    Universe* key = (ownerUniverse >= 0 && ownerUniverse < kUniverseSlotCount)
                        ? &g_universes[ownerUniverse]
                        : nullptr;
    int v7 = 0;
    for (int i = 0; i < kLiveCapacity; ++i) {
        LiveActor* a = g_live[i];
        if (!a)
            continue;
        if (ownerUniverse != -1 && a->universe != key)
            continue;
        if (!anyMesh && !MeshGateOpen(a->mesh))
            continue;
        // BoxWithin(mesh+76, center, tol)
        if (a->mesh && BoxWithin(a->mesh->pos, center, tol))
            ++v7;
    }
    return v7;
}

// gilde.exe 0x401bd8 — VIBE_Character_CountWithTransport.
//   if (v6 && *(DWORD*)(v6+292) && (a1==-1 || a1==dword_649D60)) {
//     if (a2 || *(BYTE*)(*(DWORD*)(v6+52)+533) != 1 || *(DWORD*)(v6+296)) ++v4;
//   }
int CountWithTransport(int ownerUniverse, int anyMesh) {
    int v4 = 0;
    for (int i = 0; i < kLiveCapacity; ++i) {
        LiveActor* a = g_live[i];
        if (!a || !a->transport)
            continue;
        if (ownerUniverse != -1 && ownerUniverse != g_activeUniverseId)
            continue;
        if (anyMesh || !MeshGateOpen(a->mesh) /* gate==1 still counts here */ || a->action)
            ++v4;
    }
    return v4;
}

// gilde.exe 0x4b99ac — VIBE_Character_CollectByOwner.
//   (a1 != 0 branch)  v3 = 0; v5 = 0; v6 = 0;
//     do {
//       v7 = *((DWORD*)v4 + 97);                 // p+388 live actor
//       if (v7 && *(DWORD*)(v7+44) == *(DWORD*)(a1+1)) {  // home == owner key
//         ++v6; ++v3;
//         dword_11BB69C[v6] = v4;                 // PRE-increment: writes [1],[2],...
//         if (v3 > 8) SetVisible(p+388, 0);        // hide overflow (9th match on)
//       }
//       ++v5; v4 += 268;
//     } while (v5 < 768 && v3 < 31);
//   (a1 == 0 branch) collect actors with home universe id == -1 (wild),
//   same pre-incremented index (dword_11BB69C[++v11]).
// `ownerKeyId` is the *(DWORD*)(a1+1) value already resolved by the caller.
//
// IMPORTANT (1:1): the original stores matches into the global result buffer
// dword_11BB69C using a PRE-incremented index, so the first match lands at
// outPersons[1], the second at outPersons[2], ... — outPersons[0] is reserved
// (slot 0 == dword_11BB69C[0], read separately by the renderer). We reproduce
// that exact layout here.
int CollectByOwner(int ownerKeyId, LiveActor* const* personLiveActor,
                   int personCount, LiveActor** outPersons, int maxOut) {
    int matches = 0;  // v3 / v6 (== v11 in the wild branch)
    if (ownerKeyId) {
        for (int i = 0; i < personCount && matches < 31; ++i) {
            LiveActor* a = personLiveActor[i];
            if (a && a->universeId == ownerKeyId) {
                ++matches;                                // pre-increment index
                if (matches < maxOut)
                    outPersons[matches] = a;              // dword_11BB69C[v6]
                if (matches > 8)
                    GetCharQueryHooks().setVisible(a, 0);
            }
        }
    } else {
        for (int i = 0; i < personCount && matches < 31; ++i) {
            LiveActor* a = personLiveActor[i];
            if (a && a->universeId == -1) {
                ++matches;
                if (matches < maxOut)
                    outPersons[matches] = a;
            }
        }
    }
    return matches;
}

// gilde.exe 0x401c3c — VIBE_Character_CollectNearbyAtTile.
// flt_61003C == 2.0, dbl_610044 == 3.0 (recovered via get_bytes).
namespace {
constexpr float kRepulsionDistScale = 2.0f;   // flt_61003C
constexpr double kRepulsionStrength = 3.0;    // dbl_610044
} // namespace

int CollectNearbyAtTile(LiveActor* self, unsigned int (*rng)()) {
    // Eligibility gate: not sitting (+140 & 0x10), not dirty-mesh (& 8), no
    // transport target (+292), and not already in a type-45 action.
    if ((self->flagsA & kLaSitting) || (self->flagsA & kLaDirtyMesh) || self->transport)
        return 0;
    if (self->action && self->actionType == 45)
        return 0;

    // Radius: 50 normally, 100 if the actor is already talking (action type 45).
    float radius = 50.0f;
    if (self->action && self->actionType == 45)
        radius = 100.0f;

    LiveActor* found[16] = {};
    int hits = 0;
    for (int i = 0; i < kLiveCapacity && hits < 15; ++i) {
        LiveActor* c = g_live[i];
        if (!c || c == self)
            continue;
        // same universe id (+44) and group (+48)
        if (self->universeId != c->universeId || self->groupId != c->groupId)
            continue;
        // re-check self's gate (the engine re-reads it inside the loop)
        if ((self->flagsA & kLaSitting) || (self->flagsA & kLaDirtyMesh) || self->transport)
            continue;
        // candidate mesh gate: mesh+533 != 1, OR low-poly mesh exists with +533 != 1.
        bool gateOk = (c->mesh && c->mesh->cullGate != 1)
                   || (c->lowPoly && c->lowPoly->cullGate != 1);
        if (!gateOk)
            continue;
        if (self->mesh && c->mesh
            && BoxWithin(self->mesh->pos, c->mesh->pos, radius)) {
            found[hits++] = c;
        }
    }

    // For each neighbour: compute a repulsion vector and set the redraw target.
    for (int n = 0; n < hits; ++n) {
        LiveActor* other = found[n];
        if (!other->mesh || !self->mesh)
            continue;
        float dx = other->mesh->pos[0] - self->mesh->pos[0];
        float dy = other->mesh->pos[1] - self->mesh->pos[1];
        float dz = other->mesh->pos[2] - self->mesh->pos[2];
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist == 0.0f) {
            // coincident: pick a small pseudo-random offset in [-1,0]
            dx = static_cast<float>(static_cast<int>(rng()) % 2 - 1);
            dy = static_cast<float>(static_cast<int>(rng()) % 2 - 1);
            dz = static_cast<float>(static_cast<int>(rng()) % 2 - 1);
            dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        float scaled = dist * kRepulsionDistScale;       // v34
        float falloff = (radius - scaled) * (1.0f / radius);  // v39 * v35
        if (falloff < 0.0f)
            falloff = 0.0f;
        // negate (push away) and normalize
        float v[3] = { -dx, -dy, -dz };
        float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (len != 0.0f) { v[0] /= len; v[1] /= len; v[2] /= len; }
        double mag = static_cast<double>(falloff) * kRepulsionStrength;
        float tx = self->mesh->pos[0] + static_cast<float>(v[0] * mag);
        float ty = self->mesh->pos[1];                   // engine keeps Y at +80
        float tz = self->mesh->pos[2] + static_cast<float>(v[2] * mag);

        self->targetPackedY = 0.0f;                      // *(+88) = 0
        self->targetX       = static_cast<float>(v[0] * mag);  // *(+84)
        self->targetZ       = static_cast<float>(v[2] * mag);  // *(+92)
        self->flagsA |= kLaRedraw;                        // +140 |= 1

        // validate the destination tile via the heightmap; clear flag if blocked.
        const CharQueryHooks& hk = GetCharQueryHooks();
        float world[3] = { tx, ty, tz };
        int col = 0, row = 0;
        if (self->universe && self->universe->meshHandle) {
            if (hk.worldToTile(self, world, &col, &row)) {
                u8 terr = hk.terrainAt(self, col, row);
                if (terr == 0 || terr == 13)
                    self->flagsA &= static_cast<u8>(~kLaRedraw);
            }
        }
    }
    return hits;
}

// gilde.exe 0x426724 — VIBE_Character_IndexFromPointer.
//   result = (a1 - base) / 0x3D8u;  if (result >= 0x40) return -1;  return result;
int IndexFromUniverse(const Universe* u) {
    if (!u)
        return -1;
    long idx = u - g_universes;
    if (idx < 0 || idx >= kUniverseSlotCount)
        return -1;
    return static_cast<int>(idx);
}

// gilde.exe 0x4266f4 — VIBE_Character_FindFreeSlot.
//   while (universes[i].byte0 || universes[i].byte1) { i += 984; if (i >= 62976) return -1; }
// We model the two lead bytes as (id != 0) — a slot is occupied when used.
int FindFreeSlot() {
    for (int i = 0; i < kUniverseSlotCount; ++i) {
        // byte +0 / +1 both clear == free; we treat a non-zero id as occupied.
        if (g_universes[i].id == 0 && g_universes[i].flags == 0 && !g_universes[i].meshHandle)
            return i;
    }
    return -1;
}

// gilde.exe 0x402314 — VIBE_Character_FindByPredicate (mesh-name match).
LiveActor* FindByPredicate(const char* name,
                           const char* (*actorMeshName)(LiveActor*)) {
    for (int i = 0; i < kLiveCapacity; ++i) {
        LiveActor* a = g_live[i];
        if (!a)
            continue;
        const char* nm = actorMeshName ? actorMeshName(a) : nullptr;
        // VIBE_Util_StrCmpNoCase returns 0 on equal (matches break condition).
        if (nm && name) {
        #if defined(_MSC_VER)
            if (_stricmp(nm, name) == 0)
        #else
            if (strcasecmp(nm, name) == 0)
        #endif
                return a;
        }
    }
    return nullptr;
}

// gilde.exe 0x402360 — VIBE_Character_FindByMesh (mesh-handle match).
LiveActor* FindByMesh(const MeshHandle* mesh) {
    for (int i = 0; i < kLiveCapacity; ++i) {
        LiveActor* a = g_live[i];
        if (a && a->mesh == mesh)
            return a;
    }
    return nullptr;
}

} // namespace guild::sim
