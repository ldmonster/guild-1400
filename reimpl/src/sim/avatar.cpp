#include "sim/avatar.h"

#include <cstring>

namespace guild::sim {

// Faithful 1:1 port of the avatar-registry lookups from gilde.exe. The table is
// scanned linearly by the owning scene-entity id packed in each entry's high
// word (dword_630E4A[i] >> 16).

AvatarEntry g_avatars[kAvatarCapacity];

void ResetAvatars() {
    std::memset(g_avatars, 0, sizeof(g_avatars));
}

// ===========================================================================
// VIBE_Avatar_LookupById  0x4859b0
//   v2 = 0;
//   while (dword_630E4A[v2] >> 16 != id) { v2 += 23; if (v2 >= 230) return 0; }
//   return &dword_630E4A[v2] + 2;
// v2 steps by 23 dwords (== one 92-byte entry); 230 == 23*10 is the scan bound.
// ===========================================================================
AvatarEntry* Avatar_LookupById(u16 id) {
    for (int i = 0; i < kAvatarCapacity; ++i) {
        if (g_avatars[i].ownerId == id)
            return &g_avatars[i];
    }
    return nullptr;
}

// ===========================================================================
// VIBE_Avatar_FindOrAllocForPerson  0x4859e0
//   for each scene-entity the person owns (QueryFind(person+376, type 5)):
//       if (LookupById(entity.id)) return it;     // first matching avatar
//   for (j = 0; j < 230; j += 23):
//       if (!(dword_630E4A[j] >> 16)) return &dword_630E4A[j] + 2;  // free entry
//   return 0;
// (The inner LookupById uses the SAME scan as Avatar_LookupById; here the owned
// entity ids are passed in as the resolved QueryFind result.)
// ===========================================================================
AvatarEntry* Avatar_FindOrAllocForPerson(const u16* ownedEntityIds, int count) {
    for (int k = 0; k < count; ++k) {
        // The original reads *i (the scene node's type word at +0) as the id key;
        // we pass the resolved owner-id list directly.
        AvatarEntry* hit = Avatar_LookupById(ownedEntityIds[k]);
        if (hit)
            return hit;
    }
    // No matching avatar: return the first free entry (owner high word == 0).
    for (int j = 0; j < kAvatarCapacity; ++j) {
        if (g_avatars[j].ownerId == 0)
            return &g_avatars[j];
    }
    return nullptr;
}

}  // namespace guild::sim
