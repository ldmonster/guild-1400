// tests/unit/statistic_recon_test.cpp — golden-vector unit tests for the statistics
// reconstruction: the round-dump text formatters (gilde.exe 0x594fd0..0x5953bc) and
// the official-comparison chart aggregation (0x58beb8) + slider-color clamp (0x55fa88).
// Self-contained: hand-built person records and a fixed StatChartEnv/StatDumpEnv;
// no game assets. Golden values cross-checked against the original arithmetic.
#include "test.h"

#include "world/statistic_recon_chart.h"
#include "world/statistic_recon_dump.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>

using namespace guild;
using namespace guild::world;

namespace {

void putU16(u8* p, std::size_t off, u16 v) {
    p[off] = static_cast<u8>(v & 0xFF);
    p[off + 1] = static_cast<u8>((v >> 8) & 0xFF);
}
void putU32(u8* p, std::size_t off, u32 v) {
    p[off] = static_cast<u8>(v & 0xFF);
    p[off + 1] = static_cast<u8>((v >> 8) & 0xFF);
    p[off + 2] = static_cast<u8>((v >> 16) & 0xFF);
    p[off + 3] = static_cast<u8>((v >> 24) & 0xFF);
}
void putF32(u8* p, std::size_t off, float f) {
    u32 b;
    std::memcpy(&b, &f, 4);
    putU32(p, off, b);
}

// Env that names every table entry "<prefix><idx>" so the formatted output is
// deterministic. Each named() call has its own persistent storage because the dump
// routines pass several lookups as arguments to a single sprintf (the pointers must
// all stay live simultaneously).
struct NamingEnv : StatDumpEnv {
    int round = 0;
    mutable std::deque<std::string> store;  // stable element addresses
    i32 CurrentRound() const override { return round; }
    const char* named(const char* pre, u8 i) const {
        store.push_back(std::string(pre) + std::to_string((int)i));
        return store.back().c_str();
    }
    const char* IdentityKindName(u8 i) const override { return named("K", i); }
    const char* ClassName(u8 i) const override { return named("C", i); }
    const char* OriginName(u8 i) const override { return named("O", i); }
    const char* ReligionName(u8 i) const override { return named("R", i); }
    const char* StatusName(u8 i) const override { return named("S", i); }
    const char* LocationName(u8 i) const override { return named("L", i); }
    const char* TraitName(u8 i) const override { return named("X", i); }
};

}  // namespace

// --- DumpRoundHeader / Footer ----------------------------------------------------
TEST(StatisticReconDump, RoundHeaderFooter) {
    NamingEnv env;
    env.round = 42;
    char out[1100];
    StatDumpRoundHeader(out, env);
    CHECK(std::string(out) ==
          "++++++++++ Begin statistic dump in round 42 +++++++++");
    StatDumpRoundFooter(out, env);
    CHECK(std::string(out) ==
          "++++++++++ End statistic dump in round 42 +++++++++");
}

// --- DumpNpcNeeds: bytes +128..+132 ----------------------------------------------
TEST(StatisticReconDump, Needs) {
    NamingEnv env;
    u8 rec[536] = {0};
    rec[128] = 5;
    rec[129] = 10;
    rec[130] = 15;
    rec[131] = 200;
    rec[132] = 255;
    char out[64];
    StatDumpNpcNeeds(rec, out, env);
    CHECK(std::string(out) == "5\t10\t15\t200\t255\t");
}

// --- DumpNpcTraits: TraitName(+358..+361) ----------------------------------------
TEST(StatisticReconDump, Traits) {
    NamingEnv env;
    u8 rec[536] = {0};
    rec[358] = 1;
    rec[359] = 2;
    rec[360] = 3;
    rec[361] = 4;
    char out[64];
    StatDumpNpcTraits(rec, out, env);
    CHECK(std::string(out) == "X1\tX2\tX3\tX4\t");
}

// --- DumpNpcAttributes: 6 floats + packed bitfields of dword@+44 -----------------
TEST(StatisticReconDump, AttributesBitfields) {
    NamingEnv env;
    u8 rec[536] = {0};
    // Five floats at +16,+20,+24,+28,+32.
    putF32(rec, 16, 1.0f);
    putF32(rec, 20, 2.5f);
    putF32(rec, 24, 3.75f);
    putF32(rec, 28, 0.0f);
    putF32(rec, 32, 12.34f);
    // The 6th double assembled from +36/+38; set both to 0 -> 0.00.
    putU32(rec, 36, 0);
    putU32(rec, 38, 0);
    // Packed bitfield dword @+44 == 0x12345678 -> golden {8,7,6,1,1,2,3,0,9}.
    putU32(rec, 44, 0x12345678u);
    char out[256] = {0};
    StatDumpNpcAttributes(rec, out, env);
    // First block: 6 %5.2f then the junk %i (we emit 0), then the 9 bitfields.
    CHECK(std::string(out) ==
          " 1.00\t  2.50\t  3.75\t  0.00\t 12.34\t  0.00\t 0\t"
          "8\t 7\t 6\t 1\t 1\t 2\t 3\t 0\t 9\t");
}

// --- DumpNpcIdentity: field reads + table lookups --------------------------------
TEST(StatisticReconDump, Identity) {
    NamingEnv env;
    env.round = 7;
    u8 rec[536] = {0};
    rec[2] = 3;                 // type byte -> kind table idx 3
    putU32(rec, 4, 12345);      // id
    putU16(rec, 0, 99);         // marker
    std::strcpy(reinterpret_cast<char*>(rec) + 48, "Hans");  // name
    rec[8] = 50;                // byte +8
    rec[9] = 4;                 // class idx (*(int*)(rec+6)>>24 == rec[9])
    rec[12] = 5;                // origin idx
    rec[13] = 6;                // religion idx
    putU16(rec, 10, 1500);      // word +10
    rec[357] = 8;               // location idx (*(int*)(rec+354)>>24 == rec[357])
    rec[356] = 9;               // status idx (*(int*)((char*)rec+353)>>24 == rec[356])
                                // (0x594ff8: dword_8C3B48 index; pin was rec[359] before
                                //  the +353 read was verified against the binary)
    char out[256];
    StatDumpNpcIdentity(rec, out, env);
    // round id marker kind name byte8 class word10 origin religion status location
    CHECK(std::string(out) ==
          "7\t12345\t99\tK3\tHans\t50\tC4\t1500\tO5\tR6\tS9\tL8\t");
}

// --- DumpNpcSkills: 6 skill dwords + currency + wealth ---------------------------
struct SkillEnv : NamingEnv {
    i32 SumCurrencyHeld(const u8*) const override { return 777; }
    i32 ComputeTotalWealth(u16 idx, const u8*) const override {
        return 1000 + idx;  // depends on marker to prove arg wiring
    }
};
TEST(StatisticReconDump, Skills) {
    SkillEnv env;
    u8 rec[536] = {0};
    putU16(rec, 0, 23);  // marker -> ComputeTotalWealth arg
    putU32(rec, 0x190, 11);
    putU32(rec, 0x194, 22);
    putU32(rec, 0x198, 33);
    putU32(rec, 0x19C, 44);
    putU32(rec, 0x1A0, 55);
    putU32(rec, 0x1A4, 66);
    char out[128];
    StatDumpNpcSkills(rec, out, env);
    // skills... currency(777) wealth(1000+23=1023)
    CHECK(std::string(out) == "11\t22\t33\t44\t55\t66\t777\t1023\t");
}

// --- DumpNpcInventory: 8 slots resolved via FindRecordById -----------------------
struct InvEnv : NamingEnv {
    mutable u8 itemRec[536];
    const u8* FindRecordById(i32 id) const override {
        if (id < 0) return nullptr;  // -1 slots unresolved -> "NIEMAND"
        std::memset(itemRec, 0, sizeof(itemRec));
        // id field at +4, name at +48
        itemRec[4] = static_cast<u8>(id);
        std::strcpy(reinterpret_cast<char*>(itemRec) + 48, "Sword");
        return itemRec;
    }
};
TEST(StatisticReconDump, Inventory) {
    InvEnv env;
    u8 rec[536];
    std::memset(rec, 0, sizeof(rec));
    // slot ids at +92 + 4*k (k=0..7): two resolved, rest -1.
    for (int k = 0; k < 8; ++k) putU32(rec, 92 + 4 * k, 0xFFFFFFFFu);  // -1
    putU32(rec, 92 + 4 * 0, 7);
    putU32(rec, 92 + 4 * 3, 9);
    char out[4096];
    out[0] = 0;
    StatDumpNpcInventory(rec, out, env);
    std::string expect = "7\tSword" "-1\tNIEMAND" "-1\tNIEMAND" "9\tSword"
                         "-1\tNIEMAND" "-1\tNIEMAND" "-1\tNIEMAND" "-1\tNIEMAND";
    CHECK(std::string(out) == expect);
}

// --- DumpNpcRecord: skips marker==0xFFFF, dumps otherwise ------------------------
TEST(StatisticReconDump, RecordSkipsFreeSlot) {
    NamingEnv env;
    u8 rec[536];
    std::memset(rec, 0, sizeof(rec));
    putU16(rec, 0, 0xFFFF);  // free slot
    char out[8200];
    out[0] = 'x';
    int r = StatDumpNpcRecord(rec, out, env);
    CHECK_EQ(r, 0);
    CHECK_EQ((int)out[0], (int)'x');  // untouched (returned before memset)
    // nullptr also returns 0
    CHECK_EQ(StatDumpNpcRecord(nullptr, out, env), 0);
}

TEST(StatisticReconDump, RecordDumpsLiveSlot) {
    NamingEnv env;
    env.round = 1;
    u8 rec[536];
    std::memset(rec, 0, sizeof(rec));
    putU16(rec, 0, 5);  // live marker
    std::strcpy(reinterpret_cast<char*>(rec) + 48, "Bob");
    char out[8200];
    std::memset(out, 0xAA, sizeof(out));
    StatDumpNpcRecord(rec, out, env);
    // The first field is the round; the name appears in the identity block.
    CHECK(std::string(out).rfind("1\t", 0) == 0);
    CHECK(std::string(out).find("\tBob\t") != std::string::npos);
}

// ============================ Chart aggregation ==================================

// Fixed env: one official at slot 2, three favorability contributors, fixed wealth.
struct ChartEnv1 : StatChartEnv {
    u8 PersonType(int slot) const override {
        if (slot == 2) return 6;          // the official
        if (slot == 4 || slot == 5 || slot == 6) return 4;  // favor contributors
        return 0;                          // not a {4..7} type
    }
    bool PersonFreeSlot(int slot) const override {
        return !(slot == 2 || slot == 4 || slot == 5 || slot == 6);
    }
    u8 PersonByte(int slot, std::size_t off) const override {
        (void)slot;
        if (off >= 128 && off <= 132) {
            static const u8 needs[5] = {10, 20, 30, 40, 50};  // sum 150
            return needs[off - 128];
        }
        if (off == 13) return 12;  // religion byte
        return 0;
    }
    u8 PersonOfficeRank(int slot, std::size_t off) const override {
        (void)slot;
        if (off == 358) return 1;
        if (off == 361) return 2;
        return 0;
    }
    u8 OfficeDefByte2(u8 rank) const override {
        if (rank == 1) return 5;  // b358
        if (rank == 2) return 7;  // b361
        return 0;
    }
    double Favorability(int self, int other) const override {
        (void)self;
        if (other == 4) return 40.0;
        if (other == 5) return 60.0;
        if (other == 6) return 80.0;  // sum 180, n=3
        return 0.0;
    }
    i32 ComputeTotalWealth(int slot) const override {
        return slot == 2 ? 5000 : -1;
    }
    double ConvertX(double x) const override {
        // round-to-nearest-even, the FRNDINT/ConvertX behavior.
        double r = x >= 0 ? (long long)(x + 0.5) : -(long long)(-x + 0.5);
        return r;
    }
};

TEST(StatisticReconChart, SingleOfficialGolden) {
    ChartEnv1 env;
    std::array<OfficialCompareRecord, kStatMaxOfficials> rows{};
    int n = StatChartBuildOfficialComparison(rows, env);
    CHECK_EQ(n, 1);
    const OfficialCompareRecord& r = rows[0];
    CHECK_EQ((int)r.slot, 2);
    CHECK_EQ(r.wealth, 5000);
    // Golden values (cross-checked against the original arithmetic):
    auto close = [](float a, float b) { return std::fabs(a - b) < 1e-6f; };
    CHECK(close(r.needs, 0.119047619f));
    CHECK(close(r.religion, 2.0f));
    CHECK(close(r.office, 0.923076987f));
    CHECK(close(r.favor, 0.599999964f));
    CHECK(close(r.ratio, 1.0f));
    CHECK(close(r.composite, 0.858223498f));
    CHECK_EQ(r.barHeight, 0);  // h = 0.4291.. -> rounds to 0
}

TEST(StatisticReconChart, NoOfficials) {
    struct Empty : StatChartEnv {} env;  // PersonType==0 everywhere
    std::array<OfficialCompareRecord, kStatMaxOfficials> rows{};
    CHECK_EQ(StatChartBuildOfficialComparison(rows, env), 0);
}

TEST(StatisticReconChart, SelectionCapAtEight) {
    struct AllOfficials : StatChartEnv {
        u8 PersonType(int) const override { return 5; }
        bool PersonFreeSlot(int) const override { return false; }
        i32 ComputeTotalWealth(int) const override { return 100; }
        double Favorability(int, int) const override { return 1.0; }
    } env;
    std::array<OfficialCompareRecord, kStatMaxOfficials> rows{};
    // Every slot is an official; selection must cap at 8 (72/9).
    CHECK_EQ(StatChartBuildOfficialComparison(rows, env), kStatMaxOfficials);
    CHECK_EQ((int)rows[0].slot, 0);
    CHECK_EQ((int)rows[7].slot, 7);
}

// --- HARDENING: max-valued table-index bytes pass through unbounded --------------
// The dump routines feed raw record bytes (0..255) as table indices to the env
// accessors; the original reads the byte verbatim (the bound, if any, is the table's
// concern). Confirm a record with every index byte == 255 dumps without our code
// over-reading the 536-byte record, and that the high byte reaches the env.
TEST(StatisticReconDump, MaxIndexBytesTraits) {
    NamingEnv env;
    u8 rec[536];
    std::memset(rec, 0xFF, sizeof(rec));   // every trait byte == 255
    char out[64];
    StatDumpNpcTraits(rec, out, env);
    CHECK(std::string(out) == "X255\tX255\tX255\tX255\t");
}

TEST(StatisticReconDump, MaxIndexBytesIdentity) {
    NamingEnv env;
    env.round = 0;
    u8 rec[536];
    std::memset(rec, 0xFF, sizeof(rec));   // all index bytes == 255, marker 0xFFFF
    // Put a NUL-terminated name so the +48 %s read is bounded.
    std::memset(rec + 48, 0, 16);
    std::strcpy(reinterpret_cast<char*>(rec) + 48, "N");
    char out[256];
    StatDumpNpcIdentity(rec, out, env);
    // class/origin/status/location come from byte +9/+12/+356/+357 (all 0xFF here).
    CHECK(std::string(out).find("\tC255\t") != std::string::npos);
    CHECK(std::string(out).find("\tO255\t") != std::string::npos);
    CHECK(std::string(out).find("\tS255\t") != std::string::npos);
    CHECK(std::string(out).find("\tL255\t") != std::string::npos);
}

// CompareSliderColor must never index outside the 8-entry palette regardless of
// the growth code (negative, zero, exactly-boundary, far-positive).
TEST(StatisticReconChart, SliderColorBoundsAllInputs) {
    for (i32 code = -100; code <= 2500; ++code) {
        i32 c = CompareSliderColor(code);
        // every result is one of the 8 palette entries (proves a bounded index).
        bool inPalette = false;
        for (i32 v : kCompareSliderPalette) inPalette = inPalette || (c == v);
        CHECK(inPalette);
    }
}

// --- Slider color clamp (0x55fa88) ----------------------------------------------
TEST(StatisticReconChart, SliderColorClamp) {
    CHECK_EQ(CompareSliderColor(1340), 0x7E);  // <=0 -> idx 0
    CHECK_EQ(CompareSliderColor(1342), 0x7E);  // ==1342 -> idx 0
    CHECK_EQ(CompareSliderColor(1343), 0x84);  // idx 1
    CHECK_EQ(CompareSliderColor(1348), 0xA2);  // idx 6
    CHECK_EQ(CompareSliderColor(1349), 0xA8);  // idx 7 (clamp high)
    CHECK_EQ(CompareSliderColor(2000), 0xA8);  // >7 -> idx 7
}

// --- Panel hook stays inert without a sink --------------------------------------
TEST(StatisticReconChart, PanelInertWithoutSink) {
    StatPanel_SetCompareSink(nullptr);
    ChartEnv1 env;
    CHECK_EQ(StatPanelShowCompareChart(env), 0);
}

TEST(StatisticReconChart, PanelEmitsThroughSink) {
    struct Sink : StatPanelCompareSink {
        int got = 0;
        bool CanOpen() override { return true; }
        void EmitRows(const std::array<OfficialCompareRecord, kStatMaxOfficials>&,
                      int count) override {
            got = count;
        }
    } sink;
    StatPanel_SetCompareSink(&sink);
    ChartEnv1 env;
    int n = StatPanelShowCompareChart(env);
    StatPanel_SetCompareSink(nullptr);
    CHECK_EQ(n, 1);
    CHECK_EQ(sink.got, 1);
}
