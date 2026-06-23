// Golden-vector unit tests for office_recon2_rules.{h,cpp} — the portable office/guild/
// privilege rule logic reconstructed 1:1 from gilde.exe. Self-contained.
#include <string>

#include "tests/framework/test.h"
#include "world/office_recon2_rules.h"

using namespace guild;
using namespace guild::world;

// ---------------------------------------------------------------------------
// ResolveStaffModel (0x57c1e8)
// ---------------------------------------------------------------------------
TEST(Office2ReconStaffModel, UnderThresholdByGender) {
    StaffModelInputs in;
    in.staffFlagA = 0;
    in.curStaffCount = 2;
    in.staffThreshold = 5.0f; // 2 < 5 -> under threshold
    in.genderByte = 0;
    CHECK_EQ((int)OfficeResolveStaffModel(in).kind, (int)StaffModelResult::kUnderThreshFemale);
    in.genderByte = 1;
    CHECK_EQ((int)OfficeResolveStaffModel(in).kind, (int)StaffModelResult::kUnderThreshMale);
}

TEST(Office2ReconStaffModel, ThresholdReachedTakesMainPath) {
    StaffModelInputs in;
    in.staffFlagA = 0;
    in.curStaffCount = 5;
    in.staffThreshold = 5.0f; // 5 >= 5 -> main path (not under-threshold)
    in.genderByte = 0;
    // No tables, neither flag -> LABEL_39 with null shortFemale -> kNone.
    CHECK_EQ((int)OfficeResolveStaffModel(in).kind, (int)StaffModelResult::kNone);
}

TEST(Office2ReconStaffModel, RoundRobinAdvancesSlotMod8) {
    StaffModelInputs in;
    in.staffFlagA = 1;          // main path
    in.staffFlagC = 1;
    in.roleIdField = 7;         // != -1 -> round-robin
    int prev = 7;
    StaffModelResult r = OfficeResolveStaffModel(in, &prev);
    CHECK_EQ((int)r.kind, (int)StaffModelResult::kRoundRobinName);
    CHECK_EQ(prev, 0);          // (7+1)%8 == 0
    CHECK_EQ(r.roundRobinSlot, 0);
    StaffModelResult r2 = OfficeResolveStaffModel(in, &prev);
    CHECK_EQ(prev, 1);
    CHECK_EQ(r2.roundRobinSlot, 1);
}

TEST(Office2ReconStaffModel, RoleIdSentinelSkipsRoundRobin) {
    StaffModelInputs in;
    in.staffFlagA = 1;
    in.staffFlagC = 1;
    in.roleIdField = -1;        // sentinel -> NOT round-robin, table path
    // staffFlagA + gender 0 + no table -> &0[0] -> kNone
    CHECK_EQ((int)OfficeResolveStaffModel(in).kind, (int)StaffModelResult::kNone);
}

TEST(Office2ReconStaffModel, BuildingType17) {
    StaffModelInputs in;
    in.staffFlagA = 1;          // main path
    in.staffFlagC = 0;          // table path
    in.buildingType = 17;
    auto rng = [](unsigned m, void*) -> int { return (int)(2u % m); };
    StaffModelResult r = OfficeResolveStaffModel(in, nullptr, rng, nullptr);
    CHECK_EQ((int)r.kind, (int)StaffModelResult::kType17);
    CHECK_EQ(r.randIndex, 2);
}

TEST(Office2ReconStaffModel, FullTableScanMatchSelectorA) {
    static const StaffModelRecord rows[] = {{11}, {22}, {33}, {0}};
    StaffModelInputs in;
    in.staffFlagA = 1;
    in.staffFlagC = 0;
    in.buildingType = 3;
    in.genderByte = 1;          // -> fullMale
    in.selectorA = 33;
    in.fullMale = {rows, 76};
    StaffModelResult r = OfficeResolveStaffModel(in);
    CHECK_EQ((int)r.kind, (int)StaffModelResult::kTableRecord);
    CHECK(r.record == &rows[2]);
}

TEST(Office2ReconStaffModel, FullTableNoMatchReturnsIndexAtStop) {
    static const StaffModelRecord rows[] = {{11}, {22}, {0}};
    StaffModelInputs in;
    in.staffFlagA = 1;
    in.buildingType = 3;
    in.genderByte = 0;          // fullFemale
    in.selectorA = 999;         // no match; loop stops at the {0} terminator (v3==2)
    in.fullFemale = {rows, 76};
    StaffModelResult r = OfficeResolveStaffModel(in);
    CHECK_EQ((int)r.kind, (int)StaffModelResult::kTableRecord);
    CHECK(r.record == &rows[2]); // &base[40*v3] with v3==2
}

TEST(Office2ReconStaffModel, ShortTableMatchSelectorB) {
    static const StaffModelRecord rows[] = {{5}, {6}, {0}};
    StaffModelInputs in;
    in.staffFlagA = 0;
    in.curStaffCount = 9;
    in.staffThreshold = 1.0f;   // main path via threshold
    in.staffFlagB = 1;
    in.buildingType = 3;
    in.genderByte = 0;          // shortFemale
    in.selectorB = 6;
    in.shortFemale = {rows, 27};
    StaffModelResult r = OfficeResolveStaffModel(in);
    CHECK_EQ((int)r.kind, (int)StaffModelResult::kTableRecord);
    CHECK(r.record == &rows[1]);
}

TEST(Office2ReconStaffModel, NeitherFlagFallbackByGender) {
    static const StaffModelRecord fem[] = {{1}};
    static const StaffModelRecord mal[] = {{2}};
    StaffModelInputs in;
    in.staffFlagA = 0;
    in.curStaffCount = 9;
    in.staffThreshold = 1.0f;
    in.staffFlagB = 0;
    in.buildingType = 3;
    in.shortFemale = {fem, 27};
    in.shortMale = {mal, 27};
    in.genderByte = 0;
    CHECK(OfficeResolveStaffModel(in).record == &fem[0]);
    in.genderByte = 1;
    CHECK(OfficeResolveStaffModel(in).record == &mal[0]);
    in.genderByte = 2; // other -> v2 (0) -> kNone
    CHECK_EQ((int)OfficeResolveStaffModel(in).kind, (int)StaffModelResult::kNone);
}

// ---------------------------------------------------------------------------
// AddEntryDefault (0x47e6c4)
// ---------------------------------------------------------------------------
static int g_lastHolder, g_lastPrim, g_lastType, g_lastSucc, g_lastFlag;
static int FakeAdd(u8 h, int p, int t, int s, int f, void*) {
    g_lastHolder = h; g_lastPrim = p; g_lastType = t; g_lastSucc = s; g_lastFlag = f;
    return 42;
}
TEST(Office2ReconAddEntry, ForwardsExactArgs) {
    OfficeAddTableEntryHook hook; hook.fn = &FakeAdd;
    int r = OfficeAddEntryDefault(0x07, hook);
    CHECK_EQ(r, 42);
    CHECK_EQ(g_lastHolder, 7);
    CHECK_EQ(g_lastPrim, 0);
    CHECK_EQ(g_lastType, 3);
    CHECK_EQ(g_lastSucc, 0);
    CHECK_EQ(g_lastFlag, 255);
}
TEST(Office2ReconAddEntry, InertWhenNoHook) {
    OfficeAddTableEntryHook hook; // fn null
    CHECK_EQ(OfficeAddEntryDefault(1, hook), -1);
}

// ---------------------------------------------------------------------------
// SpawnSessionActor fallback model (0x49da18)
// ---------------------------------------------------------------------------
TEST(Office2ReconSpawn, FallbackModelNames) {
    CHECK(std::string("buerger_MANN") == OfficeSpawnActorFallbackModel(1));
    CHECK(std::string("buerger2_MANN") == OfficeSpawnActorFallbackModel(2));
    CHECK(std::string("handwerker3_MANN") == OfficeSpawnActorFallbackModel(0));
    CHECK(std::string("handwerker3_MANN") == OfficeSpawnActorFallbackModel(3));
}

// ---------------------------------------------------------------------------
// Candidacy dialog rules (0x51fc50)
// ---------------------------------------------------------------------------
TEST(Office2ReconCandidacy, CountStopsAt16Bytes) {
    // 768 slots all matching -> capped at 4 (16 bytes / 4).
    static u8 all[768];
    for (auto& b : all) b = 9;
    CHECK_EQ(OfficeCandidacyCountCandidates(all, 768, 9), 4);
}
TEST(Office2ReconCandidacy, CountSkipsInvalidAndMismatch) {
    u8 slots[6] = {9, 0xFF, 3, 9, 9, 0xFF};
    CHECK_EQ(OfficeCandidacyCountCandidates(slots, 6, 9), 3);
}
TEST(Office2ReconCandidacy, ApplyEnableGate) {
    CHECK(OfficeCandidacyApplyEnabled(0, false));
    CHECK(OfficeCandidacyApplyEnabled(3, false));
    CHECK(!OfficeCandidacyApplyEnabled(4, false));   // count >= 4
    CHECK(!OfficeCandidacyApplyEnabled(1, true));     // holder already has office
}
TEST(Office2ReconCandidacy, CardLayout) {
    // formW=640<<16, margin=20<<16: usable=640-40=600, colW=200, extra=0.
    int x, y;
    OfficeCandidacyCardLayout(0, 640 << 16, 20 << 16, &x, &y);
    // i=0: p=0 -> x = 0 + 200*1 + 10*(-1) + 20*0 = 190 ; y = 40
    CHECK_EQ(x, 190);
    CHECK_EQ(y, 40);
    OfficeCandidacyCardLayout(1, 640 << 16, 20 << 16, &x, &y);
    // i=1: p=1 -> x = 0 + 200*2 + 10*0 + 20*1 = 420 ; y = 40
    CHECK_EQ(x, 420);
    CHECK_EQ(y, 40);
    OfficeCandidacyCardLayout(2, 640 << 16, 20 << 16, &x, &y);
    // i=2: p=0 -> x=190 ; y = 130*1 + 40 = 170
    CHECK_EQ(x, 190);
    CHECK_EQ(y, 170);
}

// ---------------------------------------------------------------------------
// Successor choice gate (0x4a03f4 / 0x4a04f4)
// ---------------------------------------------------------------------------
static SuccessorGateInputs base_gate() {
    SuccessorGateInputs g;
    g.entitySlot156Zero = true;
    g.holderValid = true;
    g.holderHoldsOffice = true;
    g.holderBlocked = false;
    g.slotType = 1;
    g.twoPersonsValid = true;
    g.nextRankInCategory = true;
    return g;
}
TEST(Office2ReconSuccessor, TwoPersonPath) {
    CHECK_EQ((int)OfficePrepareSuccessorChoice(base_gate()), (int)SuccessorChoice::kTwoPerson);
}
TEST(Office2ReconSuccessor, CategoryPath) {
    auto g = base_gate(); g.slotType = 2;
    CHECK_EQ((int)OfficePrepareSuccessorChoice(g), (int)SuccessorChoice::kCategory);
}
TEST(Office2ReconSuccessor, AbortConditions) {
    auto g = base_gate(); g.entitySlot156Zero = false;
    CHECK_EQ((int)OfficePrepareSuccessorChoice(g), (int)SuccessorChoice::kAbort);
    g = base_gate(); g.holderValid = false;
    CHECK_EQ((int)OfficePrepareSuccessorChoice(g), (int)SuccessorChoice::kAbort);
    g = base_gate(); g.holderHoldsOffice = false;
    CHECK_EQ((int)OfficePrepareSuccessorChoice(g), (int)SuccessorChoice::kAbort);
    g = base_gate(); g.holderBlocked = true;
    CHECK_EQ((int)OfficePrepareSuccessorChoice(g), (int)SuccessorChoice::kAbort);
    g = base_gate(); g.twoPersonsValid = false; // slotType 1 but persons invalid
    CHECK_EQ((int)OfficePrepareSuccessorChoice(g), (int)SuccessorChoice::kAbort);
    g = base_gate(); g.slotType = 2; g.nextRankInCategory = false;
    CHECK_EQ((int)OfficePrepareSuccessorChoice(g), (int)SuccessorChoice::kAbort);
    g = base_gate(); g.slotType = 5; // unknown slot type
    CHECK_EQ((int)OfficePrepareSuccessorChoice(g), (int)SuccessorChoice::kAbort);
}

// ---------------------------------------------------------------------------
// Guild Level3 dialog text id (0x5210e4 / 0x521234)
// ---------------------------------------------------------------------------
TEST(Office2ReconGuildDialog, BodyTextId) {
    CHECK_EQ(GuildLevel3DialogBodyTextId(false, 4), 529);  // 525 + 4
    CHECK_EQ(GuildLevel3DialogBodyTextId(true, 4), 564);   // 560 + 4
    CHECK_EQ(GuildLevel3DialogBodyTextId(false, 0), 525);
}

// ---------------------------------------------------------------------------
// Privilege miracle amounts + selection (0x56499c)
// ---------------------------------------------------------------------------
TEST(Office2ReconPrivilege, AmountCase0) {
    // rand3=1 -> mult 3 ; wealth 1000 -> 3 * (1000*0.01) = 30
    CHECK_EQ(PrivilegeMiracleAmountCase0(1, 1000), 30);
    // rand3=0 -> mult 2 ; wealth 250 -> 2 * 2.5 = 5
    CHECK_EQ(PrivilegeMiracleAmountCase0(0, 250), 5);
    // truncation: mult 2, wealth 99 -> 2 * 0.99 = 1.98 -> 1
    CHECK_EQ(PrivilegeMiracleAmountCase0(0, 99), 1);
}
TEST(Office2ReconPrivilege, AmountCase5) {
    // flt_624D6C is a 32-bit float (0.0099999998), so the product truncates low —
    // faithful to the original's single-precision constant.
    // rand5=0 -> mult 3 ; wealth 1000 -> 3 * (1000 * 0.0099999998) = 29.9999... -> 29
    CHECK_EQ(PrivilegeMiracleAmountCase5(0, 1000), 29);
    // rand5=4 -> mult 7 ; wealth 200 -> 7 * (200 * 0.0099999998) = 13.9999... -> 13
    CHECK_EQ(PrivilegeMiracleAmountCase5(4, 200), 13);
}
TEST(Office2ReconPrivilege, PickLeastWealthy) {
    i32 w[] = {500, 120, 800, 120, 300};
    CHECK_EQ(PrivilegeMiraclePickLeastWealthy(w, 5), 1); // first minimum (strict <)
    i32 single[] = {7};
    CHECK_EQ(PrivilegeMiraclePickLeastWealthy(single, 1), 0);
    CHECK_EQ(PrivilegeMiraclePickLeastWealthy(nullptr, 0), -1);
    CHECK_EQ(PrivilegeMiraclePickLeastWealthy(w, 0), -1);
}
