// e2e for sim/npcaction3 — drive a synthetic NPC burglary squad through one full
// routine tick-by-tick (case -> approach -> wait-at-door -> break-in -> packet-gate
// -> steal -> flee/teardown) and verify the state progression + emitted commands
// against a hand-computed reference. Also runs a recruitment routine end to end.
#include "sim/npcaction3.h"
#include "sim/npcaction.h"
#include "sim/gametime.h"
#include "tests/framework/test.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct FakeObj { i32 id; u8 kind; bool hasChar; };

struct E2EScene {
    std::vector<std::string> log;
    std::vector<FakeObj> objs;
    bool storableExists = true;
    bool nearDoorAll = true;
    i32 packetStatus = 1;
    i32 packetSeq = 0;
    bool jailEscape = false;
    bool detect = false;
    i32 loot = 0;
    i32 proximity = 1;
    u8 cost = 1;
    FakeObj storable{9001, 0, false};

    FakeObj* byId(i32 id) {
        for (auto& o : objs) if (o.id == id) return &o;
        return nullptr;
    }
    void add(i32 id, u8 k, bool hc) { objs.push_back({id, k, hc}); }
};

E2EScene* g_s = nullptr;
void L(const std::string& s) { g_s->log.push_back(s); }

i32 e_q29(int a, HeRecord*) { L("e29:" + std::to_string(a)); return 4242; }
i32 e_status(i32) { return g_s->packetStatus; }
i32 e_seq(i32) { return g_s->packetSeq; }
i32 e_free(HeRecord*) { L("free"); return 0; }
void e_s49(i32 id) { L("s49:" + std::to_string(id)); }
void e_n53(i32 p, i32 o, int, i32 t, int f, const char* n) {
    L("n53:" + std::to_string(p) + ">" + std::to_string(o) + "/" + std::to_string(t) +
      "/f" + std::to_string(f) + "/" + std::string(n));
}
void e_chrmove(i32 p, i32 o, const char*, i32) { L("move:" + std::to_string(p) + ">" + std::to_string(o)); }
void e_flag55(i32, u8) {}
void e_pair36(i32 s, i32 v) { L("pair:" + std::to_string(s) + "/" + std::to_string(v)); }
void e_quad43(i32 o, i32, i32, i32 st) { L("quad:" + std::to_string(o) + "/" + std::to_string(st)); }
void e_r16(i32 a, i32 b, i32 amt, u8) { L("r16:" + std::to_string(a) + ">" + std::to_string(b) + "=" + std::to_string(amt)); }
void e_setf(i32, int, i32, int) {}
void e_slot28(i32, i32) {}
void e_op91(i32, int) {}
void e_args25(i32, int, int, int, int) {}
void e_bs(const char*) {}
void e_be() {}
void* e_qb(i32 id) { return g_s->byId(id); }
void* e_fid(i32 id) { return g_s->byId(id); }
void* e_fb(i32 id) { return g_s->byId(id); }
void* e_st(void*) { return g_s->storableExists ? &g_s->storable : nullptr; }
bool e_nd(void*, void*) { return g_s->nearDoorAll; }
i32 e_pid(void* p) { return p ? static_cast<FakeObj*>(p)->id : -1; }
u8 e_pk(void* p) { return p ? static_cast<FakeObj*>(p)->kind : 0; }
u16 e_pm(void* p) { return p ? static_cast<u16>(static_cast<FakeObj*>(p)->kind) : 0; }
bool e_phc(void* p) { return p ? static_cast<FakeObj*>(p)->hasChar : false; }
i32 e_oid(void* p) { return p ? static_cast<FakeObj*>(p)->id : -1; }
void e_chg(void*, HeRecord*, u16) {}
i32 e_prox(i32, i32) { return g_s->proximity; }
u8 e_cost(int, i32) { return g_s->cost; }
void e_wander(HeRecord*, void*) {}
void e_msg(i32, int) {}
i32 e_viol(int, i32, i32, i32) { return 555; }
bool e_escape(void*, void*) { return g_s->jailEscape; }
i32 e_loot(void*, void*, int) { return g_s->loot; }
bool e_detect(void*) { return g_s->detect; }

NpcAction3Hooks MakeE2EHooks() {
    NpcAction3Hooks H{};
    H.queueRequestEntity29 = e_q29; H.packetStatus = e_status; H.packetSeq = e_seq;
    H.freeHandlerEntry = e_free; H.queueSingle49 = e_s49; H.queueNamedObject53 = e_n53;
    H.requestChrMove = e_chrmove; H.queueFlag55 = e_flag55; H.queuePair36 = e_pair36;
    H.queueQuad43 = e_quad43; H.queueRequest16 = e_r16; H.setEntityField = e_setf;
    H.queueSlotReset28 = e_slot28; H.requestBuildOp91 = e_op91; H.enqueueArgs25 = e_args25;
    H.enqueueBuildingActionStart = e_bs; H.enqueueBuildingActionEnd = e_be;
    H.personQueryBegin = e_qb; H.findPersonById = e_fid; H.findBuildingById = e_fb;
    H.findStorableObject = e_st; H.personNearDoor = e_nd; H.personId = e_pid;
    H.personKind = e_pk; H.personMarkerWord = e_pm; H.personHasCharacter = e_phc;
    H.objectIdField = e_oid; H.changePlayerAction = e_chg; H.recruitProximity = e_prox;
    H.recruitCost = e_cost; H.computeWanderPath = e_wander; H.sendMessage = e_msg;
    H.evaluateViolation = e_viol; H.jailEscapeRoll = e_escape;
    H.burglaryLootValuation = e_loot; H.burglaryDetectionRoll = e_detect;
    return H;
}

HeRecord* NewHe() { HeRecord* h = new HeRecord(); std::memset(h, 0, sizeof(HeRecord)); return h; }
void SetClock(int d, int hh, int mm, int ss) {
    GameTime t{}; t.day = d; t.hour = (u16)hh; t.minute = mm; t.second = ss; SetNpcClock(t);
}

} // namespace

// ===========================================================================
// Full burglary routine, tick by tick.
//   state 0  -> escort out, +4 min, state 1
//   state 1  -> all at door, +1 sec, state 2
//   state 2  -> violation registered, state 3
//   state 3  -> packet applied (seq nonzero), pair36, +10 min, state 4
//   state 4  -> packet no longer pending, loot transfer, state -1
//   state -1 -> teardown, free
// ===========================================================================
TEST(NpcAction3_E2E, full_burglary_progression) {
    E2EScene s; g_s = &s;
    NpcAction3Hooks H = MakeE2EHooks(); SetNpcAction3Hooks(&H);
    SetClock(10, 9, 0, 0);

    s.add(100, 0, true);   // victim building (filterA)
    s.add(200, 0, true);   // burglar building (filterB)
    s.add(11, 5, true);    // squad member 1
    s.add(12, 6, true);    // squad member 2

    HeRecord* h = NewHe();
    He_State(h) = 0;
    He_FilterA(h) = 100;
    He_FilterB(h) = 200;
    He_CityId(h) = 7;
    He_ViolationPk(h) = -1;
    He_MemberId8(h, 0) = 11;
    He_MemberId8(h, 1) = 12;
    He_MemberId8(h, 2) = -1;

    // --- tick 1: state 0 ---
    s.nearDoorAll = true;
    NpcAction3_BurglaryStep(h);
    CHECK_EQ(He_State(h), 1);
    CHECK_EQ((int)He_ApptTime(h).minute, 4);

    // --- tick 2: state 1 (all at door) ---
    NpcAction3_BurglaryStep(h);
    CHECK_EQ(He_State(h), 2);
    CHECK_EQ((int)He_ApptTime(h).second, 1);

    // --- tick 3: state 2 (break-in, violation registered) ---
    NpcAction3_BurglaryStep(h);
    CHECK_EQ(He_State(h), 3);
    CHECK_EQ(He_ViolationPk(h), 555);
    CHECK_EQ(He_TargetFilter(h), 100);
    CHECK_EQ(He_TargetPersonId(h), 200);

    // --- tick 4: state 3 (packet applied; seq nonzero -> pair + state 4) ---
    s.packetStatus = 1;
    s.packetSeq = 888;
    NpcAction3_BurglaryStep(h);
    CHECK_EQ(He_State(h), 4);
    CHECK_EQ((int)He_ApptTime(h).minute, 10);

    // --- tick 5: state 4 (packet drained -> steal + teardown to -1) ---
    s.packetSeq = 0;       // violation packet drained
    s.loot = 1500;
    s.detect = false;
    NpcAction3_BurglaryStep(h);
    CHECK_EQ(He_State(h), -1);

    // --- tick 6: state -1 (teardown -> free) ---
    NpcAction3_BurglaryStep(h);

    // Hand-computed command sequence (the load-bearing emits, in order).
    std::vector<std::string> expect = {
        // tick1 (escort out): per member single49 + named53(f1,Einbruch)
        "s49:11", "n53:11>100/-1/f1/Einbruch",
        "s49:12", "n53:12>100/-1/f1/Einbruch",
        // tick4 (pair36 with target person id)
        "pair:888/200",
        // tick5 (loot transfer victim->burglar)
        "r16:100>200=1500",
        // tick5 also recalls escorts to the get-away storable (Entdeckt name)
        "s49:11", "n53:11>200/9001/f0/Entdeckt",
        // tick6 teardown
        "free",
    };
    // Verify each expected emit appears in order (subsequence match).
    size_t idx = 0;
    for (const auto& want : expect) {
        bool found = false;
        for (; idx < s.log.size(); ++idx) {
            if (s.log[idx] == want) { found = true; ++idx; break; }
        }
        CHECK(found);
    }
    delete h;
}

// ===========================================================================
// Burglary "no gesture target" branch: state 2 with no building falls straight to
// the no-violation steal (state 5), which then frees on the next tick.
// ===========================================================================
TEST(NpcAction3_E2E, burglary_no_target_fast_path) {
    E2EScene s; g_s = &s;
    NpcAction3Hooks H = MakeE2EHooks(); SetNpcAction3Hooks(&H);
    SetClock(10, 9, 0, 0);
    s.add(200, 0, true);

    HeRecord* h = NewHe();
    He_State(h) = 2;
    He_FilterA(h) = 999;   // no victim -> query null
    He_FilterB(h) = 200;

    NpcAction3_BurglaryStep(h);            // state 2 -> 5, +15 min
    CHECK_EQ(He_State(h), 5);
    CHECK_EQ((int)He_ApptTime(h).minute, 15);

    NpcAction3_BurglaryStep(h);            // state 5 -> free
    CHECK(!s.log.empty());
    CHECK_EQ(s.log.back(), std::string("free"));
    delete h;
}

// ===========================================================================
// Full recruitment routine: state 0 (progress met) -> phase 1 -> phase 5 (sealed).
// ===========================================================================
TEST(NpcAction3_E2E, recruitment_progression) {
    E2EScene s; g_s = &s;
    NpcAction3Hooks H = MakeE2EHooks(); SetNpcAction3Hooks(&H);
    SetClock(20, 10, 0, 0);
    s.add(1, 5, true); s.add(2, 5, true);
    s.packetStatus = 1;
    s.proximity = 1; s.cost = 1;

    HeRecord* h = NewHe();
    He_State(h) = 0;
    He_ReqHandle(h) = -1;
    He_FilterA(h) = 1; He_FilterB(h) = 2;

    // tick 1: state 0, prox 1, progress 0->1, required 1 -> met -> arm phase 1.
    NpcAction3_RecruitmentState(h);
    CHECK_EQ((int)He_RecruitProgress(h), 1);
    CHECK_EQ((int)He_RecruitRequired(h), 1);

    // Move to phase 1 explicitly (the entity29(1) re-arm would set state via the
    // command apply; here we drive the state to model the applied packet).
    He_State(h) = 1;
    He_ReqHandle(h) = -1;
    NpcAction3_RecruitmentState(h);        // emits advertising, arms phase 5

    // Phase 5 with reciprocation -> seal.
    He_State(h) = 5;
    He_ReqHandle(h) = -1;
    s.proximity = 5;                       // recruitProximity(B,A) > 0
    NpcAction3_RecruitmentState(h);
    CHECK_EQ((int)He_RecruitPaired(h), 1);
    // clock day20/hour10, +2h -> hour 12 (in [7,22], no clamp), day unchanged.
    CHECK_EQ((int)He_ApptTime(h).hour, 12);
    CHECK_EQ((int)He_ApptTime(h).day, 20);

    // The advertising + seal commands appear.
    bool sawSeal = false;
    for (auto& e : s.log) if (e == "e29:4") sawSeal = true;
    CHECK(sawSeal);
    delete h;
}
