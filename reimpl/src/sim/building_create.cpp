#include "sim/building_create.h"

#include "sim/entity.h"

#include <cstring>

namespace guild::sim {

i32 g_buildingNextId = 1; // dword_649890 (seeded; tests reset)

namespace {

// gilde.exe 0x586d44 — VIBE_Building_FindFreeSlot. Linear scan of the shared
// object/building array for a dead slot (alive byte == 0). The original returns a
// raw record pointer; here we return the g_objects slot base or nullptr.
u8* FindFreeBuildingSlot() {
    for (int i = 0; i < kObjectCapacity; ++i) {
        if (g_objects[i].alive == 0)
            return reinterpret_cast<u8*>(&g_objects[i]);
    }
    return nullptr;
}

// Default backend modelling the observable record initialisation of
// VIBE_Building_CreateGebaeude @0x586fb8. We reproduce the scalar field writes the
// originals perform on the new record (alive/type, id, owner words, the stock/price
// defaults) — the parts the apply handlers and tests observe. The geometry / scene
// / name-table machinery is the deferred render leaf.
u8* DefaultCreateGebaeude(u8 typeByte, u16 ownerWord) {
    u8* rec = FindFreeBuildingSlot();
    if (!rec) return nullptr;

    std::memset(rec, 0, kObjectStride);
    rec[0] = typeByte;                               // *v3 = type (alive/type byte)

    i32 id = g_buildingNextId;                       // dword_649890
    std::memcpy(rec + 1, &id, 4);                    // *(v3+1) = id
    g_buildingNextId = id + 1;                       // dword_649890 = v8 + 1

    // Default scalar fields from the original (the ones with fixed constants):
    //   *(v3+57)=32000  *(v3+61)=100  *(v3+65)=2  *(v3+69)=100
    //   *(v3+73)=1.0f(0x3F800000)  v3[92]=100  *(v3+149)=-1
    i32 v;
    v = 32000; std::memcpy(rec + 57, &v, 4);
    v = 100;   std::memcpy(rec + 61, &v, 4);
    v = 2;     std::memcpy(rec + 65, &v, 4);
    v = 100;   std::memcpy(rec + 69, &v, 4);
    v = 0x3F800000; std::memcpy(rec + 73, &v, 4);    // float 1.0
    rec[92] = 100;
    v = -1;    std::memcpy(rec + 149, &v, 4);

    // Owner words: *(v3+37) = *(v3+39) = ownerWord.
    std::memcpy(rec + 37, &ownerWord, 2);
    std::memcpy(rec + 39, &ownerWord, 2);

    return rec;
}

CreateGebaeudeFn g_hook = &DefaultCreateGebaeude;

} // namespace

void SetCreateGebaeudeHook(CreateGebaeudeFn fn) {
    g_hook = fn ? fn : &DefaultCreateGebaeude;
}

u8* Building_CreateGebaeude(u8 typeByte, u16 ownerWord) {
    return g_hook(typeByte, ownerWord);
}

void ResetBuildingCreate() {
    g_buildingNextId = 1;
    g_hook = &DefaultCreateGebaeude;
}

} // namespace guild::sim
