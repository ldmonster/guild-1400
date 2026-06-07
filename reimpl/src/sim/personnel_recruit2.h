#pragma once
// gilde.exe — guild::sim  (MODULE slice: personnel_recruit2)
//
// Two self-contained, deterministic leaves recovered from the Personnel / Recruit
// / Animal / Avatar family that were NOT yet translated by the sibling modules
// (animal.cpp, animal_wander.cpp, avatar.cpp, recruit*.cpp, personnel.cpp,
// person_personnel2.cpp):
//
//   1. The AVATAR APPEARANCE POOL (a SEPARATE 248-byte/32-slot table at
//      `aNevmmaven` @0x62EF4C with a used-flag column `byte_62F042` @0x62F042;
//      distinct from the avatar registry dword_630E4A handled by avatar.cpp):
//        VIBE_Avatar_AllocSlot  0x484598  — RNG-pick a free slot, scale its float
//                                           appearance columns by 1/1000.
//        VIBE_Avatar_Save       0x484658  — serialise count header + per-slot
//                                           id(4) + used-flag(1) to a stream.
//        VIBE_Avatar_Load       0x4846ec  — read count, validate, match each saved
//                                           id, default missing slots to "unused".
//
//   2. The ANIMAL SPECIES SPAWNERS (record/kind-byte stampers; the scene actor
//      creation + bone-transform are routed through a hook):
//        VIBE_Animal_SpawnDog        0x483b58  (model "katze_KATZE", kind 1)
//        VIBE_Animal_SpawnCat        0x483bc4  (model "hund_HUND",   kind 0)
//        VIBE_Animal_SpawnSheep      0x483c30  (model "schaf_SCHAF", kind 4)
//        VIBE_Animal_SpawnCow        0x483ca8  (model "kuh_KUH",     kind 3)
//        VIBE_Animal_SpawnLivestock  0x483d20  (kind-byte-driven model select)
//   (The IDA names Dog/Cat are crossed vs their model strings in the binary; we
//    port byte values + model strings VERBATIM and keep the original names.)
//
// Unreconstructed callees (Character_CreateFromModel, InsertActionVararg, the
// FindDoorTarget/PickSpawnBuilding placement, the bone-chain transform) are routed
// through SpawnSceneHooks with inert defaults defined in the .cpp.
#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// 1. Avatar appearance pool (aNevmmaven @0x62EF4C / byte_62F042 @0x62F042)
// ===========================================================================
// The pool is 32 entries of 248 bytes each. The fields the recovered functions
// touch within one 248-byte entry:
//   +0x00  4-byte id key (dword; compared on Load; written on Save)
//   +0x2C (44)  start of the float "appearance" columns (14 columns, stride 12)
//   +0xF6 (246) used-flag byte (1 == allocated, 0 == free, -1 == "fresh" sentinel)
constexpr int kAvatarPoolStride   = 248;
constexpr int kAvatarPoolCount    = 32;
constexpr int kAvatarPoolIdOff    = 0x00;
constexpr int kAvatarPoolFloatOff = 0x2C;  // 44
constexpr int kAvatarPoolFlagOff  = 0xF6;  // 246
// AllocSlot scales 14 float columns (stride 12) starting near +0x2C by 1/1000.
constexpr int kAvatarPoolFloatSpan = 168;  // 14 * 12
constexpr double kAvatarFloatScale = 0.001;  // dbl_61B0A8 == 1/1000

// The whole pool as a flat byte array (mirrors the contiguous 248*32 layout).
extern u8 g_avatarPool[kAvatarPoolCount * kAvatarPoolStride];
extern u8 g_avatarPoolFlags[kAvatarPoolCount];  // logical view of the +246 column
void ResetAvatarPool();  // test/setup helper (zeros pool, clears flags)

// Accessor for a slot's 248-byte entry base.
inline u8* AvatarPoolEntry(int slot) {
    return g_avatarPool + static_cast<long>(slot) * kAvatarPoolStride;
}

// gilde.exe 0x484598 — VIBE_Avatar_AllocSlot (__thiscall, ecx unused).
//   v1 = RandomModulo(32);  scan up to 32 slots round-robin from v1; the first
//   slot whose used-flag (entry+246) is 0 is taken: its 14 float columns are
//   replaced by column*(1/1000), the used-flag set to 1, and a pointer to the
//   entry returned. Returns nullptr when all 32 slots are used.
// `randModulo32` supplies RandomModulo(32) (the live wiring passes the real RNG).
u8* Avatar_AllocSlot(int randModulo32);

// ---------------------------------------------------------------------------
// Stream hook for Save/Load. The original calls VIBE_Vfs_WriteStream /
// VIBE_Vfs_ReadStreamBool; tests wire these to a real in-memory VFS stream.
// Read/Write return true on success (matching the ...Bool wrapper / "bytes != 0").
// ---------------------------------------------------------------------------
struct AvatarStreamHooks {
    virtual ~AvatarStreamHooks() = default;
    virtual bool Write(const void* src, u32 size, u32 count) { (void)src; (void)size; (void)count; return false; }
    virtual bool Read(void* dst, u32 size, u32 count) { (void)dst; (void)size; (void)count; return false; }
};
void SetAvatarStreamHooks(AvatarStreamHooks* h);
AvatarStreamHooks* AvatarStream();

// gilde.exe 0x484658 — VIBE_Avatar_Save.
//   write u32 count(=32); then for each of `count` slots write the 4-byte id
//   (entry+0) and the 1-byte used-flag (entry+246). Returns true on full success,
//   false if any write fails (or the stream is absent).
bool Avatar_Save();

// gilde.exe 0x4846ec — VIBE_Avatar_Load.
//   read u32 count; (count != 32 is just a logged inconsistency, not fatal);
//   pre-mark every slot's used-flag column to 0xFF ("fresh"); then read `count`
//   records of {u32 id, u8 flag}, matching each id against the pool ids and
//   storing its flag; finally any slot still 0xFF is defaulted to 0 ("unused").
//   Returns true on success.
bool Avatar_Load();

// ===========================================================================
// 2. Animal species spawners
// ===========================================================================
// The spawn target slot record (`int*` from ecx in the original). Fields touched:
//   [0]     (+0)  actor token (the Character_CreateFromModel result)
//   byte +4       kind byte (per-species)
//   [4..6] (+16/20/24) spawn position X/Y/Z (floats)
struct AnimalSpawnSlot {
    i32 actor;     // +0   created scene actor token (0 == none)
    u8  kind;      // +4   species kind byte
    u8  pad[3];
    float x;       // +16
    float y;       // +20
    float z;       // +24
};

// Scene hook for the spawners. Defaults are inert (no actor, identity placement),
// so the record-stamping logic is testable without the renderer.
struct SpawnSceneHooks {
    virtual ~SpawnSceneHooks() = default;
    // VIBE_Animal_FindDoorTarget(town) -> nonzero on success; outPos receives the
    // door-anchor position (Dog/Cat path). Default fails (returns 0).
    virtual int FindDoorTarget(int town, float outPos[3]) { (void)town; (void)outPos; return 0; }
    // VIBE_Animal_PickSpawnBuilding(model, town) then PointThroughBoneChain ->
    // nonzero on success; outPos receives the bone-chain placement. Default fails.
    virtual int PickSpawnPlacement(const char* model, int town, float outPos[3]) {
        (void)model; (void)town; (void)outPos; return 0;
    }
    // VIBE_Character_CreateFromModel(model) -> actor token (0 on failure).
    virtual i32 CreateFromModel(const char* model) { (void)model; return 0; }
    // VIBE_CharAction_InsertActionVararg(actor, 56, 1) — queue the idle action.
    virtual void InsertWanderAction(i32 actor) { (void)actor; }
};
void SetSpawnSceneHooks(SpawnSceneHooks* h);
SpawnSceneHooks* SpawnScene();

// Per-species scale stamped at actor+416 (raw float bit patterns in the binary):
//   1065353216 == 1.0f  (dog/cat/livestock-default),  1069547520 == 1.5f (sheep/cow)
constexpr float kAnimalScaleSmall = 1.0f;
constexpr float kAnimalScaleLarge = 1.5f;

// gilde.exe 0x483b58 — VIBE_Animal_SpawnDog (model "katze_KATZE", kind 1, 1.0).
// gilde.exe 0x483bc4 — VIBE_Animal_SpawnCat (model "hund_HUND",  kind 0, 1.0).
//   FindDoorTarget(town); if it fails, return 0. Else CreateFromModel(model) into
//   slot->actor; if 0, return 0. Stamp scale at actor+416, copy door pos into
//   slot->x/y/z, slot->kind, actor+4 = 2, queue the wander action, return actor.
// `actorScaleOut` (when non-null) receives the scale written to actor+416 so the
// inert-default path is still observable without a live actor record.
i32 Animal_SpawnDog(AnimalSpawnSlot* slot, int town, float* actorScaleOut);
i32 Animal_SpawnCat(AnimalSpawnSlot* slot, int town, float* actorScaleOut);

// gilde.exe 0x483c30 — VIBE_Animal_SpawnSheep (model "schaf_SCHAF", kind 4, 1.5).
// gilde.exe 0x483ca8 — VIBE_Animal_SpawnCow   (model "kuh_KUH",     kind 3, 1.5).
//   PickSpawnPlacement("dummy_<species>", town) (bone-chain placement); same body.
i32 Animal_SpawnSheep(AnimalSpawnSlot* slot, int town, float* actorScaleOut);
i32 Animal_SpawnCow(AnimalSpawnSlot* slot, int town, float* actorScaleOut);

// gilde.exe 0x483d20 — VIBE_Animal_SpawnLivestock(town, kind).
//   model = "dummy_<name[kind]>" where name[] is byte_62EE44 (32-byte rows). The
//   actor model is then chosen by kind: 3 -> "kuh_KUH", 7 -> "pferd_PFERD",
//   6 -> "schwein_SCHWEIN", else the "dummy_<name>" model itself. Scale 1.0.
// `kindNames` supplies byte_62EE44 (kindNames + 32*kind is a NUL-terminated name);
// pass nullptr to use an empty name (the original reads the live name table).
i32 Animal_SpawnLivestock(AnimalSpawnSlot* slot, int town, u8 kind,
                          const char* kindNames, float* actorScaleOut);

}  // namespace guild::sim
