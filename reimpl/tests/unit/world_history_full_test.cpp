// Unit tests for the remaining History / Statistics / Stammbaum / Tutorial bodies:
//   * history_full  — chronicle FirstPass syntax validator, token classification,
//                     role/prefix tables, forward-scan step codes.
//   * statistics_full — full economy-report orchestration + recipient walk.
//   * stammbaum_query — multi-generation ancestor/descendant/inheritance queries.
//   * tutorial_steps  — chapter step tables + chain build/length.
#include "tests/framework/test.h"

#include "world/history_full.h"
#include "world/statistics_full.h"
#include "world/stammbaum_query.h"
#include "world/tutorial_steps.h"
#include "crt/rand.h"

#include <cstring>

using namespace guild;
using namespace guild::world;

// ===========================================================================
// history_full
// ===========================================================================
TEST(WorldHistoryFull, FirstPassValidBrackets) {
    CHECK(HistoryParseTextFirstPassValid("plain text"));
    CHECK(HistoryParseTextFirstPassValid("a[long#short]b"));   // balanced region
    CHECK(HistoryParseTextFirstPassValid(nullptr));            // null -> ok
}

TEST(WorldHistoryFull, FirstPassSyntaxErrors) {
    CHECK(!HistoryParseTextFirstPassValid("a#b"));     // '#' with no open region
    CHECK(!HistoryParseTextFirstPassValid("a[b]"));    // ']' with no '#' marker
    CHECK(!HistoryParseTextFirstPassValid("a[b#c"));   // region never closed
    CHECK(!HistoryParseTextFirstPassValid("a[b#c#d]"));// duplicate '#' marker
    CHECK(!HistoryParseTextFirstPassValid("a[[b#c]")); // nested '[' start
}

TEST(WorldHistoryFull, RoleNameTable) {
    CHECK_EQ(kHistoryRoleCount, 15);
    CHECK_EQ(HistoryRoleNameIndex("BUERGERMEISTER"), 0);
    CHECK_EQ(HistoryRoleNameIndex("BISCHOF"), 1);
    CHECK_EQ(HistoryRoleNameIndex("GELD"), 8);
    CHECK_EQ(HistoryRoleNameIndex("STADTKASSE"), 9);
    CHECK_EQ(HistoryRoleNameIndex("RND_SPIELER"), 14);
    CHECK_EQ(HistoryRoleNameIndex("NOT_A_ROLE"), -1);
    // Leading-prefix match (a token beginning with a role name matches).
    CHECK_EQ(HistoryRoleNameIndex("GELD_extra"), 8);
}

TEST(WorldHistoryFull, TokenModeAndSlot) {
    CHECK(HistoryClassifyTokenMode("_NEW3") == HistoryTokenMode::kNew);
    CHECK(HistoryClassifyTokenMode("_USE0") == HistoryTokenMode::kUse);
    CHECK(HistoryClassifyTokenMode("_REL5") == HistoryTokenMode::kRel);
    CHECK(HistoryClassifyTokenMode("_SET5") == HistoryTokenMode::kLiteral);  // not in 3-scan
    CHECK(HistoryClassifyTokenMode("BISCHOF") == HistoryTokenMode::kLiteral);

    CHECK_EQ(HistoryTokenSlot("_NEW3", HistoryTokenMode::kNew), 3);
    CHECK_EQ(HistoryTokenSlot("_USE7", HistoryTokenMode::kUse), 7);
    CHECK_EQ(HistoryTokenSlot("_REL9", HistoryTokenMode::kRel), -1);  // 9 >= 8
    CHECK_EQ(HistoryTokenSlot("BISCHOF", HistoryTokenMode::kLiteral), -1);
}

TEST(WorldHistoryFull, ScanStepCodes) {
    CHECK(HistoryScanStep(false, 5, false) == HistoryScanCode::kParseError);
    CHECK(HistoryScanStep(true, 1, false)  == HistoryScanCode::kEmit);
    CHECK(HistoryScanStep(true, 1, true)   == HistoryScanCode::kEmitEmpty);
    CHECK(HistoryScanStep(true, 0, false)  == HistoryScanCode::kStop);   // diff<=1
    CHECK(HistoryScanStep(true, -3, false) == HistoryScanCode::kStop);
    CHECK(HistoryScanStep(true, 5, false)  == HistoryScanCode::kEndOfFile);
}

// ===========================================================================
// statistics_full
// ===========================================================================
TEST(WorldStatsFull, CadenceGate) {
    EconomyReportInput in{};
    CHECK(!EconomyReportBuild(7, in).ran);    // round < 8
    CHECK(!EconomyReportBuild(9, in).ran);    // 9 % 4 != 0
    CHECK(EconomyReportBuild(8, in).ran);     // 8 >= 8 && 8 % 4 == 0
    CHECK(EconomyReportBuild(12, in).ran);
}

TEST(WorldStatsFull, ReportDecision) {
    EconomyReportInput in{};
    in.total0 = 0.10f;  // goods (weak)
    in.total1 = 0.40f;  // services
    in.total2 = 0.50f;  // trade
    in.lawScore = 0.30f;
    in.luxury = 0.60f;  // > 0.55 -> booming
    EconomyReportOutcome o = EconomyReportBuild(8, in);
    CHECK(o.ran);
    // trade 0.50 > 0.15 -> 6178; goods 0.10 -> 6175; law 0.30 -> 6176; svc 0.40 -> 6181
    CHECK_EQ(o.trends.trade, 6178);
    CHECK_EQ(o.trends.goods, 6175);
    CHECK_EQ(o.trends.law, 6176);
    CHECK_EQ(o.trends.services, 6181);
    CHECK(o.luxury == EconomyReportLuxury::kBooming);
    CHECK(o.broadcasts);
}

TEST(WorldStatsFull, NeutralLuxuryNoWeakLine) {
    EconomyReportInput in{};
    in.total0 = 0.50f; in.total1 = 0.50f; in.total2 = 0.50f;
    in.lawScore = 0.50f; in.luxury = 0.50f;   // neutral luxury, all above weak gate
    EconomyReportOutcome o = EconomyReportBuild(8, in);
    CHECK(o.ran);
    CHECK(o.weakLine);                          // min 0.50 > 0.20 gate
    CHECK(o.luxury == EconomyReportLuxury::kNeutral);
    CHECK(o.broadcasts);                        // weak line alone broadcasts
}

TEST(WorldStatsFull, RecipientWalk) {
    ReportRecipient table[5] = {
        {6, 100}, {1, 200}, {6, 300}, {0, 400}, {6, 500},
    };
    CHECK_EQ(ReportEligibleRecipientCount(table, 5), 3);
    CHECK_EQ(kReportRecipientCount, 768);
    CHECK_EQ(kReportRecipientStride, 134);
}

// ===========================================================================
// stammbaum_query — synthetic 3-generation dynasty
// ===========================================================================
// Tree:
//   1 (founder) + spouse 2  -> children 3, 4
//   3 + spouse 5            -> children 6, 7
//   6                       -> child 8
static FamilyRecord MakeRec(i32 id, i32 father, i32 mother, i32 spouse,
                            const i32* kids, int nk) {
    FamilyRecord r{};
    r.id = id; r.father = father; r.mother = mother; r.spouse = spouse;
    for (int i = 0; i < kMaxChildren; ++i) r.children[i] = kFamilyNone;
    for (int i = 0; i < nk && i < kMaxChildren; ++i) r.children[i] = kids[i];
    return r;
}

static FamilyTree BuildDynasty(FamilyRecord* buf) {
    i32 k1[] = {3, 4};
    i32 k3[] = {6, 7};
    i32 k6[] = {8};
    buf[0] = MakeRec(1, -1, -1, 2, k1, 2);
    buf[1] = MakeRec(2, -1, -1, 1, k1, 2);
    buf[2] = MakeRec(3, 1, 2, 5, k3, 2);
    buf[3] = MakeRec(4, 1, 2, -1, nullptr, 0);
    buf[4] = MakeRec(5, -1, -1, 3, k3, 2);
    buf[5] = MakeRec(6, 3, 5, -1, k6, 1);
    buf[6] = MakeRec(7, 3, 5, -1, nullptr, 0);
    buf[7] = MakeRec(8, 6, -1, -1, nullptr, 0);
    FamilyTree t{buf, 8, };
    return t;
}

TEST(WorldStammbaumQuery, GenerationDistance) {
    FamilyRecord buf[8];
    FamilyTree t = BuildDynasty(buf);
    CHECK_EQ(FamilyGenerationDistance(t, 1, 1), 0);
    CHECK_EQ(FamilyGenerationDistance(t, 1, 3), 1);   // parent
    CHECK_EQ(FamilyGenerationDistance(t, 1, 6), 2);   // grandparent
    CHECK_EQ(FamilyGenerationDistance(t, 1, 8), 3);   // great-grandparent
    CHECK_EQ(FamilyGenerationDistance(t, 8, 1), -1);  // not an ancestor (wrong way)
    CHECK_EQ(FamilyGenerationDistance(t, 2, 8), 3);   // via mother branch
}

TEST(WorldStammbaumQuery, AncestorsAtGen) {
    FamilyRecord buf[8];
    FamilyTree t = BuildDynasty(buf);
    i32 out[16];
    int n = FamilyCollectAncestorsAtGen(t, 8, 2, out, 16);  // grandparents of 8
    CHECK_EQ(n, 2);   // 3 and 5
    bool has3 = false, has5 = false;
    for (int i = 0; i < n; ++i) { has3 |= out[i] == 3; has5 |= out[i] == 5; }
    CHECK(has3 && has5);

    n = FamilyCollectAncestorsAtGen(t, 8, 3, out, 16);      // great-grandparents
    CHECK_EQ(n, 2);   // 1 and 2 (parents of 3); 5 has no parents in tree
}

TEST(WorldStammbaumQuery, DescendantsAtGen) {
    FamilyRecord buf[8];
    FamilyTree t = BuildDynasty(buf);
    i32 out[16];
    int n = FamilyCollectDescendantsAtGen(t, 1, 1, out, 16);  // children of 1
    CHECK_EQ(n, 2);   // 3, 4
    n = FamilyCollectDescendantsAtGen(t, 1, 2, out, 16);      // grandchildren
    CHECK_EQ(n, 2);   // 6, 7 (children of 3; 4 has none)
    n = FamilyCollectDescendantsAtGen(t, 1, 3, out, 16);      // great-grandchildren
    CHECK_EQ(n, 1);   // 8
}

TEST(WorldStammbaumQuery, InheritanceLine) {
    FamilyRecord buf[8];
    FamilyTree t = BuildDynasty(buf);
    i32 out[8];
    int n = FamilyResolveInheritance(t, 1, out, 8);  // eldest-child line from 1
    CHECK_EQ(n, 3);   // 3 -> 6 -> 8
    CHECK_EQ(out[0], 3);
    CHECK_EQ(out[1], 6);
    CHECK_EQ(out[2], 8);

    // A childless person falls back to siblings.
    n = FamilyResolveInheritance(t, 4, out, 8);
    CHECK_EQ(n, 1);   // sibling 3
    CHECK_EQ(out[0], 3);
}

TEST(WorldStammbaumQuery, BloodRelation) {
    FamilyRecord buf[8];
    FamilyTree t = BuildDynasty(buf);
    CHECK(FamilyAreBloodRelated(t, 6, 7, 3));   // siblings (common parents)
    CHECK(FamilyAreBloodRelated(t, 8, 4, 3));   // 8's gg-uncle line via 1/2
    CHECK(FamilyAreBloodRelated(t, 1, 8, 3));   // ancestor/descendant
    CHECK(!FamilyAreBloodRelated(t, 2, 5, 3));  // 2 and 5 are unrelated in-laws
}

// ===========================================================================
// tutorial_steps
// ===========================================================================
TEST(WorldTutorialSteps, Chapter1Table) {
    int n = 0;
    const TutorialNodeSpec* c1 = TutorialChapter1Steps(&n);
    CHECK_EQ(n, 11);                                 // intro + 9 steps + outro
    CHECK_EQ(c1[0].kind, kTutNodeIntro + 1);         // chapter1 intro marker == 1
    CHECK_EQ(c1[0].mainTextId, 7362);
    CHECK(std::strcmp(c1[0].mainRes, "CHAPTER_1_INTRO") == 0);
    CHECK_EQ(c1[10].kind, kTutNodeOutro);            // outro marker == 7
    CHECK_EQ(c1[10].mainTextId, 7401);
    // Step A: main 7364, remind 7366, done 7367, arrow target 22.
    CHECK_EQ(c1[1].mainTextId, 7364);
    CHECK_EQ(c1[1].remindTextId, 7366);
    CHECK_EQ(c1[1].doneTextId, 7367);
    CHECK_EQ(c1[1].arrowTarget, 22);
    CHECK_EQ(c1[1].voiceA, 1065);
}

TEST(WorldTutorialSteps, Chapter2Table) {
    int n = 0;
    const TutorialNodeSpec* c2 = TutorialChapter2Steps(&n);
    CHECK_EQ(n, 9);                                  // intro + 7 steps + outro
    CHECK_EQ(c2[0].mainTextId, 7403);
    CHECK_EQ(c2[8].mainTextId, 7432);                // outro
    // Step B has the drop-target match callback and form type 11.
    CHECK(c2[2].cb2 == TutorialCallback::kMatchObjectDropTarget);
    CHECK_EQ(c2[2].mask, 22583);
}

TEST(WorldTutorialSteps, NodeLayoutSizes) {
    CHECK_EQ((int)sizeof(TutorialChapterNode), 116);
    CHECK_EQ((int)offsetof(TutorialChapterNode, nextRaw), 112);
    CHECK_EQ((int)offsetof(TutorialChapterNode, arrowTarget), 64);
}

TEST(WorldTutorialSteps, BuildAndWalkChain) {
    int n = 0;
    const TutorialNodeSpec* c1 = TutorialChapter1Steps(&n);
    TutorialChapterNode nodes[16];
    TutorialChapterNode* head = TutorialBuildChain(c1, n, nodes);
    CHECK(head == &nodes[0]);
    CHECK_EQ(TutorialNodeChainLength(head), n);
    // Fields copied byte-for-byte into the node.
    CHECK_EQ(nodes[1].mainTextId, 7364);
    CHECK(nodes[1].mainRes32 != 0);                          // resource present
    CHECK(std::strcmp(c1[1].mainRes, "CHAPTER_1_A_MAIN") == 0);
    CHECK_EQ((int)nodes[n - 1].nextRaw, 0);                  // last node ends the chain
}

TEST(WorldTutorialSteps, MainIntroOutro) {
    const TutorialNodeSpec& in = TutorialMainIntro();
    CHECK_EQ(in.kind, kTutNodeIntro);
    CHECK_EQ(in.mainTextId, 7356);
    CHECK_EQ(in.voiceA, -1);
    const TutorialNodeSpec& out = TutorialMainOutro();
    CHECK_EQ(out.kind, kTutNodeOutro);
    CHECK_EQ(out.mainTextId, 7359);
    CHECK_EQ(out.mask, -9);
}

TEST(WorldTutorialSteps, FormTypeNames) {
    CHECK(std::strcmp(TutorialFormTypeName(11), "tutorial\\main_form") == 0);
    CHECK(std::strcmp(TutorialFormTypeName(10), "tutorial\\reminder_form") == 0);
    CHECK(std::strcmp(TutorialFormTypeName(9), "tutorial\\aux_form") == 0);
    CHECK(TutorialFormTypeName(4) == nullptr);
}
