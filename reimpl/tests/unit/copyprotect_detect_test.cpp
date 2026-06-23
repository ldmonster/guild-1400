// Golden tests for guild::drm::detect — CopyProtect drive detection + verify.
// Suite prefix: CopyProtectDetect. Headless; no main() (shared test_main.cpp).
#include "drm/copyprotect_detect.h"

#include "test.h"

#include <cstring>

using namespace guild;
using namespace guild::drm::detect;

// -----------------------------------------------------------------------------
// MemCompare / MemFindPattern primitives
// -----------------------------------------------------------------------------
TEST(CopyProtectDetect, MemCompareEquality) {
    const u8 a[] = {1, 2, 3, 4};
    const u8 b[] = {1, 2, 3, 4};
    const u8 c[] = {1, 2, 9, 4};
    CHECK_EQ(MemCompare(a, b, 4), 0);
    CHECK(MemCompare(a, c, 4) != 0);
    CHECK(MemCompare(a, c, 2) == 0);   // first two equal
    CHECK_EQ(MemCompare(a, b, 0), 0);  // zero length always equal
}

TEST(CopyProtectDetect, MemFindPatternSubstring) {
    const u8 hay[] = {0x10, 0x20, 0x30, 0x40, 0x50};
    const u8 needle[] = {0x30, 0x40};
    const u8 missing[] = {0x30, 0x99};
    const u8* hit = MemFindPattern(hay, needle, 5, 2);
    CHECK(hit != nullptr);
    CHECK_EQ((int)(hit - hay), 2);
    CHECK(MemFindPattern(hay, missing, 5, 2) == nullptr);
}

// -----------------------------------------------------------------------------
// Model table — a buffer equal to a stored entry must set exactly that flag.
// The matcher compares the (already de-obfuscated) INQUIRY field against the raw
// stored table bytes, so feeding the entry bytes verbatim is the golden vector.
// Stored bytes recovered byte-exact from .rdata (see copyprotect_detect.cpp).
// -----------------------------------------------------------------------------

// Build a 64-byte zeroed model buffer with `entry` copied at offset 0.
static void make_model(u8 out[64], const u8* entry, unsigned n) {
    std::memset(out, 0, 64);
    std::memcpy(out, entry, n);
}

TEST(CopyProtectDetect, ModelExactMatch_145A058) {
    // t_1452544 (cmp,0x11) -> a145A058. Entry bytes from 0x1452544.
    const u8 e[] = {0x18,0x70,0x63,0x07,0x69,0x7a,0x25,0x1b,0x1d,0x11,0x17,
                    0x52,0x16,0x06,0x02,0x02,0x10};
    u8 m[64];
    make_model(m, e, sizeof(e));
    DriveFlags f;
    MatchDriveModelTable(m, f);
    CHECK_EQ(f.a145A058, 1);
    CHECK_EQ(f.a145A054, 0); // a different entry must not also fire
}

TEST(CopyProtectDetect, ModelExactMatch_145A054) {
    // t_14524F8 (cmp,6) -> a145A054.
    const u8 e[] = {0x05,0x1d,0x11,0x0a,0x01,0x6e};
    u8 m[64];
    make_model(m, e, sizeof(e));
    DriveFlags f;
    MatchDriveModelTable(m, f);
    CHECK_EQ(f.a145A054, 1);
}

TEST(CopyProtectDetect, ModelSubstringMatch_145A084) {
    // t_14526F4 (find,9) -> a145A084. Place the needle mid-buffer to exercise
    // the substring search rather than a prefix compare.
    const u8 needle[] = {0x63,0x11,0x16,0x69,0x15,0x0b,0x01,0x00,0x70};
    u8 m[64];
    std::memset(m, 0, 64);
    std::memcpy(m + 20, needle, sizeof(needle));
    DriveFlags f;
    MatchDriveModelTable(m, f);
    CHECK_EQ(f.a145A084, 1);
}

TEST(CopyProtectDetect, ModelByteTest_145A05C) {
    // a145A05C: (NOT exact-match t_1452364) AND (m[0x16]==0x62 OR m[0x15]==0x66).
    u8 m[64];
    std::memset(m, 0, 64);
    m[0x16] = 0x62; // 'b'
    DriveFlags f;
    MatchDriveModelTable(m, f);
    CHECK_EQ(f.a145A05C, 1);

    std::memset(m, 0, 64);
    m[0x15] = 0x66; // 'f'
    DriveFlags g;
    MatchDriveModelTable(m, g);
    CHECK_EQ(g.a145A05C, 1);

    std::memset(m, 0, 64);
    DriveFlags h;
    MatchDriveModelTable(m, h);
    CHECK_EQ(h.a145A05C, 0);
}

TEST(CopyProtectDetect, ModelNoMatchClearsCombo) {
    // An all-0xFF buffer matches nothing; combo flags must be 0.
    u8 m[64];
    std::memset(m, 0xFF, 64);
    DriveFlags f;
    MatchDriveModelTable(m, f);
    CHECK_EQ(f.a145A04C, 0);
    CHECK_EQ(f.a145A050, 0);
    CHECK_EQ(f.a145A058, 0);
    CHECK_EQ(f.a145A0E4, 0);
}

TEST(CopyProtectDetect, ModelComboPrefersFirst_04C) {
    // The {find EC||E4||E0} block: if found AND Eq(t_14526D0) -> a145A04C,
    // else a145A050. t_14526D0 contains the find-EC needle as a substring, so
    // feeding t_14526D0 verbatim selects the a145A04C branch.
    const u8 e[] = {0x12,0x06,0x06,0x73,0x64,0x12,0x12,0x69,0x7f,0x1d,0x02,0x6d,
                    0x65,0x73};
    u8 m[64];
    make_model(m, e, sizeof(e));
    DriveFlags f;
    MatchDriveModelTable(m, f);
    CHECK_EQ(f.a145A04C, 1);
    CHECK_EQ(f.a145A050, 0);
}

// -----------------------------------------------------------------------------
// Vendor table — vendor(8) + product(16) paired comparison.
// -----------------------------------------------------------------------------
TEST(CopyProtectDetect, VendorPairMatch_145A050) {
    // a145A050 = Eq(v,t_145297C,4) && Eq(p,t_1452970,9).
    const u8 ve[] = {0x12,0x06,0x06,0x73};                 // t_145297C
    const u8 pe[] = {0x12,0x12,0x69,0x7f,0x1d,0x02,0x6d,0x65,0x73}; // t_1452970
    u8 v[8]; u8 p[16];
    std::memset(v, 0, 8); std::memset(p, 0, 16);
    std::memcpy(v, ve, sizeof(ve));
    std::memcpy(p, pe, sizeof(pe));
    DriveFlags f;
    MatchDriveVendorTable(v, p, f);
    CHECK_EQ(f.a145A050, 1);
}

TEST(CopyProtectDetect, VendorPairRequiresBoth) {
    // Vendor matches but product does not -> flag stays 0.
    const u8 ve[] = {0x12,0x06,0x06,0x73};
    u8 v[8]; u8 p[16];
    std::memset(v, 0, 8); std::memset(p, 0xAB, 16);
    std::memcpy(v, ve, sizeof(ve));
    DriveFlags f;
    MatchDriveVendorTable(v, p, f);
    CHECK_EQ(f.a145A050, 0);
}

TEST(CopyProtectDetect, VendorSubstringProduct_145A084) {
    // a145A084 = Find(p, t_14528B4, 16, 8).
    const u8 needle[] = {0x11,0x16,0x69,0x15,0x0b,0x01,0x00,0x70};
    u8 v[8]; u8 p[16];
    std::memset(v, 0, 8); std::memset(p, 0, 16);
    std::memcpy(p + 4, needle, sizeof(needle));
    DriveFlags f;
    MatchDriveVendorTable(v, p, f);
    CHECK_EQ(f.a145A084, 1);
}

// -----------------------------------------------------------------------------
// SetSpeedParams — exact branch ladder.
// -----------------------------------------------------------------------------
TEST(CopyProtectDetect, SpeedSptiA058Is50) {
    DriveFlags f;
    f.flag_1459FEC = 1;
    f.a145A058 = 1;
    SpeedParams s = SetSpeedParams(f);
    CHECK_EQ(s.speed, 50);
    CHECK_EQ(s.retry, 1);
}

TEST(CopyProtectDetect, SpeedSptiA078Is10x10) {
    DriveFlags f;
    f.flag_1459FEC = 1;
    f.a145A078 = 1; // a145A058 unset -> second branch
    SpeedParams s = SetSpeedParams(f);
    CHECK_EQ(s.speed, 10);
    CHECK_EQ(s.retry, 10);
}

TEST(CopyProtectDetect, SpeedSptiDefaultZero) {
    DriveFlags f;
    f.flag_1459FEC = 1; // neither A058 nor A078
    SpeedParams s = SetSpeedParams(f);
    CHECK_EQ(s.speed, 0);
    CHECK_EQ(s.retry, 1);
}

TEST(CopyProtectDetect, SpeedAspiA06CIs5) {
    DriveFlags f; // flag_1459FEC == 0
    f.a145A06C = 1;
    SpeedParams s = SetSpeedParams(f);
    CHECK_EQ(s.speed, 5);
    CHECK_EQ(s.retry, 1);
}

TEST(CopyProtectDetect, SpeedAspiNoneZero) {
    DriveFlags f; // all clear
    SpeedParams s = SetSpeedParams(f);
    CHECK_EQ(s.speed, 0);
    CHECK_EQ(s.retry, 1);
}

TEST(CopyProtectDetect, SpeedAspiA05COnlySpeed) {
    DriveFlags f;
    f.a145A05C = 1; // sets speed=10, retry stays default 1
    SpeedParams s = SetSpeedParams(f);
    CHECK_EQ(s.speed, 10);
    CHECK_EQ(s.retry, 1);
}

// -----------------------------------------------------------------------------
// Sector checksum + disc signature math.
// -----------------------------------------------------------------------------
TEST(CopyProtectDetect, ChecksumTableShipped) {
    const u32* t = SectorChecksumTable();
    CHECK_EQ(t[0], 0x39530FE4u);
    for (int i = 1; i < 8; ++i) CHECK_EQ(t[i], 0u);
}

TEST(CopyProtectDetect, ChecksumSumWraps) {
    u32 buf[512];
    for (int i = 0; i < 512; ++i) buf[i] = (u32)i;
    // Sum of i for i in [21,512) = sum(0..511) - sum(0..20)
    //   = 511*512/2 - 20*21/2 = 130816 - 210 = 130606.
    CHECK_EQ(SectorChecksumSum(buf), 130606u);

    // Construct a buffer whose [21..512) sum equals the table constant.
    std::memset(buf, 0, sizeof(buf));
    buf[21] = 0x39530FE4u;
    CHECK_EQ(SectorChecksumSum(buf), 0x39530FE4u);
    CHECK(SectorChecksumMatches(SectorChecksumSum(buf)));
}

TEST(CopyProtectDetect, ChecksumMatchNegative) {
    CHECK(!SectorChecksumMatches(0x12345678u));
    CHECK(SectorChecksumMatches(0x39530FE4u));
}

TEST(CopyProtectDetect, SignatureAllZeroIsGenuine) {
    // Shipped signature table is all zero -> every sig treated as genuine.
    const u32* t = DiscSignatureTable();
    for (int i = 0; i < 8; ++i) CHECK_EQ(t[i], 0u);
    CHECK(DiscSignatureMatches(0u));
    CHECK(DiscSignatureMatches(0xDEADBEEFu));
}

// -----------------------------------------------------------------------------
// EXE footer fold + verify.
// -----------------------------------------------------------------------------
TEST(CopyProtectDetect, ExeFooterFoldRoundTrip) {
    ExeFooter f;
    for (int i = 0; i < 132; ++i) f.bytes[i] = (u8)(i * 7 + 3);
    f.expectedFold = ExeFooterFold(f.bytes); // self-consistent fold
    f.statusField = kExeFooterMagic;         // == 9
    CHECK(VerifyExeFooter(f));

    // Tamper one byte -> fold changes -> verification fails.
    ExeFooter g = f;
    g.bytes[64] ^= 0xFF;
    CHECK(!VerifyExeFooter(g));

    // Wrong magic -> fails even with correct fold.
    ExeFooter h = f;
    h.statusField = 7;
    CHECK(!VerifyExeFooter(h));
}

TEST(CopyProtectDetect, ExeFooterFoldDeterministic) {
    u8 a[132], b[132];
    for (int i = 0; i < 132; ++i) { a[i] = (u8)i; b[i] = (u8)i; }
    CHECK_EQ(ExeFooterFold(a), ExeFooterFold(b));
}

TEST(CopyProtectDetect, MasterDiscIdRecovered) {
    CHECK(std::strcmp(MasterDiscId(), "UKD_548520-001.001") == 0);
    CHECK_EQ(kMasterIdLength, 18u);
    CHECK_EQ((u32)std::strlen(MasterDiscId()), kMasterIdLength);
}

// -----------------------------------------------------------------------------
// ProbeDriveGeometry — gate + fold, driven by a synthetic DiscDevice.
// -----------------------------------------------------------------------------
namespace {
struct FakeDisc : DiscDevice {
    u32 start, end;
    int rc;
    int calls = 0;
    FakeDisc(u32 s, u32 e, int r) : start(s), end(e), rc(r) {}
    int ReadTocBounds(u32& s, u32& e) override {
        ++calls;
        s = start;
        e = end;
        return rc;
    }
};
} // namespace

TEST(CopyProtectDetect, GeometryGateClosed) {
    DriveFlags f; // no selector set -> gate closed
    FakeDisc dev(0x600000u, 0x500000u, 1);
    GeometryResult g = ProbeDriveGeometry(f, dev, 0);
    CHECK_EQ(dev.calls, 0); // never touched the disc
    CHECK_EQ(g.ioOk, 0);
    CHECK_EQ(g.inRange, 0);
}

TEST(CopyProtectDetect, GeometryInRangeFolds) {
    DriveFlags f;
    f.a145A14C = 1; // open the gate (TOC selector)
    // start >= 0x5F0000, end >= 0x4A0000
    FakeDisc dev(0x5F1234u, 0x4A5678u, 1);
    GeometryResult g = ProbeDriveGeometry(f, dev, 0x0000);
    CHECK_EQ(dev.calls, 2);  // original reads twice (discards first)
    CHECK_EQ(g.ioOk, 1);
    CHECK_EQ(g.inRange, 1);
    // fold = (0 ^ (start>>16)) ; then += (end>>8) ; truncated to 16 bits.
    u16 expect = (u16)(0u ^ (u16)(0x5F1234u >> 16));
    expect = (u16)(expect + (u16)(0x4A5678u >> 8));
    CHECK_EQ(g.fold, expect);
}

TEST(CopyProtectDetect, GeometryOutOfRangeNoFold) {
    DriveFlags f;
    f.flag_1459FEC = 1; // gate open via SPTI selector
    FakeDisc dev(0x100000u, 0x100000u, 1); // below thresholds
    GeometryResult g = ProbeDriveGeometry(f, dev, 0x1234);
    CHECK_EQ(g.ioOk, 1);
    CHECK_EQ(g.inRange, 0);
    CHECK_EQ(g.fold, (u16)0x1234); // unchanged seed
}

TEST(CopyProtectDetect, GeometryReadFailNoRange) {
    DriveFlags f;
    f.a145A14C = 1;
    FakeDisc dev(0x600000u, 0x500000u, 0); // rc=0 -> read fails
    GeometryResult g = ProbeDriveGeometry(f, dev, 0);
    CHECK_EQ(g.ioOk, 0);
    CHECK_EQ(g.inRange, 0);
}
