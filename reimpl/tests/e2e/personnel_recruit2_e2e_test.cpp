// End-to-end flow across personnel_recruit2: populate the avatar appearance pool
// via AllocSlot (RNG-driven), persist it through Save, reload through Load, and in
// parallel run a herd-spawn flow (Dog/Cat/Sheep/Cow/Livestock) into spawn slots
// through a scripted scene backend. Verifies the whole chain end to end.
#include "sim/personnel_recruit2.h"
#include "test.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild::sim;
using guild::u8;
using guild::u32;
using guild::i32;

namespace {

// In-memory serialisation stream (same role the VFS plays live).
struct MemStream : AvatarStreamHooks {
    std::vector<u8> buf;
    size_t rpos = 0;
    bool Write(const void* src, u32 size, u32 count) override {
        const u8* p = static_cast<const u8*>(src);
        buf.insert(buf.end(), p, p + static_cast<size_t>(size) * count);
        return true;
    }
    bool Read(void* dst, u32 size, u32 count) override {
        size_t n = static_cast<size_t>(size) * count;
        if (rpos + n > buf.size()) return false;
        std::memcpy(dst, buf.data() + rpos, n);
        rpos += n;
        return true;
    }
};

// Scene backend that hands out actor tokens and records what was created.
struct FarmScene : SpawnSceneHooks {
    i32 nextActor = 1000;
    std::vector<std::string> models;
    std::vector<u8> kindsSeen;
    int FindDoorTarget(int, float out[3]) override {
        out[0] = 1; out[1] = 2; out[2] = 3; return 1;
    }
    int PickSpawnPlacement(const char*, int, float out[3]) override {
        out[0] = 4; out[1] = 5; out[2] = 6; return 1;
    }
    i32 CreateFromModel(const char* m) override {
        models.push_back(m ? m : "");
        return nextActor++;
    }
    void InsertWanderAction(i32) override {}
};

} // namespace

// Full avatar persistence cycle: allocate a deterministic set of slots, snapshot
// the resulting flags + ids, Save, scramble, Load, and confirm restoration.
TEST(PersRec2E2E, AvatarAllocSaveReloadCycle) {
    ResetAvatarPool();
    // Give every slot a unique id so Load can match after a reload.
    for (int s = 0; s < kAvatarPoolCount; ++s) {
        u32 id = 0x9000u + s;
        std::memcpy(AvatarPoolEntry(s), &id, 4);
    }
    // Allocate 4 slots via deterministic RNG draws (no two collide here).
    int picks[4] = {2, 9, 17, 28};
    for (int p : picks) {
        u8* got = Avatar_AllocSlot(p);
        CHECK(got == AvatarPoolEntry(p));
    }
    // Snapshot expected flags.
    u8 expect[kAvatarPoolCount];
    for (int s = 0; s < kAvatarPoolCount; ++s) expect[s] = g_avatarPoolFlags[s];

    MemStream s;
    SetAvatarStreamHooks(&s);
    CHECK(Avatar_Save());

    // Scramble both the flags and re-run Load from the saved stream.
    for (int i = 0; i < kAvatarPoolCount; ++i) {
        AvatarPoolEntry(i)[kAvatarPoolFlagOff] = 0x33; g_avatarPoolFlags[i] = 0x33;
    }
    CHECK(Avatar_Load());
    SetAvatarStreamHooks(nullptr);

    for (int s2 = 0; s2 < kAvatarPoolCount; ++s2) {
        CHECK_EQ((int)g_avatarPoolFlags[s2], (int)expect[s2]);
    }
    // The four allocated slots are flagged used; the rest free.
    for (int p : picks) CHECK_EQ((int)g_avatarPoolFlags[p], 1);
}

// Herd spawn flow: spawn one of each species into its own slot and verify the
// kind bytes, positions and actor tokens form a coherent population.
TEST(PersRec2E2E, HerdSpawnFlow) {
    FarmScene sc;
    SetSpawnSceneHooks(&sc);

    char names[8 * 32];
    std::memset(names, 0, sizeof(names));
    std::strcpy(names + 32 * 6, "PIG");

    AnimalSpawnSlot dog{}, cat{}, sheep{}, cow{}, pig{};
    i32 aDog = Animal_SpawnDog(&dog, 1, nullptr);
    i32 aCat = Animal_SpawnCat(&cat, 1, nullptr);
    i32 aSheep = Animal_SpawnSheep(&sheep, 1, nullptr);
    i32 aCow = Animal_SpawnCow(&cow, 1, nullptr);
    i32 aPig = Animal_SpawnLivestock(&pig, 1, 6, names, nullptr);

    SetSpawnSceneHooks(nullptr);

    // All five got distinct, nonzero actor tokens (1000..1004).
    CHECK(aDog && aCat && aSheep && aCow && aPig);
    CHECK(aDog != aCat && aSheep != aCow && aCow != aPig);

    // Kind bytes per the recovered tables.
    CHECK_EQ((int)dog.kind, 1);
    CHECK_EQ((int)cat.kind, 0);
    CHECK_EQ((int)sheep.kind, 4);
    CHECK_EQ((int)cow.kind, 3);
    CHECK_EQ((int)pig.kind, 6);

    // Dog/Cat placed at the door anchor; Sheep/Cow/Pig at the bone-chain anchor.
    CHECK(dog.x == 1.0f && dog.z == 3.0f);
    CHECK(sheep.x == 4.0f && sheep.z == 6.0f);
    CHECK(pig.x == 4.0f && pig.z == 6.0f);

    // Models created, in order.
    CHECK_EQ((int)sc.models.size(), 5);
    if (sc.models.size() == 5) {
        CHECK(sc.models[0] == "katze_KATZE");   // SpawnDog
        CHECK(sc.models[1] == "hund_HUND");     // SpawnCat
        CHECK(sc.models[2] == "schaf_SCHAF");   // SpawnSheep
        CHECK(sc.models[3] == "kuh_KUH");       // SpawnCow
        CHECK(sc.models[4] == "schwein_SCHWEIN"); // Livestock kind 6
    }
}

// Mixed flow: a spawn whose placement fails leaves its slot untouched while the
// rest of the herd still spawns (no cross-contamination).
TEST(PersRec2E2E, PartialFailureIsolation) {
    struct PickyScene : SpawnSceneHooks {
        i32 nextActor = 500;
        int FindDoorTarget(int, float out[3]) override { out[0]=out[1]=out[2]=0; return 1; }
        int PickSpawnPlacement(const char*, int, float[3]) override { return 0; } // always fail
        i32 CreateFromModel(const char*) override { return nextActor++; }
        void InsertWanderAction(i32) override {}
    } sc;
    SetSpawnSceneHooks(&sc);

    AnimalSpawnSlot dog{}, sheep{};
    i32 aDog = Animal_SpawnDog(&dog, 1, nullptr);     // door ok -> spawns
    i32 aSheep = Animal_SpawnSheep(&sheep, 1, nullptr); // placement fails -> 0
    SetSpawnSceneHooks(nullptr);

    CHECK(aDog != 0);
    CHECK_EQ((int)aSheep, 0);
    CHECK_EQ((int)sheep.actor, 0);
    CHECK_EQ((int)dog.kind, 1);
}
