#include "test.h"

// Unit tests for the building4 slice. Each non-trivial function is exercised
// against a recording-mock Building4Hooks and golden vectors derived by hand from
// the Hex-Rays decompilation. Cross-module leaves are all hooked; the recovered
// kDefaultObjectsTable is asserted against its byte-for-byte source.
#include "sim/building4.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct RecHooks : Building4Hooks {
    // QueryFind: return a programmed value per (groupA,groupB,proto); record calls.
    struct Q { std::int32_t container; int gA, gB, flag; std::int32_t proto; };
    std::vector<Q> queries;
    std::int32_t queryReturn = 0;                 // default "nothing found"
    std::vector<std::int32_t> queryScript;        // pop front per call if non-empty
    std::size_t queryIdx = 0;

    std::vector<std::int32_t> iterScript;
    std::size_t iterIdx = 0;

    std::vector<std::int32_t> addReturns;
    std::size_t addIdx = 0;
    struct Add { std::int32_t parent, proto; int gA; std::int32_t variant; };
    std::vector<Add> adds;

    std::int32_t removeReturn = 0;

    std::vector<std::pair<std::int32_t, std::uint16_t>> flagNodeCalls;
    std::int32_t flagNodeReturn = 0;

    const std::uint8_t* personStore = nullptr;
    std::vector<std::pair<int, std::int32_t>> personQueries;

    // Command record.
    std::vector<std::pair<int, int>> appendedFields;   // (fieldOff, value)
    int beginCount = 0, flushCount = 0;

    // Handler list.
    std::vector<std::uint32_t> handlerBlob;            // single handler record
    bool handlerHasMatch = false;
    int finalFlagSeen = -99;
    std::int32_t enqueueField44 = -1;
    std::int32_t enqueueBuildingId = -1;
    bool enqueued = false;

    std::int32_t GameObjectQueryFind(std::int32_t c, int gA, int gB, int flag,
                                     std::int32_t proto) override {
        queries.push_back({c, gA, gB, flag, proto});
        if (queryIdx < queryScript.size())
            return queryScript[queryIdx++];
        return queryReturn;
    }
    std::int32_t GameObjectIterNext() override {
        if (iterIdx < iterScript.size())
            return iterScript[iterIdx++];
        return 0;
    }
    std::int32_t GameObjectAddObjekt(std::int32_t parent, std::int32_t proto,
                                     int gA, std::int32_t variant) override {
        adds.push_back({parent, proto, gA, variant});
        if (addIdx < addReturns.size())
            return addReturns[addIdx++];
        return 0;
    }
    std::int32_t GameObjectRemoveByProt(std::int32_t, std::int16_t,
                                        std::int32_t) override {
        return removeReturn;
    }
    std::int32_t BuildFlagNodeList(std::int32_t obj, std::uint16_t owner) override {
        flagNodeCalls.push_back({obj, owner});
        return flagNodeReturn;
    }
    const std::uint8_t* PersonQueryByGoodType(int goodType,
                                              std::int32_t ctx) override {
        personQueries.push_back({goodType, ctx});
        return personStore;
    }
    void CommandBeginDeltaPacket(std::int32_t, std::int32_t) override { ++beginCount; }
    void CommandAppendCopiedField(std::uint32_t, std::uint32_t, const void* src,
                                  int fieldOff) override {
        int v = 0;
        if (src) std::memcpy(&v, src, sizeof(int));
        appendedFields.push_back({fieldOff, v});
    }
    void CommandQueueRequestState23() override { ++flushCount; }
    const std::uint32_t* HeFindFirstHandlerByFilter(int, int, int) override {
        return handlerBlob.empty() ? nullptr : handlerBlob.data();
    }
    const std::uint32_t* HeFindNextMatchingHandler() override { return nullptr; }
    void EnqueueBuyBuilding(std::int32_t buildingId, std::int32_t, const std::uint32_t*,
                            std::int32_t field44, std::int32_t, int finalFlag) override {
        enqueued = true;
        enqueueBuildingId = buildingId;
        enqueueField44 = field44;
        finalFlagSeen = finalFlag;
    }
};

// Build a 200-byte fake building record with the given +0 type byte, +93
// container handle, +39 owner word, +1 object id.
void MakeBuilding(std::uint8_t* rec, std::uint8_t type0, std::int32_t container,
                  std::uint16_t owner, std::int32_t objId) {
    std::memset(rec, 0, 200);
    rec[0] = type0;
    std::memcpy(rec + 1, &objId, 4);
    std::memcpy(rec + 39, &owner, 2);
    std::memcpy(rec + 93, &container, 4);
}

}  // namespace

// ---------------------------------------------------------------------------
// Recovered table integrity.
// ---------------------------------------------------------------------------
TEST(Building4Unit, DefaultObjectsTableShape) {
    // 23 records of 21 bytes.
    CHECK_EQ(kDefaultObjRecords, 23);
    CHECK_EQ(kDefaultObjStride, 21);
    // First record: type 0x29 at +3, first non-zero proto WORD 0x01D4 at +8.
    const std::uint8_t* r0 = &kDefaultObjectsTable[0];
    CHECK_EQ((int)r0[kDefaultObjTypeOff], 0x29);
    CHECK_EQ((int)(r0[8] | (r0[9] << 8)), 0x01D4);
    // Record #21: type 0x1F at +3, single proto 0x01CD at +12. (Record #22 is the
    // all-zero trailing slot used as the loop's spill guard.)
    const std::uint8_t* r21 = &kDefaultObjectsTable[21 * 21];
    CHECK_EQ((int)r21[kDefaultObjTypeOff], 0x1F);
    CHECK_EQ((int)(r21[12] | (r21[13] << 8)), 0x01CD);
}

// ---------------------------------------------------------------------------
// EnsureDefaultObjects.
// ---------------------------------------------------------------------------
TEST(Building4Unit, EnsureDefaultObjects_AddsRoomAndProtos) {
    RecHooks h; SetBuilding4Hooks(&h);
    std::uint8_t rec[200];
    MakeBuilding(rec, 0x29, /*container*/0x1111, /*owner*/0, /*objId*/0x2222);

    // Room query (proto 255) returns 0 -> AddObjekt creates room handle 7.
    // Then proto queries: record 0x29 has protos D4,55,56 (3 protos); make them
    // all "missing" so each triggers an AddObjekt.
    h.addReturns = { 7,    // room
                     100, 101, 102 };  // the three child objects
    // QueryFind returns 0 for everything (room missing + each proto missing).
    h.queryReturn = 0;

    std::int32_t r = Building4_EnsureDefaultObjects(rec);

    // Only record 0x29 matches; 1 room query + 3 proto queries = 4 QueryFind.
    CHECK_EQ((int)h.queries.size(), 4);
    // The room query asks the container for proto 255 (groups 2,6).
    CHECK_EQ((int)h.queries[0].proto, 255);
    CHECK_EQ((int)h.queries[0].container, 0x1111);
    // 1 room add + 3 child adds.
    CHECK_EQ((int)h.adds.size(), 4);
    CHECK_EQ((int)h.adds[0].proto, 255);
    CHECK_EQ((int)h.adds[0].parent, 0x2222);    // building object id
    CHECK_EQ((int)h.adds[1].proto, 0x01D4);     // first record-proto
    CHECK_EQ((int)h.adds[1].parent, 7);         // parented to the room handle
    CHECK_EQ((int)h.adds[2].proto, 0x0155);
    CHECK_EQ((int)h.adds[3].proto, 0x0156);
    CHECK_EQ((int)r, 102);                       // last add result
    SetBuilding4Hooks(nullptr);
}

TEST(Building4Unit, EnsureDefaultObjects_NoMatchNoOp) {
    RecHooks h; SetBuilding4Hooks(&h);
    std::uint8_t rec[200];
    MakeBuilding(rec, 0xFE, 0x1, 0, 0x2);   // type byte present in no record
    std::int32_t r = Building4_EnsureDefaultObjects(rec);
    CHECK_EQ((int)h.queries.size(), 0);
    CHECK_EQ((int)h.adds.size(), 0);
    CHECK_EQ((int)r, 0);
    SetBuilding4Hooks(nullptr);
}

TEST(Building4Unit, EnsureDefaultObjects_NullSafe) {
    SetBuilding4Hooks(nullptr);
    CHECK_EQ((int)Building4_EnsureDefaultObjects(nullptr), 0);
}

// ---------------------------------------------------------------------------
// InitWorkerCapacities — the min-of-2 clamp branches.
// ---------------------------------------------------------------------------
TEST(Building4Unit, InitWorkerCapacities_ClampBranch) {
    // cfg573 or cfg574 set -> clamp both to max(cfg,2).
    std::uint8_t w42[40] = {0}; std::uint8_t w278[40] = {0};
    WorkerCfg cfg; cfg.cfg573 = 5; cfg.cfg574 = 1; cfg.cfg575 = 9;
    Building4_InitWorkerCapacities(w42, w278, cfg);
    CHECK_EQ((int)w42[28], 5);   // max(5,2)
    CHECK_EQ((int)w42[29], 2);   // max(1,2)
    // 278 branch requires cfg573==0 && cfg574==0; here both nonzero -> untouched.
    CHECK_EQ((int)w278[28], 0);
}

TEST(Building4Unit, InitWorkerCapacities_FallbackBranch) {
    // cfg573==0, cfg574==0, cfg575!=0 -> 42 writes +28 = cfg575, 278 writes +28.
    std::uint8_t w42[40] = {0}; std::uint8_t w278[40] = {0};
    WorkerCfg cfg; cfg.cfg573 = 0; cfg.cfg574 = 0; cfg.cfg575 = 7;
    Building4_InitWorkerCapacities(w42, w278, cfg);
    CHECK_EQ((int)w42[28], 7);
    CHECK_EQ((int)w42[29], 0);   // not written in this branch
    CHECK_EQ((int)w278[28], 7);
}

TEST(Building4Unit, InitWorkerCapacities_NullSafe) {
    WorkerCfg cfg; cfg.cfg573 = 3;
    Building4_InitWorkerCapacities(nullptr, nullptr, cfg);   // must not crash
    CHECK(true);
}

// ---------------------------------------------------------------------------
// FindStorableObject branches.
// ---------------------------------------------------------------------------
TEST(Building4Unit, FindStorableObject_Production) {
    RecHooks h; SetBuilding4Hooks(&h);
    h.queryReturn = 555;
    // kind 11 is a production kind.
    std::int32_t r = Building4_FindStorableObject(11, 0x4242);
    CHECK_EQ((int)r, 555);
    if (!h.queries.empty()) {
        CHECK_EQ((int)h.queries[0].proto, 253);
        CHECK_EQ((int)h.queries[0].container, 0x4242);
    }
    SetBuilding4Hooks(nullptr);
}

TEST(Building4Unit, FindStorableObject_Kind4) {
    RecHooks h; SetBuilding4Hooks(&h);
    h.queryReturn = 888;
    std::int32_t r = Building4_FindStorableObject(4, 0x10);
    CHECK_EQ((int)r, 888);
    if (!h.queries.empty())
        CHECK_EQ((int)h.queries[0].proto, 84);
    SetBuilding4Hooks(nullptr);
}

TEST(Building4Unit, FindStorableObject_GenericIterate) {
    RecHooks h; SetBuilding4Hooks(&h);
    // kind 7 is neither production nor 4. First query returns the group-4 scan.
    // queryScript: first found is 253 (skip-and-iterate), iter returns 990.
    h.queryScript = { 253 };
    h.iterScript = { 990 };
    std::int32_t r = Building4_FindStorableObject(7, 0x33);
    CHECK_EQ((int)r, 990);
    SetBuilding4Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// FindUpgradeStorage — category 3 / 5 branches and the class-byte switch.
// ---------------------------------------------------------------------------
TEST(Building4Unit, FindUpgradeStorage_Cat3) {
    RecHooks h; SetBuilding4Hooks(&h);
    std::uint8_t store[200]; std::memset(store, 0, sizeof store);
    std::int32_t sc = 0x7777; std::memcpy(store + 93, &sc, 4);
    h.personStore = store;
    h.queryReturn = 321;
    // kind 1 maps to category 3 (Building_MapKindToCategory).
    std::int32_t r = Building4_FindUpgradeStorage(/*kind*/1, /*class*/0, /*ctx*/9);
    CHECK_EQ((int)r, 321);
    if (!h.personQueries.empty())
        CHECK_EQ(h.personQueries[0].first, 1);     // good type 1
    if (!h.queries.empty()) {
        CHECK_EQ((int)h.queries[0].proto, 277);
        CHECK_EQ((int)h.queries[0].container, 0x7777);
    }
    SetBuilding4Hooks(nullptr);
}

TEST(Building4Unit, FindUpgradeStorage_Cat5_ClassSwitch) {
    // kind 0x17 (23) maps to category 5. class byte 32 -> good type 4, proto 322.
    RecHooks h; SetBuilding4Hooks(&h);
    std::uint8_t store[200]; std::memset(store, 0, sizeof store);
    std::int32_t sc = 0x5050; std::memcpy(store + 93, &sc, 4);
    h.personStore = store;
    h.queryReturn = 654;
    std::int32_t r = Building4_FindUpgradeStorage(0x17, 32, 0x11);
    CHECK_EQ((int)r, 654);
    if (!h.personQueries.empty())
        CHECK_EQ(h.personQueries[0].first, 4);     // class 32 -> good type 4
    if (!h.queries.empty())
        CHECK_EQ((int)h.queries[0].proto, 322);
    SetBuilding4Hooks(nullptr);
}

TEST(Building4Unit, FindUpgradeStorage_Cat5_UnknownClass) {
    RecHooks h; SetBuilding4Hooks(&h);
    // class byte 99 hits the switch default -> 0, no person query.
    std::int32_t r = Building4_FindUpgradeStorage(0x17, 99, 0);
    CHECK_EQ((int)r, 0);
    CHECK_EQ((int)h.personQueries.size(), 0);
    SetBuilding4Hooks(nullptr);
}

TEST(Building4Unit, FindUpgradeStorage_OtherCategory) {
    RecHooks h; SetBuilding4Hooks(&h);
    // kind 2 maps to category 6 -> neither branch -> 0.
    std::int32_t r = Building4_FindUpgradeStorage(2, 30, 0);
    CHECK_EQ((int)r, 0);
    SetBuilding4Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// AttachStorageRooms.
// ---------------------------------------------------------------------------
TEST(Building4Unit, AttachStorageRooms_StorageKind) {
    RecHooks h; SetBuilding4Hooks(&h);
    std::uint8_t rec[200];
    MakeBuilding(rec, 0, 0x1234, /*owner*/0xBEEF, 0);
    // First QueryFind returns obj 10, then iterate 11, then 0.
    h.queryScript = { 10 };
    h.iterScript = { 11, 0 };
    h.flagNodeReturn = 42;
    std::int32_t r = Building4_AttachStorageRooms(rec, /*isStorage*/true);
    // Two flag-node calls (obj 10, 11), each with owner 0xBEEF.
    CHECK_EQ((int)h.flagNodeCalls.size(), 2);
    if (h.flagNodeCalls.size() == 2) {
        CHECK_EQ((int)h.flagNodeCalls[0].first, 10);
        CHECK_EQ((int)h.flagNodeCalls[0].second, 0xBEEF);
        CHECK_EQ((int)h.flagNodeCalls[1].first, 11);
    }
    CHECK_EQ((int)r, 42);
    SetBuilding4Hooks(nullptr);
}

TEST(Building4Unit, AttachStorageRooms_NotStorage) {
    RecHooks h; SetBuilding4Hooks(&h);
    std::uint8_t rec[200];
    MakeBuilding(rec, 5, 0x1, 0, 0);
    Building4_AttachStorageRooms(rec, /*isStorage*/false);
    CHECK_EQ((int)h.flagNodeCalls.size(), 0);
    SetBuilding4Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// RemoveStorageRoom.
// ---------------------------------------------------------------------------
TEST(Building4Unit, RemoveStorageRoom_NotPresent) {
    RecHooks h; SetBuilding4Hooks(&h);
    h.removeReturn = -2;
    CHECK_EQ((int)Building4_RemoveStorageRoom(0, 100, 0), -3);
    SetBuilding4Hooks(nullptr);
}

TEST(Building4Unit, RemoveStorageRoom_OK) {
    RecHooks h; SetBuilding4Hooks(&h);
    h.removeReturn = 0;
    CHECK_EQ((int)Building4_RemoveStorageRoom(0, 100, 0), 0);
    SetBuilding4Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// SyncProfessionState — the three profession-code derivations.
// ---------------------------------------------------------------------------
TEST(Building4Unit, SyncProfessionState_WorkKind32) {
    RecHooks h; SetBuilding4Hooks(&h);
    Building4_SyncProfessionState(0xAA, 5, /*workKind*/32, /*defaultProf*/7);
    // Two packets: field 357 == 12, field 372 == 0.
    CHECK_EQ(h.beginCount, 2);
    CHECK_EQ(h.flushCount, 2);
    CHECK_EQ((int)h.appendedFields.size(), 2);
    if (h.appendedFields.size() == 2) {
        CHECK_EQ(h.appendedFields[0].first, 357);
        CHECK_EQ(h.appendedFields[0].second, 12);
        CHECK_EQ(h.appendedFields[1].first, 372);
        CHECK_EQ(h.appendedFields[1].second, 0);
    }
    SetBuilding4Hooks(nullptr);
}

TEST(Building4Unit, SyncProfessionState_WorkKind31) {
    RecHooks h; SetBuilding4Hooks(&h);
    Building4_SyncProfessionState(0xAA, 5, 31, 7);
    if (!h.appendedFields.empty())
        CHECK_EQ(h.appendedFields[0].second, 11);
    SetBuilding4Hooks(nullptr);
}

TEST(Building4Unit, SyncProfessionState_Default) {
    RecHooks h; SetBuilding4Hooks(&h);
    // workKind 0 -> defaultProf - 1.
    Building4_SyncProfessionState(0xAA, 5, 0, 9);
    if (!h.appendedFields.empty())
        CHECK_EQ(h.appendedFields[0].second, 8);
    SetBuilding4Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// EvalBuyBuilding.
// ---------------------------------------------------------------------------
TEST(Building4Unit, EvalBuyBuilding_NotBuildingPlot) {
    RecHooks h; SetBuilding4Hooks(&h);
    CHECK_EQ(Building4_EvalBuyBuilding(/*kind*/5, 0x10, 0x20), 0);
    CHECK(!h.enqueued);
    SetBuilding4Hooks(nullptr);
}

TEST(Building4Unit, EvalBuyBuilding_NoHandler) {
    RecHooks h; SetBuilding4Hooks(&h);
    // No handler blob installed -> HeFindFirst returns null.
    CHECK_EQ(Building4_EvalBuyBuilding(4, 0x10, 0x20), 0);
    CHECK(!h.enqueued);
    SetBuilding4Hooks(nullptr);
}

TEST(Building4Unit, EvalBuyBuilding_Match) {
    RecHooks h; SetBuilding4Hooks(&h);
    // Build a handler record: index 43 must equal targetId, index 44 = field44.
    h.handlerBlob.assign(64, 0);
    h.handlerBlob[43] = 0x10;       // == targetId
    h.handlerBlob[44] = 0x99;       // field44
    int r = Building4_EvalBuyBuilding(/*kind*/4, /*targetId*/0x10, /*actorId*/0x20);
    CHECK_EQ(r, 16);
    CHECK(h.enqueued);
    CHECK_EQ((int)h.enqueueBuildingId, 0x10);
    CHECK_EQ((int)h.enqueueField44, 0x99);
    CHECK_EQ(h.finalFlagSeen, 1);
    SetBuilding4Hooks(nullptr);
}
