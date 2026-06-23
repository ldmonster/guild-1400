#include "test.h"

#include "sim/character_path.h"
#include "sim/character_query.h"   // g_live, LiveActor, ResetCharacterQuery

#include <cstring>
#include <cstdint>

using namespace guild;
using namespace guild::sim;

namespace {

// A reusable record arena for the slot tests: AllocSlot returns a 516-byte block
// whose dword[0] is the slot index. We hand AllocDebug fixed-size buffers so the
// LiveActor view (slotIndex at +0) lands in real memory.
struct SlotEnv {
    static constexpr int kMax = 600;
    unsigned char pool[kMax][520];
    int next;
    SlotEnv() : next(0) { std::memset(pool, 0, sizeof(pool)); }
    void* take() { return next < kMax ? pool[next++] : nullptr; }
};
SlotEnv* g_env = nullptr;

void* TestAlloc(unsigned, const char*) { return g_env ? g_env->take() : nullptr; }
void  TestFree(void*) {}
void  TestClear(int v, int c, void* base) { if (base) std::memset(base, v, (unsigned)c); }
int   g_errorCount = 0;
void  TestError(const char*) { ++g_errorCount; }

CharacterPathHooks SlotHooks() {
    CharacterPathHooks h = CharacterPathGetHooks();  // start from defaults
    h.allocDebug = TestAlloc;
    h.freeDebug = TestFree;
    h.clearRecord = TestClear;
    h.reportError = TestError;
    return h;
}

}  // namespace

// --------------------------------------------------------------------------
// AllocSlot / AllocSlotAtIndex
// --------------------------------------------------------------------------
TEST(CharPathSlot, AllocSlotFillsFirstFree) {
    ResetCharacterQuery();
    SlotEnv env; g_env = &env; g_errorCount = 0;
    CharacterPathHooks h = SlotHooks();
    CharacterPathHooks prev = CharacterPathSetHooks(&h);

    LiveActor* a = AllocSlot();
    CHECK(a != nullptr);
    if (a) {
        CHECK_EQ(a->slotIndex, 0);
        CHECK_EQ((void*)g_live[0], (void*)a);
    }
    LiveActor* b = AllocSlot();
    CHECK(b != nullptr);
    if (b) {
        CHECK_EQ(b->slotIndex, 1);
        CHECK_EQ((void*)g_live[1], (void*)b);
    }
    CHECK_EQ(g_errorCount, 0);
    CharacterPathSetHooks(&prev);
    g_env = nullptr;
}

TEST(CharPathSlot, AllocSlotOverflowReportsAndReturnsNull) {
    ResetCharacterQuery();
    SlotEnv env; g_env = &env; g_errorCount = 0;
    CharacterPathHooks h = SlotHooks();
    CharacterPathHooks prev = CharacterPathSetHooks(&h);

    // Fill every slot.
    for (int i = 0; i < kCharSlotCount; ++i) {
        LiveActor* a = AllocSlot();
        CHECK(a != nullptr);
    }
    // Next alloc overflows -> null + one error report.
    LiveActor* over = AllocSlot();
    CHECK_EQ((void*)over, (void*)nullptr);
    CHECK_EQ(g_errorCount, 1);

    CharacterPathSetHooks(&prev);
    g_env = nullptr;
}

TEST(CharPathSlot, AllocSlotAtIndexHonoursOccupancy) {
    ResetCharacterQuery();
    SlotEnv env; g_env = &env;
    CharacterPathHooks h = SlotHooks();
    CharacterPathHooks prev = CharacterPathSetHooks(&h);

    LiveActor* a = AllocSlotAtIndex(7);
    CHECK(a != nullptr);
    if (a) {
        CHECK_EQ(a->slotIndex, 7);
        CHECK_EQ((void*)g_live[7], (void*)a);
    }
    // Index already occupied -> null, slot unchanged.
    LiveActor* again = AllocSlotAtIndex(7);
    CHECK_EQ((void*)again, (void*)nullptr);
    CHECK_EQ((void*)g_live[7], (void*)a);

    CharacterPathSetHooks(&prev);
    g_env = nullptr;
}

// --- HARDENING: AllocSlotAtIndex at the top of the 512-entry live table ------
// The live table g_live has exactly kLiveCapacity (512) slots; index 511 is the
// last valid one. Pin that allocating there writes inside the table (no OOB) and
// the slot-index round-trips. (Indices outside [0,512) are an upstream/save-file
// concern the original did not bound — see progress doc, BEHAVIORAL/needs-MCP.)
TEST(CharPathSlot, AllocSlotAtIndexTopOfTableInBounds) {
    ResetCharacterQuery();
    SlotEnv env; g_env = &env;
    CharacterPathHooks h = SlotHooks();
    CharacterPathHooks prev = CharacterPathSetHooks(&h);

    const int last = kLiveCapacity - 1;   // 511
    LiveActor* a = AllocSlotAtIndex(last);
    CHECK(a != nullptr);
    if (a) {
        CHECK_EQ(a->slotIndex, last);
        CHECK_EQ((void*)g_live[last], (void*)a);
    }
    CharacterPathSetHooks(&prev);
    g_env = nullptr;
}

// --- HARDENING: WaitSlotCallback queue boundaries ---------------------------
// The 960-byte template is scanned in 64-byte steps (15 entries). With an all-zero
// template no entry is selectable, so the function walks to the end and returns
// (queueCount < 32) WITHOUT appending (no write past the queue). With a selectable
// entry it appends actorId at slotRec[count] and bumps the count.
namespace {
int WaitSelectAll(int /*entry*/, const unsigned char* /*rec*/) { return 1; }
} // namespace

TEST(CharPathWait, AllZeroTemplateNoAppendNoOverrun) {
    unsigned char tmpl[960];
    std::memset(tmpl, 0, sizeof(tmpl));   // every entry byte0 == 0 -> never selected
    int slotRec[33];
    std::memset(slotRec, 0, sizeof(slotRec));
    slotRec[32] = 5;                       // queue count
    bool room = WaitSlotCallback(/*actorId*/ 77, slotRec, tmpl, WaitSelectAll);
    CHECK(room);                           // 5 < 32
    CHECK_EQ(slotRec[32], 5);              // count unchanged (nothing appended)
    CHECK_EQ(slotRec[5], 0);              // no actor written into the queue
}

TEST(CharPathWait, AppendsAtCountAndBumps) {
    unsigned char tmpl[960];
    std::memset(tmpl, 0, sizeof(tmpl));
    tmpl[0] = 1;                            // first entry non-zero -> selectable
    int slotRec[33];
    std::memset(slotRec, 0, sizeof(slotRec));
    slotRec[32] = 3;                       // append at index 3
    // callback selects (returns 1) the first non-zero entry -> append actorId there.
    bool room = WaitSlotCallback(/*actorId*/ 99, slotRec, tmpl, WaitSelectAll);
    CHECK(room);                           // 4 < 32 after the append
    CHECK_EQ(slotRec[32], 4);              // count bumped 3 -> 4
    CHECK_EQ(slotRec[3], 99);              // actorId written at the old count, in-bounds
}

// --- HARDENING: PickWaitAnimation index stays in the 32-int buffer ----------
// The collector fills a 32-int buffer; the engine indexes it by (u16)rand % count.
// At the max safe count (32) the index is in [0,31]. count==0 returns 0; a count
// whose low word is 0 returns buf[0].
namespace {
int g_collectCount = 0;
int CollectFixed(int* out, int /*cap*/) {
    for (int i = 0; i < g_collectCount && i < 32; ++i) out[i] = 100 + i;
    return g_collectCount;
}
unsigned RandZero() { return 0; }
unsigned RandBig()  { return 33; }   // 33 % 32 == 1
} // namespace

TEST(CharPathPick, EmptyCollectionReturnsZero) {
    g_collectCount = 0;
    CHECK_EQ(PickWaitAnimation(CollectFixed, RandZero), 0);
}

TEST(CharPathPick, MaxCountIndexInBounds) {
    g_collectCount = 32;                   // fills the whole buffer
    // RandBig() % 32 == 1 -> buf[1] == 101 ; index 1 is inside the 32-int buffer.
    CHECK_EQ(PickWaitAnimation(CollectFixed, RandBig), 101);
    // rand 0 -> buf[0] == 100.
    CHECK_EQ(PickWaitAnimation(CollectFixed, RandZero), 100);
}

// --------------------------------------------------------------------------
// FindNearbyWide
// --------------------------------------------------------------------------
namespace {
bool BoxTol(const float* a, const float* b, float tol) {
    for (int i = 0; i < 3; ++i) {
        float d = a[i] - b[i];
        if (d < 0) d = -d;
        if (d > tol) return false;
    }
    return true;
}
bool TestVecTol(const float* a, const float* b, float tol) { return BoxTol(a, b, tol); }

// Build a LiveActor with a mesh at world pos (x,y,z), sharing a caller-owned
// Universe pointer (the original groups by the +136 universe pointer).
struct ActorBuilder {
    LiveActor actor{};
    MeshHandle mesh{};
    ActorBuilder(Universe* uni, int uid, int gid, float x, float y, float z, u8 cull = 0) {
        mesh.cullGate = cull;
        mesh.pos[0] = x; mesh.pos[1] = y; mesh.pos[2] = z;
        actor.mesh = &mesh;
        actor.universe = uni;
        actor.universeId = uid;
        actor.groupId = gid;
    }
};
}  // namespace

TEST(CharPathNearby, FindNearbyWideCountsSameUniverseInRange) {
    ResetCharacterQuery();
    CharacterPathHooks h = CharacterPathGetHooks();
    h.vectorWithinTolerance = TestVecTol;
    CharacterPathHooks prev = CharacterPathSetHooks(&h);

    static Universe uniA{};
    static Universe uniB{};
    static ActorBuilder self(&uniA, 1, 0, 0, 0, 0);
    static ActorBuilder near1(&uniA, 1, 0, 100, 0, 0);     // in range (|100|<=300)
    static ActorBuilder near2(&uniA, 1, 0, 0, 250, 0);     // in range
    static ActorBuilder far1(&uniA, 1, 0, 1000, 0, 0);     // out of range
    static ActorBuilder otherUni(&uniB, 1, 0, 10, 0, 0);   // different universe ptr
    static ActorBuilder culled(&uniA, 1, 0, 5, 0, 0, 1);   // mesh culled

    g_live[0] = &self.actor;
    g_live[1] = &near1.actor;
    g_live[2] = &near2.actor;
    g_live[3] = &far1.actor;
    g_live[4] = &otherUni.actor;
    g_live[5] = &culled.actor;

    LiveActor* out[kNearbyWideMax];
    int n = FindNearbyWide(&self.actor, out);
    CHECK_EQ(n, 2);

    CharacterPathSetHooks(&prev);
    ResetCharacterQuery();
}

// --------------------------------------------------------------------------
// CreateMapNode
// --------------------------------------------------------------------------
TEST(CharPathNode, CreateMapNodeStoresPayload) {
    SlotEnv env; g_env = &env;
    CharacterPathHooks h = SlotHooks();
    CharacterPathHooks prev = CharacterPathSetHooks(&h);

    MapNode* node = CreateMapNode(0xCAFE);
    CHECK(node != nullptr);
    if (node) CHECK_EQ(node->payload, 0xCAFE);

    CharacterPathSetHooks(&prev);
    g_env = nullptr;
}

// --------------------------------------------------------------------------
// CountActiveByTurn
// --------------------------------------------------------------------------
namespace {
int HeFirst(int, int, int) { return 1; }       // start a walk
int g_heRemaining = 0;
int HeNext() { return g_heRemaining-- > 0 ? 1 : 0; }
}  // namespace

TEST(CharPathTurn, CountActiveByTurnTallyPlusMultiplier) {
    CharacterPathHooks h = CharacterPathGetHooks();
    h.heFindFirst = HeFirst;
    h.heFindNext = HeNext;
    h.heActiveTally = 17;        // the edx value left after the walk
    h.heTurnMultiplier = 4;      // dword_62EB98
    h.heTurnByte = 3;            // byte_63CC1D
    g_heRemaining = 5;
    CharacterPathHooks prev = CharacterPathSetHooks(&h);

    int r = CountActiveByTurn();
    CHECK_EQ(r, 17 + 4 * 3);     // 29

    CharacterPathSetHooks(&prev);
}

// --------------------------------------------------------------------------
// NearestTargetRankWeight / NearestTargetWalkSteps (golden vectors)
// --------------------------------------------------------------------------
TEST(CharPathTarget, RankWeightGolden) {
    CHECK_EQ(NearestTargetRankWeight(0), 1.0f);
    CHECK_EQ(NearestTargetRankWeight(20), 0.0f);
    // rd=4 -> 0.8 (float32). Compare via the same float arithmetic.
    float w4 = NearestTargetRankWeight(4);
    CHECK(w4 > 0.7999f && w4 < 0.8001f);
    float w1 = NearestTargetRankWeight(1);
    CHECK(w1 > 0.9499f && w1 < 0.9501f);
}

TEST(CharPathTarget, WalkStepsGolden) {
    CHECK_EQ(NearestTargetWalkSteps(0, 0), 15);
    CHECK_EQ(NearestTargetWalkSteps(30, 0), 15);
    CHECK_EQ(NearestTargetWalkSteps(300, 0), 35);
    CHECK_EQ(NearestTargetWalkSteps(60, 5), 25);
    CHECK_EQ(NearestTargetWalkSteps(9, 0), 15);
    CHECK_EQ(NearestTargetWalkSteps(200, 10), 55);
}

// --------------------------------------------------------------------------
// ClampTileCoord
// --------------------------------------------------------------------------
TEST(CharPathClamp, ClampTileCoordBounds) {
    // gridDim 100 -> upper clamp 98.
    CHECK_EQ(ClampTileCoord(-5, 100), 1);
    CHECK_EQ(ClampTileCoord(0, 100), 1);
    CHECK_EQ(ClampTileCoord(1, 100), 1);
    CHECK_EQ(ClampTileCoord(50, 100), 50);
    CHECK_EQ(ClampTileCoord(98, 100), 98);
    CHECK_EQ(ClampTileCoord(99, 100), 98);
    CHECK_EQ(ClampTileCoord(500, 100), 98);
}

// --------------------------------------------------------------------------
// WaitSlotCallback
// --------------------------------------------------------------------------
namespace {
// Accept only entry at offset 128 (the third 64-byte slot).
int WaitCbAcceptThird(int entry, const unsigned char*) { return entry == 128 ? 1 : 0; }
int WaitCbAcceptNone(int, const unsigned char*) { return 0; }
}  // namespace

TEST(CharPathWait, WaitSlotCallbackAppendsOnHit) {
    unsigned char tmpl[960];
    std::memset(tmpl, 0, sizeof(tmpl));
    // Mark slots 0,64,128 active (first byte nonzero) so the scan reaches 128.
    tmpl[0] = 1; tmpl[64] = 1; tmpl[128] = 1;

    int slotRec[40];
    std::memset(slotRec, 0, sizeof(slotRec));
    // slotRec[32] is the count (byte +128).
    bool more = WaitSlotCallback(0x55, slotRec, tmpl, WaitCbAcceptThird);
    CHECK(more);                       // count 1 < 32
    CHECK_EQ(slotRec[32], 1);          // count incremented
    CHECK_EQ(slotRec[0], 0x55);        // actorId appended at queue[0]
}

TEST(CharPathWait, WaitSlotCallbackNoHitLeavesCount) {
    unsigned char tmpl[960];
    std::memset(tmpl, 0, sizeof(tmpl));
    tmpl[0] = 1; tmpl[64] = 1;         // active but callback rejects all
    int slotRec[40];
    std::memset(slotRec, 0, sizeof(slotRec));
    slotRec[32] = 3;                   // pre-existing count
    bool more = WaitSlotCallback(0x99, slotRec, tmpl, WaitCbAcceptNone);
    CHECK(more);                       // 3 < 32
    CHECK_EQ(slotRec[32], 3);          // unchanged
}

// --------------------------------------------------------------------------
// PickWaitAnimation
// --------------------------------------------------------------------------
namespace {
int CollectNone(int*, int) { return 0; }
int CollectOne(int* out, int) { out[0] = 42; return 1; }
int CollectThree(int* out, int) { out[0] = 10; out[1] = 20; out[2] = 30; return 3; }
unsigned FixedRand4() { return 4; }  // 4 % 3 == 1 -> picks out[1]
}  // namespace

TEST(CharPathPick, PickWaitAnimationNone) {
    CHECK_EQ(PickWaitAnimation(CollectNone, FixedRand4), 0);
}
TEST(CharPathPick, PickWaitAnimationOne) {
    CHECK_EQ(PickWaitAnimation(CollectOne, FixedRand4), 42);
}
TEST(CharPathPick, PickWaitAnimationManyUsesRng) {
    // count 3, rand 4 -> 4%3 == 1 -> out[1] == 20.
    CHECK_EQ(PickWaitAnimation(CollectThree, FixedRand4), 20);
}
