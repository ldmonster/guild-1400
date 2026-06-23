// tests/unit/world_history_blob_harden_test.cpp — wave-12 hardening tests for the
// History/Chronicle/Statistics save-blob parsers (guild::world). These drive the
// malformed / truncated / empty / boundary-index paths the brief calls out:
//   * a truncated / 0-byte / NUL-less date string (HistoryParseDate / FormatDate)
//   * a chronicle with 0 / 1 / many entries, plus out-of-range FormatDate/At indices
//   * the bracket-collapse second pass on unbalanced / empty labels
//   * the role-name scan on a short / NUL-less token
//   * the statistics category-total reduction at its valid column boundary
// Every check below is asset-free and behaviour-identical to the original; the OOB
// fixes (bounded Chronicle index, strncmp scans) are pinned here under ASAN+UBSAN.
#include "test.h"

#include "world/history.h"
#include "world/history_parse.h"
#include "world/history_second_pass.h"
#include "world/history_full.h"
#include "world/history_chronicle.h"
#include "world/statistics.h"

#include <cstring>
#include <string>

using namespace guild;
using namespace guild::world;

// =============================================================================
// HistoryParseDate / HistoryFormatDate — truncated / empty / NUL-less input.
// =============================================================================
TEST(HistHarden, ParseDateRejectsWrongLength) {
    ParsedDate p{};
    // The original gates on strlen(text) == 10. Truncated / empty / overlong fail.
    CHECK(!HistoryParseDate("", &p));
    CHECK(!HistoryParseDate("01.01", &p));
    CHECK(!HistoryParseDate("01.01.140", &p));     // 9 chars
    CHECK(!HistoryParseDate("01.01.14000", &p));   // 11 chars
    CHECK(!HistoryParseDate(nullptr, &p));
}

TEST(HistHarden, ParseDateValidAndNormalises) {
    ParsedDate p{};
    CHECK(HistoryParseDate("00.00.1400", &p));     // day/month 0 -> normalised to 1
    CHECK_EQ(p.day, 1);
    CHECK_EQ(p.month, 1);
    CHECK_EQ(p.year, 1400);
    CHECK_EQ(p.yearOffset, 0);
    CHECK_EQ(p.mode, 2);                            // month-was-0 latches mode 2
}

TEST(HistHarden, FormatDateFixedWidthAndRoundtrip) {
    char out[16];
    HistoryFormatDate(7, 3, 1415, out);
    CHECK(std::string(out) == "07.03.1415");
    // A parse->format roundtrip is bit-stable on a normalised date.
    char rt[16];
    ParsedDate p{};
    CHECK(HistoryRoundtripDate("07.03.1415", rt, &p));
    CHECK(std::string(rt) == "07.03.1415");
}

// =============================================================================
// Chronicle — 0 / 1 / many entries, out-of-range FormatDate & At indices.
// =============================================================================
TEST(HistHarden, ChronicleEmptyScanAndFormat) {
    Chronicle c;
    CHECK_EQ(c.Count(), 0);
    CHECK_EQ(c.ScanNextForward(100), -1);          // nothing to emit
    // FormatDate on an out-of-range (here ANY) index must not read past the array;
    // the bounded path yields a zero date rather than UB.
    char out[16];
    c.FormatDate(0, out);
    CHECK(std::string(out) == "00.00.0000");
    c.FormatDate(-1, out);
    CHECK(std::string(out) == "00.00.0000");
    c.FormatDate(9999, out);
    CHECK(std::string(out) == "00.00.0000");
}

TEST(HistHarden, ChronicleSingleEntry) {
    Chronicle c;
    ChronicleEntry e{};
    e.day = 50; e.month = 6; e.year = 1402; e.textId = 7319; e.text = nullptr;
    CHECK_EQ(c.Add(e), 0);
    CHECK_EQ(c.Count(), 1);
    // currentDay - entryDay == 1 -> emit; else stop/continue.
    CHECK_EQ(c.ScanNextForward(51), 0);            // yesterday window
    CHECK_EQ(c.ScanNextForward(50), -1);           // on/after today -> stop
    CHECK_EQ(c.ScanNextForward(100), -1);          // older only -> none
    char out[16];
    c.FormatDate(0, out);
    CHECK(std::string(out) == "50.06.1402");
}

TEST(HistHarden, ChronicleManyEntriesSortedScan) {
    Chronicle c;
    // Insert out of order; Add keeps ascending-day order.
    for (int d : {30, 10, 20, 40, 10}) {
        ChronicleEntry e{}; e.day = d; e.month = 1; e.year = 1400;
        c.Add(e);
    }
    CHECK_EQ(c.Count(), 5);
    // entries sorted by day: 10,10,20,30,40
    CHECK_EQ(c.At(0).day, 10);
    CHECK_EQ(c.At(4).day, 40);
    // out-of-range At clamps into the live range (no over-read).
    CHECK_EQ(c.At(-5).day, 10);                     // clamps to first
    CHECK_EQ(c.At(999).day, 40);                    // clamps to last
    // ScanNextForward finds the (currentDay-1) entry.
    CHECK_EQ(c.At(c.ScanNextForward(21)).day, 20);  // day-20 is "yesterday" of 21
}

TEST(HistHarden, ChronicleFullCapacityRejects) {
    Chronicle c;
    for (int i = 0; i < kChronicleCapacity; ++i) {
        ChronicleEntry e{}; e.day = i; e.month = 1; e.year = 1400;
        CHECK(c.Add(e) >= 0);
    }
    CHECK_EQ(c.Count(), kChronicleCapacity);
    ChronicleEntry overflow{}; overflow.day = 9999;
    CHECK_EQ(c.Add(overflow), -1);                  // full -> rejected, no OOB write
    CHECK_EQ(c.Count(), kChronicleCapacity);
}

// =============================================================================
// HistoryCollapseLabel — unbalanced / empty / nested bracket regions.
// =============================================================================
TEST(HistHarden, CollapseEmptyAndNull) {
    char out[64];
    out[0] = 'x';
    CHECK(HistoryCollapseLabel(nullptr, out) == HistoryCollapseResult::kRendered);
    CHECK_EQ((int)out[0], 0);                       // null -> empty out
    CHECK(HistoryCollapseLabel("", out) == HistoryCollapseResult::kRendered);
    CHECK(std::string(out) == "");
}

TEST(HistHarden, CollapseBalancedRegion) {
    char out[64];
    // "[keep#drop]" collapses to "keep".
    CHECK(HistoryCollapseLabel("a[keep#drop]b", out) == HistoryCollapseResult::kRendered);
    CHECK(std::string(out) == "akeepb");
}

TEST(HistHarden, CollapseUnbalancedIsSyntaxError) {
    char out[64];
    CHECK(HistoryCollapseLabel("[unclosed", out) == HistoryCollapseResult::kSyntaxErr);
    CHECK(HistoryCollapseLabel("nostart]", out) == HistoryCollapseResult::kSyntaxErr);
    CHECK(HistoryCollapseLabel("[[double", out) == HistoryCollapseResult::kSyntaxErr);
    CHECK(HistoryCollapseLabel("[a]extra]", out) == HistoryCollapseResult::kSyntaxErr);
    CHECK(HistoryCollapseLabel("[noescape]", out) == HistoryCollapseResult::kSyntaxErr); // ']' before '#'
}

// =============================================================================
// HistoryRoleNameIndex — short / NUL-less / unknown token (strncmp-bounded).
// =============================================================================
TEST(HistHarden, RoleNameIndexShortAndUnknown) {
    CHECK_EQ(HistoryRoleNameIndex("BUERGERMEISTER"), 0);
    CHECK_EQ(HistoryRoleNameIndex("GELD"), 8);
    // A token shorter than a role name must not over-read (the original strncmp).
    CHECK_EQ(HistoryRoleNameIndex("BUE"), -1);
    CHECK_EQ(HistoryRoleNameIndex(""), -1);
    CHECK_EQ(HistoryRoleNameIndex("UNKNOWN_ROLE"), -1);
    CHECK_EQ(HistoryRoleNameIndex(nullptr), -1);
}

TEST(HistHarden, FirstPassValidEmptyAndUnbalanced) {
    CHECK(HistoryParseTextFirstPassValid(nullptr));   // null -> nothing to validate
    CHECK(HistoryParseTextFirstPassValid(""));
    CHECK(HistoryParseTextFirstPassValid("plain text"));
    CHECK(HistoryParseTextFirstPassValid("[a#b]"));
    CHECK(!HistoryParseTextFirstPassValid("[unclosed"));
    CHECK(!HistoryParseTextFirstPassValid("#outside"));
}

// =============================================================================
// StatisticsCategoryTotal — valid column boundary (k = 0..3 reads up to a[k+15]).
// =============================================================================
TEST(HistHarden, StatisticsCategoryTotalBoundary) {
    float accum[kStatAccumCount];
    for (int i = 0; i < kStatAccumCount; ++i) accum[i] = static_cast<float>(i);
    // k=3 reads a[3]+a[8]+a[13]+a[18] = 3+8+13+18 = 42; *0.25 = 10.5 (last valid k,
    // reads a[18] which is in-bounds for the 20-float window).
    CHECK(StatisticsCategoryTotal(accum, 3) == (3 + 8 + 13 + 18) * kStatScale);
    // The accumulate helper fills all four category columns without over-reading.
    float out[kStatCategoryCnt];
    StatisticsAccumulateCategoryTotals(accum, out);
    CHECK(out[0] == (0 + 5 + 10 + 15) * kStatScale);
    CHECK(out[3] == (3 + 8 + 13 + 18) * kStatScale);
}
