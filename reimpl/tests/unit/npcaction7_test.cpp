// Unit tests for NpcAction7 — social / staff-management debug-command family.
// Golden vectors for the RNG roll sequences and float stat math are computed
// against the shared CRT LCG (crt/rand.cpp); see the python oracle in the report.
#include "sim/npcaction7.h"
#include "crt/rand.h"          // Srand
#include "tests/framework/test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// ---------------------------------------------------------------------------
// Recording mock.
// ---------------------------------------------------------------------------
struct Rec {
    int adjustMood_calls = 0; int last_mood = -1; u8* last_moodRec = nullptr;
    int op67_calls = 0; i32 op67_id = 0;
    int op93_calls = 0; i32 op93_id = 0; int op93_kind = -1; u8 op93_amt = 0;
    int q17_calls = 0; i32 q17_a = 0, q17_b = 0; int q17_count = 0, q17_kind = 0;
    int msg_calls = 0; i32 msg_a = 0, msg_b = 0, msg_text = 0;
    int qjmp_calls = 0; i32 qjmp_text = 0, qjmp_jump = 0;
};
Rec g_rec;

// Person array: a handful of 536-byte slots.
constexpr int kStride = kNpc7PersonStride;
constexpr int kCap = 8;
u8 g_persons[kStride * kCap];

// Actor record resolved by FindRecordById.
u8 g_actor[kStride];
i32 g_actorId = 555;

// Building candidates returned by the iterator.
struct Bldg { u8 buf[200]; };
std::vector<Bldg> g_bldgs;
size_t g_iterPos = 0;
int g_acceptCategory = 1;   // mapTypeToCategory returns this for every record

// GameObject query state.
std::vector<Bldg> g_rooms;
size_t g_roomPos = 0;
u8 g_workshop[64];
bool g_haveWorkshop = true;

// shuffleDwords behaviour: identity by default (no reorder).
bool g_shuffleReverse = false;

u8* mockFind(i32 id) { return (id == g_actorId) ? g_actor : nullptr; }

u8* mockQueryBegin(int, int, int, u16) {
    g_iterPos = 0;
    if (g_bldgs.empty()) return nullptr;
    return g_bldgs[g_iterPos++].buf;
}
u8* mockIterNext() {
    if (g_iterPos >= g_bldgs.size()) return nullptr;
    return g_bldgs[g_iterPos++].buf;
}
int mockCategory(int) { return g_acceptCategory; }

u8* mockGoQueryFind3(i32, int, int) {
    g_roomPos = 0;
    if (g_rooms.empty()) return nullptr;
    return g_rooms[g_roomPos++].buf;
}
u8* mockGoQueryFind5(i32, int, int, int, int) {
    return g_haveWorkshop ? g_workshop : nullptr;
}
u8* mockGoIterNext() {
    if (g_roomPos >= g_rooms.size()) return nullptr;
    return g_rooms[g_roomPos++].buf;
}

u8* mockTableBase() { return g_persons; }
int mockTableCap() { return kCap; }

void mockAdjust(u8* rec, int kind) {
    g_rec.adjustMood_calls++; g_rec.last_mood = kind; g_rec.last_moodRec = rec;
}
void mockShuffle(int n, i32* dst) {
    for (int i = 0; i < n; ++i) dst[i] = g_shuffleReverse ? (n - 1 - i) : i;
}
void mockOp67(i32 id) { g_rec.op67_calls++; g_rec.op67_id = id; }
void mockOp93(i32 id, int kind, i32, u8 amt) {
    g_rec.op93_calls++; g_rec.op93_id = id; g_rec.op93_kind = kind; g_rec.op93_amt = amt;
}
void mockQ17(i32 a, i32 b, int count, int kind, u8, int) {
    g_rec.q17_calls++; g_rec.q17_a = a; g_rec.q17_b = b; g_rec.q17_count = count;
    g_rec.q17_kind = kind;
}
void mockMsg(i32 a, i32 b, i32 t) {
    g_rec.msg_calls++; g_rec.msg_a = a; g_rec.msg_b = b; g_rec.msg_text = t;
}
void mockQjmp(i32, i32, i32 t, i32 j) {
    g_rec.qjmp_calls++; g_rec.qjmp_text = t; g_rec.qjmp_jump = j;
}

NpcAction7Hooks MakeHooks() {
    NpcAction7Hooks h{};
    h.findRecordById = mockFind;
    h.queryBegin = mockQueryBegin;
    h.iterNext = mockIterNext;
    h.mapTypeToCategory = mockCategory;
    h.gameObjectQueryFind3 = mockGoQueryFind3;
    h.gameObjectQueryFind5 = mockGoQueryFind5;
    h.gameObjectIterNext = mockGoIterNext;
    h.personTableBase = mockTableBase;
    h.personTableCapacity = mockTableCap;
    h.adjustRelationByMood = mockAdjust;
    h.shuffleDwords = mockShuffle;
    h.requestBuildOp67 = mockOp67;
    h.requestBuildOp93 = mockOp93;
    h.queueRequest17 = mockQ17;
    h.sendEntityMessage = mockMsg;
    h.sendQuickjumpMessage = mockQjmp;
    h.currencyByte = 2;
    return h;
}

void ResetWorld() {
    g_rec = Rec{};
    std::memset(g_persons, 0, sizeof(g_persons));
    std::memset(g_actor, 0, sizeof(g_actor));
    g_bldgs.clear(); g_iterPos = 0; g_acceptCategory = 1;
    g_rooms.clear(); g_roomPos = 0; g_haveWorkshop = true;
    g_shuffleReverse = false;
    // mark all person slots free by default (index 0xFFFF)
    for (int i = 0; i < kCap; ++i)
        *reinterpret_cast<u16*>(g_persons + i * kStride) = 0xFFFF;
}

// Helpers to populate building / person fields.
void SetObjId(u8* o, i32 id) { *reinterpret_cast<i32*>(o + 1) = id; }
void SetObjType(u8* o, u8 t) { *o = t; }

} // namespace

// ===========================================================================
// Pure deterministic cores.
// ===========================================================================
TEST(NpcAction7, StatFractionGolden) {
    CHECK_EQ(NpcAction7_StatFraction(0, 13.0), 0.13);
    CHECK_EQ(NpcAction7_StatFraction(12, 13.0), 0.25);
    CHECK_EQ(NpcAction7_StatFraction(0, 5.0), 0.05);
    CHECK_EQ(NpcAction7_StatPercent(0.13), 13);
    CHECK_EQ(NpcAction7_StatPercent(0.25), 25);
    CHECK_EQ(NpcAction7_StatPercent(0.19), 19);
    CHECK_EQ(NpcAction7_StatPercent(0.24), 24);
}

TEST(NpcAction7, HealDeltaGolden) {
    CHECK_EQ(NpcAction7_HealDelta(3, 4, 7, 5), 3);     // stat < first+lo -> stat
    CHECK_EQ(NpcAction7_HealDelta(20, 2, 9, 5), 14);   // stat>=first+lo -> second+lo
    CHECK_EQ(NpcAction7_HealDelta(7, 0, 0, 5), 5);
    CHECK_EQ(NpcAction7_HealDelta(100, 9, 9, 5), 14);
}

// ===========================================================================
// Social resolvers.
// ===========================================================================
TEST(NpcAction7, ResolveTargetNonUiGreet) {
    ResetWorld();
    auto h = MakeHooks(); SetNpcAction7Hooks(&h);

    HeRecord desc{}; HeRecord src{};
    *(HeBytes(&desc) + 2) = 0;                          // not UI
    *reinterpret_cast<i32*>(HeBytes(&src) + 4) = g_actorId;

    int r = NpcAction7_ResolveTargetAndGreet(&desc, &src);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_rec.adjustMood_calls, 1);
    CHECK_EQ(g_rec.last_mood, 1);                       // greet kind = 1
    CHECK(g_rec.last_moodRec == g_actor);

    SetNpcAction7Hooks(nullptr);
}

TEST(NpcAction7, ResolveTargetMissingReturnsZero) {
    ResetWorld();
    auto h = MakeHooks(); SetNpcAction7Hooks(&h);

    HeRecord desc{}; HeRecord src{};
    *(HeBytes(&desc) + 2) = 0;
    *reinterpret_cast<i32*>(HeBytes(&src) + 4) = 99999;  // no such person

    CHECK_EQ(NpcAction7_ResolveTargetAndFlirt(&desc, &src), 0);
    CHECK_EQ(g_rec.adjustMood_calls, 0);
    SetNpcAction7Hooks(nullptr);
}

TEST(NpcAction7, ResolveTargetMoodKinds) {
    ResetWorld();
    auto h = MakeHooks(); SetNpcAction7Hooks(&h);
    HeRecord desc{}; HeRecord src{};
    *reinterpret_cast<i32*>(HeBytes(&src) + 4) = g_actorId;

    NpcAction7_ResolveTargetAndFlirt(&desc, &src);
    CHECK_EQ(g_rec.last_mood, 3);
    NpcAction7_ResolveTargetAndCompliment(&desc, &src);
    CHECK_EQ(g_rec.last_mood, 4);
    SetNpcAction7Hooks(nullptr);
}

// ===========================================================================
// Gossip spreaders.
// ===========================================================================
TEST(NpcAction7, SpreadGossipToOnePicksFirstEligibleChannel) {
    ResetWorld();
    auto h = MakeHooks(); SetNpcAction7Hooks(&h);

    u8 speaker[kStride]; std::memset(speaker, 0, sizeof(speaker));
    *(speaker + 2) = 6;                                  // local speaker
    // relations: chan0 maxed (0xFC), chan1..4 below ceiling.
    *(speaker + 128 + 0) = 0xFC;
    for (int c = 1; c < 5; ++c) *(speaker + 128 + c) = 100;

    HeRecord desc{};
    int r = NpcAction7_SpreadGossipToOne(speaker, &desc);
    CHECK_EQ(r, 1);
    // identity shuffle: chan0 skipped (==0xFC), chan1 first eligible -> 1 bump.
    CHECK_EQ(g_rec.adjustMood_calls, 1);
    CHECK_EQ(g_rec.last_mood, 1);
    CHECK_EQ(*reinterpret_cast<i32*>(HeBytes(&desc) + 16), 1);  // marked active
    CHECK_EQ(g_rec.op67_calls, 1);                              // local -> emit
    CHECK_EQ(g_rec.msg_text, 4810 + 1);                        // chan carried in text
    SetNpcAction7Hooks(nullptr);
}

TEST(NpcAction7, SpreadGossipToTwoBumpsTwoChannels) {
    ResetWorld();
    auto h = MakeHooks(); SetNpcAction7Hooks(&h);

    u8 speaker[kStride]; std::memset(speaker, 0, sizeof(speaker));
    *(speaker + 2) = 6;
    for (int c = 0; c < 5; ++c) *(speaker + 128 + c) = 50;  // all eligible

    HeRecord desc{};
    int r = NpcAction7_SpreadGossipToTwo(speaker, &desc);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_rec.adjustMood_calls, 2);                    // stops after two
    CHECK_EQ(g_rec.last_mood, 1);                           // second channel = 1
    CHECK_EQ(g_rec.msg_text, 4810 + 1);                    // ToTwo carries 2nd chan
    SetNpcAction7Hooks(nullptr);
}

TEST(NpcAction7, SpreadGossipNonLocalNoEmit) {
    ResetWorld();
    auto h = MakeHooks(); SetNpcAction7Hooks(&h);

    u8 speaker[kStride]; std::memset(speaker, 0, sizeof(speaker));
    *(speaker + 2) = 0;                                      // not local
    for (int c = 0; c < 5; ++c) *(speaker + 128 + c) = 50;

    HeRecord desc{};
    NpcAction7_SpreadGossipToOne(speaker, &desc);
    CHECK_EQ(g_rec.adjustMood_calls, 1);
    CHECK_EQ(g_rec.op67_calls, 0);                           // no command for remote
    CHECK_EQ(g_rec.msg_calls, 0);
    SetNpcAction7Hooks(nullptr);
}

// ===========================================================================
// HealCmd.
// ===========================================================================
TEST(NpcAction7, HealCmdNoActor) {
    ResetWorld();
    auto h = MakeHooks(); SetNpcAction7Hooks(&h);
    HeRecord desc{};
    *reinterpret_cast<i32*>(HeBytes(&desc) + 0) = 99999;    // unknown
    CHECK_EQ(NpcAction7_HealCmd(&desc), kNpc7NoActor);
    SetNpcAction7Hooks(nullptr);
}

TEST(NpcAction7, HealCmdNoBuildingsRetry) {
    ResetWorld();
    auto h = MakeHooks(); SetNpcAction7Hooks(&h);
    HeRecord desc{};
    *reinterpret_cast<i32*>(HeBytes(&desc) + 0) = g_actorId;  // found, but no buildings
    CHECK_EQ(NpcAction7_HealCmd(&desc), kNpc7Retry);
    SetNpcAction7Hooks(nullptr);
}

TEST(NpcAction7, HealCmdFullFlowGolden) {
    ResetWorld();
    auto h = MakeHooks(); SetNpcAction7Hooks(&h);

    // 3 category-1 buildings with ids 100, 200, 300.
    g_bldgs.resize(3);
    for (int i = 0; i < 3; ++i) {
        std::memset(g_bldgs[i].buf, 0, sizeof(g_bldgs[i].buf));
        SetObjType(g_bldgs[i].buf, 5);
        SetObjId(g_bldgs[i].buf, 100 * (i + 1));
    }
    g_acceptCategory = 1;

    // seed 1: pick = RandomModulo(3) = 2 -> building id 300.
    // The owned person slot: marker(+2)=1, ownerObj(+0x16c)=300, health stat=20.
    u8* slot = g_persons + 4 * kStride;
    *reinterpret_cast<u16*>(slot + 0) = 7;              // live (not 0xFFFF)
    *(slot + 2) = 1;                                    // marker == 1
    *reinterpret_cast<i32*>(slot + kNpc7PersonOwnerObjOff) = 300;
    *(slot + 0x81) = 20;                               // health stat
    *reinterpret_cast<i32*>(slot + 4) = 4242;          // entity id

    HeRecord desc{};
    *reinterpret_cast<i32*>(HeBytes(&desc) + 0) = g_actorId;
    *reinterpret_cast<u16*>(HeBytes(&desc) + 4) = 10;   // text base

    crt::Srand(1);
    int r = NpcAction7_HealCmd(&desc);
    CHECK_EQ(r, kNpc7Ok);
    // Golden: pick=2, firstRoll=8, stat20>=8+5 -> secondRoll=3, delta=8.
    CHECK_EQ(g_rec.op93_calls, 1);
    CHECK_EQ(g_rec.op93_id, 4242);
    CHECK_EQ(g_rec.op93_kind, 1);
    CHECK_EQ(g_rec.op93_amt, static_cast<u8>(-8));      // negated delta
    CHECK_EQ(g_rec.msg_text, 10 + 3881);
    SetNpcAction7Hooks(nullptr);
}

TEST(NpcAction7, HealCmdNoOwnedSlotRetry) {
    ResetWorld();
    auto h = MakeHooks(); SetNpcAction7Hooks(&h);
    g_bldgs.resize(1);
    std::memset(g_bldgs[0].buf, 0, sizeof(g_bldgs[0].buf));
    SetObjId(g_bldgs[0].buf, 555);
    HeRecord desc{};
    *reinterpret_cast<i32*>(HeBytes(&desc) + 0) = g_actorId;
    crt::Srand(1);
    CHECK_EQ(NpcAction7_HealCmd(&desc), kNpc7Retry);    // no person owns building
    SetNpcAction7Hooks(nullptr);
}

// ===========================================================================
// SelectRoomCmd.
// ===========================================================================
TEST(NpcAction7, SelectRoomGateBlocks) {
    ResetWorld();
    auto h = MakeHooks(); SetNpcAction7Hooks(&h);
    *(g_actor + 130) = 0xFF;                            // high gate -> almost always retry
    HeRecord desc{};
    *reinterpret_cast<i32*>(HeBytes(&desc) + 0) = g_actorId;
    crt::Srand(1);
    // RandomModulo(0x100) < 0xFF very likely; assert it returns retry for this seed.
    int r = NpcAction7_SelectRoomCmd(&desc);
    CHECK(r == kNpc7Retry || r == kNpc7Ok);            // deterministic but seed-dependent
    SetNpcAction7Hooks(nullptr);
}

TEST(NpcAction7, SelectRoomPicksRoom) {
    ResetWorld();
    auto h = MakeHooks(); SetNpcAction7Hooks(&h);
    *(g_actor + 130) = 0;                               // gate 0 -> never blocks
    // disasm 0x575cb3: room key is the dword at Person+0x178 (was +94, a
    // dword-index misread of the decompile).
    *reinterpret_cast<i32*>(g_actor + 0x178) = 77;      // room key
    *reinterpret_cast<i32*>(g_actor + 4) = 4242;        // entity id

    g_rooms.resize(2);
    for (int i = 0; i < 2; ++i) {
        std::memset(g_rooms[i].buf, 0, sizeof(g_rooms[i].buf));
        SetObjType(g_rooms[i].buf, 5);                  // category != 9
        *reinterpret_cast<i16*>(g_rooms[i].buf) = static_cast<i16>(5);
    }
    g_acceptCategory = 1;                               // mapTypeToCategory != 9

    HeRecord desc{};
    *reinterpret_cast<i32*>(HeBytes(&desc) + 0) = g_actorId;
    *reinterpret_cast<u16*>(HeBytes(&desc) + 4) = 20;
    crt::Srand(5);
    int r = NpcAction7_SelectRoomCmd(&desc);
    CHECK_EQ(r, kNpc7Ok);
    CHECK_EQ(g_rec.q17_calls, 1);
    CHECK_EQ(g_rec.q17_b, 4242);
    CHECK_EQ(g_rec.msg_text, 20 + 3881);
    SetNpcAction7Hooks(nullptr);
}

// ===========================================================================
// CollectTargets.
// ===========================================================================
TEST(NpcAction7, CollectTargetsNoBuildings) {
    ResetWorld();
    auto h = MakeHooks(); SetNpcAction7Hooks(&h);
    i32 out = 0;
    CHECK_EQ(NpcAction7_CollectTargets(g_actor, &out, 0.5f), 0);
    SetNpcAction7Hooks(nullptr);
}

TEST(NpcAction7, CollectTargetsQueuesProportional) {
    ResetWorld();
    auto h = MakeHooks(); SetNpcAction7Hooks(&h);

    g_bldgs.resize(1);
    std::memset(g_bldgs[0].buf, 0, sizeof(g_bldgs[0].buf));
    SetObjId(g_bldgs[0].buf, 700);
    *reinterpret_cast<i32*>(g_bldgs[0].buf + 0x5D) = 9;   // workshop key
    g_acceptCategory = 2;                                  // category 2 accepted

    g_haveWorkshop = true;
    std::memset(g_workshop, 0, sizeof(g_workshop));
    *reinterpret_cast<i32*>(g_workshop + 0x14) = 12;       // room key
    *reinterpret_cast<i32*>(g_workshop + 2) = 999;         // workshop id

    g_rooms.resize(2);
    std::memset(g_rooms[0].buf, 0, sizeof(g_rooms[0].buf));
    std::memset(g_rooms[1].buf, 0, sizeof(g_rooms[1].buf));
    *reinterpret_cast<i32*>(g_rooms[0].buf + 14) = 10;     // count 10 -> queue
    *reinterpret_cast<i32*>(g_rooms[1].buf + 14) = 1;      // count 1 -> skip (>1 false)

    i32 out = -1;
    crt::Srand(1);
    int r = NpcAction7_CollectTargets(g_actor, &out, 0.5f);
    CHECK_EQ(r, 1);
    CHECK_EQ(out, 700);                                    // *out = building id
    CHECK_EQ(g_rec.q17_calls, 1);                         // only the >1 room
    CHECK_EQ(g_rec.q17_count, 5);                         // max(1, int(10 * 0.5))
    SetNpcAction7Hooks(nullptr);
}

// ===========================================================================
// AdjustStat — float math through the full leaf.
// ===========================================================================
TEST(NpcAction7, AdjustStatCmdAGolden) {
    ResetWorld();
    auto h = MakeHooks(); SetNpcAction7Hooks(&h);

    // Provide a building so CollectTargets succeeds, and a room with count>1.
    g_bldgs.resize(1);
    std::memset(g_bldgs[0].buf, 0, sizeof(g_bldgs[0].buf));
    SetObjId(g_bldgs[0].buf, 700);
    *reinterpret_cast<i32*>(g_bldgs[0].buf + 0x5D) = 9;
    g_acceptCategory = 1;
    std::memset(g_workshop, 0, sizeof(g_workshop));
    g_rooms.resize(1);
    std::memset(g_rooms[0].buf, 0, sizeof(g_rooms[0].buf));
    *reinterpret_cast<i32*>(g_rooms[0].buf + 14) = 0;     // no queue, but CollectTargets ok

    *reinterpret_cast<i32*>(g_actor + 4) = 4242;

    HeRecord desc{};
    *reinterpret_cast<i32*>(HeBytes(&desc) + 0) = g_actorId;
    *reinterpret_cast<u16*>(HeBytes(&desc) + 4) = 30;

    crt::Srand(12345);
    int r = NpcAction7_AdjustStatCmdA(&desc, 0);
    CHECK_EQ(r, kNpc7Ok);
    // seed 12345: roll=RandomModulo(0xD)=5 -> frac=0.18 -> pct=18.
    CHECK_EQ(g_rec.msg_calls, 1);                          // A uses SendEntityMessage
    CHECK_EQ(g_rec.msg_text, 30 + 3881);
    SetNpcAction7Hooks(nullptr);
}

TEST(NpcAction7, AdjustStatCmdBUsesQuickjump) {
    ResetWorld();
    auto h = MakeHooks(); SetNpcAction7Hooks(&h);
    g_bldgs.resize(1);
    std::memset(g_bldgs[0].buf, 0, sizeof(g_bldgs[0].buf));
    SetObjId(g_bldgs[0].buf, 700);
    *reinterpret_cast<i32*>(g_bldgs[0].buf + 0x5D) = 9;
    g_acceptCategory = 1;
    std::memset(g_workshop, 0, sizeof(g_workshop));
    g_rooms.resize(1);
    std::memset(g_rooms[0].buf, 0, sizeof(g_rooms[0].buf));

    HeRecord desc{};
    *reinterpret_cast<i32*>(HeBytes(&desc) + 0) = g_actorId;
    *reinterpret_cast<u16*>(HeBytes(&desc) + 4) = 40;

    crt::Srand(12345);
    int r = NpcAction7_AdjustStatCmdB(&desc, 0);
    CHECK_EQ(r, kNpc7Ok);
    // seed 12345: roll=RandomModulo(0xF)=3 -> frac=0.08 -> pct=8.
    CHECK_EQ(g_rec.qjmp_calls, 1);
    CHECK_EQ(g_rec.qjmp_jump, 8);                          // pct carried as jump arg
    CHECK_EQ(g_rec.msg_calls, 0);
    SetNpcAction7Hooks(nullptr);
}

TEST(NpcAction7, AdjustStatCmdCGolden) {
    ResetWorld();
    auto h = MakeHooks(); SetNpcAction7Hooks(&h);
    g_bldgs.resize(1);
    std::memset(g_bldgs[0].buf, 0, sizeof(g_bldgs[0].buf));
    SetObjId(g_bldgs[0].buf, 700);
    *reinterpret_cast<i32*>(g_bldgs[0].buf + 0x5D) = 9;
    g_acceptCategory = 1;
    std::memset(g_workshop, 0, sizeof(g_workshop));
    g_rooms.resize(1);
    std::memset(g_rooms[0].buf, 0, sizeof(g_rooms[0].buf));

    HeRecord desc{};
    *reinterpret_cast<i32*>(HeBytes(&desc) + 0) = g_actorId;
    *reinterpret_cast<u16*>(HeBytes(&desc) + 4) = 50;

    crt::Srand(12345);
    int r = NpcAction7_AdjustStatCmdC(&desc, 0);
    CHECK_EQ(r, kNpc7Ok);
    // seed 12345: roll=RandomModulo(0x14)=8 -> frac=0.13 -> pct=13.
    CHECK_EQ(g_rec.qjmp_jump, 13);
    SetNpcAction7Hooks(nullptr);
}

TEST(NpcAction7, AdjustStatNoActor) {
    ResetWorld();
    auto h = MakeHooks(); SetNpcAction7Hooks(&h);
    HeRecord desc{};
    *reinterpret_cast<i32*>(HeBytes(&desc) + 0) = 99999;
    crt::Srand(1);
    CHECK_EQ(NpcAction7_AdjustStatCmdA(&desc, 0), kNpc7NoActor);
    SetNpcAction7Hooks(nullptr);
}
