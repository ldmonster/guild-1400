// Golden tests for RefreshAllFlags (0x4b5fa4) and SyncTurnState (0x531e60).
#include "test.h"
#include "sim/character_recon4_flags.h"

#include <vector>
#include <string>

using namespace guild;
using namespace guild::sim;

namespace {

// Trace recorder shared by the SyncTurnState tests.
struct SyncTrace {
    int beginPackets = 0;
    int rawFields = 0;
    int state22 = 0;
    int slotResets = 0;
    int buildOps = 0;
    int lastBuildOpRec = 0;
};

TurnStateHooks MakeHooks(SyncTrace& t, f32 gauge, void* handler, int rank) {
    TurnStateHooks H;
    H.ctx = &t;
    H.beginDeltaPacket = [](void* c, void*, int) { static_cast<SyncTrace*>(c)->beginPackets++; };
    H.appendRawField = [](void* c, unsigned, unsigned, const void*, int) { static_cast<SyncTrace*>(c)->rawFields++; };
    H.queueRequestState22 = [](void* c) { static_cast<SyncTrace*>(c)->state22++; };
    H.queueRequestSlotReset28 = [](void* c, void*, int) { static_cast<SyncTrace*>(c)->slotResets++; };
    // rank/gauge/handler are captured per-test via statics below.
    (void)gauge; (void)handler; (void)rank;
    return H;
}

} // namespace

// State 6 with prev state 6 -> emits a state-22 delta packet (HIBYTE branch).
TEST(CharacterRecon4, SyncTurn_state6_prev6_emits_packet) {
    SyncTrace t;
    TurnStateHooks H = MakeHooks(t, 0.0f, nullptr, 0);
    TurnStateView v;
    v.stateByte = 6;
    v.prevStateByte = 6;
    v.recId = 100;
    SyncTurnState(H, v);
    CHECK_EQ(t.beginPackets, 1);
    CHECK_EQ(t.rawFields, 1);
    CHECK_EQ(t.state22, 1);
    CHECK_EQ(t.buildOps, 0);
}

// State 10 with prev state 1 -> no packet (HIBYTE branch requires prev in {6,7}).
TEST(CharacterRecon4, SyncTurn_state10_prev1_no_packet) {
    SyncTrace t;
    TurnStateHooks H = MakeHooks(t, 0.0f, nullptr, 0);
    TurnStateView v;
    v.stateByte = 10;
    v.prevStateByte = 1;
    SyncTurnState(H, v);
    CHECK_EQ(t.beginPackets, 0);
    CHECK_EQ(t.state22, 0);
}

// State byte 0xFF: the >=10 test is a SIGNED compare (0x531e96 `jge`), so 0xFF
// (== -1 as i8) is < 10 and falls to the else branch.  With data372==0 and the
// state not in {1,2,4,5}, no packet is emitted (proves it took the else branch,
// not the HIBYTE branch which would also emit nothing but for a different reason).
TEST(CharacterRecon4, SyncTurn_state255_signed_falls_to_else_branch) {
    SyncTrace t;
    TurnStateHooks H = MakeHooks(t, 0.0f, nullptr, 0);
    TurnStateView v;
    v.stateByte = 0xFF;       // signed -1 < 10 -> else branch
    v.prevStateByte = 6;      // would emit IF this were the HIBYTE branch
    v.data372 = 0;            // else branch: no emit (state not in {1,2,4,5})
    v.buildingFlag = 0;
    SyncTurnState(H, v);
    CHECK_EQ(t.beginPackets, 0);
    CHECK_EQ(t.state22, 0);
}

// Else branch: state 1 (in the {1,2,4,5} set) -> emits packet, no building work.
TEST(CharacterRecon4, SyncTurn_state1_emits_packet) {
    SyncTrace t;
    TurnStateHooks H = MakeHooks(t, 0.0f, nullptr, 0);
    TurnStateView v;
    v.stateByte = 1;
    v.recId = 42;
    v.buildingFlag = 0;
    SyncTurnState(H, v);
    CHECK_EQ(t.beginPackets, 1);
    CHECK_EQ(t.state22, 1);
    CHECK_EQ(t.buildOps, 0);
}

// Else branch: state 3 (not in set) with data372==0 -> no packet.
TEST(CharacterRecon4, SyncTurn_state3_no_data_no_packet) {
    SyncTrace t;
    TurnStateHooks H = MakeHooks(t, 0.0f, nullptr, 0);
    TurnStateView v;
    v.stateByte = 3;
    v.data372 = 0;
    SyncTurnState(H, v);
    CHECK_EQ(t.beginPackets, 0);
}

// Else branch: state 3 with data372 nonzero -> packet emitted.
TEST(CharacterRecon4, SyncTurn_state3_with_data_emits_packet) {
    SyncTrace t;
    TurnStateHooks H = MakeHooks(t, 0.0f, nullptr, 0);
    TurnStateView v;
    v.stateByte = 3;
    v.data372 = 1;
    SyncTurnState(H, v);
    CHECK_EQ(t.beginPackets, 1);
}

// Building branch fires build-op when: no handler, rank in 1..5, gauge>=1, ledger>=0.
TEST(CharacterRecon4, SyncTurn_building_fires_buildop) {
    SyncTrace t;
    TurnStateHooks H = MakeHooks(t, 0.0f, nullptr, 0);
    H.computeRank = [](void*, int) { return 3; };
    H.findFirstHandler = [](void*, int, int, int, int, int) -> void* { return nullptr; };
    H.drawProductionGauge = [](void*, int, void*) { return 1.0f; };
    H.requestBuildOp90 = [](void* c, int, int rec) { auto* tr = static_cast<SyncTrace*>(c); tr->buildOps++; tr->lastBuildOpRec = rec; };

    TurnStateView v;
    v.stateByte = 3;          // else branch, not in emit set
    v.data372 = 0;            // no state-22 packet
    v.buildingFlag = 1;
    v.ledger = 0;             // >= 0
    v.recId = 555;
    SyncTurnState(H, v);
    CHECK_EQ(t.beginPackets, 0);
    CHECK_EQ(t.slotResets, 1);
    CHECK_EQ(t.buildOps, 1);
    CHECK_EQ(t.lastBuildOpRec, 555);
}

// Building branch suppressed when a handler exists.
TEST(CharacterRecon4, SyncTurn_building_handler_present_no_buildop) {
    SyncTrace t;
    TurnStateHooks H = MakeHooks(t, 0.0f, nullptr, 0);
    static int dummy = 1;
    H.computeRank = [](void*, int) { return 3; };
    H.findFirstHandler = [](void*, int, int, int, int, int) -> void* { return &dummy; };
    H.drawProductionGauge = [](void*, int, void*) { return 1.0f; };
    H.requestBuildOp90 = [](void* c, int, int) { static_cast<SyncTrace*>(c)->buildOps++; };

    TurnStateView v;
    v.stateByte = 3;
    v.buildingFlag = 1;
    v.ledger = 0;
    SyncTurnState(H, v);
    CHECK_EQ(t.buildOps, 0);
    CHECK_EQ(t.slotResets, 0);
}

// Building branch suppressed when gauge < 1.
TEST(CharacterRecon4, SyncTurn_building_gauge_below_one_no_buildop) {
    SyncTrace t;
    TurnStateHooks H = MakeHooks(t, 0.0f, nullptr, 0);
    H.computeRank = [](void*, int) { return 3; };
    H.findFirstHandler = [](void*, int, int, int, int, int) -> void* { return nullptr; };
    H.drawProductionGauge = [](void*, int, void*) { return 0.5f; };
    H.requestBuildOp90 = [](void* c, int, int) { static_cast<SyncTrace*>(c)->buildOps++; };

    TurnStateView v;
    v.stateByte = 3;
    v.buildingFlag = 1;
    v.ledger = 0;
    SyncTurnState(H, v);
    CHECK_EQ(t.buildOps, 0);
}

// Building branch suppressed when rank out of [1,5] (rank 6).
TEST(CharacterRecon4, SyncTurn_building_rank6_no_buildop) {
    SyncTrace t;
    TurnStateHooks H = MakeHooks(t, 0.0f, nullptr, 0);
    H.computeRank = [](void*, int) { return 6; };
    H.findFirstHandler = [](void*, int, int, int, int, int) -> void* { return nullptr; };
    H.drawProductionGauge = [](void*, int, void*) { return 1.0f; };
    H.requestBuildOp90 = [](void* c, int, int) { static_cast<SyncTrace*>(c)->buildOps++; };

    TurnStateView v;
    v.stateByte = 3;
    v.buildingFlag = 1;
    v.ledger = 0;
    SyncTurnState(H, v);
    CHECK_EQ(t.buildOps, 0);
}

// Building branch suppressed when ledger < 0.
TEST(CharacterRecon4, SyncTurn_building_negative_ledger_no_buildop) {
    SyncTrace t;
    TurnStateHooks H = MakeHooks(t, 0.0f, nullptr, 0);
    H.computeRank = [](void*, int) { return 3; };
    H.findFirstHandler = [](void*, int, int, int, int, int) -> void* { return nullptr; };
    H.drawProductionGauge = [](void*, int, void*) { return 1.0f; };
    H.requestBuildOp90 = [](void* c, int, int) { static_cast<SyncTrace*>(c)->buildOps++; };

    TurnStateView v;
    v.stateByte = 3;
    v.buildingFlag = 1;
    v.ledger = -1;
    SyncTurnState(H, v);
    CHECK_EQ(t.buildOps, 0);
}

// --- RefreshAllFlags ---------------------------------------------------------

namespace {
struct RefreshTrace {
    std::vector<int> slotsSwitched;
    int walks = 0;
    int textureSets = 0;
    int lastTexIndex = 0;
};
} // namespace

// Active slot is switched to 0 at entry and restored to the saved slot at exit.
TEST(CharacterRecon4, Refresh_saves_and_restores_active_slot) {
    RefreshTrace tr;
    RefreshFlagsHooks H;
    H.ctx = &tr;
    H.savedActiveSlot = 9;
    H.switchActiveSlot = [](void* c, int slot, int, int, int) {
        static_cast<RefreshTrace*>(c)->slotsSwitched.push_back(slot);
    };
    // No iterators -> just the entry/exit switches.
    RefreshRecordView view;
    RefreshAllFlags(H, /*player*/ 2, view);
    CHECK_EQ((int)tr.slotsSwitched.size(), 2);
    CHECK_EQ(tr.slotsSwitched.front(), 0);
    CHECK_EQ(tr.slotsSwitched.back(), 9);
}

// Person walk invokes ShowFlag for persons with a non-null object.
TEST(CharacterRecon4, Refresh_walks_persons_with_object) {
    RefreshTrace tr;
    RefreshFlagsHooks H;
    H.ctx = &tr;
    static int persons[] = {1, 2, 3};
    static int idx;
    idx = 0;
    H.switchActiveSlot = [](void*, int, int, int, int) {};
    H.personQueryBegin = [](void*, void*, int, int, int) -> void* { idx = 0; return &persons[0]; };
    H.personIterNext = [](void*) -> void* { idx++; return idx < 3 ? &persons[idx] : nullptr; };
    H.personObjPtr = [](void*, void* p) -> void* { return p; };  // non-null
    H.walkShowFlag = [](void* c, void*, void*) { static_cast<RefreshTrace*>(c)->walks++; };

    RefreshRecordView view;
    RefreshAllFlags(H, 0, view);
    CHECK_EQ(tr.walks, 3);
}

// Texture set applied when type in {5,6,7}, buildingId != 1341, present.
TEST(CharacterRecon4, Refresh_applies_texture_set_under_predicate) {
    RefreshTrace tr;
    RefreshFlagsHooks H;
    H.ctx = &tr;
    static int gobj = 1;
    static bool done;
    done = false;
    H.switchActiveSlot = [](void*, int, int, int, int) {};
    H.gameObjectQueryFind = [](void*, int) -> void* { done = false; return &gobj; };
    H.gameObjectIterNext = [](void*) -> void* { if (done) return nullptr; done = true; return nullptr; };
    H.gameObjectCharHandle = [](void*, void* g) -> void* { return g; };
    H.indexFromPointer = [](void*, int) { return 0; };
    H.charUniversePtr = [](void*, void*) { return 0; };
    H.selectTextureSet = [](void* c, void*, int, int texIndex, int) {
        auto* t = static_cast<RefreshTrace*>(c); t->textureSets++; t->lastTexIndex = texIndex;
    };

    RefreshRecordView view;
    view.present = true;
    view.buildingId = 200;     // != 1341
    view.type = 6;             // in {5,6,7}
    view.textureByte = 70;     // texIndex = 70 - 61 = 9
    RefreshAllFlags(H, 0, view);
    CHECK_EQ(tr.textureSets, 1);
    CHECK_EQ(tr.lastTexIndex, 9);
}

// Texture set suppressed when buildingId == 1341.
TEST(CharacterRecon4, Refresh_texture_suppressed_for_id1341) {
    RefreshTrace tr;
    RefreshFlagsHooks H;
    H.ctx = &tr;
    static int gobj = 1;
    static bool done;
    done = false;
    H.switchActiveSlot = [](void*, int, int, int, int) {};
    H.gameObjectQueryFind = [](void*, int) -> void* { done = false; return &gobj; };
    H.gameObjectIterNext = [](void*) -> void* { if (done) return nullptr; done = true; return nullptr; };
    H.gameObjectCharHandle = [](void*, void* g) -> void* { return g; };
    H.indexFromPointer = [](void*, int) { return 0; };
    H.charUniversePtr = [](void*, void*) { return 0; };
    H.selectTextureSet = [](void* c, void*, int, int, int) { static_cast<RefreshTrace*>(c)->textureSets++; };

    RefreshRecordView view;
    view.present = true;
    view.buildingId = 1341;
    view.type = 6;
    RefreshAllFlags(H, 0, view);
    CHECK_EQ(tr.textureSets, 0);
}
