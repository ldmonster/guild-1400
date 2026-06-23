// Golden-vector tests for the AiAction request/dispatch builders (ai_recon2).
#include "tests/framework/test.h"

#include "src/sim/aiaction_dispatch_ai_recon2.h"

using namespace guild::sim;

TEST(Ai2ReconDispatch, RequestCmd58) {
    RequestPacket p = BuildRequestCmd58(0x1234);
    CHECK_EQ((int)p.cmdId, 58);
    CHECK_EQ(p.objectId, 0x1234);
    CHECK_EQ(p.sentinel, -1);
    CHECK_EQ((int)p.mode, 1);
    CHECK(!p.hasTime);
}

TEST(Ai2ReconDispatch, RequestCmd59WithTime) {
    GameTimeStamp t; t.packed = 0xAABBCCDD; t.extra = 7; t.tail = 9;
    bool freed = false;
    RequestPacket p = BuildRequestCmd59(42, t, /*existingHandler*/true, &freed);
    CHECK(freed);                       // existing handler → freed first
    CHECK_EQ((int)p.cmdId, 59);
    CHECK_EQ(p.objectId, 42);
    CHECK_EQ((int)p.mode, 1);
    CHECK(p.hasTime);
    CHECK(p.time.packed == 0xAABBCCDDull);
    CHECK_EQ(p.time.extra, 7);
}

TEST(Ai2ReconDispatch, RequestCmd59NoExistingHandler) {
    GameTimeStamp t;
    bool freed = true;
    RequestPacket p = BuildRequestCmd59(1, t, /*existingHandler*/false, &freed);
    CHECK(!freed);                      // no handler → nothing freed, still emits
    CHECK_EQ((int)p.cmdId, 59);
}

TEST(Ai2ReconDispatch, RequestCmd115) {
    GameTimeStamp t;
    RequestPacket p = BuildRequestCmd115(0x9999, t, false, nullptr);
    CHECK_EQ((int)p.cmdId, 115);
    CHECK_EQ(p.objectId, 0x9999);
    CHECK_EQ((int)p.mode, 1);
    CHECK(p.hasTime);
}

TEST(Ai2ReconDispatch, RequestCmd109OnlyWhenNoHandler) {
    GameTimeStamp t;
    RequestPacket p;
    // existing handler → no emit.
    CHECK(!BuildRequestCmd109(5, t, /*existingHandler*/true, &p));
    // no handler → emit, mode=2.
    RequestPacket p2;
    CHECK(BuildRequestCmd109(5, t, /*existingHandler*/false, &p2));
    CHECK_EQ((int)p2.cmdId, 109);
    CHECK_EQ((int)p2.mode, 2);
    CHECK(p2.hasTime);
}

TEST(Ai2ReconDispatch, GuildhallTransferPath) {
    ObjPair p; p.aType = 1; p.bType = 18; p.bHas8 = 1;
    int amt = -1; bool up = true;
    int r = HandleGuildhallTrigger(p, /*flaggedWorth*/1000, &amt, &up);
    CHECK_EQ(r, 57);
    CHECK(!up);            // transfer path is not an upgrade
}

TEST(Ai2ReconDispatch, GuildhallUpgradePath) {
    ObjPair p; p.aType = 4; p.bType = 1;
    int amt = -1; bool up = false;
    // amt = trunc(1000 * 0.3) = trunc(300.00001) = 300.
    int r = HandleGuildhallTrigger(p, 1000, &amt, &up);
    CHECK_EQ(r, 57);
    CHECK(up);
    CHECK_EQ(amt, 300);
    // worth=33 → 33*0.3=9.9 → trunc 9.
    HandleGuildhallTrigger(p, 33, &amt, &up);
    CHECK_EQ(amt, 9);
}

TEST(Ai2ReconDispatch, GuildhallReject) {
    ObjPair p; p.aType = 2; p.bType = 2;
    CHECK_EQ(HandleGuildhallTrigger(p, 100, nullptr, nullptr), 0);
    // aType==1 but bType not 18 and aType!=4 → 0
    p.aType = 1; p.bType = 5; p.bHas8 = 1;
    CHECK_EQ(HandleGuildhallTrigger(p, 100, nullptr, nullptr), 0);
    // aType==1 bType==18 but bHas8==0 → falls through, aType!=4 → 0
    p.aType = 1; p.bType = 18; p.bHas8 = 0;
    CHECK_EQ(HandleGuildhallTrigger(p, 100, nullptr, nullptr), 0);
}

TEST(Ai2ReconDispatch, HandleObjectType23) {
    CHECK_EQ(HandleObjectType23(7, 23), 58);
    CHECK_EQ(HandleObjectType23(7, 22), 0);
    CHECK_EQ(HandleObjectType23(6, 23), 0);
}

TEST(Ai2ReconDispatch, HandleObjectType14) {
    CHECK_EQ(HandleObjectType14(7, 14, true), 59);
    CHECK_EQ(HandleObjectType14(7, 14, false), 0);   // no person record
    CHECK_EQ(HandleObjectType14(7, 13, true), 0);
    CHECK_EQ(HandleObjectType14(6, 14, true), 0);
}

TEST(Ai2ReconDispatch, FinderForType) {
    CHECK(FinderForType(0) == SearchFinder::FactionPerson);
    CHECK(FinderForType(3) == SearchFinder::AdjacentEntityLarge);
    CHECK(FinderForType(4) == SearchFinder::EligibleNeighbor);
    CHECK(FinderForType(7) == SearchFinder::NearbyBuilding);
    CHECK(FinderForType(8) == SearchFinder::NearbyWealthyTarget);
    CHECK(FinderForType(99) == SearchFinder::None);
    CHECK(FinderForType(1) == SearchFinder::None);
}

TEST(Ai2ReconDispatch, RouteOnlyList1) {
    std::array<int, 3> types{0, 3, 7};
    // probe: type 0 mode1 → found; others not.
    auto probe = [](int code, int mode) -> int {
        return (code == 0 && mode == 1) ? 2 : 0;
    };
    auto coin = []() { return 0; };
    auto index = [](int) { return 0; };
    SearchRoute r = RouteSecondarySearch(types, /*follow*/false, probe, coin, index);
    CHECK(r.finder == SearchFinder::FactionPerson);
    CHECK_EQ(r.typeCode, 0);
    CHECK_EQ(r.mode, 1);
}

TEST(Ai2ReconDispatch, RouteCoinPicksList2) {
    std::array<int, 3> types{3, 4, 8};
    // both lists non-empty: list1 = {3} (mode1), list2 = {8} (mode2).
    auto probe = [](int code, int mode) -> int {
        if (mode == 1 && code == 3) return 2;
        if (mode == 2 && code == 8) return 2;
        return 0;
    };
    auto index = [](int) { return 0; };
    // coin == 1 → pick list2.
    SearchRoute r2 = RouteSecondarySearch(types, /*follow*/true, probe,
                                          []() { return 1; }, index);
    CHECK(r2.finder == SearchFinder::NearbyWealthyTarget);
    CHECK_EQ(r2.typeCode, 8);
    CHECK_EQ(r2.mode, 2);
    // coin == 0 → pick list1.
    SearchRoute r1 = RouteSecondarySearch(types, /*follow*/true, probe,
                                          []() { return 0; }, index);
    CHECK(r1.finder == SearchFinder::AdjacentEntityLarge);
    CHECK_EQ(r1.typeCode, 3);
    CHECK_EQ(r1.mode, 1);
}

TEST(Ai2ReconDispatch, RouteEmpty) {
    std::array<int, 3> types{1, 2, 5};
    auto probe = [](int, int) { return 0; };
    SearchRoute r = RouteSecondarySearch(types, true, probe,
                                         []() { return 0; }, [](int) { return 0; });
    CHECK(r.finder == SearchFinder::None);
    CHECK_EQ(r.typeCode, -1);
}
