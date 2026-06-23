// Unit tests for the chronicle commandline + text second-pass parsers
// (world/history_commandline_pass, world/history_text_pass). Golden vectors are
// derived directly from the recovered control flow of gilde.exe
// VIBE_History_ParseCommandlineSecondPass (0x4fd8ac) and
// VIBE_History_ParseTextSecondPass (0x4fdcec).
#include "test.h"
#include "world/history_commandline_pass.h"
#include "world/history_text_pass.h"

#include <string>
#include <vector>

using namespace guild;
using namespace guild::world;

// --- command-keyword table integrity ---------------------------------------
TEST(HistCmdline, CommandTableRecovered) {
    // 27 entries, recovered byte-for-byte from aFest @0x633938.
    CHECK_EQ(kHistoryCommandCount, 27);
    CHECK(std::string(kHistoryCommandNames[0])  == "FEST");
    CHECK(std::string(kHistoryCommandNames[1])  == "BELAGERUNG_START");
    CHECK(std::string(kHistoryCommandNames[5])  == "STADTKASSE");
    CHECK(std::string(kHistoryCommandNames[13]) == "GESETZ");
    CHECK(std::string(kHistoryCommandNames[14]) == "GESETZ_REL");
    CHECK(std::string(kHistoryCommandNames[25]) == "PEST");
    CHECK(std::string(kHistoryCommandNames[26]) == "INVENTAR_PLUS");
}

// --- FULL byte-exact command-keyword table (aFest @0x633938, 64-byte stride) -
// Pins every one of the 27 rows by index, so a single-row drift is caught. Values
// are the source's own recovery (history_commandline_pass.cpp kHistoryCommandNames).
TEST(HistCmdline, CommandTableFullByteExact) {
    static const char* const kExpect[27] = {
        "FEST", "BELAGERUNG_START", "AUFSTAND", "BRAND", "WIRBELSTURM",
        "STADTKASSE", "VERMOEGEN_STEUER", "VERMOEGEN", "ANSEHEN_BEI_AMTSTRAEGERN",
        "KILL_PLAYER", "STRAFE_HINRICHTUNG", "STRAFE_KERKER",
        "AMTSTRAEGER_INVENTAR_PLUS", "GESETZ", "GESETZ_REL", "GLAUBENSWECHSEL",
        "AUFRUHR", "GILDENSITZE_BRACH", "NACHFRAGE", "SOELDNER_PLUENDERN",
        "SOELDNER_MARODIEREN", "FERNHANDEL_RAUBRITTER", "SPENDE_ANSEHEN",
        "BETEILIGUNG", "KOMMENTARE", "PEST", "INVENTAR_PLUS",
    };
    CHECK_EQ(kHistoryCommandCount, 27);
    CHECK_EQ(kHistoryCommandStride, 64);
    for (int i = 0; i < 27; ++i)
        CHECK(std::string(kHistoryCommandNames[i]) == kExpect[i]);
}

// --- HistoryCommandIndex: leading-prefix memcmp, 27 == not found -----------
TEST(HistCmdline, CommandIndexMatch) {
    CHECK_EQ(HistoryCommandIndex("FEST"), 0);
    CHECK_EQ(HistoryCommandIndex("STADTKASSE+500"), 5);   // leading-prefix match
    CHECK_EQ(HistoryCommandIndex("PEST"), 25);
    CHECK_EQ(HistoryCommandIndex("UNKNOWN"), 27);         // sentinel
    CHECK_EQ(HistoryCommandIndex(""), 27);
    CHECK_EQ(HistoryCommandIndex(nullptr), 27);
    // GESETZ is a prefix of GESETZ_REL; the table order means "GESETZ_REL" still
    // matches GESETZ first (index 13) — faithful to the original's linear scan.
    CHECK_EQ(HistoryCommandIndex("GESETZ_REL"), 13);
}

// --- group-reference classification ----------------------------------------
TEST(HistCmdline, GroupRefClassify) {
    // Layout: [0]=marker, [1..4]=prefix, [6]=slot digit. Use a leading underscore
    // marker then the prefix at +1.
    HistoryGroupRef none = HistoryClassifyGroupRef("FEST blah");
    CHECK(none.kind == HistoryGroupKind::kNone);

    HistoryGroupRef set = HistoryClassifyGroupRef("X_SET 2 rest");  // +1.."_SET", +6='2'
    CHECK(set.kind == HistoryGroupKind::kSet);
    CHECK(set.valid);
    CHECK_EQ(set.slot, 2);

    HistoryGroupRef use = HistoryClassifyGroupRef("X_USE 0 rest");
    CHECK(use.kind == HistoryGroupKind::kUse);
    CHECK_EQ(use.slot, 0);

    HistoryGroupRef bad = HistoryClassifyGroupRef("X_SET 9 rest");  // slot 9 >= 4
    CHECK(bad.kind == HistoryGroupKind::kSet);
    CHECK(!bad.valid);
}

// --- HARDENING: malformed/short tokens must not over-read -------------------
// The original memcmp's each token against `strlen(name)` keyword bytes; its tokens
// live in a 6080-byte NUL-padded scratch so the read stays in-bounds. Our callers
// pass exact-length C strings, so a token shorter than the keyword would have made
// the original's memcmp read past the token end (caught by ASAN). The fix bounds the
// comparison at the token's NUL (strncmp), which is behaviour-identical (a short
// token has its NUL where the keyword has a non-NUL char -> never a prefix match).
TEST(HistCmdline, CommandIndexShortTokenNoOverread) {
    // "STAD" is a prefix of "STADTKASSE" but shorter -> NOT a match (the original's
    // memcmp would read 10 bytes from a 5-byte buffer). Must return the 27 sentinel.
    CHECK_EQ(HistoryCommandIndex("STAD"), kHistoryCommandCount);
    // A one-char token shorter than every keyword.
    CHECK_EQ(HistoryCommandIndex("F"), kHistoryCommandCount);
    // A token equal to a keyword still matches.
    CHECK_EQ(HistoryCommandIndex("STADTKASSE"), 5);
    // "STADTKASSE+500" is longer than "BELAGERUNG_START" (16); the scan must not
    // over-read the token when comparing against the longer keyword row.
    CHECK_EQ(HistoryCommandIndex("STADTKASSE+500"), 5);
}

// The leading group-ref classifier loads a 4-byte prefix dword at head+1 and the
// slot digit at head+6; on a short head the original (NUL-padded scratch) read NUL,
// but an exact-length C string would over-read. These pin the strncmp + head[5]
// guard fix.
TEST(HistCmdline, GroupRefShortHeadNoOverread) {
    // 1-char head: head+1 is the NUL; prefix compare must not read 4 bytes past it.
    CHECK(HistoryClassifyGroupRef("X").kind == HistoryGroupKind::kNone);
    // 2-char head whose +1 byte starts "_" but is truncated mid-prefix.
    CHECK(HistoryClassifyGroupRef("X_").kind == HistoryGroupKind::kNone);
    CHECK(HistoryClassifyGroupRef("X_S").kind == HistoryGroupKind::kNone);
    CHECK(HistoryClassifyGroupRef("X_SE").kind == HistoryGroupKind::kNone);
    // Exactly "X_SET" (5 chars): the prefix matches but the slot digit at head[6] is
    // OOB. The original's NUL-padded scratch reads NUL there -> slot 0; the guard
    // reproduces that (head[5]==NUL -> slot char '\0' -> ParseInt -> 0).
    HistoryGroupRef set5 = HistoryClassifyGroupRef("X_SET");
    CHECK(set5.kind == HistoryGroupKind::kSet);
    CHECK(set5.valid);
    CHECK_EQ(set5.slot, 0);
    // "X_USE" likewise.
    HistoryGroupRef use5 = HistoryClassifyGroupRef("X_USE");
    CHECK(use5.kind == HistoryGroupKind::kUse);
    CHECK_EQ(use5.slot, 0);
    // Empty head -> kNone (existing early-out).
    CHECK(HistoryClassifyGroupRef("").kind == HistoryGroupKind::kNone);
    CHECK(HistoryClassifyGroupRef(nullptr).kind == HistoryGroupKind::kNone);
}

// A NUL-less / overlong token in the second pass: a body that is one giant token
// must tokenize and match (or not) without over-reading.
TEST(HistCmdline, SecondPassOverlongTokenNoOverread) {
    int fired = 0;
    auto disp = [&](int, const std::string&, const std::string&, int) { ++fired; };
    // A 300-char token, no spaces: unmatched (not a keyword prefix) -> no dispatch.
    std::string big(300, 'Z');
    auto r = HistoryParseCommandlineSecondPass(big, disp);
    CHECK(r == HistoryCmdlineResult::kOk);
    CHECK_EQ(fired, 0);
    // A short trailing token shorter than any keyword must not over-read at EOF.
    fired = 0;
    auto r2 = HistoryParseCommandlineSecondPass("FEST ST", disp);  // "ST" too short
    CHECK(r2 == HistoryCmdlineResult::kOk);
    CHECK_EQ(fired, 1);  // only FEST matched
    // A truncated "_SET"-prefixed body (no slot digit / no body) must not over-read.
    fired = 0;
    auto r3 = HistoryParseCommandlineSecondPass("X_SET", disp);
    // head+7 body start is clamped to the string end; no tokens -> no dispatch.
    CHECK(r3 == HistoryCmdlineResult::kOk || r3 == HistoryCmdlineResult::kDisabled);
    CHECK_EQ(fired, 0);
}

// --- commandline second pass: gates ----------------------------------------
TEST(HistCmdline, SecondPassDisabledGate) {
    int fired = 0;
    auto disp = [&](int, const std::string&, const std::string&, int) { ++fired; };
    // !enabled -> kDisabled, no dispatch (dword_B537B4 == 0).
    auto r = HistoryParseCommandlineSecondPass("FEST", disp, nullptr, false);
    CHECK(r == HistoryCmdlineResult::kDisabled);
    CHECK_EQ(fired, 0);
}

TEST(HistCmdline, SecondPassDispatchesTokens) {
    std::vector<int> idx;
    std::vector<std::string> names, args;
    auto disp = [&](int i, const std::string& n, const std::string& a, int g) {
        (void)g; idx.push_back(i); names.push_back(n); args.push_back(a);
    };
    // Two commands; STADTKASSE carries a trailing argument after the keyword.
    auto r = HistoryParseCommandlineSecondPass("FEST STADTKASSE+500", disp);
    CHECK(r == HistoryCmdlineResult::kOk);
    CHECK_EQ((int)idx.size(), 2);
    if (idx.size() == 2) {
        CHECK_EQ(idx[0], 0);            // FEST
        CHECK(args[0] == "");
        CHECK_EQ(idx[1], 5);            // STADTKASSE
        CHECK(args[1] == "+500");       // funcs_4FDAA5[5](&arg[nameLen])
    }
}

TEST(HistCmdline, SecondPassSkipsLeadingAndDoubleSpaces) {
    std::vector<int> idx;
    auto disp = [&](int i, const std::string&, const std::string&, int) { idx.push_back(i); };
    // Leading + interior duplicate spaces are skipped (++v9 path), unknown tokens
    // are matched (27) and not dispatched.
    auto r = HistoryParseCommandlineSecondPass("  FEST   BOGUS  PEST ", disp);
    CHECK(r == HistoryCmdlineResult::kOk);
    CHECK_EQ((int)idx.size(), 2);       // FEST + PEST (BOGUS unmatched)
    if (idx.size() == 2) {
        CHECK_EQ(idx[0], 0);
        CHECK_EQ(idx[1], 25);
    }
}

TEST(HistCmdline, SecondPassGroupEmptySlotAborts) {
    int fired = 0;
    auto disp = [&](int, const std::string&, const std::string&, int) { ++fired; };
    // Group prefix present, slot valid (1) but the group table row is EMPTY.
    auto empty = [](int) { return false; };
    auto r = HistoryParseCommandlineSecondPass("X_USE 1 FEST", disp, empty);
    CHECK(r == HistoryCmdlineResult::kDisabled);   // `if (*v35)` false -> 0
    CHECK_EQ(fired, 0);
}

TEST(HistCmdline, SecondPassGroupPopulatedDispatches) {
    std::vector<int> slots;
    auto disp = [&](int, const std::string&, const std::string&, int g) { slots.push_back(g); };
    auto pop = [](int) { return true; };
    // "X_USE 0 FEST": body begins after head+7 -> "FEST"; group slot 0 propagated.
    auto r = HistoryParseCommandlineSecondPass("X_USE 0 FEST", disp, pop);
    CHECK(r == HistoryCmdlineResult::kOk);
    CHECK_EQ((int)slots.size(), 1);
    if (slots.size() == 1)
        CHECK_EQ(slots[0], 0);          // groupSlot threaded through to dispatch
}

// --- text second pass: pass-through + substitution -------------------------
TEST(HistText, PlainTextPassesThrough) {
    std::string out;
    auto r = HistoryParseTextSecondPass("Hello World 1400", out);
    CHECK(r == HistoryTextResult::kOk);
    CHECK(out == "Hello World 1400");   // no '_' tokens -> verbatim copy
}

TEST(HistText, SubstitutionResolved) {
    std::string out;
    std::vector<std::string> seen;
    auto resolve = [&](const std::string& tok) {
        seen.push_back(tok);
        HistorySubstResult r; r.text = "Hans"; r.ok = true; return r;
    };
    // "_NEW--0" : '_' opens, "NEW" head, two '-', then digit-class run "0".
    auto r = HistoryParseTextSecondPass("Mayor _NEW--0 spoke", out, resolve);
    CHECK(r == HistoryTextResult::kOk);
    CHECK_EQ((int)seen.size(), 1);
    if (seen.size() == 1)
        CHECK(seen[0] == "_NEW--0");
    // The resolved text replaces the token; the terminating space is copied through.
    CHECK(out == "Mayor Hans spoke");
}

TEST(HistText, TrailingSubstitutionAtEnd) {
    std::string out;
    auto resolve = [&](const std::string&) {
        HistorySubstResult r; r.text = "X"; return r;
    };
    // Token at end-of-text with exactly two '-' and a digit run resolves.
    auto r = HistoryParseTextSecondPass("end _USE--7", out, resolve);
    CHECK(r == HistoryTextResult::kOk);
    CHECK(out == "end X");
}

TEST(HistText, SpaceInTokenIsSyntaxError) {
    std::string out;
    auto r = HistoryParseTextSecondPass("_NEW 0", out);   // space while in token
    CHECK(r == HistoryTextResult::kSyntaxErr);
}

TEST(HistText, ThirdDashIsSyntaxError) {
    std::string out;
    auto r = HistoryParseTextSecondPass("_NEW---", out);   // v15 > 2
    CHECK(r == HistoryTextResult::kSyntaxErr);
}

TEST(HistText, NonDigitAfterDoubleDashIsSyntaxError) {
    std::string out;
    // After two '-', the next char must be digit-class; 'X' is not.
    auto r = HistoryParseTextSecondPass("_NEW--X", out);
    CHECK(r == HistoryTextResult::kSyntaxErr);
}

TEST(HistText, OpenTokenWithoutTwoDashesAtEndIsError) {
    std::string out;
    auto r = HistoryParseTextSecondPass("tail _NEW-0", out);  // only one '-'
    CHECK(r == HistoryTextResult::kSyntaxErr);
}

TEST(HistText, ResolverFailureAborts) {
    std::string out;
    auto resolve = [&](const std::string&) {
        HistorySubstResult r; r.ok = false; return r;   // ParseContext failed
    };
    auto r = HistoryParseTextSecondPass("_NEW--0", out, resolve);
    CHECK(r == HistoryTextResult::kSyntaxErr);
}
