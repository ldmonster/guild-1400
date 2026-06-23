// Golden-vector tests for VIBE_MissionReq_Evaluate (gilde.exe 0x5398c4),
// reconstructed in world/mission_recon3_evaluate.cpp.  Each test installs inert
// stub hooks to drive a single dispatch branch and asserts the exact decision /
// comparison polarity recovered from the disassembly.
#include "test.h"

#include "world/mission_recon3_evaluate.h"
#include "world/mission.h"   // g_missionSlotMode

#include <cstring>

using namespace guild::world;

// Fixed-width aliases (mission_requirement.h declares its API in terms of these
// via guild/common/types.h; bring them into scope for the test fixture).
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using i32 = std::int32_t;

namespace {

// ---- shared fixture state the stub hooks read --------------------------------
struct StubState {
    const u8*  person          = nullptr;
    const i32* family          = nullptr;
    i32        buildingCode    = 0;
    u8         stateByte       = 0;
    u16        markerWord      = 0;
    i32        convertReturn   = 0;   // last MoneyConvertToDisplayCoord result
    i32        convertInput    = 0;   // captured amount
    i32        rankReturn      = 0;
    i32        wealthReturn    = 0;
    i32        currencyReturn  = 0;
    double     lawScore        = 1.0;
    float      memberAvg       = 0.0f;
    bool       timerMet        = false;  // captured 'met' arg to accumulateTimer
    bool       timerResult     = false;  // what accumulateTimer returns
    bool       leafResult      = false;  // generic leaf checker return
};
StubState g;

const u8* StubFind(i32) { return g.person; }
const i32* StubFamily(const u8*) { return g.family; }
i32  StubBldCode(const u8*) { return g.buildingCode; }
u8   StubStateByte(const u8*) { return g.stateByte; }
u16  StubMarker(const u8*) { return g.markerWord; }
i32  StubConvert(i32 amt, u8) { g.convertInput = amt; return g.convertReturn; }
i32  StubRank(u8) { return g.rankReturn; }
i32  StubWealth(u16, const u8*) { return g.wealthReturn; }
i32  StubCurrency(const u8*, u8) { return g.currencyReturn; }
double StubLaw(const u8*, u8) { return g.lawScore; }
void StubCount(u8, MemberCount* out) { out->average = g.memberAvg; }
bool StubTimer(ObjectiveRecord*, bool met, i32) { g.timerMet = met; return g.timerResult; }
bool StubLeaf2(const u8*, const ReqTableRow*) { return g.leafResult; }
bool StubLeafObj(const ObjectiveRecord*) { return g.leafResult; }

// Install a hook table wired to the stubs above, then let callers tweak.
void InstallStubs() {
    MissionReq3Hooks& h = MissionReq3GetHooks();
    h = MissionReq3Hooks{};  // reset to inert
    h.personFindRecordById   = StubFind;
    h.personGetFamilyRecord  = StubFamily;
    h.personBuildingTypeCode = StubBldCode;
    h.personStateByte        = StubStateByte;
    h.personMarkerWord       = StubMarker;
    h.moneyConvertToDisplayCoord = StubConvert;
    h.buildingTypeComputeRank    = StubRank;
    h.personComputeTotalWealth   = StubWealth;
    h.personGetCurrencyAmount    = StubCurrency;
    h.economyComputeWeightedLawScore = StubLaw;
    h.countGuildMembers          = StubCount;
    h.accumulateTimer            = StubTimer;
}

// A dummy non-null person record (the dispatcher only passes it to hooks here).
u8 kDummyPerson[8] = {0};

// Build a one-row requirement table for `type` with given threshold/timer.
ReqTableRow MakeRow(u8 type, i32 threshold, i32 timerMin) {
    ReqTableRow r{};
    r.type = type;
    r.threshold = threshold;
    r.timerMin = timerMin;
    return r;
}

ObjectiveRecord MakeObj(u8 type, i32 personId, i32 value) {
    ObjectiveRecord o{};
    o.type = type;
    o.personId = personId;
    o.value = value;
    return o;
}

// Reset all dispatcher globals + stubs into a known-good "enabled, person found"
// baseline for one row of `type`.
void Baseline(u8 type, i32 threshold, i32 timerMin, const ReqTableRow*& rowOut,
              ReqTableRow& row) {
    g = StubState{};
    g.person = kDummyPerson;
    InstallStubs();
    g_missionReqEnabled     = 1;
    g_missionReqSpecialFlag = 0;
    g_missionReqDisplayCcy  = 0;
    g_missionSlotMode       = 0xFE;  // -2 signed: special short-circuit NOT taken
    row = MakeRow(type, threshold, timerMin);
    MissionReq3SetTable(&row, 1);
    rowOut = &row;
}

}  // namespace

// ---- gates -------------------------------------------------------------------

TEST(MissionRecon3, DisabledGateReturnsFalse) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(11, 5, 0, rp, row);
    g_missionReqEnabled = 0;                 // !byte_63CC40
    ObjectiveRecord o = MakeObj(11, 1, 999);
    CHECK(!MissionReqEvaluate(&o));
}

TEST(MissionRecon3, NoMatchingRowReturnsFalse) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(11, 5, 0, rp, row);
    ObjectiveRecord o = MakeObj(12, 1, 999); // type 12 not in single-row table
    CHECK(!MissionReqEvaluate(&o));
}

TEST(MissionRecon3, EmptyTableReturnsFalse) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(11, 5, 0, rp, row);
    MissionReq3SetTable(nullptr, 0);
    ObjectiveRecord o = MakeObj(11, 1, 999);
    CHECK(!MissionReqEvaluate(&o));
}

TEST(MissionRecon3, SpecialShortCircuitMetClearsFlag) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(11, 5, 0, rp, row);
    g_missionReqSpecialFlag = 1;
    g_missionSlotMode = 3;                   // signed in [0,5] -> short-circuit
    ObjectiveRecord o = MakeObj(11, 1, 0);   // value below threshold; ignored
    CHECK(MissionReqEvaluate(&o));
    CHECK_EQ(g_missionReqSpecialFlag, 0);    // one-shot: flag cleared
}

TEST(MissionRecon3, SpecialShortCircuitSkippedWhenModeOutOfRange) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(11, 5, 0, rp, row);
    g_missionReqSpecialFlag = 1;
    g_missionSlotMode = 6;                    // > 5 -> NOT short-circuited
    ObjectiveRecord o = MakeObj(11, 1, 999);  // 999 >= 5 -> true via case 11
    CHECK(MissionReqEvaluate(&o));
    CHECK_EQ(g_missionReqSpecialFlag, 1);     // unchanged
}

TEST(MissionRecon3, NullPersonReturnsFalse) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(11, 5, 0, rp, row);
    g.person = nullptr;                        // FindRecordById -> null
    ObjectiveRecord o = MakeObj(11, 1, 999);
    CHECK(!MissionReqEvaluate(&o));
}

// ---- value compare cases (11,19,23,28,40): objective.value >= threshold ------

TEST(MissionRecon3, Case11ValueGreaterEqual) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(11, 5, 0, rp, row);
    ObjectiveRecord o = MakeObj(11, 1, 5);     // 5 >= 5 -> true (setnl)
    CHECK(MissionReqEvaluate(&o));
    o.value = 4;                               // 4 >= 5 -> false
    CHECK(!MissionReqEvaluate(&o));
}

TEST(MissionRecon3, Case40ValueGreaterEqual) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(40, 100, 0, rp, row);
    ObjectiveRecord o = MakeObj(40, 1, 100);
    CHECK(MissionReqEvaluate(&o));
}

// ---- family-wealth cases use strict ">" (setnle) -----------------------------

TEST(MissionRecon3, Case1FamilyWealthStrictGreater) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(1, 50, 0, rp, row);
    i32 fam[16] = {0}; fam[14] = 777;          // family[+0x38]
    g.family = fam;
    g.convertReturn = 51;                      // 51 > 50 -> true
    ObjectiveRecord o = MakeObj(1, 1, 0);
    CHECK(MissionReqEvaluate(&o));
    CHECK_EQ(g.convertInput, 777);             // converted family[14]
    g.convertReturn = 50;                      // 50 > 50 -> false (strict)
    CHECK(!MissionReqEvaluate(&o));
}

TEST(MissionRecon3, Case1NullFamilyFalse) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(1, 50, 0, rp, row);
    g.family = nullptr;
    ObjectiveRecord o = MakeObj(1, 1, 0);
    CHECK(!MissionReqEvaluate(&o));
}

TEST(MissionRecon3, Case20FamilySumStrictGreater) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(20, 10, 0, rp, row);
    i32 fam[16] = {0}; fam[11] = 3; fam[13] = 4;  // sum = 7
    g.family = fam;
    g.convertReturn = 11;
    ObjectiveRecord o = MakeObj(20, 1, 0);
    CHECK(MissionReqEvaluate(&o));
    CHECK_EQ(g.convertInput, 7);                  // family[11]+family[13]
}

TEST(MissionRecon3, Case27FamilyDword15) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(27, 0, 0, rp, row);
    i32 fam[16] = {0}; fam[15] = 99;
    g.family = fam;
    g.convertReturn = 1;                          // 1 > 0 -> true
    ObjectiveRecord o = MakeObj(27, 1, 0);
    CHECK(MissionReqEvaluate(&o));
    CHECK_EQ(g.convertInput, 99);
}

// ---- rank case (2,7) uses ">=" (setnl) ---------------------------------------

TEST(MissionRecon3, Case2RankGreaterEqual) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(2, 3, 0, rp, row);
    g.buildingCode = 7;
    g.rankReturn = 3;                             // 3 >= 3 -> true
    ObjectiveRecord o = MakeObj(2, 1, 0);
    CHECK(MissionReqEvaluate(&o));
    g.rankReturn = 2;                             // 2 >= 3 -> false
    CHECK(!MissionReqEvaluate(&o));
}

// ---- currency case (31) uses ">=" --------------------------------------------

TEST(MissionRecon3, Case31CurrencyGreaterEqual) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(31, 1000, 0, rp, row);
    g.currencyReturn = 5000;
    g.convertReturn = 1000;                       // 1000 >= 1000 -> true
    ObjectiveRecord o = MakeObj(31, 1, 0);
    CHECK(MissionReqEvaluate(&o));
    CHECK_EQ(g.convertInput, 5000);
}

// ---- wealth case (5,13,21,30) uses ">=" --------------------------------------

TEST(MissionRecon3, Case5WealthGreaterEqual) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(5, 100, 0, rp, row);
    g.markerWord = 9;
    g.wealthReturn = 250;
    g.convertReturn = 100;                        // 100 >= 100 -> true
    ObjectiveRecord o = MakeObj(5, 1, 0);
    CHECK(MissionReqEvaluate(&o));
    CHECK_EQ(g.convertInput, 250);
}

// ---- timer-gated cases (3,10,24): person[+0x0D] >= threshold -> accumulateTimer

TEST(MissionRecon3, Case3ClanSizeTimerGate) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(3, 4, 30, rp, row);
    u8 personRec[0x20] = {0};
    personRec[0x0D] = 5;                          // 5 >= 4 -> met = true
    g.person = personRec;
    g.timerResult = true;
    ObjectiveRecord o = MakeObj(3, 1, 0);
    CHECK(MissionReqEvaluate(&o));
    CHECK_EQ(g.timerMet ? 1 : 0, 1);              // condition passed to timer
    personRec[0x0D] = 3;                          // 3 >= 4 -> met = false
    g.timerResult = false;
    CHECK(!MissionReqEvaluate(&o));
    CHECK_EQ(g.timerMet ? 1 : 0, 0);
}

// ---- timer-gated state-equality cases (15-18,34,35): person[+0x166] == thr ----

TEST(MissionRecon3, Case15StateEqualityTimerGate) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(15, 7, 60, rp, row);
    g.stateByte = 7;                              // 7 == 7 -> met = true
    g.timerResult = true;
    ObjectiveRecord o = MakeObj(15, 1, 0);
    CHECK(MissionReqEvaluate(&o));
    CHECK_EQ(g.timerMet ? 1 : 0, 1);
    g.stateByte = 8;                              // 8 == 7 -> met = false
    g.timerResult = false;
    CHECK(!MissionReqEvaluate(&o));
    CHECK_EQ(g.timerMet ? 1 : 0, 0);
}

// ---- float case (47): (double)threshold * 0.01 <= memberAvg -------------------

TEST(MissionRecon3, Case47AverageFloatCompare) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(47, 200, 0, rp, row);                // 200*0.01 = 2.0
    g.memberAvg = 2.0f;                           // 2.0 <= 2.0 -> true (setbe)
    ObjectiveRecord o = MakeObj(47, 1, 0);
    CHECK(MissionReqEvaluate(&o));
    g.memberAvg = 1.99f;                          // 2.0 <= 1.99 -> false
    CHECK(!MissionReqEvaluate(&o));
}

// ---- law-score case (48): weighted score == 0.0 -------------------------------

TEST(MissionRecon3, Case48LawScoreZero) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(48, 0, 0, rp, row);
    g.lawScore = 0.0;                             // == 0.0 -> true
    ObjectiveRecord o = MakeObj(48, 1, 0);
    CHECK(MissionReqEvaluate(&o));
    g.lawScore = 0.5;                             // != 0.0 -> false
    CHECK(!MissionReqEvaluate(&o));
}

// ---- leaf-delegation cases route to the right hook ----------------------------

TEST(MissionRecon3, Case4DelegatesToStatThreshold) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(4, 0, 0, rp, row);
    MissionReq3GetHooks().checkStatThreshold = StubLeaf2;
    g.leafResult = true;
    ObjectiveRecord o = MakeObj(4, 1, 0);
    CHECK(MissionReqEvaluate(&o));
    g.leafResult = false;
    CHECK(!MissionReqEvaluate(&o));
}

TEST(MissionRecon3, Case39DelegatesToMultiStat) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(39, 0, 0, rp, row);
    MissionReq3GetHooks().checkMultiStat = StubLeafObj;
    g.leafResult = true;
    ObjectiveRecord o = MakeObj(39, 1, 0);
    CHECK(MissionReqEvaluate(&o));
}

TEST(MissionRecon3, Case49DelegatesToNoActiveCombat) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(49, 0, 0, rp, row);
    MissionReq3GetHooks().checkNoActiveCombat = StubLeafObj;
    g.leafResult = true;
    ObjectiveRecord o = MakeObj(49, 1, 0);
    CHECK(MissionReqEvaluate(&o));
}

// ---- default / case 42 -> false ----------------------------------------------

TEST(MissionRecon3, Case42DefaultFalse) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(42, 0, 0, rp, row);                  // 42 is the explicit default
    ObjectiveRecord o = MakeObj(42, 1, 0);
    CHECK(!MissionReqEvaluate(&o));
}

TEST(MissionRecon3, UnknownTypeDefaultFalse) {
    const ReqTableRow* rp; ReqTableRow row;
    Baseline(50, 0, 0, rp, row);                  // out of [1,49]
    ObjectiveRecord o = MakeObj(50, 1, 0);
    CHECK(!MissionReqEvaluate(&o));
}
