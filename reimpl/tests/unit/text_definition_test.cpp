// WAVE-20 — localized-text .def driver + .dat per-label compiler.
//
// Covers the two reconstructed originals:
//   VIBE_Text_LoadDefinitionFile   @0x44b8f4 — the gilde_text.def driver:
//       parse #include "Name.ext" lines (skipping // comments and non-#include
//       directives), strip each name's extension, and call a per-file loader in
//       order, bailing at the first failure.
//   VIBE_Text_ParseLabelDefinition @0x44b2e0 — the .dat per-line compiler:
//       gender selector (m)/(w)/(s) -> tag, the prefix[single/plural]suffix
//       bracket grammar, multi-language '|'-joined singular||plural blob, and the
//       synthesized "<filePrefix>+<localIndex>" key.
//
// All synthetic (no asset). The real gilde_text.def + .res path is the guarded e2e.
#include "tests/framework/test.h"

#include "gui/text_definition.h"
#include "gui/text/textdb.h"

#include <string>
#include <vector>

using namespace guild;
using namespace guild::gui::text;

namespace {
std::vector<std::string> Includes(const std::string& s) {
    return ParseDefinitionIncludes(s.c_str(), s.size());
}
} // namespace

// ---- .def parse ----------------------------------------------------------

TEST(TextDef, ParseIncludesBasic) {
    std::string def =
        "// header comment\n"
        "#outputpath \"\\Project\\textbin_deutsch\\\"\n"
        "#headerfile \"die_gilde_text.h\"\n"
        "\n"
        "// list\n"
        "#include \"Text_A_Allgemein.dat\"\n"
        "#include \"Text_C_Personen.dat\"\n";
    auto inc = Includes(def);
    CHECK_EQ((int)inc.size(), 2);
    CHECK_EQ(inc[0], std::string("Text_A_Allgemein"));
    CHECK_EQ(inc[1], std::string("Text_C_Personen"));
}

TEST(TextDef, ParseIncludesStripsExtAndKeepsSubdir) {
    // A subdir name has one '.' (the extension), stripped at it.
    std::string def = "#include \"Gesetze\\Text_G0_Gesetze.dat\"\n";
    auto inc = Includes(def);
    CHECK_EQ((int)inc.size(), 1);
    CHECK_EQ(inc[0], std::string("Gesetze\\Text_G0_Gesetze"));
}

TEST(TextDef, ParseIncludesStripsAtLastDot) {
    // gilde.exe 0x44bad2: the extension strip uses VIBE_Util_StrChr(name,'.')
    // which returns the LAST '.', not the first. A name with multiple dots keeps
    // everything up to the final dot.
    std::string def = "#include \"Text.v2.final.dat\"\n";
    auto inc = Includes(def);
    CHECK_EQ((int)inc.size(), 1);
    CHECK_EQ(inc[0], std::string("Text.v2.final"));
}

TEST(TextDef, CommentLinesAreSkippedEvenWithInclude) {
    // Any line CONTAINING "//" is a comment -> the strstr("//") test wins.
    std::string def =
        "// #include \"ShouldBeIgnored.dat\"\n"
        "#include \"Real.dat\"\n";
    auto inc = Includes(def);
    CHECK_EQ((int)inc.size(), 1);
    CHECK_EQ(inc[0], std::string("Real"));
}

TEST(TextDef, NoQuotesOrNoIncludeSkipped) {
    std::string def =
        "#include Text_NoQuotes.dat\n"   // no quote -> skipped
        "#include \"OnlyOpen.dat\n"       // no closing quote -> skipped
        "random text line\n"             // no #include -> skipped
        "#include \"Good.dat\"\n";
    auto inc = Includes(def);
    CHECK_EQ((int)inc.size(), 1);
    CHECK_EQ(inc[0], std::string("Good"));
}

TEST(TextDef, CrlfTolerant) {
    // Real file is CRLF; ReadLine leaves '\r' in the buffer but the quote scan
    // never sees it inside the name.
    std::string def = "#include \"Text_B_Menues.dat\"\r\n#include \"Text_C.dat\"\r\n";
    auto inc = Includes(def);
    CHECK_EQ((int)inc.size(), 2);
    CHECK_EQ(inc[0], std::string("Text_B_Menues"));
    CHECK_EQ(inc[1], std::string("Text_C"));
}

// ---- .def driver ordering + early-out ------------------------------------

TEST(TextDef, DriverLoadsInOrder) {
    std::string def =
        "#include \"A.dat\"\n#include \"B.dat\"\n#include \"C.dat\"\n";
    std::vector<std::string> seen;
    bool loaded = false;
    bool ok = LoadDefinitionFile(def.c_str(), def.size(),
        [&](const std::string& n) { seen.push_back(n); return true; },
        &loaded);
    CHECK(ok);
    CHECK(loaded);
    CHECK_EQ((int)seen.size(), 3);
    CHECK_EQ(seen[0], std::string("A"));
    CHECK_EQ(seen[1], std::string("B"));
    CHECK_EQ(seen[2], std::string("C"));
}

TEST(TextDef, DriverBailsAtFirstFailure) {
    std::string def = "#include \"A.dat\"\n#include \"B.dat\"\n#include \"C.dat\"\n";
    std::vector<std::string> seen;
    bool loaded = false;
    bool ok = LoadDefinitionFile(def.c_str(), def.size(),
        [&](const std::string& n) { seen.push_back(n); return n != "B"; },
        &loaded);
    CHECK(!ok);
    CHECK(!loaded);                       // flag NOT set on failure
    CHECK_EQ((int)seen.size(), 2);        // stopped after B
    CHECK_EQ(seen[1], std::string("B"));
}

// ---- .dat per-label compiler ---------------------------------------------

TEST(TextDefLabel, GenderTagsAndKey) {
    LabelCompilerState st;
    st.BeginFile(0, "txt_a");
    TextDb db;

    CHECK(ParseLabelDefinition("(m),\"Mann\"", 0, st, db));
    CHECK(ParseLabelDefinition("(w),\"Frau\"", 0, st, db));
    CHECK(ParseLabelDefinition("(s),\"Sache\"", 0, st, db));
    CHECK(ParseLabelDefinition("xx,\"Default\"", 0, st, db)); // unknown -> tag 0

    CHECK_EQ(db.Count(), 4);
    CHECK_EQ((int)db.Tag(0), 0);  // (m)
    CHECK_EQ((int)db.Tag(1), 1);  // (w)
    CHECK_EQ((int)db.Tag(2), 2);  // (s)
    CHECK_EQ((int)db.Tag(3), 0);  // default

    // Keys are "<prefix>+<localIndex>".
    CHECK_EQ(std::string(db.Name(0)), std::string("txt_a+0"));
    CHECK_EQ(std::string(db.Name(3)), std::string("txt_a+3"));
}

TEST(TextDefLabel, NoBracketSingleLanguageBlob) {
    LabelCompilerState st;
    st.BeginFile(0, "p");
    TextDb db;
    // one value column, no brackets -> singular==plural=="Hallo".
    CHECK(ParseLabelDefinition("(m),\"Hallo\"", 0, st, db));
    // blob = s0|s1|s2|s3|p0|p1|p2|p3| with only column 0 populated.
    CHECK_EQ(std::string(db.Text(0)), std::string("Hallo||||Hallo||||"));
}

TEST(TextDefLabel, MultiLanguageColumns) {
    LabelCompilerState st;
    st.BeginFile(0, "p");
    TextDb db;
    // two language columns.
    CHECK(ParseLabelDefinition("(m),\"de\",\"en\"", 0, st, db));
    CHECK_EQ(std::string(db.Text(0)), std::string("de|en|||de|en|||"));
}

TEST(TextDefLabel, BracketSlashSingularPlural) {
    LabelCompilerState st;
    st.BeginFile(0, "p");
    TextDb db;
    // prefix "Haus", [er/ern] -> singular "Hauser", plural "Hausern", suffix "X".
    CHECK(ParseLabelDefinition("(m),\"Haus[er/ern]X\"", 0, st, db));
    // column 0: sing="Hauser"+"X" wait: prefix=Haus single=er plural=ern suffix=X
    //   singular = Haus + er + X = HauserX ; plural = Haus + ern + X = HausernX
    CHECK_EQ(std::string(db.Text(0)), std::string("HauserX||||HausernX||||"));
}

TEST(TextDefLabel, BracketNoSlashQuirk) {
    LabelCompilerState st;
    st.BeginFile(0, "p");
    TextDb db;
    // [abc] with no '/': single = "abc" (full bracket contents), plural = "bc"
    // (the recovered quirk: plural drops the first contents char). prefix "P".
    CHECK(ParseLabelDefinition("(m),\"P[abc]Q\"", 0, st, db));
    // singular = P + abc + Q = PabcQ ; plural = P + bc + Q = PbcQ
    CHECK_EQ(std::string(db.Text(0)), std::string("PabcQ||||PbcQ||||"));
}

TEST(TextDefLabel, MalformedBracketReturnsFalse) {
    LabelCompilerState st;
    st.BeginFile(0, "p");
    TextDb db;
    // '[' with no ']' -> return 0, nothing added.
    CHECK(!ParseLabelDefinition("(m),\"Broken[er\"", 0, st, db));
    CHECK_EQ(db.Count(), 0);
    // '[' .. '/' with no ']' -> return 0.
    CHECK(!ParseLabelDefinition("(m),\"Broken[er/ern\"", 0, st, db));
    CHECK_EQ(db.Count(), 0);
}

TEST(TextDefLabel, LocalIndexResetsPerFile) {
    LabelCompilerState st;
    TextDb db;
    st.BeginFile(0, "fileA");
    CHECK(ParseLabelDefinition("(m),\"a0\"", 0, st, db));
    CHECK(ParseLabelDefinition("(m),\"a1\"", 0, st, db));
    st.BeginFile(1, "fileB");   // base = current count (2)
    CHECK(ParseLabelDefinition("(m),\"b0\"", 1, st, db));

    CHECK_EQ(std::string(db.Name(0)), std::string("fileA+0"));
    CHECK_EQ(std::string(db.Name(1)), std::string("fileA+1"));
    CHECK_EQ(std::string(db.Name(2)), std::string("fileB+0")); // localIndex 2-2==0
}
