#include "sim/personnel_recruit2.h"

#include <cstdio>
#include <cstring>

namespace guild::sim {

// ===========================================================================
// 1. Avatar appearance pool
// ===========================================================================
// aNevmmaven @0x62EF4C (248-byte stride, 32 entries) and its used-flag column
// byte_62F042 @0x62F042 (== entry+246). We keep the pool as a single flat byte
// array so the original's `*(float*)((char*)&flt_62EF78 + 248*slot + off)`
// pointer arithmetic ports byte-for-byte. `g_avatarPoolFlags` is a convenience
// mirror of the +246 column (kept in sync so tests can read it directly).

u8 g_avatarPool[kAvatarPoolCount * kAvatarPoolStride];
u8 g_avatarPoolFlags[kAvatarPoolCount];

void ResetAvatarPool() {
    std::memset(g_avatarPool, 0, sizeof(g_avatarPool));
    std::memset(g_avatarPoolFlags, 0, sizeof(g_avatarPoolFlags));
}

namespace {
// Keep the embedded +246 flag byte and the mirror array consistent.
inline void SetFlag(int slot, u8 v) {
    g_avatarPool[static_cast<long>(slot) * kAvatarPoolStride + kAvatarPoolFlagOff] = v;
    g_avatarPoolFlags[slot] = v;
}
inline u8 GetFlag(int slot) {
    return g_avatarPool[static_cast<long>(slot) * kAvatarPoolStride + kAvatarPoolFlagOff];
}
inline u32 GetId(int slot) {
    u32 v;
    std::memcpy(&v, &g_avatarPool[static_cast<long>(slot) * kAvatarPoolStride + kAvatarPoolIdOff], 4);
    return v;
}
} // namespace

// ---------------------------------------------------------------------------
// VIBE_Avatar_AllocSlot  0x484598
//   v1 = (u16)RandomModulo(0x20);  result = 32;
//   loop: v3 = 248*v1; if (!byte_62F042[v3]) break;
//         if (--result == 0) return 0;  v1 = (v1+1) & 0x1F;
//   // found free slot v1:
//   for (v5=0; v5 != 168; v5 += 12)  entry[48+v5] = (float)entry[44+v5] * (1/1000)
//   byte_62F042[248*v1] = 1;  return &aNevmmaven[248*v1];
// (The original reads/writes float columns offset by +44/+48 within the entry; we
//  mirror that exact offsetting. The write region overlaps the next read by design
//  of the original code, so we reproduce it as written — sequentially.)
// ---------------------------------------------------------------------------
u8* Avatar_AllocSlot(int randModulo32) {
    int slot = static_cast<u16>(randModulo32);
    int remaining = kAvatarPoolCount;
    while (true) {
        if (GetFlag(slot) == 0)
            break;
        if (--remaining == 0)
            return nullptr;
        slot = (slot + 1) & 0x1F;
    }
    u8* entry = AvatarPoolEntry(slot);
    // Read column at +44+v5, write scaled value at +48+v5, v5 = 0,12,..,156.
    for (int v5 = 0; v5 != kAvatarPoolFloatSpan; v5 += 12) {
        float src;
        std::memcpy(&src, entry + (kAvatarPoolFloatOff + v5), 4);
        float dst = static_cast<float>(static_cast<double>(src) * kAvatarFloatScale);
        std::memcpy(entry + (kAvatarPoolFloatOff + 4 + v5), &dst, 4);
    }
    SetFlag(slot, 1);
    return entry;
}

// ---------------------------------------------------------------------------
// Avatar stream hook plumbing
// ---------------------------------------------------------------------------
namespace {
AvatarStreamHooks g_defaultAvatarStream;
AvatarStreamHooks* g_avatarStream = &g_defaultAvatarStream;
} // namespace

void SetAvatarStreamHooks(AvatarStreamHooks* h) {
    g_avatarStream = h ? h : &g_defaultAvatarStream;
}
AvatarStreamHooks* AvatarStream() { return g_avatarStream; }

// ---------------------------------------------------------------------------
// VIBE_Avatar_Save  0x484658
//   v4[0] = 32;  if (!handle) return ...; WriteStream(&v4, 4, handle, 1);
//   for i in [0,count): WriteStream(entry.id, 4, ...); WriteStream(entry.flag, 1, ...)
//   return 1 on full success.
// ---------------------------------------------------------------------------
bool Avatar_Save() {
    u32 count = kAvatarPoolCount;
    if (!g_avatarStream->Write(&count, 4, 1))
        return false;
    if (count == 0)
        return true;
    for (u32 i = 0; i < count; ++i) {
        u8* entry = AvatarPoolEntry(static_cast<int>(i));
        if (!g_avatarStream->Write(entry + kAvatarPoolIdOff, 4, 1))
            return false;
        if (!g_avatarStream->Write(entry + kAvatarPoolFlagOff, 1, 1))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// VIBE_Avatar_Load  0x4846ec
//   ReadStream(&count, 4); (count != 32 -> log only)
//   for j: byte_62EF4A[248*j] = -1;   // pre-mark used-flag column "fresh"
//   for i in [0,count): ReadStream(&id, 4); ReadStream(&flag, 1);
//       scan pool for id == entry.id -> entry.flag = flag (matched);
//       (unmatched -> just logged; counter still advances)
//   for j: if (entry.flag == -1) entry.flag = 0;   // default missing to "unused"
//   return 1.
// (byte_62EF4A is the SAME used-flag column as byte_62F042, two bytes apart in the
//  original's labelling of the 248-byte stride; both refer to the per-entry flag.)
// ---------------------------------------------------------------------------
bool Avatar_Load() {
    u32 count = 0;
    if (!g_avatarStream->Read(&count, 4, 1))
        return false;
    // (count inconsistency vs 32 is only a logged warning in the original.)

    // Pre-mark every slot's flag to 0xFF ("fresh / not yet seen").
    for (int j = 0; j < kAvatarPoolCount; ++j)
        SetFlag(j, 0xFF);

    for (u32 i = 0; i < count; ++i) {
        u32 id = 0;
        u8 flag = 0;
        if (!g_avatarStream->Read(&id, 4, 1))
            return false;
        if (!g_avatarStream->Read(&flag, 1, 1))
            return false;
        for (int j = 0; j < kAvatarPoolCount; ++j) {
            if (GetId(j) == id) {
                SetFlag(j, flag);
                break;
            }
        }
        // (no match -> only a log message; the read counter still advances)
    }

    // Any slot never matched (still 0xFF) defaults to 0 ("unused").
    for (int j = 0; j < kAvatarPoolCount; ++j) {
        if (GetFlag(j) == 0xFF)
            SetFlag(j, 0);
    }
    return true;
}

// ===========================================================================
// 2. Animal species spawners
// ===========================================================================
namespace {
SpawnSceneHooks g_defaultSpawnScene;
SpawnSceneHooks* g_spawnScene = &g_defaultSpawnScene;

// Shared body for the door-placed spawners (Dog/Cat) and the bone-chain-placed
// spawners (Sheep/Cow): create the actor, stamp the record + actor fields, queue
// the wander action. `pos` is the placement; `model` the actor model; `kind` the
// kind byte; `scale` the float written to actor+416.
i32 FinishSpawn(AnimalSpawnSlot* slot, const char* model, u8 kind, float scale,
                const float pos[3], float* actorScaleOut) {
    i32 actor = g_spawnScene->CreateFromModel(model);
    if (slot)
        slot->actor = actor;
    if (!actor)
        return actor;
    // *(actor + 416) = scale  (the actor record's scale float)
    if (actorScaleOut)
        *actorScaleOut = scale;
    if (slot) {
        slot->x = pos[0];
        slot->y = pos[1];
        slot->z = pos[2];
        slot->kind = kind;
    }
    // *(actor + 4) = 2 — handled by the actor record owner (renderer); not modelled.
    g_spawnScene->InsertWanderAction(actor);
    return actor;
}
} // namespace

void SetSpawnSceneHooks(SpawnSceneHooks* h) {
    g_spawnScene = h ? h : &g_defaultSpawnScene;
}
SpawnSceneHooks* SpawnScene() { return g_spawnScene; }

// VIBE_Animal_SpawnDog  0x483b58  (model "katze_KATZE", kind 1, scale 1.0)
i32 Animal_SpawnDog(AnimalSpawnSlot* slot, int town, float* actorScaleOut) {
    float pos[3] = {0, 0, 0};
    if (!g_spawnScene->FindDoorTarget(town, pos))
        return 0;
    return FinishSpawn(slot, "katze_KATZE", 1, kAnimalScaleSmall, pos, actorScaleOut);
}

// VIBE_Animal_SpawnCat  0x483bc4  (model "hund_HUND", kind 0, scale 1.0)
i32 Animal_SpawnCat(AnimalSpawnSlot* slot, int town, float* actorScaleOut) {
    float pos[3] = {0, 0, 0};
    if (!g_spawnScene->FindDoorTarget(town, pos))
        return 0;
    return FinishSpawn(slot, "hund_HUND", 0, kAnimalScaleSmall, pos, actorScaleOut);
}

// VIBE_Animal_SpawnSheep  0x483c30  (model "schaf_SCHAF", kind 4, scale 1.5)
i32 Animal_SpawnSheep(AnimalSpawnSlot* slot, int town, float* actorScaleOut) {
    float pos[3] = {0, 0, 0};
    if (!g_spawnScene->PickSpawnPlacement("dummy_SHEEP", town, pos))
        return 0;
    return FinishSpawn(slot, "schaf_SCHAF", 4, kAnimalScaleLarge, pos, actorScaleOut);
}

// VIBE_Animal_SpawnCow  0x483ca8  (model "kuh_KUH", kind 3, scale 1.5)
i32 Animal_SpawnCow(AnimalSpawnSlot* slot, int town, float* actorScaleOut) {
    float pos[3] = {0, 0, 0};
    if (!g_spawnScene->PickSpawnPlacement("dummy_COW", town, pos))
        return 0;
    return FinishSpawn(slot, "kuh_KUH", 3, kAnimalScaleLarge, pos, actorScaleOut);
}

// VIBE_Animal_SpawnLivestock  0x483d20
//   sprintf(model, "dummy_%s", &byte_62EE44[32*kind]);
//   PickSpawnPlacement(model, town); if fail return 0;
//   actorModel = switch(kind){ 3:"kuh_KUH", 7:"pferd_PFERD", 6:"schwein_SCHWEIN",
//                              default: model };
//   CreateFromModel(actorModel); stamp scale 1.0, pos, kind; queue action.
i32 Animal_SpawnLivestock(AnimalSpawnSlot* slot, int town, u8 kind,
                          const char* kindNames, float* actorScaleOut) {
    char model[128];
    const char* name = kindNames ? (kindNames + 32 * static_cast<int>(kind)) : "";
    // VIBE_Crt_Sprintf_0(model, "dummy_%s", name)
    std::snprintf(model, sizeof(model), "dummy_%s", name);

    float pos[3] = {0, 0, 0};
    if (!g_spawnScene->PickSpawnPlacement(model, town, pos))
        return 0;

    const char* actorModel;
    switch (kind) {
        case 3: actorModel = "kuh_KUH"; break;
        case 7: actorModel = "pferd_PFERD"; break;
        case 6: actorModel = "schwein_SCHWEIN"; break;
        default: actorModel = model; break;
    }
    return FinishSpawn(slot, actorModel, kind, kAnimalScaleSmall, pos, actorScaleOut);
}

}  // namespace guild::sim
