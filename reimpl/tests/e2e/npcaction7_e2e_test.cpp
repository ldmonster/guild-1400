// End-to-end flow for NpcAction7: a single AI "manager" actor cycles through the
// social and staff-management commands against a small mock world, and we assert
// the cross-module effects (relation bumps, build commands, status messages) and
// the deterministic RNG-driven picks line up across the whole sequence.
#include "sim/npcaction7.h"
#include "crt/rand.h"
#include "tests/framework/test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

constexpr int kStride = kNpc7PersonStride;
constexpr int kCap = 8;

struct World {
    u8 persons[kStride * kCap];
    u8 actor[kStride];
    i32 actorId = 42;

    struct Buf { u8 b[200]; };
    std::vector<Buf> bldgs;
    size_t iterPos = 0;
    int category = 1;

    std::vector<Buf> rooms;
    size_t roomPos = 0;
    u8 workshop[64];
    bool haveWorkshop = true;

    // effect log
    int moodBumps = 0;
    int op93 = 0; u8 op93amt = 0;
    int q17 = 0;
    int msgs = 0; int lastMsgText = 0;
    int qjmp = 0; int lastJump = 0;
};
World g;

u8* find(i32 id) { return id == g.actorId ? g.actor : nullptr; }
u8* qbegin(int, int, int, u16) { g.iterPos = 0; return g.bldgs.empty() ? nullptr : g.bldgs[g.iterPos++].b; }
u8* inext() { return g.iterPos >= g.bldgs.size() ? nullptr : g.bldgs[g.iterPos++].b; }
int cat(int) { return g.category; }
u8* gf3(i32, int, int) { g.roomPos = 0; return g.rooms.empty() ? nullptr : g.rooms[g.roomPos++].b; }
u8* gf5(i32, int, int, int, int) { return g.haveWorkshop ? g.workshop : nullptr; }
u8* ginext() { return g.roomPos >= g.rooms.size() ? nullptr : g.rooms[g.roomPos++].b; }
u8* tbase() { return g.persons; }
int tcap() { return kCap; }
void adjust(u8*, int) { g.moodBumps++; }
void shuffle(int n, i32* d) { for (int i = 0; i < n; ++i) d[i] = i; }
void op67(i32) {}
void op93(i32, int, i32, u8 amt) { g.op93++; g.op93amt = amt; }
void q17(i32, i32, int, int, u8, int) { g.q17++; }
void msg(i32, i32, i32 t) { g.msgs++; g.lastMsgText = t; }
void qjmp(i32, i32, i32, i32 j) { g.qjmp++; g.lastJump = j; }

NpcAction7Hooks Hooks() {
    NpcAction7Hooks h{};
    h.findRecordById = find; h.queryBegin = qbegin; h.iterNext = inext;
    h.mapTypeToCategory = cat; h.gameObjectQueryFind3 = gf3;
    h.gameObjectQueryFind5 = gf5; h.gameObjectIterNext = ginext;
    h.personTableBase = tbase; h.personTableCapacity = tcap;
    h.adjustRelationByMood = adjust; h.shuffleDwords = shuffle;
    h.requestBuildOp67 = op67; h.requestBuildOp93 = op93; h.queueRequest17 = q17;
    h.sendEntityMessage = msg; h.sendQuickjumpMessage = qjmp; h.currencyByte = 2;
    return h;
}

void SetupWorld() {
    std::memset(g.persons, 0, sizeof(g.persons));
    std::memset(g.actor, 0, sizeof(g.actor));
    for (int i = 0; i < kCap; ++i)
        *reinterpret_cast<u16*>(g.persons + i * kStride) = 0xFFFF;
    g.bldgs.clear(); g.iterPos = 0; g.category = 1;
    g.rooms.clear(); g.roomPos = 0; g.haveWorkshop = true;
    g.moodBumps = g.op93 = g.q17 = g.msgs = g.qjmp = 0;
    g.lastMsgText = g.lastJump = 0; g.op93amt = 0;
}

} // namespace

TEST(NpcAction7E2E, ManagerActionCycle) {
    SetupWorld();
    auto h = Hooks(); SetNpcAction7Hooks(&h);

    // --- Step 1: a flirt resolves to a known person and bumps the mood. ---
    HeRecord desc{}; HeRecord src{};
    *reinterpret_cast<i32*>(HeBytes(&src) + 4) = g.actorId;
    CHECK_EQ(NpcAction7_ResolveTargetAndFlirt(&desc, &src), 1);
    CHECK_EQ(g.moodBumps, 1);

    // --- Step 2: the actor spreads a rumor across two of its relation channels. ---
    u8 speaker[kStride]; std::memset(speaker, 0, sizeof(speaker));
    *(speaker + 2) = 6;                               // local speaker
    for (int c = 0; c < 5; ++c) *(speaker + 128 + c) = 60;  // all eligible
    HeRecord gdesc{};
    CHECK_EQ(NpcAction7_SpreadGossipToTwo(speaker, &gdesc), 1);
    CHECK_EQ(g.moodBumps, 3);                          // +2 from gossip
    CHECK_EQ(*reinterpret_cast<i32*>(HeBytes(&gdesc) + 16), 1);

    // --- Step 3: a heal command on a house the actor owns. ---
    g.bldgs.resize(2);
    for (int i = 0; i < 2; ++i) {
        std::memset(g.bldgs[i].b, 0, sizeof(g.bldgs[i].b));
        *g.bldgs[i].b = 5;
        *reinterpret_cast<i32*>(g.bldgs[i].b + 1) = 100 * (i + 1);  // ids 100,200
    }
    g.category = 1;
    // person owning building 100, marker 1, stat 30, entity 7
    u8* owner = g.persons + 1 * kStride;
    *reinterpret_cast<u16*>(owner) = 3;               // live
    *(owner + 2) = 1;
    *reinterpret_cast<i32*>(owner + kNpc7PersonOwnerObjOff) = 100;
    *(owner + 0x81) = 30;
    *reinterpret_cast<i32*>(owner + 4) = 7;
    // also person for building 200
    u8* owner2 = g.persons + 2 * kStride;
    *reinterpret_cast<u16*>(owner2) = 4;
    *(owner2 + 2) = 1;
    *reinterpret_cast<i32*>(owner2 + kNpc7PersonOwnerObjOff) = 200;
    *(owner2 + 0x81) = 30;
    *reinterpret_cast<i32*>(owner2 + 4) = 8;

    HeRecord hdesc{};
    *reinterpret_cast<i32*>(HeBytes(&hdesc) + 0) = g.actorId;
    *reinterpret_cast<u16*>(HeBytes(&hdesc) + 4) = 11;
    crt::Srand(99);
    int hr = NpcAction7_HealCmd(&hdesc);
    CHECK_EQ(hr, kNpc7Ok);
    CHECK_EQ(g.op93, 1);
    CHECK(g.op93amt != 0);                             // a non-zero (negated) delta
    CHECK_EQ(g.lastMsgText, 11 + 3881);

    // --- Step 4: an AdjustStat (B) command rolls a percent and quickjumps. ---
    g.rooms.resize(1);
    std::memset(g.rooms[0].b, 0, sizeof(g.rooms[0].b));
    std::memset(g.workshop, 0, sizeof(g.workshop));
    HeRecord adesc{};
    *reinterpret_cast<i32*>(HeBytes(&adesc) + 0) = g.actorId;
    *reinterpret_cast<u16*>(HeBytes(&adesc) + 4) = 12;
    crt::Srand(12345);
    int ar = NpcAction7_AdjustStatCmdB(&adesc, 0);
    CHECK_EQ(ar, kNpc7Ok);
    CHECK_EQ(g.qjmp, 1);
    CHECK_EQ(g.lastJump, 8);                           // seed 12345 -> roll 3 -> pct 8

    SetNpcAction7Hooks(nullptr);
}

TEST(NpcAction7E2E, InertHooksAreSafe) {
    // With no hooks installed, every leaf degenerates gracefully (no world).
    SetNpcAction7Hooks(nullptr);
    HeRecord desc{}; HeRecord src{};
    CHECK_EQ(NpcAction7_ResolveTargetAndGreet(&desc, &src), 0);  // no person resolver
    *reinterpret_cast<i32*>(HeBytes(&desc) + 0) = 1;
    CHECK_EQ(NpcAction7_HealCmd(&desc), kNpc7NoActor);
    CHECK_EQ(NpcAction7_AdjustStatCmdA(&desc, 0), kNpc7NoActor);
    i32 out = 0;
    u8 rec[8] = {0};
    CHECK_EQ(NpcAction7_CollectTargets(rec, &out, 0.5f), 0);
}
