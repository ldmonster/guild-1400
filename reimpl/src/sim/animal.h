#pragma once
// Ambient animal (pets/livestock) behaviour + spawn/despawn pool for the Guild
// simulation (gilde.exe). MODULE: Personnel/Recruit remainder + Animal/Plant.
// (namespace guild::sim).
//
// The original keeps a fixed pool of 32 animal records (292 bytes each) in a
// heap block (dword_62EE3C @0x62EE3C). VIBE_Animal_Update walks one slot per
// call (round-robin cursor dword_62EE40), occasionally spawns a new animal
// (season/weather/population gated, RNG-driven type pick) and despawns animals
// past their lifetime. Per-animal AI (UpdateCat/UpdateSheep) issues wander/sound
// actions.
//
// The model/character/heightmap/scene leaves (Character_CreateFromModel,
// Heightmap_*, SceneGraph_WalkAndInvoke, Transform_PointThroughBoneChain,
// Universe_SwitchActiveSlot) are render/world-coupled and are routed through the
// IAnimalWorld hook so the pool + spawn/despawn/decision rules are testable in
// isolation. The model loaders (LoadModels/ResetModelHandles), the bone-chain
// spatial helpers (FindHerdGrouping, FindDoorTarget, BuildWanderPath,
// PickSpawnBuilding, CollectSpawnBuilding) and the per-species AI steps
// (UpdateCat/UpdateSheep) are now translated in sim/animal_wander.{h,cpp}
// (routed through IAnimalSceneOps for the building/scene/mesh leaves).
//
// Translated functions:
//   VIBE_Animal_AllocPool   0x4835c0
//   VIBE_Animal_FreePool    0x4835ec
//   VIBE_Animal_AllocSlot   0x483960
//   VIBE_Animal_FreeSlot    0x4839cc
//   VIBE_Animal_Update      0x48364c  (spawn-decision + lifetime/despawn core)
//   VIBE_Animal_SpawnDog/Cat/Sheep/Cow/Livestock 0x483b58..0x483d20 (type bytes)
#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Animal pool record (gilde.exe 292-byte stride, 32 slots).
//   +0x00  character ptr (0 == free slot)   — the live ch_t / scene actor
//   +0x04  kind byte (Cat=0, Dog=1, Cow=3, Sheep=4, Pig=6, Horse=7)
//   +0x08  herd grouping count (-1 == not yet computed; set by FindHerdGrouping)
//   +0x0C  despawn flag byte (1 == marked for removal)
//   +0x10  spawn world X (float)
//   +0x14  spawn world Y (float)
//   +0x18  spawn world Z (float)
//   +0x20  spawn game-tick (dword; lifetime base)
// (Remaining bytes: herd target buffer / scratch, untouched by the rules core.)
// ---------------------------------------------------------------------------
constexpr int kAnimalRecordStride = 292;
constexpr int kAnimalPoolCapacity = 32;

GUILD_PACKED_BEGIN
struct AnimalRec {
    i32 actor;     // +0x00  character ptr token (0 == free)
    u8  kind;      // +0x04  animal kind byte
    u8  pad5[3];   // +0x05..+0x07
    i32 herdCount; // +0x08  herd grouping count (-1 == uncomputed)
    u8  despawn;   // +0x0C  despawn-request flag
    u8  pad13[3];  // +0x0D..+0x0F
    float x;       // +0x10  spawn X
    float y;       // +0x14  spawn Y
    float z;       // +0x18  spawn Z
    u8  pad28[4];  // +0x1C..+0x1F
    i32 spawnTick; // +0x20  spawn game-tick
    u8  pad36[256];// +0x24..+0x123  herd buffer / scratch
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(AnimalRec) == kAnimalRecordStride, "AnimalRec stride 292");

// Animal kind ids (the +4 byte the spawners stamp and Update switches on).
enum AnimalKind : u8 {
    kAnimalCat   = 0,
    kAnimalDog   = 1,
    kAnimalCow   = 3,
    kAnimalSheep = 4,
    kAnimalPig   = 6,
    kAnimalHorse = 7,
};

// ---------------------------------------------------------------------------
// World/render leaf hook. The spawner/despawner and per-animal AI call into the
// scene; tests provide a mock that just records the calls (and can fabricate a
// spawned actor token). `SpawnAnimal` returns the new actor token (0 on fail).
// ---------------------------------------------------------------------------
struct IAnimalWorld {
    virtual ~IAnimalWorld() = default;
    // Create the scene actor for an animal of `kind`; returns the actor token
    // (nonzero) or 0 if creation failed. (Models Character_CreateFromModel +
    // FindDoorTarget/PickSpawnBuilding placement.) `outX/Y/Z` receive the spawn
    // position the default backend would compute (0 here).
    virtual i32 SpawnAnimal(u8 kind, float* outX, float* outY, float* outZ) {
        (void)kind; (void)outX; (void)outY; (void)outZ; return 0;
    }
    // Per-animal AI step (UpdateCat/UpdateSheep wander+sound). No-op by default.
    virtual void UpdateAnimal(AnimalRec* a) { (void)a; }
    // Destroy the scene actor (Character_Destroy).
    virtual void DestroyAnimal(i32 actor) { (void)actor; }
    // "Animal is far off-screen / culled" test used in the despawn gate
    // (the +492/+2317 & 0x40 mesh flag check). Default false.
    virtual bool IsAnimalCulled(const AnimalRec* a) { (void)a; return false; }
};
void SetAnimalWorld(IAnimalWorld* world);
IAnimalWorld* AnimalWorld();

// ---------------------------------------------------------------------------
// Pool globals (gilde.exe). Exposed so the turn/update orchestrator + tests can
// drive them. ResetAnimalPool() clears everything (test/setup helper).
// ---------------------------------------------------------------------------
extern AnimalRec* g_animalPool;    // dword_62EE3C (heap base; null until AllocPool)
extern int        g_animalCount;   // dword_62EE38 (live count)
extern int        g_animalCursor;  // dword_62EE40 (round-robin update slot)
extern int        g_animalLastSpawnTick; // dword_62EF44
extern unsigned int g_gameTick;    // dword_62EB38 (clock; defined in actionqueue.cpp)
extern u8         g_weatherState;  // byte_1233514 (0 clear, 1 light, 2 heavy)

void ResetAnimalPool();

// gilde.exe 0x4835c0 — VIBE_Animal_AllocPool. Allocates the 0x2480-byte (==
// 292*32) pool, zeroes it, clears the live count.
void Animal_AllocPool();

// gilde.exe 0x4835ec — VIBE_Animal_FreePool. Frees the pool (if any).
void Animal_FreePool();

// gilde.exe 0x483960 — VIBE_Animal_AllocSlot. Finds the first free record
// (actor ptr == 0); returns it (and stamps spawnTick = g_gameTick, bumps count)
// or nullptr when all 32 are in use.
AnimalRec* Animal_AllocSlot();

// gilde.exe 0x4839cc — VIBE_Animal_FreeSlot. Destroys the actor, zeroes the
// record, decrements the live count.
void Animal_FreeSlot(AnimalRec* a);

// gilde.exe 0x48364c — VIBE_Animal_Update (spawn-decision + lifetime core).
// Runs one round-robin update step:
//   1. season/weather/population-gated spawn roll (RNG-driven type pick),
//   2. per-animal AI step (via the world hook),
//   3. lifetime decay -> despawn flag, then despawn if flagged & culled/winter.
// `season` is the current season (3 == winter); supply via GameTime_GetSeason.
// Returns the despawn decision count (mirrors the original's bookkeeping).
void Animal_Update(int season);

// gilde.exe 0x483b58.. — the spawn type-byte mapping recovered from the per-
// species spawners (kind stamped at AnimalRec+4). Pure helper for the decision
// table; spawning itself goes through the world hook.
u8 Animal_SpawnKindForRoll(int commonRoll, int rareRoll);

}  // namespace guild::sim
