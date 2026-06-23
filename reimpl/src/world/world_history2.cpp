// History-event ("He") engine tail — see world_history2.h for the slice notes.
// 1:1 translation of the still-untranslated deterministic VIBE_He_* functions
// the History/Statistics/Stammbaum reconstructions wire into. All cross-module
// leaves route through WorldHistory2Hooks (inert defaults below) so the module
// runs with no live sim/render/universe, matching the CourtCouncilHooks pattern.
#include "world/world_history2.h"

#include <cstdio>   // std::snprintf (VIBE_Crt_Sprintf_0 clone for the diagnostics)

namespace guild::world {

// ---------------------------------------------------------------------------
// Inert hook defaults. Defined here in the library so nothing in src/ references
// an undefined symbol in the unified build.
// ---------------------------------------------------------------------------
namespace {

void   InertLog(const char*) {}
void   InertNews(u16) {}
int    InertSwitchSlot(int, int, int, int) { return 0; }
int    InertFindByHandle(int) { return 1; }   // treat handle as live
void   InertDetach(int) {}
void   InertArrange(int) {}
int    InertCreateMesh(HeIconSlot*, int, int) { return 0; }
int    InertIconsEnabled() { return 1; }       // icons enabled
int    InertParentId(int parent) { return parent; }
void   InertStoreSlot(int, HeIconSlot*) {}

const WorldHistory2Hooks kInertHooks = {
    InertLog, InertNews, InertSwitchSlot, InertFindByHandle,
    InertDetach, InertArrange, InertCreateMesh, InertIconsEnabled,
    InertParentId, InertStoreSlot,
};

// The live hook table. Starts inert; SetWorldHistory2Hooks copies a caller's
// table in, substituting the inert default for any null member so a partially
// populated hooks struct never dereferences a null pointer.
WorldHistory2Hooks g_hooks = kInertHooks;

// The recovered 64-slot icon pool (gilde.exe dword_11C6160, 16-byte stride).
HeIconSlot g_iconPool[kHeIconSlotCount];

} // namespace

void SetWorldHistory2Hooks(const WorldHistory2Hooks* hooks) {
    if (!hooks) { g_hooks = kInertHooks; return; }
    WorldHistory2Hooks h = *hooks;
    if (!h.logMessage)              h.logMessage = kInertHooks.logMessage;
    if (!h.processPlayerNews)       h.processPlayerNews = kInertHooks.processPlayerNews;
    if (!h.universeSwitchActiveSlot) h.universeSwitchActiveSlot = kInertHooks.universeSwitchActiveSlot;
    if (!h.objectFindByHandle)      h.objectFindByHandle = kInertHooks.objectFindByHandle;
    if (!h.objectDetachAndRelease)  h.objectDetachAndRelease = kInertHooks.objectDetachAndRelease;
    if (!h.arrangeIconsInCircle)    h.arrangeIconsInCircle = kInertHooks.arrangeIconsInCircle;
    if (!h.createIconMesh)          h.createIconMesh = kInertHooks.createIconMesh;
    if (!h.iconsEnabled)            h.iconsEnabled = kInertHooks.iconsEnabled;
    if (!h.parentEntityId)          h.parentEntityId = kInertHooks.parentEntityId;
    if (!h.storeSlotInParent)       h.storeSlotInParent = kInertHooks.storeSlotInParent;
    g_hooks = h;
}
const WorldHistory2Hooks& GetWorldHistory2Hooks() { return g_hooks; }

HeIconSlot* HeIconPool() { return g_iconPool; }

void HeIconPoolReset() {
    for (int i = 0; i < kHeIconSlotCount; ++i) {
        g_iconPool[i].entityId = 0;
        g_iconPool[i].parent   = 0;
        g_iconPool[i].mesh     = 0;
        g_iconPool[i].node     = 0;
    }
}

// ===========================================================================
// gilde.exe 0x4c45e4 — VIBE_He_ValidatePunishmentType
//   if (!rec)          return -1;
//   if (type >= 10)    return -3;
//   if (type == 5)     return *(rec+358) ? 0 : -4;   // needs the +358 class byte
//   if (type == 7 && !*(rec+13)) return -4;          // needs the +13 flag
//   return 0;
// ===========================================================================
int He_ValidatePunishmentType(const void* rec, u32 type) {
    if (!rec)                                   // if ( !a1 ) return -1;
        return -1;
    if (type >= 0xA)                            // if ( a2 >= 0xA ) return -3;
        return -3;
    const u8* p = static_cast<const u8*>(rec);
    if (type == 5) {                            // if ( a2 == 5 )
        if (!p[358])                            //   if ( !*((_BYTE*)a1 + 358) ) return -4;
            return -4;
        return 0;                               //   return 0;
    }
    if (type != 7 || p[13])                     // if ( a2 != 7 || *(a1+13) ) return 0;
        return 0;
    return -4;                                  // return -4;
}

// ===========================================================================
// gilde.exe 0x4c5244 — VIBE_He_NullHandler. (empty body)
// ===========================================================================
void He_NullHandler() {}

// ===========================================================================
// gilde.exe 0x4c51b4 — VIBE_He_LogInvalidAllocType
//   sprintf(buf, "he_AllocNone(): Invalid HE-Type : %i, von Spieler %s",
//           rec[0], playerNameFor(rec[+8]));
// The player name (the original indexes word_12CE910[268*rec[+8]+24]) is supplied
// by the caller as `playerName`.
// ===========================================================================
void He_LogInvalidAllocType(const u8* rec, const char* playerName) {
    char buf[260];
    std::snprintf(buf, sizeof(buf),
                  "he_AllocNone(): Invalid HE-Type : %i, von Spieler %s",
                  rec ? static_cast<int>(rec[0]) : 0,
                  playerName ? playerName : "");
    g_hooks.logMessage(buf);
}

// ===========================================================================
// gilde.exe 0x4c51fc — VIBE_He_LogInvalidRunType
//   sprintf(buf, "he_RunNone(): Invalid HE-Type : %i, von Spieler %s", ...);
//   VIBE_He_NullHandler();
// ===========================================================================
void He_LogInvalidRunType(const u8* rec, const char* playerName) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "he_RunNone(): Invalid HE-Type : %i, von Spieler %s",
                  rec ? static_cast<int>(rec[0]) : 0,
                  playerName ? playerName : "");
    g_hooks.logMessage(buf);
    He_NullHandler();                           // tail call
}

// ===========================================================================
// gilde.exe 0x4c5018 — VIBE_He_ProcessAllPlayerNews
//   edx = 0;
//   do { ax = dx; edx++; ProcessPlayerNews(ax); } while (edx < 0x300);
// (768 iterations, slot indices 0..767 passed as the u16 arg.)
// ===========================================================================
void He_ProcessAllPlayerNews() {
    int edx = 0;
    do {
        u16 slot = static_cast<u16>(edx);       // ax = dx
        ++edx;                                   // inc edx
        g_hooks.processPlayerNews(slot);        // VIBE_He_ProcessPlayerNews(ax)
    } while (edx < 0x300);                        // cmp edx, 300h ; jl
}

// ===========================================================================
// gilde.exe 0x4c68d8 — VIBE_He_DestroyIconGfx
//   v4 = slot[2];                       // mesh field
//   if (slot[3]) {                      // node present
//       slot[2] = 0;
//       v5 = activeSlot;
//       SwitchActiveSlot(0, 1, .., v4);
//       if (v4 && FindByHandle(.., v4)) ArrangeIconsInCircle(v4);
//       DetachAndRelease(slot[3]);
//       slot[3] = 0;
//       SwitchActiveSlot(v5, ..);
//       slot[1] = 0;
//       slot[0] = -1;
//   } else {
//       slot[1] = 0;
//       slot[0] = -1;
//   }
// ===========================================================================
void He_DestroyIconGfx(HeIconSlot* slot) {
    if (!slot)
        return;
    if (slot->node) {                                  // if ( result[3] )
        int mesh = slot->mesh;                         // v4 = result[2]
        slot->mesh = 0;                                // result[2] = 0
        int prevSlot = g_hooks.universeSwitchActiveSlot(0, 1, 0, mesh);
        if (mesh && g_hooks.objectFindByHandle(mesh))  // v4 && FindByHandle(.., v4)
            g_hooks.arrangeIconsInCircle(mesh);        // ArrangeIconsInCircle(v4)
        g_hooks.objectDetachAndRelease(slot->node);    // DetachAndRelease(v3[3])
        slot->node = 0;                                 // v3[3] = 0
        g_hooks.universeSwitchActiveSlot(prevSlot, 1, 0, mesh);
        slot->parent   = 0;                             // v3[1] = 0
        slot->entityId = -1;                            // *v3 = -1
    } else {
        slot->parent   = 0;                             // result[1] = 0
        slot->entityId = -1;                            // *result = -1
    }
}

// ===========================================================================
// gilde.exe 0x4c6c50 — VIBE_He_DestroyStaleIcons
//   for (i = 0; i < 64; ) {
//     v3 = slot[i].parent;                 // dword_11C6164[4*i]
//     if (!v3) { ++i; continue; }
//     if ( *(v3+4) != slot[i].entityId || !iconsEnabled() )
//        DestroyIconGfx(&slot[i]);          // and re-test the same i
//     else
//        ++i;
//   }
// The original `while(1)` re-reads the same slot after destroying it; since
// DestroyIconGfx clears parent to 0, the re-test takes the !v3 branch and
// advances. We make the advance explicit (provably identical).
// ===========================================================================
void He_DestroyStaleIcons() {
    for (int i = 0; i < kHeIconSlotCount; ++i) {
        HeIconSlot& s = g_iconPool[i];
        if (!s.parent)                                  // if ( v3 )
            continue;
        int parentId = g_hooks.parentEntityId(s.parent);   // *(v3+4)
        if (parentId != s.entityId || !g_hooks.iconsEnabled())
            He_DestroyIconGfx(&s);                       // clears parent -> next i advances
    }
}

// ===========================================================================
// gilde.exe 0x4c6ca4 — VIBE_He_DestroyIconsForEntity
//   for (i = 0; i != 1024; i += 16)
//     if ( parent == slot[i/16].mesh )      // *(dword_11C6168 + i) == field +8
//        DestroyIconGfx(&slot[i/16]);
// Matches on the +8 field. Binary-confirmed (W16) semantics: CreateIconMesh @0x4c662c
// stores `*(a1+8) = a2` where a2 is the parent passed down from CreateGfxInfo, so the
// struct field named `mesh` actually holds the OWNING PARENT pointer; field +12 (node)
// holds the universe-node handle from AttachToUniverseNode. The match here against the
// `parent` argument is therefore correct (name is a historical misnomer).
// ===========================================================================
void He_DestroyIconsForEntity(int parent) {
    for (int i = 0; i < kHeIconSlotCount; ++i) {
        if (parent == g_iconPool[i].mesh)               // *(dword_11C6168 + i)
            He_DestroyIconGfx(&g_iconPool[i]);
    }
}

// ===========================================================================
// gilde.exe 0x4c67e0 — VIBE_He_CreateGfxInfo
//   if (!iconsEnabled()) return 0;
//   v4 = first slot index with slot[v4].parent == 0 (scan up to 64);
//   if (v4 == 64) return 0;                 // pool full
//   SwitchActiveSlot(0, 1, 0, parentEntity);
//   if (FindByHandle(.., parentName, parentEntity)) {
//       slot.parent   = parentEntity;       // v5[1] = v3
//       slot.entityId = *(parentEntity+4);  // *v5 = *(v3+4)
//       slot.mesh     = 0;                  // v5[2] = 0
//       storeSlotInParent(parentEntity, &slot);   // *(v3+136) = v5
//       CreateIconMesh(&slot, gfxArg, parentName);
//   } else {
//       log("he_CreateGfxInfo(): invalid parent :%s", parentName);
//   }
//   SwitchActiveSlot(prev, ..);
//   return success;
// ===========================================================================
int He_CreateGfxInfo(int parentEntity, int gfxArg, const char* parentName) {
    if (!g_hooks.iconsEnabled())                       // if ( byte_123356B )  (else fall through)
        return 0;

    // Scan for the first free slot (parent field == 0). The original walks the
    // 64-slot pool by 16-byte stride; v4 == 64 means full.
    int v4 = 0;
    if (g_iconPool[0].parent) {                          // if ( dword_11C6164[0] )
        do {
            ++v4;                                         // a1 += 16; ++v4
        } while (v4 < kHeIconSlotCount && g_iconPool[v4].parent);
    }
    if (v4 == kHeIconSlotCount)                           // pool full
        return 0;

    HeIconSlot& slot = g_iconPool[v4];
    int prevSlot = g_hooks.universeSwitchActiveSlot(0, 1, 0, parentEntity);
    int ok = 0;
    if (g_hooks.objectFindByHandle(parentEntity)) {      // FindByHandle(.., parentName, parentEntity)
        slot.parent   = parentEntity;                     // v5[1] = v3
        slot.mesh     = 0;                                // v5[2] = 0
        slot.entityId = g_hooks.parentEntityId(parentEntity); // *v5 = *(v3+4)
        g_hooks.storeSlotInParent(parentEntity, &slot);  // *(v3+136) = v5
        // CreateIconMesh mutates the slot in place (it stores the attached node
        // at +12 and the parent float ptr at +8); its return is ignored by the
        // original CreateGfxInfo.
        g_hooks.createIconMesh(&slot, gfxArg, parentEntity);
        ok = 1;
    } else {
        char buf[260];
        std::snprintf(buf, sizeof(buf),
                      "he_CreateGfxInfo(): invalid parent :%s",
                      parentName ? parentName : "");
        g_hooks.logMessage(buf);
    }
    g_hooks.universeSwitchActiveSlot(prevSlot, 1, 0, parentEntity);
    return ok;
}

} // namespace guild::world
