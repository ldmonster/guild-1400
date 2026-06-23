// Golden tests for the Character avatar-slot bookkeeping cluster.
// gilde.exe 0x505074 / 0x505134 / 0x505870 / 0x43cb20 / 0x43d9f0.
#include "test.h"
#include "sim/character_recon4_avatar.h"
#include "sim/avatar.h"   // g_avatars / Avatar_LookupById / Avatar_FindOrAllocForPerson

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// Deterministic free-slot allocator: hands out 0,1,2,... then -1.
struct Allocator {
    int next = 0;
    int max = 4;
};
int AllocSlot(void* ctx) {
    auto* a = static_cast<Allocator*>(ctx);
    if (a->next >= a->max) return -1;
    return a->next++;
}

void SeedObjectName(AvatarSlotState& st, int type, const char* name) {
    // Name source row begins at +1 (the engine reads dword_13CE27C + 65*type + 1).
    std::memcpy(&st.objectNames[kObjectNameStride * type + 1], name, std::strlen(name) + 1);
}
void SeedBuildingName(AvatarSlotState& st, int type, const char* name) {
    std::memcpy(&st.buildingNames[kBuildingNameStride * type + 1], name, std::strlen(name) + 1);
}

} // namespace

// Already-assigned slot is returned without allocating.
TEST(CharacterRecon4, ObjectAvatar_already_assigned_returns_cached) {
    AvatarSlotState st;
    AvatarHooks H;
    Allocator alloc;
    H.ctx = &alloc;
    H.findFreeSlot = AllocSlot;

    // Pre-set type 7's slot to 5 in the table high-byte cell.
    st.objectTable[7 + kObjectSlotHiByte] = 5;
    CHECK_EQ(EnsureObjectAvatar(st, H, 7), 5);
    CHECK_EQ(alloc.next, 0);  // no allocation happened
}

// First allocation: slot 0, live flag set, name copied.
TEST(CharacterRecon4, ObjectAvatar_allocates_marks_live_and_copies_name) {
    AvatarSlotState st;
    AvatarHooks H;
    Allocator alloc;
    H.ctx = &alloc;
    H.findFreeSlot = AllocSlot;
    SeedObjectName(st, 3, "well");

    int slot = EnsureObjectAvatar(st, H, 3);
    CHECK_EQ(slot, 0);
    CHECK_EQ((int)st.objectTable[3 + kObjectSlotHiByte], 0);
    CHECK_EQ((int)st.avatarLive[kAvatarRecordStride * 0], 1);
    CHECK(std::strcmp((const char*)&st.avatarRec[kAvatarRecordStride * 0], "well") == 0);
    // Second call returns the cached slot, no further allocation.
    CHECK_EQ(EnsureObjectAvatar(st, H, 3), 0);
    CHECK_EQ(alloc.next, 1);
}

// Allocator exhausted -> returns -1, nothing marked.
TEST(CharacterRecon4, ObjectAvatar_no_free_slot_returns_minus1) {
    AvatarSlotState st;
    AvatarHooks H;
    Allocator alloc;
    alloc.next = 4; alloc.max = 4;  // full
    H.ctx = &alloc;
    H.findFreeSlot = AllocSlot;

    CHECK_EQ(EnsureObjectAvatar(st, H, 9), -1);
    CHECK_EQ((int)st.objectTable[9 + kObjectSlotHiByte], 0xFF);  // still default -1
}

// Special object type forces a scene load and returns whatever the table holds.
TEST(CharacterRecon4, ObjectAvatar_special_type_loads_scene) {
    AvatarSlotState st;
    AvatarHooks H;
    Allocator alloc;
    H.ctx = &alloc;
    H.findFreeSlot = AllocSlot;
    H.specialObjectType = 12;
    static int loaded = -1;
    H.loadObjektScene = [](void*, int type) { loaded = type; };

    // The scene loader (in the real engine) populates the table; emulate by
    // returning the still-default -1 here.  We assert the loader fired.
    int r = EnsureObjectAvatar(st, H, 12);
    CHECK_EQ(loaded, 12);
    CHECK_EQ(r, -1);          // table not populated by our inert loader
    CHECK_EQ(alloc.next, 0);  // no slot allocation on the special path
}

// Building avatar: name stride 589, slot hi-byte at +3.
TEST(CharacterRecon4, BuildingAvatar_allocates_and_copies_name) {
    AvatarSlotState st;
    AvatarHooks H;
    Allocator alloc;
    H.ctx = &alloc;
    H.findFreeSlot = AllocSlot;
    SeedBuildingName(st, 2, "smithy");

    int slot = EnsureBuildingAvatar(st, H, 2, /*a2*/ 0, /*a3*/ 0);
    CHECK_EQ(slot, 0);
    CHECK_EQ((int)st.buildingTable[2 + kBuildingSlotHiByte], 0);
    CHECK_EQ((int)st.avatarLive[kAvatarRecordStride * 0], 1);
    CHECK(std::strcmp((const char*)&st.avatarRec[kAvatarRecordStride * 0], "smithy") == 0);
}

// Building type 30 (0x1E) is a special scene-load type.
TEST(CharacterRecon4, BuildingAvatar_type30_loads_scene) {
    AvatarSlotState st;
    AvatarHooks H;
    Allocator alloc;
    H.ctx = &alloc;
    H.findFreeSlot = AllocSlot;
    static int loadedType = -1, loadedA2 = -1;
    H.loadGebaeudeScene = [](void*, int t, int a2, i16) { loadedType = t; loadedA2 = a2; };

    int r = EnsureBuildingAvatar(st, H, 30, /*a2*/ 77, /*a3*/ 0);
    CHECK_EQ(loadedType, 30);
    CHECK_EQ(loadedA2, 77);
    CHECK_EQ(r, -1);
    CHECK_EQ(alloc.next, 0);
}

// Name copy loop: a string with an odd length round-trips exactly.
TEST(CharacterRecon4, AvatarName_odd_length_copy_is_exact) {
    AvatarSlotState st;
    AvatarHooks H;
    Allocator alloc;
    H.ctx = &alloc;
    H.findFreeSlot = AllocSlot;
    SeedObjectName(st, 1, "abcde");  // 5 chars + NUL

    EnsureObjectAvatar(st, H, 1);
    CHECK(std::strcmp((const char*)&st.avatarRec[0], "abcde") == 0);
}

// Empty name (immediate NUL) copies just the terminator.
TEST(CharacterRecon4, AvatarName_empty_copies_terminator) {
    AvatarSlotState st;
    AvatarHooks H;
    Allocator alloc;
    H.ctx = &alloc;
    H.findFreeSlot = AllocSlot;
    SeedObjectName(st, 1, "");

    EnsureObjectAvatar(st, H, 1);
    CHECK_EQ((int)st.avatarRec[0], 0);
}

// EnsureGateAvatars: empty person query -> -1; populated -> forwards to building.
TEST(CharacterRecon4, GateAvatars_empty_query_returns_minus1) {
    AvatarSlotState st;
    AvatarHooks H;
    H.personQueryBegin = [](void*, void*, int, int, int) -> void* { return nullptr; };
    CHECK_EQ(EnsureGateAvatars(st, H, 0, 1), -1);
}

TEST(CharacterRecon4, GateAvatars_forwards_to_building_avatar) {
    AvatarSlotState st;
    AvatarHooks H;
    Allocator alloc;
    H.ctx = &alloc;
    H.findFreeSlot = AllocSlot;
    static i8 rec;
    rec = 4;  // building type 4
    H.personQueryBegin = [](void*, void*, int, int, int) -> void* { return &rec; };
    SeedBuildingName(st, 4, "gate");

    int slot = EnsureGateAvatars(st, H, 0, 1);
    CHECK_EQ(slot, 0);
    CHECK_EQ((int)st.buildingTable[4 + kBuildingSlotHiByte], 0);
}

// DestroyThunk dereferences and forwards.
TEST(CharacterRecon4, DestroyThunk_forwards_dereferenced_handle) {
    AvatarHooks H;
    static int seen = 0;
    H.characterDestroy = [](int h) { seen = h; return h + 1; };
    int handle = 321;
    CHECK_EQ(DestroyThunk(H, &handle), 322);
    CHECK_EQ(seen, 321);
}

// CmdPreloadSitMeshStub is a compiled-out handler.
TEST(CharacterRecon4, CmdPreloadSitMeshStub_returns_zero) {
    CHECK_EQ(CmdPreloadSitMeshStub(), 0);
}

// --- HARDENING: boundary type indices stay inside the reconstructed tables ----
// Table extents recovered from VIBE_Object_DestroySpawnedEntities @0x4fff10
// (disasm-verified): object slot/name table has 731 entries (`cmp ecx,2DBh`),
// building slot/name table has 72 entries (`cmp ecx,48h`).  The functions
// themselves bounds-check nothing; the bound is implicit in the table sizing.
// These tests pin the *highest valid* type for each table landing inside the
// reconstructed arrays (ASAN would trip if undersized).
//
// EnsureObjectAvatar indexes objectTable[type+4] and objectNames[65*type+1]; the
// max valid object type is kObjectTypeCount-1 == 730.
TEST(CharacterRecon4, ObjectAvatar_max_type_index_in_bounds) {
    AvatarSlotState st;
    AvatarHooks H;
    Allocator alloc;
    H.ctx = &alloc;
    H.findFreeSlot = AllocSlot;
    const int maxType = kObjectTypeCount - 1;  // 730 (0x2DA)
    SeedObjectName(st, maxType, "edge");

    int slot = EnsureObjectAvatar(st, H, (i16)maxType);
    CHECK_EQ(slot, 0);
    CHECK_EQ((int)st.objectTable[maxType + kObjectSlotHiByte], 0);
    CHECK(std::strcmp((const char*)&st.avatarRec[0], "edge") == 0);
}

// EnsureBuildingAvatar indexes buildingTable[type+3] and buildingNames[589*type+1];
// the max valid building type is kBuildingTypeCount-1 == 71 (0x47).
TEST(CharacterRecon4, BuildingAvatar_max_type_index_in_bounds) {
    AvatarSlotState st;
    AvatarHooks H;
    Allocator alloc;
    H.ctx = &alloc;
    H.findFreeSlot = AllocSlot;
    const int maxBld = kBuildingTypeCount - 1;  // 71 (0x47)
    SeedBuildingName(st, maxBld, "wall");

    int slot = EnsureBuildingAvatar(st, H, (i8)maxBld, /*a2*/ 0, /*a3*/ 0);
    CHECK_EQ(slot, 0);
    CHECK_EQ((int)st.buildingTable[maxBld + kBuildingSlotHiByte], 0);
    CHECK(std::strcmp((const char*)&st.avatarRec[0], "wall") == 0);
}

// Pin the recovered extents themselves so a future regression in sizing trips here.
TEST(CharacterRecon4, TableExtents_match_binary) {
    CHECK_EQ(kObjectTypeCount, 731);    // VIBE_Object_DestroySpawnedEntities cmp ecx,2DBh
    CHECK_EQ(kBuildingTypeCount, 72);   //                                    cmp ecx,48h
    CHECK_EQ(AvatarSlotState::kSlotCount, 64);  // FindFreeSlot 62976/984
    CHECK_EQ(kObjectNameStride, 65);
    CHECK_EQ(kBuildingNameStride, 589);
}

// Avatar registry lookups: full-table and empty-owned-list boundaries.
//   Avatar_LookupById scans all kAvatarCapacity entries; a miss returns null
//   without reading past the table. FindOrAllocForPerson with count==0 must skip
//   the owned-id scan entirely (no read of a null/empty list) and return the first
//   free entry; with a full table it must return null, not walk off the end.
TEST(CharacterRecon4, AvatarRegistry_lookup_and_alloc_boundaries) {
    ResetAvatars();
    // miss on an empty table -> null (full scan, no OOB).
    CHECK(Avatar_LookupById(999) == nullptr);
    // empty owned list -> first free entry (index 0).
    CHECK(Avatar_FindOrAllocForPerson(nullptr, 0) == &g_avatars[0]);

    // Fill every entry; FindOrAllocForPerson must now return null (table full),
    // and the owned-id scan over a 1-element list must not over-read.
    for (int i = 0; i < kAvatarCapacity; ++i) g_avatars[i].ownerId = (u16)(i + 1);
    u16 owned[1] = { 0xBEEF };  // not present -> no match
    CHECK(Avatar_FindOrAllocForPerson(owned, 1) == nullptr);
    // an owned id that IS present resolves to its entry.
    CHECK(Avatar_LookupById(3) == &g_avatars[2]);
    ResetAvatars();
}
