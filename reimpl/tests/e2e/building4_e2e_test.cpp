#include "test.h"

// E2E: a storage-building lifecycle flow stitched across the building4 functions
// with a single stateful scene/command backend, then a "buy this building" flow.
// The backend models a tiny in-memory scene: rooms/objects under a container, a
// person good-store, a command journal and a handler list — so the cross-function
// behaviour (seed defaults -> init workers -> find/attach storage -> sync
// profession -> remove room) is exercised end to end as the live wiring would.
#include "sim/building4.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct SceneBackend : Building4Hooks {
    // A trivial scene: handle counter, set of (container -> [proto]) members.
    std::int32_t nextHandle = 1000;
    std::map<std::int32_t, std::vector<std::int32_t>> children;  // container -> handles
    std::map<std::int32_t, std::int32_t> protoOf;                // handle -> proto

    // Iteration state for the group-4 scan.
    std::vector<std::int32_t> iterBuf;
    std::size_t iterPos = 0;

    const std::uint8_t* store = nullptr;

    std::vector<std::pair<std::int32_t, std::uint16_t>> attached;
    std::vector<int> journal;       // appended profession field values
    bool buyEnqueued = false;
    std::vector<std::uint32_t> handler;

    std::int32_t Spawn(std::int32_t container, std::int32_t proto) {
        std::int32_t h = nextHandle++;
        children[container].push_back(h);
        protoOf[h] = proto;
        return h;
    }

    std::int32_t GameObjectQueryFind(std::int32_t c, int, int gB, int,
                                     std::int32_t proto) override {
        if (gB == 4) {
            // group-4 scan: begin an iteration over the container's children.
            iterBuf.clear(); iterPos = 0;
            auto it = children.find(c);
            if (it != children.end())
                for (std::int32_t h : it->second) iterBuf.push_back(h);
            if (iterBuf.empty()) return 0;
            return iterBuf[iterPos++];
        }
        // proto-specific lookup.
        auto it = children.find(c);
        if (it != children.end())
            for (std::int32_t h : it->second)
                if (protoOf[h] == proto) return h;
        return 0;
    }
    std::int32_t GameObjectIterNext() override {
        if (iterPos < iterBuf.size()) return iterBuf[iterPos++];
        return 0;
    }
    std::int32_t GameObjectAddObjekt(std::int32_t parent, std::int32_t proto,
                                     int, std::int32_t) override {
        return Spawn(parent, proto);
    }
    std::int32_t GameObjectRemoveByProt(std::int32_t container, std::int16_t proto,
                                        std::int32_t) override {
        auto it = children.find(container);
        if (it == children.end()) return -2;
        for (auto vit = it->second.begin(); vit != it->second.end(); ++vit) {
            if (protoOf[*vit] == proto) { it->second.erase(vit); return 0; }
        }
        return -2;   // not present
    }
    std::int32_t BuildFlagNodeList(std::int32_t obj, std::uint16_t owner) override {
        attached.push_back({obj, owner});
        return obj;
    }
    const std::uint8_t* PersonQueryByGoodType(int, std::int32_t) override {
        return store;
    }
    void CommandAppendCopiedField(std::uint32_t, std::uint32_t, const void* src,
                                  int) override {
        int v = 0; if (src) std::memcpy(&v, src, sizeof v);
        journal.push_back(v);
    }
    const std::uint32_t* HeFindFirstHandlerByFilter(int, int, int) override {
        return handler.empty() ? nullptr : handler.data();
    }
    void EnqueueBuyBuilding(std::int32_t, std::int32_t, const std::uint32_t*,
                            std::int32_t, std::int32_t, int) override {
        buyEnqueued = true;
    }
};

void MakeBuilding(std::uint8_t* rec, std::uint8_t type0, std::int32_t container,
                  std::uint16_t owner, std::int32_t objId) {
    std::memset(rec, 0, 200);
    rec[0] = type0;
    std::memcpy(rec + 1, &objId, 4);
    std::memcpy(rec + 39, &owner, 2);
    std::memcpy(rec + 93, &container, 4);
}

}  // namespace

TEST(Building4E2E, StorageBuildingLifecycle) {
    SceneBackend be; SetBuilding4Hooks(&be);

    // The original adds the room under rec+1 (object id) but queries it under
    // rec+93 (container) — in the live scene both resolve to the same node, so the
    // e2e backend models them with one handle value.
    const std::int32_t container = 5000;
    std::uint8_t rec[200];
    MakeBuilding(rec, /*type0*/0x29, container, /*owner*/0x77, /*objId*/container);

    // 1. Seed default objects for type 0x29 (3 protos: D4,55,56) — creates a room
    //    object and the three children under it.
    std::int32_t seedResult = Building4_EnsureDefaultObjects(rec);
    CHECK(seedResult != 0);
    // The room (proto 255) now exists under the container.
    std::int32_t room = be.GameObjectQueryFind(container, 2, 6, 0, 255);
    CHECK(room != 0);
    // Re-running is idempotent: the room is found, not recreated.
    std::size_t handleCountBefore = be.protoOf.size();
    Building4_EnsureDefaultObjects(rec);
    CHECK_EQ((int)be.protoOf.size(), (int)handleCountBefore);

    // 2. Worker capacities clamp on a fake worker object record.
    std::uint8_t worker42[40] = {0};
    WorkerCfg cfg; cfg.cfg573 = 4; cfg.cfg574 = 0; cfg.cfg575 = 0;
    Building4_InitWorkerCapacities(worker42, nullptr, cfg);
    CHECK_EQ((int)worker42[28], 4);
    CHECK_EQ((int)worker42[29], 2);

    // 3. Spawn a couple of group-4 storage sub-objects, then attach them (storage
    //    kind). Both should get a flag-node call carrying the +39 owner word.
    be.Spawn(container, 2);   // group-4 member A
    be.Spawn(container, 2);   // group-4 member B
    Building4_AttachStorageRooms(rec, /*isStorage*/true);
    CHECK(be.attached.size() >= 2);
    if (!be.attached.empty())
        CHECK_EQ((int)be.attached[0].second, 0x77);

    // 4. Push an occupant's profession state. workKind 31 -> code 11, then 0.
    Building4_SyncProfessionState(/*rec*/(std::int32_t)400, /*objId*/300, 31, 5);
    CHECK_EQ((int)be.journal.size(), 2);
    if (be.journal.size() == 2) {
        CHECK_EQ(be.journal[0], 11);
        CHECK_EQ(be.journal[1], 0);
    }

    // 5. Remove a storage room by proto. Removing proto 2 (present) -> 0;
    //    removing it again -> -3 (engine "not present").
    CHECK_EQ((int)Building4_RemoveStorageRoom(container, 2, 0), 0);
    // (one of the two group-4 members removed; remove the other)
    CHECK_EQ((int)Building4_RemoveStorageRoom(container, 2, 0), 0);
    CHECK_EQ((int)Building4_RemoveStorageRoom(container, 2, 0), -3);

    SetBuilding4Hooks(nullptr);
}

TEST(Building4E2E, BuyBuildingFlow) {
    SceneBackend be; SetBuilding4Hooks(&be);

    // No matching handler -> no enqueue, returns 0.
    CHECK_EQ(Building4_EvalBuyBuilding(/*kind*/4, /*targetId*/0x500, /*actor*/0x900), 0);
    CHECK(!be.buyEnqueued);

    // Install a handler whose +43 matches the target -> enqueue, returns 16.
    be.handler.assign(64, 0);
    be.handler[43] = 0x500;
    be.handler[44] = 0x1;
    int r = Building4_EvalBuyBuilding(4, 0x500, 0x900);
    CHECK_EQ(r, 16);
    CHECK(be.buyEnqueued);

    // Non-plot target kind short-circuits.
    be.buyEnqueued = false;
    CHECK_EQ(Building4_EvalBuyBuilding(9, 0x500, 0x900), 0);
    CHECK(!be.buyEnqueued);

    SetBuilding4Hooks(nullptr);
}
