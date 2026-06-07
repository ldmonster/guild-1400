// Unit tests for the NpcActionPerform handlers (gilde.exe VIBE_NpcAction_Perform*).
// Golden vectors: the result code each handler returns for matching vs mismatching
// interaction-type bytes, and the exact arguments forwarded to the command/dialog
// leaves (recorded through a mock NpcActionPerformHooks). All values are read
// straight off the Hex-Rays pseudocode.
#include "test.h"

#include "sim/npcaction_perform.h"

#include <cstring>
#include <vector>
#include <string>
#include <array>

using namespace guild::sim;
using guild::u8;
using guild::i32;

namespace {

// ---- recording mock ---------------------------------------------------------
struct Rec {
    std::vector<std::string> labels;     // EnqueueBuildingActionStart labels
    int beginCount = 0, endCount = 0;
    // op28: (actionId, objectId, seq, ownerId, fieldA, fieldB)
    std::vector<std::array<i32, 6>> op28;
    // op25: (id, a, b, c, d)
    std::vector<std::array<i32, 5>> op25;
    std::vector<std::pair<i32, int>> op72;   // (id, value)
    std::vector<std::pair<int, i32>> op90;   // (kind, id)
    // dialogs: (which, target, kind, id, field)
    std::vector<std::array<i32, 5>> dialogs;
    int threaten = 0, slander = 0, slanderLabel = 0, spy = 0, dark = 0, tavern = 0;
    int rejectCode = 99;
    // gate results (set by the test)
    int gThreaten = 0, gSlander = 0, gSlanderLabel = 0, gSpy = 0, gDark = 0, gTavern = 0;
} g_rec;

void RReset() { g_rec = Rec{}; }

void h_start(const char* l) { g_rec.labels.emplace_back(l); g_rec.beginCount++; }
void h_end() { g_rec.endCount++; }
void h_op28(int a, i32 o, int s, i32 w, i32 fa, i32 fb) {
    g_rec.op28.push_back({a, o, s, w, fa, fb});
}
void h_op25(i32 id, int a, int b, int c, int d) {
    g_rec.op25.push_back({id, a, b, c, d});
}
void h_op72(i32 id, u8 v) { g_rec.op72.push_back({id, v}); }
void h_op90(int k, i32 id) { g_rec.op90.push_back({k, id}); }
int  h_threaten() { g_rec.threaten++; return g_rec.gThreaten; }
int  h_slander(i32) { g_rec.slander++; return g_rec.gSlander; }
int  h_slanderLabel() { g_rec.slanderLabel++; return g_rec.gSlanderLabel; }
int  h_spy() { g_rec.spy++; return g_rec.gSpy; }
int  h_dark() { g_rec.dark++; return g_rec.gDark; }
int  h_tavern(i32, const void*, i32) { g_rec.tavern++; return g_rec.gTavern; }
void h_menu(i32 t, int k, i32 id, i32 f) { g_rec.dialogs.push_back({0, t, k, id, f}); }
void h_elect(i32 t, int k, i32 id, i32 f) { g_rec.dialogs.push_back({1, t, k, id, f}); }
void h_office(i32 t, int k, i32 id, int s) { g_rec.dialogs.push_back({2, t, k, id, s}); }
int  h_reject() { return g_rec.rejectCode; }

NpcActionPerformHooks MakeHooks() {
    NpcActionPerformHooks hk{};
    hk.enqueueBuildingActionStart = h_start;
    hk.enqueueBuildingActionEnd = h_end;
    hk.queueRequestSlotReset28 = h_op28;
    hk.queueRequestArgs25 = h_op25;
    hk.requestBuildOp72 = h_op72;
    hk.requestBuildOp90 = h_op90;
    hk.aiExecThreaten = h_threaten;
    hk.aiExecSlander = h_slander;
    hk.aiFormatSlanderLabel = h_slanderLabel;
    hk.aiQueueSpyMission = h_spy;
    hk.aiBuyDarkCorner = h_dark;
    hk.aiLoadBuildingGraphic = h_tavern;
    hk.amtBuildGuildOfficeMenu = h_menu;
    hk.amtBuildElectionDialog = h_elect;
    hk.amtBuildOfficeActionDialog = h_office;
    hk.interactionEvalRejectStub = h_reject;
    return hk;
}

// A small action/context record: type byte @+0, dword @+4, dword @+16.
struct Ctx { u8 buf[64]; };
Ctx MakeCtx(u8 type, i32 at4 = 0, i32 at16 = 0) {
    Ctx c{};
    std::memset(c.buf, 0, sizeof(c.buf));
    c.buf[0] = type;
    std::memcpy(c.buf + 4, &at4, 4);
    std::memcpy(c.buf + 16, &at16, 4);
    return c;
}

} // namespace

TEST(NpcActionPerform, Spionage_typeMismatch_returns0) {
    RReset();
    auto hk = MakeHooks();
    SetNpcActionPerformHooks(&hk);
    Ctx ctx = MakeCtx(/*type*/ 5);   // not 7
    u8 live[400]; std::memset(live, 0, sizeof(live));
    CHECK_EQ(NpcActionPerform_Spionage(nullptr, ctx.buf, live), 0);
    CHECK_EQ(g_rec.beginCount, 0);
    SetNpcActionPerformHooks(nullptr);
}

TEST(NpcActionPerform, Spionage_match_emitsOp28_returns37) {
    RReset();
    auto hk = MakeHooks();
    SetNpcActionPerformHooks(&hk);
    Ctx ctx = MakeCtx(/*type*/ 7, /*+4*/ 111, /*+16*/ 222);
    u8 live[400]; std::memset(live, 0, sizeof(live));
    i32 objId = 0xABCD;
    std::memcpy(live + 4, &objId, 4);   // *(live+4)
    i32 nullSub = 0; std::memcpy(live + 368, &nullSub, 4);  // owner -> -1

    CHECK_EQ(NpcActionPerform_Spionage(nullptr, ctx.buf, live), 37);
    CHECK_EQ(g_rec.beginCount, 1);
    CHECK_EQ(g_rec.endCount, 1);
    CHECK_EQ(g_rec.labels.size(), (size_t)1);
    CHECK(g_rec.labels[0] == "spionage");
    CHECK_EQ(g_rec.op28.size(), (size_t)1);
    CHECK_EQ(g_rec.op28[0][0], 24);       // actionId
    CHECK_EQ(g_rec.op28[0][1], 0xABCD);   // objectId
    CHECK_EQ(g_rec.op28[0][2], -1);       // seq
    CHECK_EQ(g_rec.op28[0][3], -1);       // ownerId (null sub)
    CHECK_EQ(g_rec.op28[0][4], 111);      // fieldA = ctx+4
    CHECK_EQ(g_rec.op28[0][5], 222);      // fieldB = ctx+16
    SetNpcActionPerformHooks(nullptr);
}

TEST(NpcActionPerform, EnterBuilding_threatenGate) {
    RReset();
    auto hk = MakeHooks();
    SetNpcActionPerformHooks(&hk);
    g_rec.gThreaten = 1;
    CHECK_EQ(NpcActionPerform_EnterBuilding(), 40);
    g_rec.gThreaten = 0;
    CHECK_EQ(NpcActionPerform_EnterBuilding(), 99);  // reject stub
    SetNpcActionPerformHooks(nullptr);
}

TEST(NpcActionPerform, UseBack_slanderGate_op25) {
    RReset();
    auto hk = MakeHooks();
    SetNpcActionPerformHooks(&hk);
    Ctx ctx = MakeCtx(0, /*+4*/ 7777);
    g_rec.gSlander = 0;
    CHECK_EQ(NpcActionPerform_UseBack(ctx.buf), 0);
    CHECK_EQ(g_rec.op25.size(), (size_t)0);

    g_rec.gSlander = 1;
    CHECK_EQ(NpcActionPerform_UseBack(ctx.buf), 42);
    CHECK_EQ(g_rec.op25.size(), (size_t)1);
    CHECK_EQ(g_rec.op25[0][0], 7777);
    CHECK_EQ(g_rec.op25[0][1], 484);
    CHECK_EQ(g_rec.op25[0][2], 512);
    CHECK_EQ(g_rec.op25[0][3], 4);
    CHECK_EQ(g_rec.op25[0][4], 0);
    SetNpcActionPerformHooks(nullptr);
}

TEST(NpcActionPerform, OpenDoor_largeAndSmall) {
    RReset();
    auto hk = MakeHooks();
    SetNpcActionPerformHooks(&hk);
    g_rec.gSlanderLabel = 1;
    CHECK_EQ(NpcActionPerform_OpenDoorLarge(), 43);
    CHECK_EQ(NpcActionPerform_OpenDoorSmall(), 44);
    g_rec.gSlanderLabel = 0;
    CHECK_EQ(NpcActionPerform_OpenDoorLarge(), 99);
    CHECK_EQ(NpcActionPerform_OpenDoorSmall(), 99);
    SetNpcActionPerformHooks(nullptr);
}

TEST(NpcActionPerform, ShopTransaction_typePairs) {
    RReset();
    auto hk = MakeHooks();
    SetNpcActionPerformHooks(&hk);
    u8 a4 = 4, b1 = 1, a19 = 19, b4 = 4, other = 9;

    // (4,1) spy gate
    g_rec.gSpy = 1;
    CHECK_EQ(NpcActionPerform_ShopTransaction(&a4, &b1), 50);
    g_rec.gSpy = 0;
    CHECK_EQ(NpcActionPerform_ShopTransaction(&a4, &b1), 0);

    // (19,4) dark-corner gate
    g_rec.gDark = 1;
    CHECK_EQ(NpcActionPerform_ShopTransaction(&a19, &b4), 50);
    g_rec.gDark = 0;
    CHECK_EQ(NpcActionPerform_ShopTransaction(&a19, &b4), 0);

    // neither pair
    CHECK_EQ(NpcActionPerform_ShopTransaction(&other, &b4), 0);
    SetNpcActionPerformHooks(nullptr);
}

TEST(NpcActionPerform, DrinkTavern_gateAndOps) {
    RReset();
    auto hk = MakeHooks();
    SetNpcActionPerformHooks(&hk);
    Ctx actor = MakeCtx(0, /*+4 actorId*/ 314);
    Ctx ctx   = MakeCtx(0, 0, /*+16 -> op72 byte*/ 0);
    ctx.buf[16] = 0x5A;

    g_rec.gTavern = 0;
    CHECK_EQ(NpcActionPerform_DrinkTavern(actor.buf, ctx.buf, 1), 0);
    CHECK_EQ(g_rec.op25.size(), (size_t)0);

    g_rec.gTavern = 1;
    CHECK_EQ(NpcActionPerform_DrinkTavern(actor.buf, ctx.buf, 1), 52);
    CHECK_EQ(g_rec.op25.size(), (size_t)1);
    CHECK_EQ(g_rec.op25[0][0], 314);
    CHECK_EQ(g_rec.op25[0][1], 456);
    CHECK_EQ(g_rec.op25[0][2], 0x1000000);
    CHECK_EQ(g_rec.op25[0][3], 4);
    CHECK_EQ(g_rec.op72.size(), (size_t)1);
    CHECK_EQ(g_rec.op72[0].first, 314);
    CHECK_EQ(g_rec.op72[0].second, 0x5A);
    CHECK_EQ(g_rec.op90.size(), (size_t)1);
    CHECK_EQ(g_rec.op90[0].first, 8);
    CHECK_EQ(g_rec.op90[0].second, 314);
    SetNpcActionPerformHooks(nullptr);
}

TEST(NpcActionPerform, Verdict_type21_menu) {
    RReset();
    auto hk = MakeHooks();
    SetNpcActionPerformHooks(&hk);
    Ctx good = MakeCtx(21, /*+4*/ 12, /*+16*/ 34);
    Ctx bad  = MakeCtx(20);
    CHECK_EQ(NpcActionPerform_Verdict(nullptr, bad.buf, 5), 0);
    CHECK_EQ(g_rec.dialogs.size(), (size_t)0);
    CHECK_EQ(NpcActionPerform_Verdict(nullptr, good.buf, 5), 54);
    CHECK_EQ(g_rec.dialogs.size(), (size_t)1);
    CHECK_EQ(g_rec.dialogs[0][0], 0);   // menu
    CHECK_EQ(g_rec.dialogs[0][1], 5);   // target
    CHECK_EQ(g_rec.dialogs[0][2], 3);   // kind
    CHECK_EQ(g_rec.dialogs[0][3], 12);
    CHECK_EQ(g_rec.dialogs[0][4], 34);
    SetNpcActionPerformHooks(nullptr);
}

TEST(NpcActionPerform, Arrest_type4_election) {
    RReset();
    auto hk = MakeHooks();
    SetNpcActionPerformHooks(&hk);
    Ctx good = MakeCtx(4, 60, 70);
    Ctx bad  = MakeCtx(5);
    CHECK_EQ(NpcActionPerform_Arrest(nullptr, bad.buf, 9), 0);
    CHECK_EQ(NpcActionPerform_Arrest(nullptr, good.buf, 9), 55);
    CHECK_EQ(g_rec.dialogs.size(), (size_t)1);
    CHECK_EQ(g_rec.dialogs[0][0], 1);   // election
    CHECK_EQ(g_rec.dialogs[0][2], 2);   // kind
    SetNpcActionPerformHooks(nullptr);
}

TEST(NpcActionPerform, PickupCarry_types7and22) {
    RReset();
    auto hk = MakeHooks();
    SetNpcActionPerformHooks(&hk);
    Ctx c7  = MakeCtx(7, 100, 0); c7.buf[16] = 11;
    Ctx c22 = MakeCtx(22, 200, 0); c22.buf[16] = 12;
    Ctx bad = MakeCtx(8);
    CHECK_EQ(NpcActionPerform_PickupCarry(nullptr, bad.buf, 1), 0);
    CHECK_EQ(NpcActionPerform_PickupCarry(nullptr, c7.buf, 3), 56);
    CHECK_EQ(NpcActionPerform_PickupCarry(nullptr, c22.buf, 4), 56);
    CHECK_EQ(g_rec.dialogs.size(), (size_t)2);
    CHECK_EQ(g_rec.dialogs[0][2], 11);  // kind = byte +16
    CHECK_EQ(g_rec.dialogs[0][3], 100);
    CHECK_EQ(g_rec.dialogs[1][2], 12);
    CHECK_EQ(g_rec.dialogs[1][3], 200);
    SetNpcActionPerformHooks(nullptr);
}

TEST(NpcActionPerform, InertHooks_noCrash) {
    SetNpcActionPerformHooks(nullptr);  // inert defaults
    Ctx ctx = MakeCtx(7, 1, 2);
    u8 live[400]; std::memset(live, 0, sizeof(live));
    // type matches, but every leaf is a no-op; result code still returned.
    CHECK_EQ(NpcActionPerform_Spionage(nullptr, ctx.buf, live), 37);
    CHECK_EQ(NpcActionPerform_EnterBuilding(), 0);  // no threaten, no reject -> 0
    CHECK_EQ(NpcActionPerform_UseBack(ctx.buf), 0); // slander absent -> 0
}
