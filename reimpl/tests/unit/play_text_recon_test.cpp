// Golden-vector unit tests for the text reconstruction (src/play/text_recon.*).
#include "tests/framework/test.h"
#include "play/text_recon.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild::play;

// -------------------------------------------------------------------------------------
// VIBE_Text_FormatBuildVersionString @0x527c68
// -------------------------------------------------------------------------------------
TEST(TextReconVersion, EmitsExactRecoveredBytes) {
    char out[64];
    std::memset(out, 0xAB, sizeof(out));
    int n = TextFormatBuildVersionString(out);

    // Exact recovered bytes @0x622990 (22 visible bytes + NUL).
    static const unsigned char kExpect[23] = {
        0xC3, 0xE8, 0xEB, 0xFC, 0xE4, 0xE8, 0xB0, 0x20, 0x2D, 0x20, 0xC2,
        0xE5, 0xF0, 0xF1, 0xE8, 0xB0, 0x20, 0x31, 0x2E, 0x30, 0x33, 0xF1, 0x00,
    };
    CHECK_EQ(n, 22);
    CHECK(std::memcmp(out, kExpect, sizeof(kExpect)) == 0);
}

TEST(TextReconVersion, BuildDateConstantMatchesBinary) {
    CHECK(std::strcmp(kBuildDateString, "Oct 10 2002") == 0);
    CHECK_EQ(kVersionStringBytes[0], 0xC3);
    CHECK_EQ(kVersionStringBytes[22], 0x00);
}

// -------------------------------------------------------------------------------------
// VIBE_Text_LookupLabelEntry @0x44add4
// -------------------------------------------------------------------------------------
static const char* const kLabelNames[]  = {"_OB_HAUS", "_OB_KIRCHE", "_GB_BACKEREI"};
static const int         kLabelValues[] = {100, 205, 7};
static const char* LabelName(int i)  { return kLabelNames[i]; }
static int         LabelValue(int i) { return kLabelValues[i]; }

TEST(TextReconLabel, EmptyTableReturnsMinusOne) {
    LabelTableHooks saved = LabelTableHookTable();
    LabelTableHookTable() = LabelTableHooks{0, nullptr, nullptr};
    CHECK_EQ(TextLookupLabelEntry("_OB_HAUS"), -1);
    LabelTableHookTable() = saved;
}

TEST(TextReconLabel, CaseInsensitiveLookup) {
    LabelTableHooks saved = LabelTableHookTable();
    LabelTableHookTable() = LabelTableHooks{3, LabelName, LabelValue};
    CHECK_EQ(TextLookupLabelEntry("_OB_HAUS"),     100);
    CHECK_EQ(TextLookupLabelEntry("_ob_kirche"),   205);  // case-insensitive
    CHECK_EQ(TextLookupLabelEntry("_GB_BACKEREI"), 7);
    CHECK_EQ(TextLookupLabelEntry("_NOT_THERE"),   -1);
    LabelTableHookTable() = saved;
}

// -------------------------------------------------------------------------------------
// VIBE_Text_PrintfWrapper @0x5e9eb0
// -------------------------------------------------------------------------------------
static int g_flush_count = 0;
TEST(TextReconPrintf, FlushesAndIgnoresArgs) {
    DebugSinkHooks saved = DebugSinkHookTable();
    g_flush_count = 0;
    DebugSinkHookTable().flush = []() -> int { ++g_flush_count; return 42; };
    int r = TextPrintfWrapper("ignored %s %d", "x", 7);
    CHECK_EQ(r, 42);
    CHECK_EQ(g_flush_count, 1);
    DebugSinkHookTable() = saved;
}

// -------------------------------------------------------------------------------------
// VIBE_Text_ReadLine @0x5e9e30
// -------------------------------------------------------------------------------------
namespace {
struct FakeStream {
    std::string data;
    size_t pos = 0;
    int mode = 0;  // *(stream+12) flags; 0x20 = error/eof-flag
};
FakeStream* g_stream = nullptr;
int FakeGetc() {
    if (!g_stream || g_stream->pos >= g_stream->data.size())
        return -1;
    return static_cast<unsigned char>(g_stream->data[g_stream->pos++]);
}
int FakeMode() { return g_stream ? g_stream->mode : 0; }
void FakeSetMode(int m) { if (g_stream) g_stream->mode = m; }

StreamReadHooks MakeHooks() { return StreamReadHooks{FakeGetc, FakeMode, FakeSetMode}; }
} // namespace

TEST(TextReconReadLine, ReadsUpToNewlineInclusive) {
    FakeStream s; s.data = "hello\nworld\n"; g_stream = &s;
    StreamReadHooks h = MakeHooks();
    char buf[64];
    char* r = TextReadLine(buf, 64, h);
    CHECK(r == buf);
    CHECK(std::strcmp(buf, "hello\n") == 0);  // newline retained, NUL-terminated
    // Second line.
    r = TextReadLine(buf, 64, h);
    CHECK(r == buf);
    CHECK(std::strcmp(buf, "world\n") == 0);
    g_stream = nullptr;
}

TEST(TextReconReadLine, EofWithNothingReadReturnsNull) {
    FakeStream s; s.data = ""; g_stream = &s;
    StreamReadHooks h = MakeHooks();
    char buf[64];
    char* r = TextReadLine(buf, 64, h);
    CHECK(r == nullptr);
    g_stream = nullptr;
}

TEST(TextReconReadLine, EofAfterPartialLineReturnsBuffer) {
    FakeStream s; s.data = "tail"; g_stream = &s;  // no trailing newline, hits EOF
    StreamReadHooks h = MakeHooks();
    char buf[64];
    char* r = TextReadLine(buf, 64, h);
    CHECK(r == buf);
    CHECK(std::strcmp(buf, "tail") == 0);
    g_stream = nullptr;
}

TEST(TextReconReadLine, SizeLimitStopsBeforeOverflow) {
    FakeStream s; s.data = "abcdef\n"; g_stream = &s;
    StreamReadHooks h = MakeHooks();
    char buf[8];
    // size==4 => at most 3 chars stored (pre-decrement loop), then NUL.
    char* r = TextReadLine(buf, 4, h);
    CHECK(r == buf);
    CHECK(std::strcmp(buf, "abc") == 0);
    g_stream = nullptr;
}
