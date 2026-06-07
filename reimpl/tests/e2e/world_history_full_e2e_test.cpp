// End-to-end flow across the remaining History / Statistics / Stammbaum / Tutorial
// bodies: build a synthetic dynasty + an in-memory chronicle, run multi-generation
// family queries, drive a chronicle parse/scan, assemble a statistics report, and
// build + walk a tutorial chapter chain — verifying each against a hand-computed
// reference.
#include "tests/framework/test.h"

#include "world/history.h"
#include "world/history_chronicle.h"
#include "world/history_full.h"
#include "world/statistics.h"
#include "world/statistics_full.h"
#include "world/stammbaum.h"
#include "world/stammbaum_query.h"
#include "world/tutorial_steps.h"
#include "world/tutorial.h"
#include "crt/rand.h"

#include <cstring>

using namespace guild;
using namespace guild::world;

namespace {
FamilyRecord MakeRec(i32 id, i32 father, i32 mother, i32 spouse,
                     const i32* kids, int nk) {
    FamilyRecord r{};
    r.id = id; r.father = father; r.mother = mother; r.spouse = spouse;
    for (int i = 0; i < kMaxChildren; ++i) r.children[i] = kFamilyNone;
    for (int i = 0; i < nk && i < kMaxChildren; ++i) r.children[i] = kids[i];
    return r;
}
} // namespace

TEST(WorldHistoryFullE2E, DynastyAndChronicleFlow) {
    // --- Synthetic 4-generation dynasty -------------------------------------
    //   10 + 11 -> 12, 13
    //   12 + 14 -> 15
    //   15      -> 16
    FamilyRecord buf[7];
    i32 k10[] = {12, 13};
    i32 k12[] = {15};
    i32 k15[] = {16};
    buf[0] = MakeRec(10, -1, -1, 11, k10, 2);
    buf[1] = MakeRec(11, -1, -1, 10, k10, 2);
    buf[2] = MakeRec(12, 10, 11, 14, k12, 1);
    buf[3] = MakeRec(13, 10, 11, -1, nullptr, 0);
    buf[4] = MakeRec(14, -1, -1, 12, k12, 1);
    buf[5] = MakeRec(15, 12, 14, -1, k15, 1);
    buf[6] = MakeRec(16, 15, -1, -1, nullptr, 0);
    FamilyTree tree{buf, 7};

    // Multi-gen queries vs reference.
    CHECK_EQ(FamilyGenerationDistance(tree, 10, 16), 3);
    CHECK_EQ(FamilyGenerationDistance(tree, 12, 16), 2);

    i32 line[8];
    int ln = FamilyResolveInheritance(tree, 10, line, 8);   // 12 -> 15 -> 16
    CHECK_EQ(ln, 3);
    CHECK_EQ(line[0], 12);
    CHECK_EQ(line[1], 15);
    CHECK_EQ(line[2], 16);

    i32 gens[8];
    CHECK_EQ(FamilyCollectAncestorsAtGen(tree, 16, 3, gens, 8), 2);  // 10, 11

    CHECK(FamilyAreBloodRelated(tree, 16, 13, 4));  // 16 descends from 10/11; 13 too
    CHECK(!FamilyAreBloodRelated(tree, 11, 14, 4)); // unrelated in-laws

    // --- Chronicle: parse a date, add entries, scan the forward window --------
    ParsedDate pd;
    CHECK(HistoryParseDate("05.06.1412", &pd));
    CHECK_EQ(pd.day, 5);
    CHECK_EQ(pd.month, 6);
    CHECK_EQ(pd.year, 1412);
    CHECK_EQ(pd.yearOffset, 12);   // 1412 - 1400

    // FirstPass syntax validation over a couple of chronicle labels.
    CHECK(HistoryParseTextFirstPassValid("The [Mayor#mayor] was arrested."));
    CHECK(!HistoryParseTextFirstPassValid("Unbalanced [region#"));

    // Token classification of a substitution placeholder.
    CHECK(HistoryClassifyTokenMode("_USE2") == HistoryTokenMode::kUse);
    CHECK_EQ(HistoryTokenSlot("_USE2", HistoryTokenMode::kUse), 2);
    CHECK_EQ(HistoryRoleNameIndex("RND_SPIELER"), 14);

    Chronicle chron;
    ChronicleEntry e{};
    e.day = 98;  e.month = 6; e.year = 1412; e.textId = 7313; e.text = "arrest";
    chron.Add(e);
    e.day = 99;  e.textId = 7317; e.text = "office";        chron.Add(e);
    e.day = 100; e.textId = 7328; e.text = "crime";         chron.Add(e);

    // ScanNextForward at currentDay 100 emits the day-99 ("yesterday") entry.
    int idx = chron.ScanNextForward(100);
    CHECK(idx >= 0);
    CHECK_EQ(chron.At(idx).day, 99);
    CHECK_EQ(chron.At(idx).textId, 7317);

    // The forward-scan step codes line up with the chronicle classifier.
    CHECK(HistoryScanStep(true, 100 - 99, false) == HistoryScanCode::kEmit);
    CHECK(HistoryScanStep(true, 100 - 100, false) == HistoryScanCode::kStop);

    // --- Statistics report assembled from a synthetic accumulator block -------
    float accum[kStatAccumCount];
    for (int i = 0; i < kStatAccumCount; ++i) accum[i] = 0.0f;
    // Drive goods (column 0) weak and luxury (column 3) high.
    accum[0] = accum[5] = accum[10] = accum[15] = 0.40f;  // col0 total = 0.40
    accum[1] = accum[6] = accum[11] = accum[16] = 1.60f;  // col1 total = 1.60
    accum[2] = accum[7] = accum[12] = accum[17] = 1.60f;  // col2 total = 1.60
    accum[3] = accum[8] = accum[13] = accum[18] = 2.40f;  // col3 (luxury) = 2.40
    EconomyReportInput rin = EconomyReportFromAccum(accum, /*lawScore*/ 0.5f);
    CHECK(rin.total0 > 0.39f && rin.total0 < 0.41f);   // 0.40 (each *4 *0.25)
    CHECK(rin.luxury > 0.59f);                          // 0.60 -> booming

    EconomyReportOutcome rep = EconomyReportBuild(8, rin);
    CHECK(rep.ran);
    CHECK(rep.luxury == EconomyReportLuxury::kBooming);
    CHECK_EQ(rep.trends.goods, 6179);                  // goods 0.40 > 0.15 -> "up"
    CHECK(rep.broadcasts);

    ReportRecipient recips[4] = { {6, 1}, {6, 2}, {0, 3}, {6, 4} };
    CHECK_EQ(ReportEligibleRecipientCount(recips, 4), 3);

    // --- Tutorial: build a chapter chain and walk it --------------------------
    int nc = 0;
    const TutorialNodeSpec* c1 = TutorialChapter1Steps(&nc);
    TutorialChapterNode nodes[16];
    TutorialChapterNode* head = TutorialBuildChain(c1, nc, nodes);
    CHECK_EQ(TutorialNodeChainLength(head), nc);
    CHECK_EQ(nodes[0].mainTextId, 7362);
    CHECK(std::strcmp(c1[nc - 1].mainRes, "CHAPTER_1_OUTRO") == 0);

    // The runtime advance state machine (world/tutorial.h) still classifies an
    // empty chapter correctly — sanity that the two tutorial models coexist.
    TutorialState st{};
    st.active = true;
    CHECK(!TutorialIsInactive(st));
}
