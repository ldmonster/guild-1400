#include "test.h"
#include "sim/command_apply10.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Spy state for the hooks. Defined locally in the test TU (the library owns its
// own inert defaults; tests install pointers into this state).
// ---------------------------------------------------------------------------
namespace {

struct Spy {
    // resolveEntityById
    bool   resolveOk = true;
    ResolvedEntity nextResolved;
    i32    lastResolveId = 0;

    // specialTarget
    i32 special[3] = {0, 0, 0}; // [-2,-3,-4]

    // queryFind
    void* queryFindResult = nullptr;

    // personQueryBegin / iter
    std::vector<void*> personChain;
    size_t personPos = 0;

    void* personBegin = nullptr;
    void* findRecord = nullptr;

    i32   count = 0;
    i32   freeCap = 0, carryCap = 0;
    void* ownerParent = nullptr;

    // selectionMatch
    int   selMatchSlot = -1;   // slot that matches
    int   selMatchCls  = -999; // cls required (-999 = any)
    void* selRecord = nullptr;

    i32   roomWorthBase = 0;
    i32   entityId = 0;
};

Spy g;

int   spyResolve(i32 id, void*, ResolvedEntity* out) { g.lastResolveId = id; if (out) *out = g.nextResolved; return g.resolveOk ? 1 : 0; }
i32   spySpecial(i32 s) { return g.special[-s - 2]; }
void* spyQueryFind(i32, i32, i32, i32, i32) { return g.queryFindResult; }
void* spyPersonBegin(i32, i32, i32, i32) { g.personPos = 0; return g.personBegin; }
void* spyPersonIter() { return g.personPos < g.personChain.size() ? g.personChain[g.personPos++] : nullptr; }
void* spyFindRecord(i32) { return g.findRecord; }
i32   spyCount(i32) { return g.count; }
i32   spyFree(void*, i32, void*, i32) { return g.freeCap; }
i32   spyCarry(void*, i32, i32) { return g.carryCap; }
void* spyOwner(void*, void*) { return g.ownerParent; }
int   spySel(int slot, int cls, void** out) {
    bool m = (slot == g.selMatchSlot) && (g.selMatchCls == -999 || cls == g.selMatchCls || cls == -1);
    if (out) *out = m ? g.selRecord : nullptr;
    return m ? 1 : 0;
}
// Worth keyed off the record's entity-id field (+4) so the winner is deterministic
// (keying off the raw pointer truncates to a signed i32 that can fall below the
// best=-1 seed under ASLR, leaving `chosen` null -> a flaky nullptr deref in the test).
i32   spyRoom(void* rec, i32, void*) { i32 v = 0; if (rec) std::memcpy(&v, static_cast<u8*>(rec) + 4, 4); return g.roomWorthBase + v; }
i32   spyEntityId(void*) { return g.entityId; }

void install() {
    ApplyTargetHooks h{};
    h.resolveEntityById     = &spyResolve;
    h.specialTarget         = &spySpecial;
    h.queryFind             = &spyQueryFind;
    h.queryIterNext         = nullptr;
    h.personQueryBegin      = &spyPersonBegin;
    h.personIterNext        = &spyPersonIter;
    h.personFindRecordById  = &spyFindRecord;
    h.countAtLocation       = &spyCount;
    h.invFreeCapacity       = &spyFree;
    h.invCarryCapacity      = &spyCarry;
    h.resolveOwnerOrParentB = &spyOwner;
    h.selectionMatch        = &spySel;
    h.roomWorth             = &spyRoom;
    h.recordEntityId        = &spyEntityId;
    SetApplyTargetHooks(&h);
}

// Build a flat command record (large enough for all reads).
struct Cmd { u8 b[128]; Cmd() { std::memset(b, 0, sizeof b); } };
void put32(Cmd& c, int off, i32 v) { std::memcpy(c.b + off, &v, 4); }

} // namespace

// ---------------------------------------------------------------------------
// CheckSyncRangeAcked — pure golden vectors (oracle in python; see report).
// ---------------------------------------------------------------------------
namespace {
std::vector<u8> mkAck(u32 lo, std::initializer_list<std::pair<u32,u8>> over) {
    std::vector<u8> t(0x8000 * 10, 1);
    (void)lo;
    for (auto& kv : over) t[10 * (kv.first & 0x7FFF)] = kv.second;
    return t;
}
}

TEST(CmdApply10_SyncAck, EmptyRange) {
    auto t = mkAck(0, {});
    CHECK_EQ(CheckSyncRangeAcked(5, 5, t.data()), 1);
}
TEST(CmdApply10_SyncAck, StartGreater) {
    auto t = mkAck(0, {});
    CHECK_EQ(CheckSyncRangeAcked(9, 3, t.data()), 1);
}
TEST(CmdApply10_SyncAck, AllAcked) {
    auto t = mkAck(0, {{3,1},{4,1},{5,1},{6,1}});
    CHECK_EQ(CheckSyncRangeAcked(3, 7, t.data()), 1);
}
TEST(CmdApply10_SyncAck, PendingMidRange) {
    auto t = mkAck(0, {{3,1},{4,1},{5,0},{6,1}});
    CHECK_EQ(CheckSyncRangeAcked(3, 7, t.data()), 0);
}
TEST(CmdApply10_SyncAck, NakSeen) {
    auto t = mkAck(0, {{3,1},{4,2},{5,1},{6,1}});
    CHECK_EQ(CheckSyncRangeAcked(3, 7, t.data()), -1);
}
TEST(CmdApply10_SyncAck, NakThenPending) {
    auto t = mkAck(0, {{3,2},{4,0},{5,1},{6,1}});
    CHECK_EQ(CheckSyncRangeAcked(3, 7, t.data()), 0);
}

// ---------------------------------------------------------------------------
// SubstituteSpecialTarget.
// ---------------------------------------------------------------------------
TEST(CmdApply10_Special, Sentinels) {
    install();
    g.special[0] = 100; g.special[1] = 200; g.special[2] = 300;
    CHECK_EQ(SubstituteSpecialTarget(42), 42);   // passthrough
    CHECK_EQ(SubstituteSpecialTarget(-1), -1);   // -1 is NOT special
    CHECK_EQ(SubstituteSpecialTarget(-2), 100);
    CHECK_EQ(SubstituteSpecialTarget(-3), 200);
    CHECK_EQ(SubstituteSpecialTarget(-4), 300);
}

// ---------------------------------------------------------------------------
// CheckTargetNotInUse.
// ---------------------------------------------------------------------------
TEST(CmdApply10_NotInUse, FirstSlotMinusOne) {
    install();
    Cmd c; put32(c, 16, -1);
    CHECK_EQ(CheckTargetNotInUse(c.b), 1);   // immediate accept
}
TEST(CmdApply10_NotInUse, NoConflict) {
    install();
    Cmd c; put32(c, 16, 50);          // owner key
    put32(c, 20, 7);                  // first person id
    // record found, but state byte is not 6/7 -> no conflict
    u8 rec[600]; std::memset(rec, 0, sizeof rec);
    rec[2] = 3;                       // state not 6/7
    g.findRecord = rec;
    CHECK_EQ(CheckTargetNotInUse(c.b), 0);
}
TEST(CmdApply10_NotInUse, ConflictBoundElsewhere) {
    install();
    Cmd c; put32(c, 16, 50);          // owner key
    put32(c, 20, 7);
    u8 rec[600]; std::memset(rec, 0, sizeof rec);
    rec[2] = 6;                       // state 6
    i32 bound = 99;                   // != owner 50, != -1
    std::memcpy(rec + 520, &bound, 4);
    g.findRecord = rec;
    CHECK_EQ(CheckTargetNotInUse(c.b), 1);   // conflict -> reject
}
TEST(CmdApply10_NotInUse, BoundToSelfOk) {
    install();
    Cmd c; put32(c, 16, 50);
    put32(c, 20, 7);
    u8 rec[600]; std::memset(rec, 0, sizeof rec);
    rec[2] = 7;
    i32 bound = 50;                   // bound to its own owner -> not a conflict
    std::memcpy(rec + 520, &bound, 4);
    g.findRecord = rec;
    CHECK_EQ(CheckTargetNotInUse(c.b), 0);
}

// ---------------------------------------------------------------------------
// CheckTargetCooldown.
// ---------------------------------------------------------------------------
TEST(CmdApply10_Cooldown, MinusOneSlotReachable) {
    install();
    Cmd c; put32(c, 20, -1);
    CHECK_EQ(CheckTargetCooldown(c.b, nullptr), 0);
}
TEST(CmdApply10_Cooldown, OnCooldownRejected) {
    install();
    Cmd c; put32(c, 20, 5); put32(c, 29, 2); // threshold 2
    u8 imm[200]; std::memset(imm, 0, sizeof imm);
    i32 key = 1234; std::memcpy(imm + 93, &key, 4);
    g.nextResolved = ResolvedEntity{}; g.nextResolved.immediate = imm;
    g.count = 5;                              // 5 - 2 >= 0 -> reject
    CHECK_EQ(CheckTargetCooldown(c.b, nullptr), 0);
}
TEST(CmdApply10_Cooldown, BelowThresholdReady) {
    install();
    Cmd c; put32(c, 20, 5); put32(c, 29, 9); // threshold 9
    u8 imm[200]; std::memset(imm, 0, sizeof imm);
    g.nextResolved = ResolvedEntity{}; g.nextResolved.immediate = imm;
    g.count = 3;                              // 3 - 9 < 0 -> ready
    CHECK_EQ(CheckTargetCooldown(c.b, nullptr), 1);
}
TEST(CmdApply10_Cooldown, ResolveFailReachable) {
    install();
    Cmd c; put32(c, 20, 5);
    g.resolveOk = false;
    CHECK_EQ(CheckTargetCooldown(c.b, nullptr), 1); // not resolved -> 1 (returns 1)
    g.resolveOk = true;
}

// ---------------------------------------------------------------------------
// CheckTargetOwnership.
// ---------------------------------------------------------------------------
TEST(CmdApply10_Ownership, ResolveFailAccept) {
    install();
    Cmd c; put32(c, 16, 7);
    g.resolveOk = false;
    CHECK_EQ(CheckTargetOwnership(c.b, nullptr), 1);
    g.resolveOk = true;
}
TEST(CmdApply10_Ownership, RejectWhenAliasReservedSlot) {
    install();
    Cmd c; put32(c, 16, 7);
    put32(c, 20, 456);   // *(cmd+20): base offset that lands on object+456
    c.b[30] = 0;         // *(cmd+30) >= 0
    u8 obj[600]; std::memset(obj, 0, sizeof obj);
    obj[458] = 0;        // (i8)object[458] >= 0
    g.nextResolved = ResolvedEntity{}; g.nextResolved.object = obj; // immediate/parent null -> pick=object
    CHECK_EQ(CheckTargetOwnership(c.b, nullptr), 0); // reject
}
TEST(CmdApply10_Ownership, AcceptWhenFieldNegative) {
    install();
    Cmd c; put32(c, 16, 7);
    put32(c, 20, 456);
    c.b[30] = 0x80;      // *(cmd+30) < 0  -> guard fails -> accept
    u8 obj[600]; std::memset(obj, 0, sizeof obj);
    g.nextResolved = ResolvedEntity{}; g.nextResolved.object = obj;
    CHECK_EQ(CheckTargetOwnership(c.b, nullptr), 1);
}

// ---------------------------------------------------------------------------
// CheckPersonHasOfficeTag — 'rdpm' (1668048242) branch.
// ---------------------------------------------------------------------------
TEST(CmdApply10_OfficeTag, UnknownTagAccept) {
    install();
    Cmd c; put32(c, 16, 12345);   // neither tag
    CHECK_EQ(CheckPersonHasOfficeTag(c.b, nullptr), 1);
}
TEST(CmdApply10_OfficeTag, RdpmFreshOk) {
    install();
    Cmd c; put32(c, 16, 1668048242);
    u8 begin[200]; std::memset(begin, 0, sizeof begin);
    g.personBegin = begin;
    g.queryFindResult = nullptr;  // no office -> return 1
    CHECK_EQ(CheckPersonHasOfficeTag(c.b, nullptr), 1);
}
TEST(CmdApply10_OfficeTag, RdpmStaleReject) {
    install();
    Cmd c; put32(c, 16, 1668048242);
    u8 begin[200]; std::memset(begin, 0, sizeof begin);
    g.personBegin = begin;
    u8 off[200]; std::memset(off, 0, sizeof off);
    i32 storedTick = 10; std::memcpy(off + 40, &storedTick, 4);
    g.queryFindResult = off;
    g.entityId = 20;   // liveTick 20 - 10 = 10 > 1 -> reject
    CHECK_EQ(CheckPersonHasOfficeTag(c.b, nullptr), 0);
}

// ---------------------------------------------------------------------------
// ResolveTargetGuard / Official.
// ---------------------------------------------------------------------------
TEST(CmdApply10_Resolve, GuardScanMatchWritesBack) {
    install();
    u8 rec[600]; std::memset(rec, 0, sizeof rec);
    u16 w0 = 0x1234; std::memcpy(rec + 0, &w0, 2);
    g.selMatchSlot = 3; g.selMatchCls = 15; g.selRecord = rec;
    g.entityId = 7777;
    i32 slots[8] = {0};
    u16 rendered = 0;
    int r = ResolveTargetGuard(/*mode*/0, slots, /*idx*/2, &rendered);
    CHECK_EQ(r, 1);
    CHECK_EQ(rendered, (u16)0x1234);
    CHECK_EQ(slots[2 * 2 + 1], 7777);  // wrote *(rec+4) id into slot
}
TEST(CmdApply10_Resolve, GuardNoMatch) {
    install();
    g.selMatchSlot = -1;  // nothing matches
    i32 slots[8] = {0};
    CHECK_EQ(ResolveTargetGuard(0, slots, 0, nullptr), 0);
}
TEST(CmdApply10_Resolve, OfficialMode1Validate) {
    install();
    u8 rec[600]; std::memset(rec, 0, sizeof rec);
    rec[8] = 1;        // active
    rec[358] = 21;     // class 21 (official)
    u16 w0 = 9; std::memcpy(rec + 0, &w0, 2);
    g.findRecord = rec;
    i32 slots[8] = {0}; slots[2 * 1 + 1] = 55;
    u16 rendered = 0;
    CHECK_EQ(ResolveTargetOfficial(1, slots, 1, &rendered), 1);
    CHECK_EQ(rendered, (u16)9);
}
TEST(CmdApply10_Resolve, OfficialMode1WrongClass) {
    install();
    u8 rec[600]; std::memset(rec, 0, sizeof rec);
    rec[8] = 1; rec[358] = 5;  // not 26/21
    g.findRecord = rec;
    i32 slots[8] = {0};
    CHECK_EQ(ResolveTargetOfficial(1, slots, 0, nullptr), 0);
}

// ---------------------------------------------------------------------------
// ResolveTargetBestThief.
// ---------------------------------------------------------------------------
TEST(CmdApply10_Thief, Mode2Rejected) {
    install();
    CHECK_EQ(ResolveTargetBestThief(2, nullptr, 0, nullptr), 0);
}
TEST(CmdApply10_Thief, IteratePicksHighestWorth) {
    install();
    static u8 p1[200], p2[200], p3[200];
    std::memset(p1, 0, sizeof p1); std::memset(p2, 0, sizeof p2); std::memset(p3, 0, sizeof p3);
    // roomWorth returns base + ptr value; bias each via roomWorthBase per call is
    // not possible, so use distinct entity-id reads to verify the chosen record.
    i32 e1 = 11, e2 = 22, e3 = 33;
    std::memcpy(p1 + 4, &e1, 4); std::memcpy(p2 + 4, &e2, 4); std::memcpy(p3 + 4, &e3, 4);
    g.personBegin = p1;
    g.personChain = {p2, p3, nullptr};
    g.personPos = 0;
    // worth = base + *(i32*)(rec+4); the highest entity id wins -> p3 (33) deterministically.
    void* out = nullptr;
    i32 slots[4] = {0};
    int r = ResolveTargetBestThief(0, slots, 1, &out);
    CHECK_EQ(r, 1);
    CHECK(out == p3);  // highest-worth record chosen
    // the written-back id must equal *(chosen+4)
    if (out) {
        i32 chosenId = 0; std::memcpy(&chosenId, static_cast<u8*>(out) + 4, 4);
        CHECK_EQ(slots[1], chosenId);
        CHECK_EQ(slots[1], 33);
    }
}
TEST(CmdApply10_Thief, IterateEmptyNoChoice) {
    install();
    g.personBegin = nullptr;
    g.personChain.clear();
    CHECK_EQ(ResolveTargetBestThief(0, nullptr, 0, nullptr), 0);
}

// ---------------------------------------------------------------------------
// HARDENING (wave-11): CheckParamRefsValid walks (width,count,off:2)+values field
// descriptors out of the 153-byte command record. A malformed +20 field count or
// oversized fields must not run the cursor off the record (OOB read). Drive it
// with a full 153-byte record (the real CommandPacket stride) and a bogus count.
// ---------------------------------------------------------------------------
TEST(CmdApply10_Malformed, ParamRefsOverlargeFieldCount) {
    install();
    // Full-size 153-byte record (CheckParamRefsValid reads up to the record end).
    u8 cmd[0x99] = {0};
    i32 id = 7; std::memcpy(cmd + 16, &id, 4);
    cmd[20] = 255;                       // bogus field count
    // Fill the record tail with width=4,count=255 descriptors so the cursor would
    // sprint past the 153-byte record without the bound.
    for (int off = 21; off + 1 < 0x99; off += 4) {
        cmd[off] = 4;                    // width
        cmd[off + 1] = 255;              // count
    }
    g.resolveOk = true;
    g.nextResolved = ResolvedEntity{};   // immediate/parent null -> pick==object(null)
    // Must return without reading past cmd[153] (ASAN gate); result is don't-care.
    int r = CheckParamRefsValid(cmd);
    CHECK(r == 0 || r == 1);
}

// Many zero-body descriptors (width=4,count=0 -> advance 4 each) walk the cursor
// up to and past the 153-byte record end. The header read at the top of the loop
// must be bounded so it never reads cmd[>=153].
TEST(CmdApply10_Malformed, ParamRefsCursorWalksOffEnd) {
    install();
    u8 cmd[0x99] = {0};
    i32 id = 7; std::memcpy(cmd + 16, &id, 4);
    cmd[20] = 60;                        // 60 descriptors; cur advances 4 each ->
                                         // reaches 153 after 33, must stop there.
    for (int off = 21; off + 1 < 0x99; off += 4) {
        cmd[off] = 4;                    // width 4
        cmd[off + 1] = 0;                // count 0 -> no inline body; header-only stride 4
    }
    static u8 obj[600]; std::memset(obj, 0, sizeof obj);
    g.resolveOk = true;
    g.nextResolved = ResolvedEntity{}; g.nextResolved.object = obj;
    int r = CheckParamRefsValid(cmd);   // must not read past cmd[153] (ASAN gate)
    CHECK(r == 0 || r == 1);
}
