// Unit tests for the Amt office-administration leaves (src/world/amt_economy2.*).
// Golden vectors computed offline with python (see the brief's report).
#include <cstring>
#include <string>
#include <vector>

#include "tests/framework/test.h"
#include "world/amt_economy2.h"

using namespace guild;
using namespace guild::world;

namespace {

// ---- recording harness for the hooks --------------------------------------
struct Recorder {
    std::vector<std::string> log;
    int addEntryRet = -1;
};
Recorder* g_rec = nullptr;

int RecAddEntry(u8 holder, i32 primary, int state, int succ, int flag) {
    g_rec->log.push_back("add h=" + std::to_string(holder) + " p=" +
                         std::to_string(primary) + " s=" + std::to_string(state) +
                         " succ=" + std::to_string(succ) + " f=" + std::to_string(flag));
    return g_rec->addEntryRet;
}
void RecDelta(i32 id, int field) {
    g_rec->log.push_back("delta id=" + std::to_string(id) + " f=" + std::to_string(field));
}
void RecCoord(i32 a, i32 b, int c) {
    g_rec->log.push_back("coord " + std::to_string(a) + "," + std::to_string(b) +
                         "," + std::to_string(c));
}
void RecReq16(i32 p, i32 r, i32 amt, int cur) {
    g_rec->log.push_back("req16 " + std::to_string(p) + "," + std::to_string(r) +
                         "," + std::to_string(amt) + "," + std::to_string(cur));
}
void RecArgs26(i32 p, int f, float v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "args26 %d %d %.3f", p, f, v);
    g_rec->log.push_back(buf);
}
void RecMsg(i32 id, const char* t) {
    g_rec->log.push_back("msg id=" + std::to_string(id) + " [" + std::string(t) + "]");
}

AmtEconomy2Hooks MakeRecordingHooks() {
    AmtEconomy2Hooks h;
    h.officeAddTableEntry = RecAddEntry;
    h.queueDeltaFlag = RecDelta;
    h.queueCoord27 = RecCoord;
    h.queueRequest16 = RecReq16;
    h.queueArgs26 = RecArgs26;
    h.sendEntityMessage = RecMsg;
    return h;
}

OfficeHolder MakeSlot(u8 holder, i32 city, u8 type, u8 state) {
    OfficeHolder s{};
    s.holder = holder;
    s.city = city;
    s.type = type;
    s.state = state;
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
TEST(AmtEconomy2, TriggerNoticeFindsMatchingTypeAndPostsVacant) {
    Recorder rec;
    g_rec = &rec;
    rec.addEntryRet = 777;
    AmtEconomy2SetHooks(MakeRecordingHooks());

    OfficeHolder slots[3] = {
        MakeSlot(/*holder*/5, /*city*/10, /*type*/2, /*state*/1),
        MakeSlot(/*holder*/9, /*city*/11, /*type*/4, /*state*/1),
        MakeSlot(/*holder*/3, /*city*/12, /*type*/4, /*state*/3),
    };
    // first slot with type==4 is index 1 (holder 9).
    i32 r = TriggerOfficeNotice(4, slots, 3);
    CHECK_EQ(r, 777);
    CHECK_EQ((int)rec.log.size(), 1);
    CHECK_EQ(rec.log[0], std::string("add h=9 p=0 s=3 succ=0 f=255"));

    AmtEconomy2ResetHooks();
}

TEST(AmtEconomy2, TriggerNoticeNoMatchReturnsSpan) {
    Recorder rec;
    g_rec = &rec;
    AmtEconomy2SetHooks(MakeRecordingHooks());
    OfficeHolder slots[2] = {MakeSlot(1, 1, 1, 1), MakeSlot(2, 2, 2, 1)};
    i32 r = TriggerOfficeNotice(/*type*/9, slots, 2);
    CHECK_EQ(r, 2 * kOfficeHolderStride); // 48 — walked past both, no emit
    CHECK_EQ((int)rec.log.size(), 0);
    AmtEconomy2ResetHooks();
}

// ---------------------------------------------------------------------------
namespace {
// person-find oracle: id -> (present, dirty)
struct PersonOracle {
    bool present;
    bool dirty;
};
PersonOracle* g_oracle = nullptr;
int g_oracleN = 0;
bool OraclePersonFind(i32 id, bool* present, bool* dirty) {
    if (id < 0 || id >= g_oracleN)
        return false; // not found
    *present = g_oracle[id].present;
    *dirty = g_oracle[id].dirty;
    return true;
}
} // namespace

TEST(AmtEconomy2, ResetGuildSlotsBranches) {
    Recorder rec;
    g_rec = &rec;
    AmtEconomy2Hooks h = MakeRecordingHooks();
    h.personFind = OraclePersonFind;
    AmtEconomy2SetHooks(h);

    // person id is the slot.city field. Oracle indexed by id.
    PersonOracle oracle[4] = {
        {true,  false}, // id0: present, active holder
        {false, true},  // id1: absent + dirty -> vacancy + delta 0x166
        {true,  false}, // id2: present but slot.state already 1 -> no assign
        {false, false}, // id3: absent, not dirty -> vacancy only
    };
    g_oracle = oracle;
    g_oracleN = 4;

    OfficeHolder slots[4] = {
        MakeSlot(/*holder*/100, /*city(id)*/0, /*type*/1, /*state*/3), // present -> assign(state1)
        MakeSlot(/*holder*/101, /*city(id)*/1, /*type*/1, /*state*/1), // absent+dirty -> vacancy + delta
        MakeSlot(/*holder*/102, /*city(id)*/2, /*type*/1, /*state*/1), // present, state==1 -> nothing
        MakeSlot(/*holder*/103, /*city(id)*/3, /*type*/1, /*state*/3), // absent, state==3 -> nothing-vacancy (state already 3)
    };

    bool dirtyB[3] = {true, false, true};
    ResetGuildSlotsResult res = ResetGuildSlots(slots, 4, dirtyB, 3);

    CHECK_EQ(res.slotAssignsQueued, 1);     // slot0
    CHECK_EQ(res.slotVacanciesQueued, 1);   // slot1 (slot3 already state 3 -> skipped)
    CHECK_EQ(res.holderFlagsCommitted, 1);  // slot1 dirty
    CHECK_EQ(res.buildingFlagsCommitted, 2);// buildings 0 and 2

    AmtEconomy2ResetHooks();
}

// ---------------------------------------------------------------------------
TEST(AmtEconomy2, HighlightGuildMembersDedup) {
    Recorder rec;
    g_rec = &rec;
    AmtEconomy2SetHooks(MakeRecordingHooks());

    i32 ids[5] = {7, 42, -1, 99, 7}; // self=7 -> skip both 7s, skip -1
    int q = HighlightGuildMembers(/*self*/7, /*category*/3, /*arg*/55, ids, 5);
    CHECK_EQ(q, 2);
    CHECK_EQ((int)rec.log.size(), 2);
    CHECK_EQ(rec.log[0], std::string("coord 7,42,55"));
    CHECK_EQ(rec.log[1], std::string("coord 7,99,55"));

    // out-of-range category gates everything off
    q = HighlightGuildMembers(7, 8, 55, ids, 5);
    CHECK_EQ(q, 0);
    AmtEconomy2ResetHooks();
}

// ---------------------------------------------------------------------------
namespace {
const char* EntryTextOracle(u8 index, void*) {
    static const char* tbl[3] = {"ALPHA", "BETA", "GAMMA"};
    return tbl[index];
}
} // namespace

TEST(AmtEconomy2, BuildOfficeInfoTextGatesAndConcat) {
    Recorder rec;
    g_rec = &rec;
    AmtEconomy2SetHooks(MakeRecordingHooks());

    u8 entries[3] = {0, 2, 1}; // ALPHA, GAMMA, BETA
    bool sent = BuildOfficeInfoText(/*holder*/500, /*rank*/3, /*count*/3, entries,
                                    EntryTextOracle, nullptr);
    CHECK(sent);
    CHECK_EQ((int)rec.log.size(), 1);
    CHECK_EQ(rec.log[0], std::string("msg id=500 [ALPHA$AGAMMA$ABETA$A]"));

    // rank < 2 -> gated off
    rec.log.clear();
    CHECK(!BuildOfficeInfoText(500, 1, 3, entries, EntryTextOracle, nullptr));
    CHECK_EQ((int)rec.log.size(), 0);
    // empty list -> gated off
    CHECK(!BuildOfficeInfoText(500, 3, 0, entries, EntryTextOracle, nullptr));
    AmtEconomy2ResetHooks();
}

// ---------------------------------------------------------------------------
TEST(AmtEconomy2, ComputeOfficeRenderOffsetGolden) {
    AmtEconomy2ResetHooks(); // default truncation == (int) round-toward-zero

    // lawLevel 0 -> stretch 1.10000002 -> x = (int)1.1 = 1.
    // occupied slots of types {1,2,3} -> tiles {5,7,10} -> each step = tiles*3200.
    OfficeHolder slots[4] = {
        MakeSlot(0, 100, /*type*/1, 0), // occupied
        MakeSlot(0, 200, /*type*/2, 0), // occupied
        MakeSlot(0, -1,  /*type*/9, 0), // vacant (city == -1) -> skipped
        MakeSlot(0, 300, /*type*/3, 0), // occupied
    };
    i32 off = ComputeOfficeRenderOffset(/*baseX*/0, /*law*/0, slots, 4);
    CHECK_EQ(off, (5 + 7 + 10) * 3200); // 70400
}

// ---------------------------------------------------------------------------
TEST(AmtEconomy2, FindNextActiveBuildingScanAndCache) {
    AmtEconomy2ResetHooks();
    ActiveBuildingCache cache; // starts invalid

    BuildingScanEntry b[4];
    b[0] = {/*id*/10, /*type*/1, /*active*/true,  /*wealth*/500};
    b[1] = {/*id*/11, /*type*/3, /*active*/true,  /*wealth*/9000}; // richest active
    b[2] = {/*id*/12, /*type*/20,/*active*/true,  /*wealth*/99999}; // type>=10 -> skip
    b[3] = {/*id*/-1, /*type*/1, /*active*/true,  /*wealth*/7000}; // empty slot -> skip

    i32 id = 0, wealth = 0;
    int r = FindNextActiveBuilding(&cache, /*gen*/5, b, 4, &id, &wealth);
    CHECK_EQ(r, 1);
    CHECK_EQ(id, 11);
    CHECK_EQ(wealth, 9000);
    CHECK(cache.cachedValid);

    // Same generation -> cache hit returns the same without rescanning.
    i32 id2 = 0, w2 = 0;
    int r2 = FindNextActiveBuilding(&cache, 5, b, 4, &id2, &w2);
    CHECK_EQ(r2, 1);
    CHECK_EQ(id2, 11);
    CHECK_EQ(w2, 9000);

    // No active buildings -> returns 0, wealth defaults to 3200.
    ActiveBuildingCache empty;
    BuildingScanEntry none[1] = {{-1, 0, false, 0}};
    i32 id3 = 0, w3 = 0;
    int r3 = FindNextActiveBuilding(&empty, 1, none, 1, &id3, &w3);
    CHECK_EQ(r3, 0);
    CHECK_EQ(id3, -1);
    CHECK_EQ(w3, 3200);
}

// ---------------------------------------------------------------------------
namespace {
float g_fixedRoll = 0.0f;
float FixedRoll() { return g_fixedRoll; }
} // namespace

TEST(AmtEconomy2, RivalryScoreSameRegionGolden) {
    Recorder rec;
    g_rec = &rec;
    AmtEconomy2Hooks h = MakeRecordingHooks();
    h.randFloatScaled = FixedRoll; // 0 -> roll always wins (0*2 <= base)
    AmtEconomy2SetHooks(h);
    g_fixedRoll = 0.0f;

    RivalrySelf self;
    self.cityId = 1;
    self.cityRegion = 0;
    self.cityRegion2 = 5;
    self.workstationSum = 8.0f;
    self.wealth = 1000;
    self.payoutCoordFlag = false;

    RivalryRival rivals[1];
    rivals[0].id = 42;
    rivals[0].type = 1;
    rivals[0].cityId = 2;       // != self.cityId
    rivals[0].cityRegion = 0;   // == self.cityRegion -> same-region branch (+0.05)
    rivals[0].workForce = 4;
    rivals[0].wealth = 2000;
    rivals[0].reputation = 0.5f; // inside (0.05, 0.95) -> rep delta queued

    RivalryResult res = ComputeBuildingRivalryScore(self, rivals, 1, /*currency*/3);
    // supply = 8*0.0125+1 = 1.1; base = 4*0.25*1.1 = 1.1
    // wf = 2000/1000*160 = 320; payout = trunc((320+160)*1.1) = 528
    CHECK_EQ(res.rivalsPaid, 1);
    CHECK_EQ(res.totalPayout, 528);
    // money + reputation commands queued
    CHECK_EQ(rec.log[0], std::string("req16 -1,42,528,3"));
    CHECK_EQ(rec.log[1], std::string("args26 42 460 0.050"));

    AmtEconomy2ResetHooks();
}

TEST(AmtEconomy2, RivalrySkipsSameCityAndExemptTypes) {
    Recorder rec;
    g_rec = &rec;
    AmtEconomy2Hooks h = MakeRecordingHooks();
    h.randFloatScaled = FixedRoll;
    AmtEconomy2SetHooks(h);
    g_fixedRoll = 0.0f;

    RivalrySelf self;
    self.cityId = 1;
    self.workstationSum = 8.0f;
    self.wealth = 1000;

    RivalryRival rivals[3];
    rivals[0].id = 1; rivals[0].type = 1; rivals[0].cityId = 1; // same city -> skip
    rivals[1].id = 2; rivals[1].type = 7; rivals[1].cityId = 2; // exempt type 7 -> skip
    rivals[2].id = 3; rivals[2].type = 1; rivals[2].cityId = 9; rivals[2].cityRegion = 0;
    rivals[2].workForce = 0; rivals[2].wealth = 0; rivals[2].reputation = 0.0f;

    RivalryResult res = ComputeBuildingRivalryScore(self, rivals, 3, 0);
    // only rivals[2] is eligible. base = 0*0.25*1.1 = 0 -> roll 0*2<=0 true.
    // wf: ratio 0/1000*160=0 -> wf=0; payout=trunc((0+160)*0)=0.
    CHECK_EQ(res.rivalsPaid, 1);
    CHECK_EQ(res.totalPayout, 0);
    AmtEconomy2ResetHooks();
}

// ---------------------------------------------------------------------------
namespace {
const char* LookupHit(const char* key, void*) {
    // verify the upcased key is what we built
    if (std::strcmp(key, "_STADTAUSWAHL_KOELN_INFO+0") == 0)
        return "Hi"; // narrow source; widened to "H\0i\0"
    return nullptr;
}
const char* LookupMiss(const char*, void*) { return nullptr; }
} // namespace

TEST(AmtEconomy2, LookupSelectionInfoTextWidens) {
    AmtEconomy2ResetHooks();
    char out[64];
    std::memset(out, 0x7f, sizeof(out));
    int r = LookupSelectionInfoText("koeln", out, LookupHit, nullptr);
    CHECK_EQ(r, 1);
    // "Hi" -> bytes 'H',0,'i',0  (widen copies pairs; src[1] of "Hi"[0..] = 'i')
    CHECK_EQ(out[0], 'H');
    CHECK_EQ(out[1], 'i'); // src[1] is 'i' (the original 2-byte stride copies it)
    CHECK_EQ(out[2], '\0');

    // miss -> falls through to copying the key itself
    char out2[64];
    r = LookupSelectionInfoText("X", out2, LookupMiss, nullptr);
    CHECK_EQ(r, 1);
    CHECK_EQ(out2[0], 'X');
}

// ---------------------------------------------------------------------------
TEST(AmtEconomy2, OpenOfficeWindowArgs) {
    OfficeWindowArgs a = OpenOfficeWindow(/*a1*/77, /*a2*/0);
    CHECK_EQ(a.slot0, 77);
    CHECK_EQ(a.windowId, 516);
    CHECK_EQ((int)a.officeType, 6);
}
