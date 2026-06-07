#include "sim/person_create.h"

#include "sim/entity.h"

#include <cstring>

namespace guild::sim {

i32 g_personNextId = 1; // dword_649890 (seeded; tests reset)

namespace {

// Default backend modelling the observable allocation of
// VIBE_Person_CreateAndSpawn @0x58da70: find a free Person slot (marker word == -1),
// stamp marker(-1 alive)/kind/id/owner-word, return the slot index. The full
// stat/family/avatar/RNG initialisation is the deferred render+RNG leaf.
u16 DefaultPersonCreate(const PersonSpawnArgs& args) {
    // Scan for a free slot. The original walks word_12CE910[268*i] looking for the
    // first slot whose marker word != -1 is FALSE — i.e. the first free (-1) slot
    // is the alive sentinel it writes. Our g_persons free slots have marker == -1
    // already (ResetEntityArrays), so we look for marker == -1 AND id == 0 (unused).
    int idx = -1;
    for (int i = 0; i < kPersonCapacity; ++i) {
        if (g_persons[i].marker == -1 && g_persons[i].id == 0) { idx = i; break; }
    }
    if (idx < 0) return 0xFFFF;

    u8* rec = reinterpret_cast<u8*>(&g_persons[idx]);
    std::memset(rec, 0, kPersonStride);

    // marker word (+0) is the alive indicator: -1 == free, so we stamp a non-(-1)
    // value (the slot index, mirroring the alive records the binary leaves) so
    // PersonFindRecordById will match. kind byte (+2) = args.kind.
    i16 marker = static_cast<i16>(idx);
    std::memcpy(rec + 0, &marker, 2);
    rec[2] = args.kind;

    // id (+4) from the allocator; mirror into the parallel id column.
    i32 id = g_personNextId;
    std::memcpy(rec + 4, &id, 4);
    g_personIds[idx] = id;
    g_personNextId = id + 1;

    // owner index word at +10 (word index 5).
    std::memcpy(rec + 10, &args.ownerWord, 2);

    // spawn-context bytes the originals stash at +356/+357 and gender at +9.
    rec[356] = args.a6;
    rec[357] = args.a7;
    rec[9]   = args.a8;

    return static_cast<u16>(idx);
}

PersonSpawnFn g_hook = &DefaultPersonCreate;

} // namespace

void SetPersonSpawnHook(PersonSpawnFn fn) {
    g_hook = fn ? fn : &DefaultPersonCreate;
}

u16 Person_CreateAndSpawn(const PersonSpawnArgs& args) {
    return g_hook(args);
}

void ResetPersonCreate() {
    g_personNextId = 1;
    g_hook = &DefaultPersonCreate;
}

} // namespace guild::sim
