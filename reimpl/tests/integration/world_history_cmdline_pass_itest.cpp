// Integration tests: drive the chronicle commandline + text second-pass parsers
// (world/history_commandline_pass, world/history_text_pass) against their REAL
// sibling modules — the FirstPass validator / ParseContext classifier in
// world/history_full, the bracket collapser in world/history_second_pass, and
// VIBE_Util_ParseInt in world/city — exactly as the engine chains them.
#include "test.h"
#include "world/history_commandline_pass.h"
#include "world/history_text_pass.h"
#include "world/history_full.h"
#include "world/history_second_pass.h"
#include "world/city.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::world;

// The group-digit parse must agree with the real VIBE_Util_ParseInt sibling.
TEST(HistCmdlineITest, GroupSlotUsesRealParseInt) {
    // ClassifyGroupRef calls UtilParseInt on the slot digit. Cross-check that the
    // same parser the rest of world/ uses produces the slot.
    HistoryGroupRef g = HistoryClassifyGroupRef("X_SET 3 ...");
    CHECK_EQ(g.slot, (int)UtilParseInt("3"));
    CHECK_EQ(g.slot, 3);
    CHECK(g.valid);
}

// A realistic ParseContext resolver built from the REAL history_full siblings:
// classify the token's prefix mode, then map the trailing role name through the
// recovered role-name table. This is the resolution the engine's ParseContext
// performs (funcs_4FD5D2 dispatch) modeled over the real tables.
static HistorySubstResult ResolveViaHistoryFull(const std::string& token) {
    HistorySubstResult r;
    // Strip the leading '_' and split the "<PREFIX><slot>--<ROLE>" shape.
    // The token always begins with '_' and contains "--".
    std::size_t dd = token.find("--");
    if (dd == std::string::npos) { r.ok = false; return r; }
    std::string head = token.substr(0, dd);          // "_NEW0"
    std::string role = token.substr(dd + 2);         // role/digit run

    HistoryTokenMode mode = HistoryClassifyTokenMode(head.c_str());
    // The post-'--' run in the real label is a role-name reference; map it.
    int roleIdx = HistoryRoleNameIndex(role.c_str());
    if (roleIdx < 0 && mode == HistoryTokenMode::kLiteral) {
        // Unknown literal -> ParseContext "Unknown Replacement" -> failure.
        r.ok = false; return r;
    }
    // Resolve to a deterministic, mode-tagged replacement string.
    r.text = "<" + std::string(roleIdx >= 0 ? kHistoryRoleNames[roleIdx] : "SLOT") + ">";
    r.ok = true;
    return r;
}

TEST(HistCmdlineITest, TextPassResolvesThroughRealParseContextTables) {
    std::string out;
    // "_NEW--12345" : the digit-class run is "12345"; resolve maps it (no role)
    // to a SLOT placeholder via the real classifier.
    auto r = HistoryParseTextSecondPass("Year _NEW--12345 ended", out,
                                        ResolveViaHistoryFull);
    CHECK(r == HistoryTextResult::kOk);
    CHECK(out == "Year <SLOT> ended");
}

// The full per-label chain the orchestrator runs: FirstPass validator (real) gates
// SecondPass text resolution (this module), then the commandline pass dispatches.
TEST(HistCmdlineITest, FullLabelPipelineFirstPassGatesText) {
    const char* label = "Mayor _NEW--0 won [the#a] vote";

    // 1) FirstPass syntax validation (real sibling).
    CHECK(HistoryParseTextFirstPassValid(label));

    // 2) Bracket collapse (real history_second_pass sibling): "[the#a]" -> "the".
    char collapsed[256];
    auto cr = HistoryCollapseLabel(label, collapsed);
    CHECK(cr == HistoryCollapseResult::kRendered);
    CHECK(std::string(collapsed) == "Mayor _NEW--0 won the vote");

    // 3) Text second pass over the collapsed text (this module).
    std::string out;
    auto tr = HistoryParseTextSecondPass(collapsed, out, ResolveViaHistoryFull);
    CHECK(tr == HistoryTextResult::kOk);
    CHECK(out == "Mayor <SLOT> won the vote");
}

// Commandline pass dispatch wired to a real command-table-driven sink: confirm the
// matched indices line up with the recovered keyword table and the group context.
TEST(HistCmdlineITest, CommandlinePassMatchesRecoveredTable) {
    std::vector<int> idx;
    std::vector<std::string> args;
    auto disp = [&](int i, const std::string& n, const std::string& a, int) {
        (void)n; idx.push_back(i); args.push_back(a);
    };
    auto pop = [](int) { return true; };
    // Group "_SET 1", then three event commands (two with args).
    auto r = HistoryParseCommandlineSecondPass(
        "X_SET 1 AUFSTAND STADTKASSE-200 VERMOEGEN+10", disp, pop);
    CHECK(r == HistoryCmdlineResult::kOk);
    CHECK_EQ((int)idx.size(), 3);
    if (idx.size() == 3) {
        CHECK_EQ(idx[0], 2);                    // AUFSTAND
        CHECK_EQ(idx[1], 5);                    // STADTKASSE
        CHECK(args[1] == "-200");
        CHECK_EQ(idx[2], 7);                    // VERMOEGEN
        CHECK(args[2] == "+10");
        // VERMOEGEN (7) must NOT swallow VERMOEGEN_STEUER (6): index 6 is checked
        // first in the linear scan, but "VERMOEGEN+10" doesn't match "VERMOEGEN_STEUER".
    }
}

// VERMOEGEN_STEUER vs VERMOEGEN ordering: the longer name (index 6) precedes the
// shorter (index 7) in the table, so a "VERMOEGEN_STEUER..." token matches 6.
TEST(HistCmdlineITest, LongerKeywordWinsByTableOrder) {
    std::vector<int> idx;
    auto disp = [&](int i, const std::string&, const std::string&, int) { idx.push_back(i); };
    auto r = HistoryParseCommandlineSecondPass("VERMOEGEN_STEUER+5", disp);
    CHECK(r == HistoryCmdlineResult::kOk);
    CHECK_EQ((int)idx.size(), 1);
    if (idx.size() == 1)
        CHECK_EQ(idx[0], 6);                    // VERMOEGEN_STEUER, not VERMOEGEN
}
