#include "test.h"

// Integration: drive building4 against the REAL reconstructed sibling
// classifiers in building_type.cpp (gilde.exe) — NO mock classifier.
//
//   1. Building4_FindUpgradeStorage routes its category decision through the
//      genuine Building_MapKindToCategory (0x5878b0). We assert that, for several
//      building KIND bytes, the finder asks the scene for exactly the proto id the
//      real classifier dictates (cat 3 -> 277, cat 5 -> 322, other -> no query).
//
//   2. Building4_FindStorableObject branches on the genuine Building_IsProductionKind
//      (0x587f80). We assert the proto it queries tracks the real predicate
//      (production -> 253, kind 4 -> 84).
//
// Only the scene-query / person-store leaves are hooked; the classification is the
// live wiring.
#include "sim/building4.h"
#include "sim/building_type.h"   // real Building_MapKindToCategory / IsProductionKind

#include <cstdint>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

struct ProtoRecorder : Building4Hooks {
    std::int32_t lastProto = -1;
    const std::uint8_t* store = nullptr;
    std::int32_t GameObjectQueryFind(std::int32_t, int, int, int,
                                     std::int32_t proto) override {
        lastProto = proto;
        return 0xC0DE;     // pretend the scene found something
    }
    std::int32_t GameObjectIterNext() override { return 0; }
    const std::uint8_t* PersonQueryByGoodType(int, std::int32_t) override {
        return store;
    }
};

}  // namespace

// 1. FindUpgradeStorage's proto choice tracks the real MapKindToCategory.
TEST(Building4IT, FindUpgradeStorage_UsesRealClassifier) {
    // kind 1  -> category 3 -> proto 277.
    // kind 23 -> category 5 -> proto 322 (class byte 32 -> good type 4).
    // kind 2  -> category 6 -> no query, returns 0.
    struct Case { std::uint8_t kind; std::uint8_t cls; int expectCat;
                  std::int32_t expectProto; };
    const Case cases[] = {
        { 1,  0,  3, 277 },
        { 23, 32, 5, 322 },
        { 2,  30, 6, -1 },
    };
    for (const Case& cs : cases) {
        // Sanity: the genuine sibling agrees with the expectation.
        CHECK_EQ((int)Building_MapKindToCategory(cs.kind), cs.expectCat);

        std::uint8_t store[200]; std::memset(store, 0, sizeof store);
        std::int32_t sc = 0x4444; std::memcpy(store + 93, &sc, 4);

        ProtoRecorder rec; rec.store = store; SetBuilding4Hooks(&rec);
        std::int32_t r = Building4_FindUpgradeStorage(cs.kind, cs.cls, /*ctx*/7);

        if (cs.expectProto < 0) {
            CHECK_EQ((int)r, 0);
            CHECK_EQ((int)rec.lastProto, -1);   // never queried the scene
        } else {
            CHECK_EQ((int)r, (int)0xC0DE);
            CHECK_EQ((int)rec.lastProto, (int)cs.expectProto);
        }
        SetBuilding4Hooks(nullptr);
    }
}

// 2. FindStorableObject branches on the real production predicate.
TEST(Building4IT, FindStorableObject_TracksRealProductionPredicate) {
    struct Case { std::uint8_t kind; std::int32_t expectProto; };
    const Case cases[] = {
        { 11, 253 },   // production
        { 12, 253 },   // production
        { 4,  84  },   // kind 4 (non-production)
    };
    for (const Case& cs : cases) {
        // Sanity: the genuine production predicate matches our expectation.
        bool isProd = Building_IsProductionKind(cs.kind);
        if (cs.expectProto == 253) CHECK(isProd);
        if (cs.kind == 4)          CHECK(!isProd);

        ProtoRecorder rec; SetBuilding4Hooks(&rec);
        std::int32_t r = Building4_FindStorableObject(cs.kind, /*container*/0x55);
        CHECK_EQ((int)r, (int)0xC0DE);
        CHECK_EQ((int)rec.lastProto, (int)cs.expectProto);
        SetBuilding4Hooks(nullptr);
    }
}

// 3. The inert default-hook path degrades gracefully (live "nothing found").
TEST(Building4IT, DefaultHooksAreInert) {
    SetBuilding4Hooks(nullptr);
    // Production building, default hooks -> scene returns 0.
    CHECK_EQ((int)Building4_FindStorableObject(11, 0x10), 0);
    // Category-3 building, default person store null -> 0.
    CHECK_EQ((int)Building4_FindUpgradeStorage(1, 0, 0), 0);
    // EvalBuyBuilding with no handler list -> 0.
    CHECK_EQ(Building4_EvalBuyBuilding(4, 0x1, 0x2), 0);
}
