#include "sim/animal.h"

#include "util/math_random.h"     // RandomModulo (VIBE_Math_RandomModulo @0x58b89c)
#include "util/math_rng_float.h"  // RandomFloatScaled (@0x58b910)

#include <cstring>
#include <vector>

// Faithful 1:1 port of the ambient-animal pool + spawn/despawn rules from
// gilde.exe. The render/scene leaves are abstracted behind IAnimalWorld; the
// pool record machinery, the population/season/weather spawn gate, the RNG type
// pick and the lifetime->despawn computation are translated verbatim.

namespace guild::sim {

// ---------------------------------------------------------------------------
// Pool globals. The original pool is a raw heap block; we back it with a vector
// so AllocPool/FreePool model the alloc/free without a real allocator.
// ---------------------------------------------------------------------------
AnimalRec* g_animalPool          = nullptr;  // dword_62EE3C
int        g_animalCount         = 0;        // dword_62EE38
int        g_animalCursor        = 0;        // dword_62EE40
int        g_animalLastSpawnTick = 0;        // dword_62EF44
// g_gameTick (dword_62EB38) is the shared game clock — defined once in
// actionqueue.cpp; declared extern in animal.h. Do NOT redefine it here.
u8         g_weatherState        = 0;        // byte_1233514

static std::vector<AnimalRec> g_poolStorage;

// ---------------------------------------------------------------------------
// World hook.
// ---------------------------------------------------------------------------
static IAnimalWorld g_defaultWorld;
static IAnimalWorld* g_world = &g_defaultWorld;
void SetAnimalWorld(IAnimalWorld* world) {
    g_world = world ? world : &g_defaultWorld;
}
IAnimalWorld* AnimalWorld() { return g_world; }

void ResetAnimalPool() {
    g_poolStorage.clear();
    g_animalPool = nullptr;
    g_animalCount = 0;
    g_animalCursor = 0;
    g_animalLastSpawnTick = 0;
    g_gameTick = 0;
    g_weatherState = 0;
    g_world = &g_defaultWorld;
}

// ===========================================================================
// VIBE_Animal_AllocPool  0x4835c0
//   dword_62EE3C = AllocDebug(0x2480 == 9344 == 292*32, "anm:animals");
//   dword_62EE38 = 0;  memset(pool, 0, 4)  (the original zeroes the head dword;
//   the real heap block is fresh — we zero the whole pool to match its observable
//   "all slots free" state).
// ===========================================================================
void Animal_AllocPool() {
    g_poolStorage.assign(kAnimalPoolCapacity, AnimalRec{});
    g_animalPool = g_poolStorage.data();
    g_animalCount = 0;
}

// ===========================================================================
// VIBE_Animal_FreePool  0x4835ec
// ===========================================================================
void Animal_FreePool() {
    if (g_animalPool) {
        g_poolStorage.clear();
        g_animalPool = nullptr;
    }
}

// ===========================================================================
// VIBE_Animal_AllocSlot  0x483960
//   for (i = 0; i < 9344; i += 292) if (!*(pool+i)) break;   // first free actor
//   if (count-equivalent == 32) return 0;
//   else { ++dword_62EE38; rec = pool + 292*slot; rec.spawnTick = gameTick; }
// (The original counts free-slot probes in v1; if it walked all 32 it's full.)
// ===========================================================================
AnimalRec* Animal_AllocSlot() {
    if (!g_animalPool)
        return nullptr;
    int slot = 0;
    for (; slot < kAnimalPoolCapacity; ++slot) {
        if (g_animalPool[slot].actor == 0)
            break;
    }
    if (slot == kAnimalPoolCapacity)
        return nullptr;
    ++g_animalCount;
    AnimalRec* rec = &g_animalPool[slot];
    rec->spawnTick = g_gameTick;   // *(result+32) = dword_62EB38
    return rec;
}

// ===========================================================================
// VIBE_Animal_FreeSlot  0x4839cc
//   Character_Destroy(rec.actor); memset(rec, 0, 292); --dword_62EE38;
// ===========================================================================
void Animal_FreeSlot(AnimalRec* a) {
    if (!a)
        return;
    g_world->DestroyAnimal(a->actor);
    std::memset(a, 0, kAnimalRecordStride);
    --g_animalCount;
}

// ===========================================================================
// Spawn type pick (from VIBE_Animal_Update's two RNG branches).
//   commonRoll = RandomModulo(100); if <= 30 -> pick from {Cat,Dog} via
//                RandomModulo(2):  0->Cat, 1->Dog.
//   else -> pick from {Cow,Sheep,_,Pig,Horse} via RandomModulo(5):
//                0->Cow, 1->Sheep, 2->(none), 3->Pig, 4->Horse.
// Returns the kind byte, or 0xFF for "no spawn" (the RandomModulo(5)==2 hole).
// ===========================================================================
u8 Animal_SpawnKindForRoll(int commonRoll, int rareRoll) {
    if (commonRoll <= 30) {
        switch (rareRoll & 1) {  // RandomModulo(2)
            case 0: return kAnimalCat;
            default: return kAnimalDog;
        }
    }
    switch (rareRoll) {  // RandomModulo(5)
        case 0: return kAnimalCow;
        case 1: return kAnimalSheep;
        case 3: return kAnimalPig;
        case 4: return kAnimalHorse;
        default: return 0xFF;  // case 2: nothing spawns
    }
}

// Place a freshly spawned animal of `kind` into the allocated slot via the
// world hook (Character_CreateFromModel + placement). On actor==0 the original
// leaves the slot's actor field 0 (AllocSlot already bumped the count, so the
// despawn pass reclaims it next cycle).
static void SpawnInto(AnimalRec* rec, u8 kind) {
    float x = 0, y = 0, z = 0;
    i32 actor = g_world->SpawnAnimal(kind, &x, &y, &z);
    rec->actor = actor;
    rec->kind = kind;
    rec->herdCount = -1;
    rec->x = x;
    rec->y = y;
    rec->z = z;
}

// ===========================================================================
// VIBE_Animal_Update  0x48364c  (spawn-decision + lifetime/despawn core)
// ---------------------------------------------------------------------------
// `season` is GameTime_GetSeasonFromDay (3 == winter). The render-side scene
// switching (Universe_SwitchActiveSlot) brackets the original; omitted here.
// ===========================================================================
void Animal_Update(int season) {
    if (!g_animalPool)
        return;

    // Weather-driven population cap basis v5: clear=0, light(1)=16, heavy=32.
    int cap;
    if (g_weatherState == 0)
        cap = 0;
    else if (g_weatherState == 1)
        cap = 16;
    else
        cap = 32;

    // Spawn gate: throttle to >=350 ticks since the last spawn attempt.
    if (static_cast<unsigned>(g_animalLastSpawnTick + 350)
            < static_cast<unsigned>(g_gameTick)) {
        // (u16)RandomModulo(2*cap) - cap > count  &&  not winter  &&  free slot.
        int roll = static_cast<u16>(util::RandomModulo(static_cast<u16>(2 * cap)));
        if (roll - cap > g_animalCount && season != 3) {
            AnimalRec* rec = Animal_AllocSlot();
            if (rec) {
                int common = static_cast<u16>(util::RandomModulo(100));
                int rare;
                if (common <= 30)
                    rare = static_cast<u8>(util::RandomModulo(2));
                else
                    rare = static_cast<u8>(util::RandomModulo(5));
                u8 kind = Animal_SpawnKindForRoll(common, rare);
                if (kind != 0xFF)
                    SpawnInto(rec, kind);
            }
        }
        g_animalLastSpawnTick = g_gameTick;
    }

    // Update one slot (round-robin cursor).
    AnimalRec* rec = &g_animalPool[g_animalCursor];
    if (rec->actor) {
        // Per-animal AI step (UpdateCat for 0/1, herd for 2, UpdateSheep 3..7).
        g_world->UpdateAnimal(rec);

        if (season == 3)
            rec->despawn = 1;

        // Lifetime decay: once spawnTick+2100 < gameTick, despawn with a
        // probability that rises as the animal ages.
        unsigned threshold = static_cast<unsigned>(rec->spawnTick) + 2100;
        if (threshold < static_cast<unsigned>(g_gameTick)) {
            // v14 = (float)(threshold / (gameTick - threshold))  [integer div]
            int denom = g_gameTick - static_cast<int>(threshold);
            float v14 = static_cast<float>(static_cast<int>(
                threshold / static_cast<unsigned>(denom)));
            if (util::RandomFloatScaled() > v14)
                rec->despawn = 1;
        }

        if (rec->despawn
            && (g_world->IsAnimalCulled(rec) || season == 3 || cap < g_animalCount)) {
            g_world->DestroyAnimal(rec->actor);
            std::memset(rec, 0, kAnimalRecordStride);
            --g_animalCount;
        }
    }

    g_animalCursor = (g_animalCursor + 1) % kAnimalPoolCapacity;
}

}  // namespace guild::sim
