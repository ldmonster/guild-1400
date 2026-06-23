// Golden-vector unit tests for src/sim/buildingtype_callers.{h,cpp} — the four
// real callers of the buildingtype_recon cluster:
//   VIBE_Scene_SyncMeisterBuildings   0x504ce0
//   VIBE_Building_RegisterNames       0x504a54  (full flow)
//   VIBE_Building_CreateGebaeude      0x586fb8  (full flow)
//   VIBE_Command_ExSellObjekt         0x496b90  (storage-node phases)
//   VIBE_Bauplatz_MapAllToSupermap    0x577464
// Vectors come straight from the Hex-Rays decompile (addresses inline).
#include "tests/framework/test.h"

#include <cstring>
#include <string>
#include <vector>

#include "crt/rand.h"
#include "sim/building.h"
#include "sim/building2.h"
#include "sim/building5.h"
#include "sim/building_create.h"
#include "sim/buildingtype_callers.h"
#include "sim/buildingtype_recon.h"
#include "sim/command_apply.h"
#include "sim/entity.h"
#include "sim/trade_sell.h"
#include "util/math_random.h"

using namespace guild;
using namespace guild::sim;

namespace {

i32 RD32(const u8* p, int off) { i32 v; std::memcpy(&v, p + off, 4); return v; }
i16 RD16(const u8* p, int off) { i16 v; std::memcpy(&v, p + off, 2); return v; }
void WR32(u8* p, int off, i32 v) { std::memcpy(p + off, &v, 4); }
void WR16(u8* p, int off, u16 v) { std::memcpy(p + off, &v, 2); }

// 65-stride object type table (dword_13CE27C image) used by the flows.
u8 g_objTypeTable[65 * 512];

void ResetAll() {
    // Full clear: ResetEntityArrays only resets the marker/alive columns; the
    // caller flows also read kind/sub-state bytes and the +5 name fields.
    std::memset(static_cast<void*>(g_persons), 0, sizeof(Person) * kPersonCapacity);
    std::memset(static_cast<void*>(g_objects), 0, sizeof(ObjectRec) * kObjectCapacity);
    ResetEntityArrays();
    ResetBuildings();
    ResetBuildingCreate();
    SetBuildingCallerHooks(nullptr);
    SetStorageRoomHooks(nullptr);
    SetBauplatzMapHooks(nullptr);
    SetBauplatzTable(nullptr, 0);
    TradeSetStoragePhase(nullptr);
    TradeSetCmdHook(nullptr);
    SetSceneSyncFlagsWord(0);
    std::memset(g_objTypeTable, 0, sizeof(g_objTypeTable));
    BuildingArrayBindings b{};
    b.objectTypeBase = g_objTypeTable;
    SetBuildingArrayBindings(b);
    g_lastTradeId = 0;
}

// Recording hook surface for the caller flows.
struct RecHooks : BuildingCallerHooks {
    // SyncMeisterBuildings leaves
    int switchCalls = 0;
    int recalcCalls = 0;
    std::vector<int> statsSlots;
    bool cityAlive[4] = {false, false, false, false};
    Person* syncedAnchor = reinterpret_cast<Person*>(this);  // sentinel
    int syncCalls = 0;

    void SwitchActiveSlot() override { ++switchCalls; }
    void RecalcAllProduction() override { ++recalcCalls; }
    bool CityAlive(int slot) override {
        return slot >= 0 && slot < 4 && cityAlive[slot];
    }
    void ComputeSlotStats(int slot) override { statsSlots.push_back(slot); }
    void SyncMasterShopObjects(Person* a) override { syncedAnchor = a; ++syncCalls; }

    // RegisterNames iteration: walk a caller-provided ObjectRec list.
    std::vector<ObjectRec*> meisterRecs;
    size_t meisterCursor = 0;
    int queryBeginCalls = 0;
    ObjectRec* MeisterQueryBegin() override {
        ++queryBeginCalls;
        meisterCursor = 0;
        return MeisterIterNext();
    }
    ObjectRec* MeisterIterNext() override {
        if (meisterCursor >= meisterRecs.size())
            return nullptr;
        return meisterRecs[meisterCursor++];
    }

    // Name tables
    const char* defaultShopName = "";
    const char* defaultBuildingName = "";
    const char* primaryName = nullptr;
    const char* templates[12] = {};
    const char* DefaultShopName() override { return defaultShopName; }
    const char* DefaultBuildingName() override { return defaultBuildingName; }
    const char* PrimaryName(u8) override { return primaryName; }
    const char* NameTemplate(u8, int i) override {
        return (i >= 0 && i < 12) ? templates[i] : nullptr;
    }

    // Scene-graph leaves
    struct AddCall { i32 parentId; i16 proto; i32 count; void* parent; };
    std::vector<AddCall> addCalls;
    u8 addNode[64] = {};
    bool addReturnsNode = true;
    u8* AddObjekt(i32 parentId, i16 proto, i32 count, void* parent) override {
        addCalls.push_back({parentId, proto, count, parent});
        return addReturnsNode ? addNode : nullptr;
    }
    i32 storageNodeWord = -1;
    i32 QueryStorageNodeTypeWord(u8*) override { return storageNodeWord; }
    i32 storableWord = -1;
    i32 StorableObjectTypeWord(u8*) override { return storableWord; }

    // Sell removal leaves
    struct RemCall { i16 proto; bool storage; };
    std::vector<RemCall> remCalls;
    i32 RemoveStorageRoom(u8*, i16 proto, const u8*) override {
        remCalls.push_back({proto, true});
        return 0;
    }
    i32 RemoveByProt(u8*, i16 proto, const u8*) override {
        remCalls.push_back({proto, false});
        return 0;
    }
};

// Recorder for the recon AllocStorageRoom scene hooks (IStorageRoomHooks).
struct AllocRec : IStorageRoomHooks {
    struct Call { i32 parentId; i16 proto; int flag; void* parent; };
    std::vector<Call> calls;
    u8 node[64] = {};
    bool allow = true;
    bool returnNode = true;
    bool CanAllocate() override { return allow; }
    void* AddObjekt(i32 parentId, i16 proto, int flag, void* parent) override {
        calls.push_back({parentId, proto, flag, parent});
        return returnNode ? node : nullptr;
    }
};

}  // namespace

// ===========================================================================
// Building_IsNameTaken169 — the shared 169-stride clash scan (0x504b65).
// ===========================================================================
TEST(BtCallersNameScan, DeadSlotsNeverMatch) {
    ResetAll();
    std::strcpy(reinterpret_cast<char*>(g_objects[3].pad5), "Mill");  // name @+5
    g_objects[3].alive = 0;                       // dead slot -> skipped
    CHECK(!Building_IsNameTaken169("Mill"));
}

TEST(BtCallersNameScan, AliveExactMatchIsTaken) {
    ResetAll();
    g_objects[3].alive = 7;
    std::strcpy(reinterpret_cast<char*>(g_objects[3].pad5), "Mill");
    CHECK(Building_IsNameTaken169("Mill"));       // StrCmp == 0 -> broke early
    CHECK(!Building_IsNameTaken169("mill"));      // case-SENSITIVE (StrCmp)
    CHECK(!Building_IsNameTaken169("Mills"));
}

// ===========================================================================
// Building_RegisterNames (0x504a54 full flow).
// ===========================================================================
TEST(BtCallersRegNames, DefaultStampedAndKind28Skipped) {
    ResetAll();
    g_buildingTypesLoaded = true;
    g_buildingTypes[2].kind = 1;                  // normal type
    g_buildingTypes[9].kind = 28;                 // *(13CE294 + 589*t) == 28 -> skip
    RecHooks h;
    h.defaultShopName = "Laden";
    g_objects[0].alive = 2;
    g_objects[1].alive = 9;
    std::strcpy(reinterpret_cast<char*>(g_objects[1].pad5), "Keep");
    h.meisterRecs = {&g_objects[0], &g_objects[1]};
    SetBuildingCallerHooks(&h);

    Building_RegisterNames();

    CHECK_EQ(std::string("Laden"),
             std::string(reinterpret_cast<char*>(g_objects[0].pad5)));
    CHECK_EQ(std::string("Keep"),                 // kind-28 record untouched
             std::string(reinterpret_cast<char*>(g_objects[1].pad5)));
    CHECK_EQ(2, h.queryBeginCalls);               // pass 1 + pass 2
    SetBuildingCallerHooks(nullptr);
}

TEST(BtCallersRegNames, SingleSurvivorStored) {
    ResetAll();
    g_buildingTypesLoaded = true;
    g_buildingTypes[2].kind = 1;
    RecHooks h;
    h.defaultShopName = "Laden";
    h.templates[0] = "Goldene Waage";             // sole candidate -> pick 0
    g_objects[0].alive = 2;
    h.meisterRecs = {&g_objects[0]};
    SetBuildingCallerHooks(&h);

    Building_RegisterNames();

    CHECK_EQ(std::string("Goldene Waage"),
             std::string(reinterpret_cast<char*>(g_objects[0].pad5)));
    SetBuildingCallerHooks(nullptr);
}

TEST(BtCallersRegNames, TakenAndTooLongFiltered) {
    ResetAll();
    g_buildingTypesLoaded = true;
    g_buildingTypes[2].kind = 1;
    RecHooks h;
    h.defaultShopName = "Laden";
    static const char kLong[] = "0123456789012345678901234567890123456789";  // >= 0x20
    h.templates[0] = "Taken";                     // in use -> dropped (0x504b83)
    h.templates[1] = kLong;                       // len >= 0x20 -> dropped (0x504bb8)
    h.templates[2] = "Frei";                      // sole survivor
    g_objects[0].alive = 2;
    g_objects[5].alive = 1;                       // a live record owning "Taken"
    std::strcpy(reinterpret_cast<char*>(g_objects[5].pad5), "Taken");
    h.meisterRecs = {&g_objects[0]};
    SetBuildingCallerHooks(&h);

    Building_RegisterNames();

    CHECK_EQ(std::string("Frei"),
             std::string(reinterpret_cast<char*>(g_objects[0].pad5)));
    SetBuildingCallerHooks(nullptr);
}

TEST(BtCallersRegNames, NoSurvivorKeepsDefault) {
    ResetAll();
    g_buildingTypesLoaded = true;
    g_buildingTypes[2].kind = 1;
    RecHooks h;
    h.defaultShopName = "Laden";
    h.templates[0] = "Taken";
    g_objects[0].alive = 2;
    g_objects[5].alive = 1;
    std::strcpy(reinterpret_cast<char*>(g_objects[5].pad5), "Taken");
    h.meisterRecs = {&g_objects[0]};
    SetBuildingCallerHooks(&h);

    Building_RegisterNames();
    CHECK_EQ(std::string("Laden"),                // default template kept
             std::string(reinterpret_cast<char*>(g_objects[0].pad5)));
    SetBuildingCallerHooks(nullptr);
}

TEST(BtCallersRegNames, UniformPickMatchesRandomModulo) {
    ResetAll();
    g_buildingTypesLoaded = true;
    g_buildingTypes[2].kind = 1;
    const char* cand[3] = {"Aaa", "Bbb", "Ccc"};

    crt::Srand(1234u);
    int expected = util::RandomModulo(static_cast<u8>(3));  // 0x504c23 vector
    CHECK(expected >= 0 && expected < 3);

    RecHooks h;
    h.defaultShopName = "Laden";
    h.templates[0] = cand[0];
    h.templates[1] = cand[1];
    h.templates[2] = cand[2];
    g_objects[0].alive = 2;
    h.meisterRecs = {&g_objects[0]};
    SetBuildingCallerHooks(&h);

    crt::Srand(1234u);                            // replay the same RNG stream
    Building_RegisterNames();
    CHECK_EQ(std::string(cand[expected]),
             std::string(reinterpret_cast<char*>(g_objects[0].pad5)));
    SetBuildingCallerHooks(nullptr);
}

// ===========================================================================
// Scene_SyncMeisterBuildings (0x504ce0 full flow).
// ===========================================================================
namespace {
u8* PersonByte(int idx) { return reinterpret_cast<u8*>(&g_persons[idx]); }
}

TEST(BtCallersSync, AnchorsOverwriteUntilBothFoundAndShopList) {
    ResetAll();
    SetSceneSyncFlagsWord(0x40);                  // skip recalc + RegisterNames
    RecHooks h;
    SetBuildingCallerHooks(&h);

    // kind byte @+2 (byte_12CE912), sub-state byte @+12 (HIBYTE(dword_12CE919)).
    PersonByte(0)[2] = 12; PersonByte(0)[12] = 0;   // anchorA candidate #1
    PersonByte(3)[2] = 12; PersonByte(3)[12] = 0;   // OVERWRITES anchorA (binary!)
    PersonByte(5)[2] = 12; PersonByte(5)[12] = 1;   // anchorB -> v5 == 3, scan stops
    PersonByte(10)[2] = 12; PersonByte(10)[12] = 0; // after stop: must NOT overwrite
    PersonByte(7)[2] = 11;                          // shop list entries
    PersonByte(9)[2] = 11;

    Scene_SyncMeisterBuildings();

    CHECK_EQ(1, h.switchCalls);                   // 0x504cf7 always
    CHECK_EQ(0, h.recalcCalls);                   // flags & 0x40 -> skipped
    CHECK(MeisterAnchorA() == &g_persons[3]);     // overwrite semantics
    CHECK(MeisterAnchorB() == &g_persons[5]);
    CHECK_EQ(2, MeisterShopCount());
    CHECK(MeisterShopAt(0) == &g_persons[7]);
    CHECK(MeisterShopAt(1) == &g_persons[9]);
    CHECK_EQ(1, h.syncCalls);                     // 0x504db5 tail
    CHECK(h.syncedAnchor == &g_persons[3]);
    SetBuildingCallerHooks(nullptr);
}

TEST(BtCallersSync, CitySlotStatsDoWhile) {
    ResetAll();
    SetSceneSyncFlagsWord(0);
    RecHooks h;
    h.cityAlive[0] = true;
    h.cityAlive[1] = true;
    h.cityAlive[2] = true;                        // slot 3 dead -> loop stops
    SetBuildingCallerHooks(&h);

    Scene_SyncMeisterBuildings();

    CHECK_EQ(1, h.recalcCalls);                   // flags clear -> 0x504db6 runs
    CHECK_EQ(3u, h.statsSlots.size());            // ComputeSlotStats(0,1,2)
    CHECK_EQ(0, h.statsSlots[0]);
    CHECK_EQ(1, h.statsSlots[1]);
    CHECK_EQ(2, h.statsSlots[2]);
    SetBuildingCallerHooks(nullptr);
}

TEST(BtCallersSync, RegisterNamesGatedOnFlagBit2) {
    ResetAll();
    RecHooks h;
    SetBuildingCallerHooks(&h);

    SetSceneSyncFlagsWord(4);                     // bit 2 set -> names skipped
    Scene_SyncMeisterBuildings();
    CHECK_EQ(0, h.queryBeginCalls);               // RegisterNames never iterated

    SetSceneSyncFlagsWord(0);                     // both bits clear -> names run
    Scene_SyncMeisterBuildings();
    CHECK_EQ(2, h.queryBeginCalls);               // pass 1 + pass 2 (0x504da1)
    SetBuildingCallerHooks(nullptr);
}

// ===========================================================================
// Building_CreateGebaeudeFlow (0x586fb8 full flow).
// ===========================================================================
TEST(BtCallersCreateGeb, NullWhenTypeTableUnloaded) {
    ResetAll();
    g_buildingTypesLoaded = false;                // dword_13CE294 == 0
    CHECK(Building_CreateGebaeudeFlow(5, 0) == nullptr);
}

TEST(BtCallersCreateGeb, FixedFieldStampGolden) {
    ResetAll();
    g_buildingTypesLoaded = true;
    g_buildingNextId = 41;                        // dword_649890
    RecHooks h;
    SetBuildingCallerHooks(&h);

    u8* rec = Building_CreateGebaeudeFlow(50, 7); // prot 50: no switch-case writes
    CHECK(rec != nullptr);
    CHECK(rec == reinterpret_cast<u8*>(&g_objects[0]));  // first free slot
    CHECK_EQ(50, rec[0]);                         // *v3 = prot
    CHECK_EQ(41, RD32(rec, 1));                   // id = dword_649890
    CHECK_EQ(42, g_buildingNextId);               // post-increment
    CHECK_EQ(0, RD16(rec, 41));                   // [esi+29h] = 0
    CHECK_EQ(0, rec[47]);                         // [esi+2Fh] = 0
    CHECK_EQ(32000, RD32(rec, 57));               // [esi+39h] = 0x7D00
    CHECK_EQ(100, RD32(rec, 61));
    CHECK_EQ(2, RD32(rec, 65));
    CHECK_EQ(100, RD32(rec, 69));
    CHECK_EQ(0x3F800000, RD32(rec, 73));          // 1.0f
    CHECK_EQ(100, rec[92]);
    CHECK_EQ(-1, RD32(rec, 149));
    CHECK_EQ(0, RD32(rec, 43));                   // ecx == 0 stores
    CHECK_EQ(0, RD32(rec, 93));
    CHECK_EQ(0, RD32(rec, 97));
    CHECK_EQ(0, RD32(rec, 48));
    CHECK_EQ(7, static_cast<u16>(RD16(rec, 37))); // owner words
    CHECK_EQ(7, static_cast<u16>(RD16(rec, 39)));
    CHECK_EQ(1, rec[153]);                        // grey-0 fill then |= 1
    CHECK_EQ(-1, RD32(rec, 165));                 // [esi+0A5h] = -1
    bool zeroed = true;
    for (int i = 101; i < 149; ++i)
        if (rec[i] != 0) zeroed = false;
    CHECK(zeroed);                                // rep-stos of +101..+148
    SetBuildingCallerHooks(nullptr);
}

TEST(BtCallersCreateGeb, NameFallbackAndPrimary) {
    ResetAll();
    g_buildingTypesLoaded = true;
    RecHooks h;
    h.defaultBuildingName = "Gebaeude";           // unk_62661C image
    SetBuildingCallerHooks(&h);

    // candidate[0] empty -> fallback name used (0x58740c).
    u8* rec = Building_CreateGebaeudeFlow(50, 0xFFFF);
    CHECK_EQ(std::string("Gebaeude"),
             std::string(reinterpret_cast<char*>(rec + 5)));

    // candidate[0] present but TAKEN -> primary name survives (no survivors).
    ResetAll();
    g_buildingTypesLoaded = true;
    RecHooks h2;
    h2.primaryName = "Haus Maier";                // dword_8C4788[14*prot]
    h2.templates[0] = "Altstadt";
    g_objects[10].alive = 1;                      // "Altstadt" already in use
    std::strcpy(reinterpret_cast<char*>(g_objects[10].pad5), "Altstadt");
    SetBuildingCallerHooks(&h2);
    rec = Building_CreateGebaeudeFlow(50, 0xFFFF);
    CHECK_EQ(std::string("Haus Maier"),
             std::string(reinterpret_cast<char*>(rec + 5)));
    SetBuildingCallerHooks(nullptr);
}

TEST(BtCallersCreateGeb, NamePickUsesRandNextModCount) {
    ResetAll();
    g_buildingTypesLoaded = true;
    RecHooks h;
    h.primaryName = "Haus";
    h.templates[0] = "Eins";
    h.templates[1] = "Zwei";
    SetBuildingCallerHooks(&h);

    crt::Srand(99u);
    int expected = crt::RandNext() % static_cast<u16>(2);   // 0x58728a vector
    const char* names[2] = {"Eins", "Zwei"};

    crt::Srand(99u);
    u8* rec = Building_CreateGebaeudeFlow(50, 0xFFFF);
    CHECK_EQ(std::string(names[expected]),
             std::string(reinterpret_cast<char*>(rec + 5)));
    SetBuildingCallerHooks(nullptr);
}

TEST(BtCallersCreateGeb, RoomSlotLoopAllocatesStorageRooms) {
    ResetAll();
    g_buildingTypesLoaded = true;
    g_buildingNextId = 90;
    // Object type table: proto 300 -> type 2 (storage), 310 -> 6 (market),
    // 320 -> 0 (plain object).
    g_objTypeTable[65 * 300] = 2;
    g_objTypeTable[65 * 310] = 6;
    g_objTypeTable[65 * 320] = 0;

    // Room list @ typeRec+35: hi-bit (0x8000) slots spawn sub-objects.
    g_buildingTypes[60].roomList[0] = 0x8000 | 300;   // -> AllocStorageRoom(300)
    g_buildingTypes[60].roomList[1] = 0x8000 | 310;   // -> AllocStorageRoom(310)
    g_buildingTypes[60].roomList[2] = 0x8000 | 320;   // v52 already set -> nothing
    g_buildingTypes[60].roomList[3] = 0;              // terminator

    RecHooks h;
    SetBuildingCallerHooks(&h);
    AllocRec ar;
    SetStorageRoomHooks(&ar);

    u8* rec = Building_CreateGebaeudeFlow(60, 0xFFFF);
    CHECK(rec != nullptr);
    CHECK_EQ(2u, ar.calls.size());                // the two storage slots
    CHECK_EQ(90, ar.calls[0].parentId);           // building id (rec+1)
    CHECK_EQ(300, ar.calls[0].proto);             // word & 0x7FFF
    CHECK_EQ(1, ar.calls[0].flag);
    CHECK(ar.calls[0].parent == rec);             // parent node == record
    CHECK_EQ(310, ar.calls[1].proto);
    CHECK_EQ(0u, h.addCalls.size());              // no plain AddObjekt issued
    SetStorageRoomHooks(nullptr);
    SetBuildingCallerHooks(nullptr);
}

TEST(BtCallersCreateGeb, RoomSlotPlainObjektOnlyWhileNoStorageSeen) {
    ResetAll();
    g_buildingTypesLoaded = true;
    g_buildingNextId = 7;
    g_objTypeTable[65 * 320] = 0;                 // plain
    g_objTypeTable[65 * 300] = 2;                 // storage
    g_objTypeTable[65 * 330] = 0;                 // plain (after storage seen)
    g_buildingTypes[61].roomList[0] = 0x8000 | 320;   // !v52 -> AddObjekt
    g_buildingTypes[61].roomList[1] = 0x8000 | 300;   // -> AllocStorageRoom, v52=1
    g_buildingTypes[61].roomList[2] = 0x8000 | 330;   // v52 -> suppressed
    g_buildingTypes[61].roomList[3] = 0;

    RecHooks h;
    SetBuildingCallerHooks(&h);
    AllocRec ar;
    SetStorageRoomHooks(&ar);

    u8* rec = Building_CreateGebaeudeFlow(61, 0xFFFF);
    CHECK(rec != nullptr);
    CHECK_EQ(1u, h.addCalls.size());              // only the pre-storage slot
    CHECK_EQ(320, h.addCalls[0].proto);
    CHECK_EQ(7, h.addCalls[0].parentId);
    CHECK_EQ(1, h.addCalls[0].count);
    CHECK_EQ(1u, ar.calls.size());
    CHECK_EQ(300, ar.calls[0].proto);
    SetStorageRoomHooks(nullptr);
    SetBuildingCallerHooks(nullptr);
}

TEST(BtCallersCreateGeb, StorageNodeWordAndTypeDefaults) {
    ResetAll();
    g_buildingTypesLoaded = true;
    RecHooks h;
    h.storageNodeWord = 270;                      // QueryFind(.,1,4,2) result word
    SetBuildingCallerHooks(&h);

    // prot 71: switch writes +48 = 80000 (0x58756c).
    u8* rec = Building_CreateGebaeudeFlow(71, 0xFFFF);
    CHECK_EQ(270, RD16(rec, 41));                 // *(rec+41) = *v42
    CHECK_EQ(80000, RD32(rec, 48));

    // prot 32: +48 = 100, +73 = 0.8f bits (0x58752a/0x587531).
    ResetAll();
    g_buildingTypesLoaded = true;
    RecHooks h2;
    SetBuildingCallerHooks(&h2);
    rec = Building_CreateGebaeudeFlow(32, 0xFFFF);
    CHECK_EQ(100, RD32(rec, 48));
    CHECK_EQ(1061997773, RD32(rec, 73));

    // prot 38: LABEL_52 epilogue word +41 = 270 (0x5873e1).
    ResetAll();
    g_buildingTypesLoaded = true;
    RecHooks h3;
    SetBuildingCallerHooks(&h3);
    rec = Building_CreateGebaeudeFlow(38, 0xFFFF);
    CHECK_EQ(270, RD16(rec, 41));
    SetBuildingCallerHooks(nullptr);
}

TEST(BtCallersCreateGeb, IsProductionKindSeedsStock5000) {
    ResetAll();
    g_buildingTypesLoaded = true;
    g_buildingTypes[50].kind = 11;                // production kind {11,12,13,16,28}
    RecHooks h;
    SetBuildingCallerHooks(&h);
    u8* rec = Building_CreateGebaeudeFlow(50, 0xFFFF);
    CHECK_EQ(5000, RD32(rec, 48));                // *((DWORD*)v4+12) = 5000
    SetBuildingCallerHooks(nullptr);
}

TEST(BtCallersCreateGeb, Label71AddObjekt437StampsNode) {
    ResetAll();
    g_buildingTypesLoaded = true;
    g_buildingNextId = 12;
    RecHooks h;
    SetBuildingCallerHooks(&h);

    u8* rec = Building_CreateGebaeudeFlow(20, 0xFFFF);   // prot 20 -> LABEL_71
    CHECK(rec != nullptr);
    CHECK_EQ(1u, h.addCalls.size());
    CHECK_EQ(437, h.addCalls[0].proto);           // AddObjekt(.., 437, 1, rec)
    CHECK_EQ(12, h.addCalls[0].parentId);
    CHECK_EQ(0xFF, h.addNode[28]);                // *(v45+28) = -1
    CHECK_EQ(0xFF, h.addNode[29]);                // *(v45+29) = -1
    CHECK_EQ(-1, RD32(h.addNode, 30));            // *(v45+30) = -1
    CHECK_EQ(0, h.addNode[34]);                   // *(v45+34) = 0
    SetBuildingCallerHooks(nullptr);
}

// gilde.exe 0x5874c0: prot 21 (0x15) reaches LABEL_71 via `cmp bl,15h; jnb
// 5874C0` then `jbe loc_5874D9` (prot == 0x15).  Previously dropped; verify it
// now triggers the AddObjekt(437) + node stamp exactly like prot 20 / 22.
TEST(BtCallersCreateGeb, Label71Prot21AlsoStampsNode) {
    ResetAll();
    g_buildingTypesLoaded = true;
    g_buildingNextId = 12;
    RecHooks h;
    SetBuildingCallerHooks(&h);

    u8* rec = Building_CreateGebaeudeFlow(21, 0xFFFF);   // prot 21 -> LABEL_71
    CHECK(rec != nullptr);
    CHECK_EQ(1u, h.addCalls.size());
    CHECK_EQ(437, h.addCalls[0].proto);
    CHECK_EQ(0xFF, h.addNode[28]);
    CHECK_EQ(0xFF, h.addNode[29]);
    CHECK_EQ(-1, RD32(h.addNode, 30));
    CHECK_EQ(0, h.addNode[34]);
    SetBuildingCallerHooks(nullptr);
}

// gilde.exe 0x5874d0: prot 22 (0x16) reaches LABEL_71 by falling past
// `cmp bl,16h; ja 5873DB` (22 is not > 0x16) to loc_5874D9.
TEST(BtCallersCreateGeb, Label71Prot22AlsoStampsNode) {
    ResetAll();
    g_buildingTypesLoaded = true;
    g_buildingNextId = 12;
    RecHooks h;
    SetBuildingCallerHooks(&h);

    u8* rec = Building_CreateGebaeudeFlow(22, 0xFFFF);   // prot 22 -> LABEL_71
    CHECK(rec != nullptr);
    CHECK_EQ(1u, h.addCalls.size());
    CHECK_EQ(437, h.addCalls[0].proto);
    CHECK_EQ(0xFF, h.addNode[28]);
    SetBuildingCallerHooks(nullptr);
}

// ===========================================================================
// ExSellObjekt storage-node phases (0x496b90).
// ===========================================================================
TEST(BtCallersSell, SourcePhaseSkipsWhenNoSrcId) {
    ResetAll();
    SellResolve r{};
    r.srcIdPresent = false;
    CHECK(Sell_DepleteSourceStockNode(r, 5));
}

TEST(BtCallersSell, SourceDecrementWithoutDepletion) {
    ResetAll();
    RecHooks h;
    SetBuildingCallerHooks(&h);
    u8 node[32] = {};
    WR32(node, 14, 10);
    SellResolve r{};
    r.proto = 320;
    r.srcStockNodePtr = node;
    CHECK(Sell_DepleteSourceStockNode(r, 3));
    CHECK_EQ(7, RD32(node, 14));                  // *(v56+14) -= qty
    CHECK_EQ(0u, h.remCalls.size());              // v20 > 0 -> no removal
    SetBuildingCallerHooks(nullptr);
}

TEST(BtCallersSell, SourceDepletedStorageRemovesRoomOrRejects) {
    ResetAll();
    g_objTypeTable[65 * 300] = 2;                 // storage object type
    RecHooks h;
    SetBuildingCallerHooks(&h);
    u8 node[32] = {};
    WR32(node, 14, 3);
    u8 building[169] = {};
    SellResolve r{};
    r.proto = 300;
    r.srcStockNodePtr = node;
    r.srcBuildingRec = building;
    CHECK(Sell_DepleteSourceStockNode(r, 3));     // count -> 0 -> removal
    CHECK_EQ(1u, h.remCalls.size());
    CHECK(h.remCalls[0].storage);                 // 0x49739b RemoveStorageRoom
    CHECK_EQ(300, h.remCalls[0].proto);

    // 0x49736c: storage type but no resolved source building -> return 1.
    u8 node2[32] = {};
    WR32(node2, 14, 2);
    SellResolve r2{};
    r2.proto = 300;
    r2.srcStockNodePtr = node2;
    r2.srcBuildingRec = nullptr;
    CHECK(!Sell_DepleteSourceStockNode(r2, 2));
    SetBuildingCallerHooks(nullptr);
}

TEST(BtCallersSell, SourceDepletedPlainUsesRemoveByProt) {
    ResetAll();
    g_objTypeTable[65 * 320] = 0;
    RecHooks h;
    SetBuildingCallerHooks(&h);
    u8 node[32] = {};
    WR32(node, 14, 1);
    SellResolve r{};
    r.proto = 320;
    r.srcStockNodePtr = node;
    CHECK(Sell_DepleteSourceStockNode(r, 4));     // -3 <= 0 -> removal
    CHECK_EQ(1u, h.remCalls.size());
    CHECK(!h.remCalls[0].storage);                // 0x49738b RemoveByProt
    SetBuildingCallerHooks(nullptr);
}

TEST(BtCallersSell, DestExistingNodeGainsQtyAndLatches) {
    ResetAll();
    u8 node[32] = {};
    WR32(node, 14, 5);
    WR32(node, 2, 7777);                          // node id @+2
    SellResolve r{};
    r.proto = 320;
    r.destStockNodePtr = node;
    CHECK(Sell_EnsureDestStorageNode(r, 4));
    CHECK_EQ(9, RD32(node, 14));                  // 0x496f69 *(v55+14) += qty
    CHECK_EQ(7777, r.lastDestNodeId);             // LABEL_58 latch
    CHECK_EQ(7777, g_lastTradeId);                // dword_631290
}

TEST(BtCallersSell, DestEnsureStorageAllocatesRoom) {
    ResetAll();
    g_objTypeTable[65 * 300] = 2;
    AllocRec ar;
    WR32(ar.node, 2, 4242);                       // the new node's id @+2
    SetStorageRoomHooks(&ar);
    ObjectRec building{};
    building.alive = 9;
    building.id = 555;
    u8 srcStorage[16] = {};
    SellResolve r{};
    r.proto = 300;
    r.destBuildingRec = reinterpret_cast<u8*>(&building);
    r.srcStorageRec = srcStorage;
    CHECK(Sell_EnsureDestStorageNode(r, 6));      // 0x4974af AllocStorageRoom
    CHECK_EQ(1u, ar.calls.size());
    CHECK_EQ(555, ar.calls[0].parentId);          // building->id
    CHECK_EQ(300, ar.calls[0].proto);
    CHECK(ar.calls[0].parent == srcStorage);      // parent node == v4
    CHECK_EQ(4242, r.lastDestNodeId);
    CHECK_EQ(4242, g_lastTradeId);

    // 0x497408: storage type but no dest building -> return 1.
    SellResolve r2{};
    r2.proto = 300;
    CHECK(!Sell_EnsureDestStorageNode(r2, 6));

    // 0x4974b8: AllocStorageRoom guard-fails -> return 1.
    ar.allow = false;
    SellResolve r3{};
    r3.proto = 300;
    r3.destBuildingRec = reinterpret_cast<u8*>(&building);
    CHECK(!Sell_EnsureDestStorageNode(r3, 6));
    SetStorageRoomHooks(nullptr);
}

TEST(BtCallersSell, DestEnsurePlainAddObjektWithStorageContainerBump) {
    ResetAll();
    g_objTypeTable[65 * 200] = 23;                // kind 23 (0x4974de gate)
    RecHooks h;
    WR32(h.addNode, 14, 0);
    WR32(h.addNode, 2, 31);
    SetBuildingCallerHooks(&h);

    u8 destStorage[16] = {};
    WR16(destStorage, 0, 42);                     // *v57 == 42
    u8 owner[16] = {};
    owner[2] = 6;                                 // v3 kind byte 6
    u8 srcStorage[8] = {};

    SellResolve r{};
    r.proto = 200;
    r.destId = 808;
    r.destStorageRec = destStorage;
    r.destOwnerRec = owner;
    r.srcStorageRec = srcStorage;
    CHECK(Sell_EnsureDestStorageNode(r, 9));
    CHECK_EQ(1u, h.addCalls.size());              // 0x49742a AddObjekt
    CHECK_EQ(808, h.addCalls[0].parentId);
    CHECK_EQ(200, h.addCalls[0].proto);
    CHECK_EQ(9, h.addCalls[0].count);             // qty (a1+31)
    CHECK_EQ(1, RD32(h.addNode, 14));             // 0x497464 count + 1
    CHECK_EQ(4, RD32(h.addNode, 28));             // *((DWORD*)v55+7) = 4
    CHECK_EQ(31, g_lastTradeId);

    // Without the dest storage container (v57 == 0): no bump, no stamp.
    RecHooks h2;
    WR32(h2.addNode, 14, 0);
    WR32(h2.addNode, 2, 32);
    SetBuildingCallerHooks(&h2);
    SellResolve r2{};
    r2.proto = 200;
    r2.destId = 808;
    CHECK(Sell_EnsureDestStorageNode(r2, 9));
    CHECK_EQ(0, RD32(h2.addNode, 14));
    CHECK_EQ(0, RD32(h2.addNode, 28));

    // AddObjekt failure -> return 1 (0x49743a).
    RecHooks h3;
    h3.addReturnsNode = false;
    SetBuildingCallerHooks(&h3);
    SellResolve r3{};
    r3.proto = 200;
    r3.destId = 808;
    CHECK(!Sell_EnsureDestStorageNode(r3, 9));
    SetBuildingCallerHooks(nullptr);
}

TEST(BtCallersSell, PhasesRunInsideTradeSellObjektResolve) {
    ResetAll();
    static SellStoragePhase1to1 phase;
    TradeSetStoragePhase(&phase);

    u8 srcNode[32] = {};
    WR32(srcNode, 14, 10);
    u8 destNode[32] = {};
    WR32(destNode, 14, 2);
    WR32(destNode, 2, 616);

    SellResolve r{};
    r.proto = 320;
    r.qty = 4;
    r.srcHasStock = true;
    r.srcRawCount = 10;
    r.srcStockNodePtr = srcNode;
    r.destStockNodePtr = destNode;

    i32 moved = TradeSellObjektResolve(r, /*commit=*/true);
    CHECK_EQ(4, moved);
    CHECK_EQ(6, RD32(srcNode, 14));               // source phase decremented
    CHECK_EQ(6, RD32(destNode, 14));              // dest phase incremented
    CHECK_EQ(616, g_lastTradeId);                 // LABEL_58 latch

    // Reject propagation: depleted storage source w/o building -> resolve == 0.
    g_objTypeTable[65 * 300] = 2;
    u8 srcNode2[32] = {};
    WR32(srcNode2, 14, 3);
    SellResolve r2{};
    r2.proto = 300;
    r2.qty = 3;
    r2.srcHasStock = true;
    r2.srcRawCount = 3;
    r2.srcStockNodePtr = srcNode2;
    CHECK_EQ(0, TradeSellObjektResolve(r2, true));
    TradeSetStoragePhase(nullptr);
}

// ===========================================================================
// Bauplatz_MapAllToSupermap (0x577464).
// ===========================================================================
namespace {
// A fake plot scene node: name at +0 ("bk_*"), zeroed frame/floats elsewhere.
struct PlotNode {
    u8 bytes[600];
    explicit PlotNode(const char* name) {
        std::memset(bytes, 0, sizeof(bytes));
        std::strcpy(reinterpret_cast<char*>(bytes), name);
    }
};

struct WalkHooks : Building5Hooks {
    std::vector<const u8*> nodes;
    const u8* SceneNode(int index) override {
        return (index >= 0 && index < static_cast<int>(nodes.size()))
                   ? nodes[static_cast<size_t>(index)]
                   : nullptr;
    }
};

struct MapRecorder : IBauplatzMapHooks {
    std::vector<int> fills;
    std::vector<int> ctxs;
    int rasterReturn = 1;
    int RasterizeBauplatzEdge(int mapCtx, const float quad[8], int fill) override {
        (void)quad;
        ctxs.push_back(mapCtx);
        fills.push_back(fill);
        return rasterReturn;
    }
};
}  // namespace

TEST(BtCallersMapAll, EmptySceneReturnsZeroAndMapsNothing) {
    ResetAll();
    SetBuilding5Hooks(nullptr);                   // inert walk: no nodes
    MapRecorder mr;
    SetBauplatzMapHooks(&mr);
    CHECK_EQ(0, Bauplatz_MapAllToSupermap(3));    // filter count 0 -> jle exit
    CHECK_EQ(0u, mr.fills.size());
    SetBauplatzMapHooks(nullptr);
}

TEST(BtCallersMapAll, MapsEachSurvivingPlotWithFill255) {
    ResetAll();
    static PlotNode a("bk_markt");
    static PlotNode b("bk_dom");
    static WalkHooks wh;
    wh.nodes = {a.bytes, b.bytes};
    SetBuilding5Hooks(&wh);

    // The plot size table: both names resolve (the node POINTER is the name).
    static BauplatzSizeRec table[2];
    std::memset(table, 0, sizeof(table));
    std::strcpy(table[0].name, "bk_markt");
    std::strcpy(table[1].name, "bk_dom");
    SetBauplatzTable(table, 2);

    MapRecorder mr;
    mr.rasterReturn = 7;
    SetBauplatzMapHooks(&mr);

    int result = Bauplatz_MapAllToSupermap(11);
    CHECK_EQ(2u, mr.fills.size());                // one MapOne per plot
    CHECK_EQ(255, mr.fills[0]);                   // fill code 255 (0x5774e2)
    CHECK_EQ(255, mr.fills[1]);
    CHECK_EQ(11, mr.ctxs[0]);                     // eax = mapCtx forwarded
    CHECK_EQ(7, result);                          // last MapOne return in eax

    SetBauplatzMapHooks(nullptr);
    SetBuilding5Hooks(nullptr);
    SetBauplatzTable(nullptr, 0);
}

TEST(BtCallersMapAll, UnknownPlotNameYieldsZeroResult) {
    ResetAll();
    static PlotNode a("bk_unbekannt");
    static WalkHooks wh;
    wh.nodes = {a.bytes};
    SetBuilding5Hooks(&wh);
    SetBauplatzTable(nullptr, 0);                 // empty table -> GetSize miss
    MapRecorder mr;
    SetBauplatzMapHooks(&mr);

    int result = Bauplatz_MapAllToSupermap(0);
    CHECK_EQ(0u, mr.fills.size());                // nothing rasterized
    CHECK_EQ(0, result);                          // MapOne unknown-plot result
    SetBauplatzMapHooks(nullptr);
    SetBuilding5Hooks(nullptr);
}

// ===========================================================================
// Wiring (rule 13): WireBuildingCallers installs the live backends.
// ===========================================================================
TEST(BtCallersWire, CreateGebaeudeBackendInstalled) {
    ResetAll();
    g_buildingTypesLoaded = true;
    RecHooks h;
    SetBuildingCallerHooks(&h);

    WireBuildingCallers();
    // The building_create entry (used by command_apply5's opcodes 0x0A/0x4C)
    // must now run the full 0x586fb8 flow — the default backend never writes
    // the -1 dword at +165; the flow does.
    u8* rec = Building_CreateGebaeude(50, 3);
    CHECK(rec != nullptr);
    CHECK_EQ(-1, RD32(rec, 165));                 // full-flow signature field
    CHECK_EQ(32000, RD32(rec, 57));

    ResetBuildingCreate();                        // restore the default backend
    SetBuildingCallerHooks(nullptr);
}

TEST(BtCallersWire, SellStoragePhaseInstalled) {
    ResetAll();
    WireBuildingCallers();

    u8 destNode[32] = {};
    WR32(destNode, 14, 1);
    WR32(destNode, 2, 99);
    SellResolve r{};
    r.proto = 320;
    r.qty = 2;
    r.srcHasStock = true;
    r.srcRawCount = 50;
    r.destStockNodePtr = destNode;

    CHECK_EQ(2, TradeSellObjektResolve(r, true)); // commit runs the phases
    CHECK_EQ(3, RD32(destNode, 14));              // dest phase live
    CHECK_EQ(99, g_lastTradeId);                  // dword_631290 latch live

    TradeSetStoragePhase(nullptr);
    ResetBuildingCreate();
}
