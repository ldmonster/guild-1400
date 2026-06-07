// Unit tests for personnel_recruit2: the avatar appearance pool (AllocSlot /
// Save / Load) and the animal species spawners. Golden vectors for the float
// scaling computed with python (x*0.001, float32 round-trip).
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

// Capturing stream hook backed by a growable byte vector. Read replays the same
// buffer from a cursor; both honour the success/failure contract.
struct MemStream : AvatarStreamHooks {
    std::vector<u8> buf;
    size_t rpos = 0;
    bool failNextWrite = false;
    bool Write(const void* src, u32 size, u32 count) override {
        if (failNextWrite) { failNextWrite = false; return false; }
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

// Helper: write a float column into a slot's entry at +44+12*i.
void SetCol(int slot, int i, float v) {
    u8* e = AvatarPoolEntry(slot);
    std::memcpy(e + (kAvatarPoolFloatOff + 12 * i), &v, 4);
}
float GetScaled(int slot, int i) {
    u8* e = AvatarPoolEntry(slot);
    float v;
    std::memcpy(&v, e + (kAvatarPoolFloatOff + 4 + 12 * i), 4);
    return v;
}
void SetId(int slot, u32 id) {
    std::memcpy(AvatarPoolEntry(slot), &id, 4);
}

} // namespace

// --- Avatar pool: AllocSlot ------------------------------------------------

// RandomModulo result lands on slot 5; that slot is free, so it is taken and its
// flag set. AllocSlot returns the entry pointer (== AvatarPoolEntry(5)).
TEST(PersRec2AvatarAlloc, PicksRngSlotWhenFree) {
    ResetAvatarPool();
    u8* got = Avatar_AllocSlot(5);
    CHECK(got == AvatarPoolEntry(5));
    if (got) {
        CHECK_EQ((int)g_avatarPoolFlags[5], 1);
        CHECK_EQ((int)got[kAvatarPoolFlagOff], 1);
    }
}

// Round-robin: slot from RNG is occupied, scan advances (& 0x1F) to the next free.
TEST(PersRec2AvatarAlloc, RoundRobinSkipsOccupied) {
    ResetAvatarPool();
    // Occupy slots 30, 31, 0 (used-flag = 1). Start RNG at 30 -> 31 -> 0 -> 1 free.
    for (int s : {30, 31, 0}) AvatarPoolEntry(s)[kAvatarPoolFlagOff] = 1, g_avatarPoolFlags[s] = 1;
    u8* got = Avatar_AllocSlot(30);
    CHECK(got == AvatarPoolEntry(1));
    if (got) CHECK_EQ((int)g_avatarPoolFlags[1], 1);
}

// All 32 slots used -> returns nullptr, flags unchanged.
TEST(PersRec2AvatarAlloc, FullPoolReturnsNull) {
    ResetAvatarPool();
    for (int s = 0; s < kAvatarPoolCount; ++s) {
        AvatarPoolEntry(s)[kAvatarPoolFlagOff] = 1;
        g_avatarPoolFlags[s] = 1;
    }
    u8* got = Avatar_AllocSlot(0);
    CHECK(got == nullptr);
}

// The 14 float columns are replaced by column * (1/1000). Golden vectors from python.
TEST(PersRec2AvatarAlloc, ScalesFloatColumns) {
    ResetAvatarPool();
    SetCol(7, 0, 1000.0f);
    SetCol(7, 1, 2000.0f);
    SetCol(7, 2, 1234.5f);
    SetCol(7, 13, 5000.0f);   // last column
    u8* got = Avatar_AllocSlot(7);
    CHECK(got == AvatarPoolEntry(7));
    if (got) {
        CHECK(GetScaled(7, 0) == 1.0f);
        CHECK(GetScaled(7, 1) == 2.0f);
        CHECK(GetScaled(7, 13) == 5.0f);
        // 1234.5 * 0.001 == 1.2345000505...
        float v = GetScaled(7, 2);
        CHECK(v > 1.2344f && v < 1.2346f);
    }
}

// --- Avatar pool: Save / Load round trip -----------------------------------

TEST(PersRec2AvatarIO, SaveWritesCountAndPerSlotRecords) {
    ResetAvatarPool();
    SetId(0, 0xAABBCCDD);
    AvatarPoolEntry(0)[kAvatarPoolFlagOff] = 1; g_avatarPoolFlags[0] = 1;
    MemStream s;
    SetAvatarStreamHooks(&s);
    bool ok = Avatar_Save();
    SetAvatarStreamHooks(nullptr);
    CHECK(ok);
    // 4-byte count + 32 * (4-byte id + 1-byte flag) = 4 + 32*5 = 164.
    CHECK_EQ((int)s.buf.size(), 164);
    if (s.buf.size() >= 4) {
        u32 cnt; std::memcpy(&cnt, s.buf.data(), 4);
        CHECK_EQ((int)cnt, 32);
    }
}

// Save then Load reproduces the per-slot flags by matching ids.
TEST(PersRec2AvatarIO, SaveLoadRoundTrip) {
    ResetAvatarPool();
    for (int s = 0; s < kAvatarPoolCount; ++s) {
        SetId(s, 0x1000 + s);
        AvatarPoolEntry(s)[kAvatarPoolFlagOff] = (s % 3 == 0) ? 1 : 0;
        g_avatarPoolFlags[s] = (s % 3 == 0) ? 1 : 0;
    }
    MemStream s;
    SetAvatarStreamHooks(&s);
    CHECK(Avatar_Save());
    // Corrupt the flags in memory, keep the ids; Load must restore from the stream.
    for (int i = 0; i < kAvatarPoolCount; ++i) {
        AvatarPoolEntry(i)[kAvatarPoolFlagOff] = 0xEE; g_avatarPoolFlags[i] = 0xEE;
    }
    bool ok = Avatar_Load();
    SetAvatarStreamHooks(nullptr);
    CHECK(ok);
    for (int i = 0; i < kAvatarPoolCount; ++i) {
        CHECK_EQ((int)g_avatarPoolFlags[i], (i % 3 == 0) ? 1 : 0);
    }
}

// Load defaults any slot whose id was NOT present in the stream to 0 ("unused").
TEST(PersRec2AvatarIO, LoadDefaultsMissingToZero) {
    ResetAvatarPool();
    for (int s = 0; s < kAvatarPoolCount; ++s) SetId(s, 0x2000 + s);
    // Build a stream that declares count=1 with an id matching slot 4 -> flag 1,
    // and an id NOT in the pool. All other slots must default to 0.
    MemStream s;
    u32 count = 2;
    s.buf.insert(s.buf.end(), (u8*)&count, (u8*)&count + 4);
    u32 id4 = 0x2000 + 4; u8 f4 = 1;
    s.buf.insert(s.buf.end(), (u8*)&id4, (u8*)&id4 + 4); s.buf.push_back(f4);
    u32 idX = 0xDEADBEEF; u8 fX = 1;
    s.buf.insert(s.buf.end(), (u8*)&idX, (u8*)&idX + 4); s.buf.push_back(fX);
    SetAvatarStreamHooks(&s);
    bool ok = Avatar_Load();
    SetAvatarStreamHooks(nullptr);
    CHECK(ok);
    CHECK_EQ((int)g_avatarPoolFlags[4], 1);
    for (int i = 0; i < kAvatarPoolCount; ++i) {
        if (i != 4) CHECK_EQ((int)g_avatarPoolFlags[i], 0);
    }
}

// Save aborts (returns false) if the count header write fails.
TEST(PersRec2AvatarIO, SaveFailsOnWriteError) {
    ResetAvatarPool();
    MemStream s;
    s.failNextWrite = true;
    SetAvatarStreamHooks(&s);
    bool ok = Avatar_Save();
    SetAvatarStreamHooks(nullptr);
    CHECK(!ok);
}

// --- Animal spawners: inert-default path -----------------------------------

// With the default (inert) scene hook, FindDoorTarget fails -> SpawnDog returns 0
// and never touches the slot beyond it staying unset.
TEST(PersRec2AnimalSpawn, DefaultHookDogFailsPlacement) {
    SetSpawnSceneHooks(nullptr);
    AnimalSpawnSlot slot{};
    i32 a = Animal_SpawnDog(&slot, 3, nullptr);
    CHECK_EQ((int)a, 0);
    CHECK_EQ((int)slot.actor, 0);
}

// --- Animal spawners: scripted scene hook ----------------------------------

namespace {
struct ScriptScene : SpawnSceneHooks {
    bool doorOk = true, placeOk = true;
    i32 nextActor = 0;
    float doorPos[3] = {10, 20, 30};
    float placePos[3] = {40, 50, 60};
    std::vector<std::string> created;
    int wanderCalls = 0;
    std::string lastPickModel;
    int FindDoorTarget(int, float out[3]) override {
        if (!doorOk) return 0;
        out[0]=doorPos[0]; out[1]=doorPos[1]; out[2]=doorPos[2]; return 1;
    }
    int PickSpawnPlacement(const char* m, int, float out[3]) override {
        lastPickModel = m ? m : "";
        if (!placeOk) return 0;
        out[0]=placePos[0]; out[1]=placePos[1]; out[2]=placePos[2]; return 1;
    }
    i32 CreateFromModel(const char* m) override {
        created.push_back(m ? m : "");
        return nextActor;
    }
    void InsertWanderAction(i32) override { ++wanderCalls; }
};
} // namespace

// SpawnDog: door placement + "katze_KATZE" model + kind 1 + scale 1.0 + wander.
TEST(PersRec2AnimalSpawn, DogStampsRecord) {
    ScriptScene sc; sc.nextActor = 0x111;
    SetSpawnSceneHooks(&sc);
    AnimalSpawnSlot slot{};
    float scale = -1;
    i32 a = Animal_SpawnDog(&slot, 7, &scale);
    SetSpawnSceneHooks(nullptr);
    CHECK_EQ((int)a, 0x111);
    CHECK_EQ((int)slot.actor, 0x111);
    CHECK_EQ((int)slot.kind, 1);
    CHECK(slot.x == 10.0f && slot.y == 20.0f && slot.z == 30.0f);
    CHECK(scale == 1.0f);
    CHECK_EQ(sc.wanderCalls, 1);
    if (!sc.created.empty()) CHECK(sc.created[0] == "katze_KATZE");
}

// SpawnCat: "hund_HUND", kind 0, scale 1.0 (names crossed vs models, per binary).
TEST(PersRec2AnimalSpawn, CatStampsRecord) {
    ScriptScene sc; sc.nextActor = 0x222;
    SetSpawnSceneHooks(&sc);
    AnimalSpawnSlot slot{};
    float scale = -1;
    i32 a = Animal_SpawnCat(&slot, 1, &scale);
    SetSpawnSceneHooks(nullptr);
    CHECK_EQ((int)a, 0x222);
    CHECK_EQ((int)slot.kind, 0);
    CHECK(scale == 1.0f);
    if (!sc.created.empty()) CHECK(sc.created[0] == "hund_HUND");
}

// SpawnSheep: bone-chain placement "dummy_SHEEP", actor "schaf_SCHAF", kind 4, 1.5.
TEST(PersRec2AnimalSpawn, SheepStampsRecord) {
    ScriptScene sc; sc.nextActor = 0x333;
    SetSpawnSceneHooks(&sc);
    AnimalSpawnSlot slot{};
    float scale = -1;
    i32 a = Animal_SpawnSheep(&slot, 2, &scale);
    SetSpawnSceneHooks(nullptr);
    CHECK_EQ((int)a, 0x333);
    CHECK_EQ((int)slot.kind, 4);
    CHECK(slot.x == 40.0f && slot.y == 50.0f && slot.z == 60.0f);
    CHECK(scale == 1.5f);
    CHECK(sc.lastPickModel == "dummy_SHEEP");
    if (!sc.created.empty()) CHECK(sc.created[0] == "schaf_SCHAF");
}

// SpawnCow: "dummy_COW" -> "kuh_KUH", kind 3, scale 1.5.
TEST(PersRec2AnimalSpawn, CowStampsRecord) {
    ScriptScene sc; sc.nextActor = 0x444;
    SetSpawnSceneHooks(&sc);
    AnimalSpawnSlot slot{};
    float scale = -1;
    i32 a = Animal_SpawnCow(&slot, 2, &scale);
    SetSpawnSceneHooks(nullptr);
    CHECK_EQ((int)slot.kind, 3);
    CHECK(scale == 1.5f);
    CHECK(sc.lastPickModel == "dummy_COW");
    if (!sc.created.empty()) CHECK(sc.created[0] == "kuh_KUH");
    (void)a;
}

// SpawnLivestock kind switch: 3->kuh_KUH, 7->pferd_PFERD, 6->schwein_SCHWEIN, else dummy_<name>.
TEST(PersRec2AnimalSpawn, LivestockKindSelectsActorModel) {
    char names[8 * 32];
    std::memset(names, 0, sizeof(names));
    std::strcpy(names + 32 * 3, "COW");
    std::strcpy(names + 32 * 7, "HORSE");
    std::strcpy(names + 32 * 6, "PIG");
    std::strcpy(names + 32 * 2, "GOAT");  // default branch

    {   // kind 7 -> pferd_PFERD, model passed to PickSpawnPlacement is "dummy_HORSE"
        ScriptScene sc; sc.nextActor = 0x55;
        SetSpawnSceneHooks(&sc);
        AnimalSpawnSlot slot{};
        i32 a = Animal_SpawnLivestock(&slot, 1, 7, names, nullptr);
        SetSpawnSceneHooks(nullptr);
        CHECK_EQ((int)slot.kind, 7);
        CHECK(sc.lastPickModel == "dummy_HORSE");
        if (!sc.created.empty()) CHECK(sc.created[0] == "pferd_PFERD");
        (void)a;
    }
    {   // kind 2 -> default branch: actor model == the "dummy_GOAT" model itself.
        ScriptScene sc; sc.nextActor = 0x66;
        SetSpawnSceneHooks(&sc);
        AnimalSpawnSlot slot{};
        Animal_SpawnLivestock(&slot, 1, 2, names, nullptr);
        SetSpawnSceneHooks(nullptr);
        CHECK(sc.lastPickModel == "dummy_GOAT");
        if (!sc.created.empty()) CHECK(sc.created[0] == "dummy_GOAT");
    }
}

// Livestock aborts when placement fails (default hook / placeOk=false).
TEST(PersRec2AnimalSpawn, LivestockFailsPlacement) {
    ScriptScene sc; sc.placeOk = false;
    SetSpawnSceneHooks(&sc);
    AnimalSpawnSlot slot{};
    i32 a = Animal_SpawnLivestock(&slot, 1, 3, nullptr, nullptr);
    SetSpawnSceneHooks(nullptr);
    CHECK_EQ((int)a, 0);
    CHECK_EQ((int)slot.actor, 0);
}
