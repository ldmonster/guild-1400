#include "test.h"

#include "sim/character_ai.h"
#include "sim/character_path.h"   // NearestTargetRankWeight / WalkSteps / constants

#include <cstring>
#include <cmath>
#include <map>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// ---- a node-field-addressable byte arena keyed by (handle, byteOff) ----
// Real actor records are >600 bytes; we back each handle with a 1KB buffer.
struct Node {
    unsigned char b[1024];
    Node() { std::memset(b, 0, sizeof(b)); }
    int&   i32(int off) { return *reinterpret_cast<int*>(b + off); }
    unsigned char& u8at(int off) { return b[off]; }
};
std::map<int, Node>* g_nodes = nullptr;
Node& N(int h) { return (*g_nodes)[h]; }

int  TReadI32(int h, int off) { return N(h).i32(off); }
void TWriteI32(int h, int off, int v) { N(h).i32(off) = v; }
unsigned char TReadU8(int h, int off) { return N(h).u8at(off); }
void TWriteU8(int h, int off, unsigned char v) { N(h).u8at(off) = v; }

// ---- a synthetic person table ----
struct Person {
    unsigned char state = 0;
    unsigned short cls = 0;
    unsigned char hostA = 0, hostB = 0;
    signed char affinity = 0;       // byte_1333110[id+768p]
    int rankPlaneHi = 0;
    int affinityPlaneHi = 0;
    int officeRank = 0;
    int linkedObj = 0;
    double favorability = 0.0;
};
Person g_persons[768];

u8  PState(int p) { return g_persons[p].state; }
u16 PClass(int p) { return g_persons[p].cls; }
u8  PHostA(int p) { return g_persons[p].hostA; }
u8  PHostB(int p) { return g_persons[p].hostB; }
i8  PAff(int p, int) { return g_persons[p].affinity; }
int PRankHi(int p, int) { return g_persons[p].rankPlaneHi; }
int PAffHi(int p, int) { return g_persons[p].affinityPlaneHi; }
int PLinked(int p) { return g_persons[p].linkedObj; }
void* PRecBase(int p) { return reinterpret_cast<void*>(static_cast<intptr_t>(100000 + p)); }

int    POfficeRank(int idx, int) { return g_persons[idx].officeRank; }
double PFavor(int candId, int, int) { return g_persons[candId].favorability; }
int    g_randVal = 0;
int    PRand(u16) { return g_randVal; }

// ---- delta-packet capture ----
struct DeltaCapture {
    int begins = 0;
    struct Field { unsigned size, count, field; long value; };
    std::vector<Field> fields;
    int state22 = 0;
    int coord27 = 0;
    int historyA = 0, historyB = 0, historyFound = 0;
} g_dc;

void DBegin(void*, int) { ++g_dc.begins; }
void DField(unsigned sz, unsigned ct, const void* src, unsigned f) {
    long v = 0; std::memcpy(&v, src, sz <= sizeof(long) ? sz : sizeof(long));
    g_dc.fields.push_back({sz, ct, f, v});
}
void DRaw(unsigned sz, unsigned ct, const void* src, unsigned off) {
    long v = 0; std::memcpy(&v, src, sz <= sizeof(long) ? sz : sizeof(long));
    g_dc.fields.push_back({sz, ct, off | 0x10000u, v});
}
void DState22() { ++g_dc.state22; }
void DCoord27(int, int, int) { ++g_dc.coord27; }
void DHistA(void*, void*) { ++g_dc.historyA; }
void DHistB(void*, void*) { ++g_dc.historyB; }
void DHistFound(void*, void*) { ++g_dc.historyFound; }

CharacterAiHooks BaseHooks() {
    CharacterAiHooks h = CharacterAiGetHooks();
    h.persons.stateType = PState;
    h.persons.classWord = PClass;
    h.persons.hostileA = PHostA;
    h.persons.hostileB = PHostB;
    h.persons.affinity = PAff;
    h.persons.rankPlaneHi = PRankHi;
    h.persons.affinityPlaneHi = PAffHi;
    h.persons.linkedObjectId = PLinked;
    h.persons.recordBase = PRecBase;
    h.computeOfficeRank = POfficeRank;
    h.computeFavorability = PFavor;
    h.randomModulo = PRand;
    h.beginDeltaPacket = DBegin;
    h.appendDeltaField = DField;
    h.appendRawField = DRaw;
    h.queueRequestState22 = DState22;
    h.queueRequestCoord27 = DCoord27;
    h.historyNotifyTargetReachedA = DHistA;
    h.historyNotifyTargetReachedB = DHistB;
    h.historyNotifyTargetFound = DHistFound;
    h.readNodeI32 = TReadI32;
    h.writeNodeI32 = TWriteI32;
    h.readNodeU8 = TReadU8;
    h.writeNodeU8 = TWriteU8;
    h.debugSpeed = 0;
    return h;
}

void ResetWorld() {
    static std::map<int, Node> nodes; nodes.clear(); g_nodes = &nodes;
    for (auto& p : g_persons) p = Person{};
    g_dc = DeltaCapture{};
    g_randVal = 0;
}

}  // namespace

// --------------------------------------------------------------------------
// The favourability curve + walk-step clamp (shared deterministic helpers).
// --------------------------------------------------------------------------
TEST(CharAi, RankWeightCurve) {
    // (10 - delta*0.5)*0.1
    CHECK(std::fabs(NearestTargetRankWeight(0) - 1.0f) < 1e-6f);
    CHECK(std::fabs(NearestTargetRankWeight(4) - 0.8f) < 1e-6f);
    CHECK(std::fabs(NearestTargetRankWeight(20) - 0.0f) < 1e-6f);
}

TEST(CharAi, WalkStepClamp) {
    // debugSpeed 0 -> lo=15, hi=35; start = |dist|/3 + 1.
    CHECK_EQ(NearestTargetWalkSteps(0, 0), 15);     // 1 -> clamped up to 15
    CHECK_EQ(NearestTargetWalkSteps(60, 0), 21);    // 60/3+1 = 21 (in range)
    CHECK_EQ(NearestTargetWalkSteps(300, 0), 35);   // 101 -> clamped down to 35
}

// --------------------------------------------------------------------------
// FindNearestTarget: no engageable flags -> returns null, no packet.
// --------------------------------------------------------------------------
TEST(CharAi, NoEngageFlagsReturnsNull) {
    ResetWorld();
    CharacterAiHooks h = BaseHooks();
    CharacterAiHooks prev = CharacterAiSetHooks(&h);

    int actor = 1;
    N(actor).i32(4 * 131) = -1;   // no current target
    N(actor).u8at(358) = 0; N(actor).u8at(361) = 0; N(actor).u8at(2) = 0;

    void* r = FindNearestTarget(actor);
    CHECK(r == nullptr);
    CHECK_EQ(g_dc.begins, 0);

    CharacterAiSetHooks(&prev);
}

// --------------------------------------------------------------------------
// FindNearestTarget: an enemy (state 6) with attractive affinity is selected and
// the engage packet is emitted (begin + the 0x211/0x212/0x20C/0x210 fields).
// --------------------------------------------------------------------------
TEST(CharAi, SelectsEnemyAndEmitsEngage) {
    ResetWorld();
    CharacterAiHooks h = BaseHooks();
    CharacterAiHooks prev = CharacterAiSetHooks(&h);

    int actor = 1;
    N(actor).i32(0) = 5;          // actor person id = 5
    N(actor).i32(4 * 131) = -1;   // no current target
    N(actor).u8at(358) = 1;       // engageable

    // person 10 is an enemy with a negative affinity (score = aff*weight < 128).
    g_persons[10].state = 6;
    g_persons[10].affinity = -100;       // (i16)(-100) * 1.0 = -100 < 128
    g_persons[10].favorability = 0.0;    // < 34 ceil
    g_persons[10].officeRank = 0;
    // combined affinity gate: affinityPlaneHi/2 + rankPlaneHi must be < -(rand+50).
    g_persons[10].affinityPlaneHi = -200;
    g_persons[10].rankPlaneHi = -50;     // -100 + -50 = -150 < -(0+50) = -50  -> engage
    g_persons[10].linkedObj = 777;

    void* r = FindNearestTarget(actor);
    CHECK(r == nullptr);                 // engage path returns null
    CHECK_EQ(g_dc.begins, 1);
    CHECK_EQ(g_dc.state22, 1);
    CHECK_EQ(g_dc.coord27, 1);
    CHECK_EQ(g_dc.historyA, 1);
    // The 0x211 (mode), 0x212 (steps), 0x20C (targetId), 0x210 (affinity) fields.
    bool saw211 = false, saw212 = false, saw20C = false, saw210 = false;
    for (auto& f : g_dc.fields) {
        if (f.field == 0x211) saw211 = true;
        if (f.field == 0x212) saw212 = true;
        if (f.field == 0x20C) saw20C = true;
        if (f.field == 0x210) saw210 = true;
    }
    CHECK(saw211 && saw212 && saw20C && saw210);

    CharacterAiSetHooks(&prev);
}

// --------------------------------------------------------------------------
// FindNearestTarget: enemy present but the combined affinity gate fails (target
// not threatening enough) -> no engage, returns null without a coord/history.
// --------------------------------------------------------------------------
TEST(CharAi, AffinityGateRejects) {
    ResetWorld();
    CharacterAiHooks h = BaseHooks();
    CharacterAiHooks prev = CharacterAiSetHooks(&h);

    int actor = 1;
    N(actor).i32(0) = 5;
    N(actor).i32(4 * 131) = -1;
    N(actor).u8at(358) = 1;

    g_persons[10].state = 6;
    g_persons[10].affinity = -100;       // selected as best
    g_persons[10].favorability = 0.0;
    g_persons[10].affinityPlaneHi = 0;   // 0/2 + 0 = 0 >= -50 -> gate fails
    g_persons[10].rankPlaneHi = 0;

    void* r = FindNearestTarget(actor);
    CHECK(r == nullptr);
    CHECK_EQ(g_dc.begins, 0);            // gate rejected before any packet
    CHECK_EQ(g_dc.coord27, 0);

    CharacterAiSetHooks(&prev);
}

// --------------------------------------------------------------------------
// FindNearestTarget: favourability ceiling rejects an otherwise-good candidate.
// --------------------------------------------------------------------------
TEST(CharAi, FavorabilityCeilingRejects) {
    ResetWorld();
    CharacterAiHooks h = BaseHooks();
    CharacterAiHooks prev = CharacterAiSetHooks(&h);

    int actor = 1;
    N(actor).i32(0) = 5;
    N(actor).i32(4 * 131) = -1;
    N(actor).u8at(358) = 1;

    g_persons[10].state = 6;
    g_persons[10].affinity = -100;
    g_persons[10].favorability = 50.0;   // >= 34 ceil -> never selected
    void* r = FindNearestTarget(actor);
    CHECK(r == nullptr);
    CHECK_EQ(g_dc.begins, 0);

    CharacterAiSetHooks(&prev);
}

// --------------------------------------------------------------------------
// SetVisible: invalid actor reports an error; valid actor toggles the +140 bit.
// --------------------------------------------------------------------------
int g_setVisErrors = 0;
void CountErr(const char*) { ++g_setVisErrors; }
int g_suspendCalls = 0;
int SuspendStub(int, int, int) { ++g_suspendCalls; return 0; }

TEST(CharAi, SetVisibleTogglesCullBit) {
    ResetWorld();
    g_setVisErrors = 0; g_suspendCalls = 0;
    CharacterAiHooks h = BaseHooks();
    h.reportError = CountErr;
    h.objectToggleSuspend = SuspendStub;
    CharacterAiHooks prev = CharacterAiSetHooks(&h);

    // invalid actor (no mesh at +52) -> error.
    int bad = 2; N(bad).i32(52) = 0;
    SetVisible(bad, 1);
    CHECK_EQ(g_setVisErrors, 1);

    // valid: mesh present. Hide (visible=0) sets the 0x20 bit at +140.
    int actor = 3;
    N(actor).i32(52) = 9999;     // mesh handle
    N(actor).u8at(140) = 0;
    SetVisible(actor, 0);
    CHECK((N(actor).u8at(140) & 0x20) != 0);   // cull bit set when hidden
    // Show clears it.
    SetVisible(actor, 1);
    CHECK((N(actor).u8at(140) & 0x20) == 0);

    CharacterAiSetHooks(&prev);
}

// --------------------------------------------------------------------------
// StandUp: null actor returns input; actor with no state returns 0; actor with a
// state sets +400 and unlinks the queue.
// --------------------------------------------------------------------------
int g_unlinks = 0;
void UnlinkStub(int) {
    ++g_unlinks;
    // Clear the head so the do/while loop terminates after the first unlink.
    if (g_nodes) {
        // the StandUp code reads state+40; we cleared via the node directly below.
    }
}

TEST(CharAi, StandUpClearsQueue) {
    ResetWorld();
    g_unlinks = 0;
    CharacterAiHooks h = BaseHooks();
    h.actionQueueUnlinkEntry = UnlinkStub;
    CharacterAiHooks prev = CharacterAiSetHooks(&h);

    CHECK_EQ(StandUp(0), 0);     // null actor -> returns 0 (the input)

    int actor = 4;
    N(actor).i32(296) = 0;       // no state
    CHECK_EQ(StandUp(actor), 0);

    int actor2 = 5;
    int state = 6;
    N(actor2).i32(296) = state;
    N(state).i32(40) = 0;        // empty queue (head null) -> no unlink loop
    int r = StandUp(actor2);
    CHECK_EQ(r, 1);
    CHECK_EQ(N(state).u8at(400), 1);   // +400 flag set
    CHECK_EQ(g_unlinks, 0);            // empty queue -> no unlink

    CharacterAiSetHooks(&prev);
}
