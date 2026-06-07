#include "test.h"

#include "util/float_math.h"
#include "util/fpu.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

using namespace guild;
using namespace guild::util;

namespace {

bool CloseOrBothNan(double a, double b, double eps = 1e-12) {
    if (std::isnan(a) && std::isnan(b)) return true;
    if (std::isinf(a) || std::isinf(b)) return a == b;
    double scale = std::fmax(1.0, std::fabs(b));
    return std::fabs(a - b) <= eps * scale;
}

bool SameBits(double a, double b) {
    std::uint64_t x, y;
    std::memcpy(&x, &a, 8);
    std::memcpy(&y, &b, 8);
    return x == y;
}

} // namespace

// ---------------------------------------------------------------- Fmod -----

TEST(UtilFpu, FmodMatchesCmath) {
    const double divs[] = {1.0, 2.0, 3.5, 0.1, 7.0, 100.0, 0.333333};
    const double nums[] = {0.0, 1.0, -1.0, 5.5, -5.5, 10.25, -10.25, 1234.5, -1234.5};
    for (double d : divs)
        for (double n : nums)
            CHECK(CloseOrBothNan(Fmod(n, d), std::fmod(n, d)));
}

TEST(UtilFpu, FmodSignFollowsDividend) {
    // x87 FPREM / std::fmod: result takes the sign of the dividend.
    CHECK(std::signbit(Fmod(-5.5, 2.0)) == true);
    CHECK(std::signbit(Fmod(5.5, -2.0)) == false);
    CHECK(SameBits(Fmod(-5.5, 2.0), std::fmod(-5.5, 2.0)));
    CHECK(SameBits(Fmod(5.5, -2.0), std::fmod(5.5, -2.0)));
}

TEST(UtilFpu, FmodEdgeCases) {
    CHECK(std::isnan(Fmod(1.0, 0.0)));                       // div by zero
    CHECK(SameBits(Fmod(0.0, 5.0), 0.0));                    // zero dividend
    CHECK(std::isnan(Fmod(std::numeric_limits<double>::infinity(), 2.0)));
    CHECK(CloseOrBothNan(Fmod(3.0, std::numeric_limits<double>::infinity()), 3.0));
}

// ---------------------------------------------------------------- Sqrt -----

TEST(UtilFpu, SqrtMatchesCmath) {
    const double xs[] = {0.0, 1.0, 2.0, 4.0, 9.0, 1e-9, 1e9, 1e300, 123456.789};
    for (double x : xs)
        CHECK(SameBits(Sqrt(x), std::sqrt(x)));
}

TEST(UtilFpu, SqrtEdgeCases) {
    CHECK(SameBits(Sqrt(0.0), 0.0));
    CHECK(SameBits(Sqrt(-0.0), -0.0));
    CHECK(std::isnan(Sqrt(-1.0)));                           // domain guard
    CHECK(std::isinf(Sqrt(std::numeric_limits<double>::infinity())));
}

// ---------------------------------------------------------------- Logs -----

TEST(UtilFpu, LogFunctionsMatchCmath) {
    const double xs[] = {1e-9, 0.5, 1.0, 2.0, std::exp(1.0), 8.0, 1000.0, 1e9};
    for (double x : xs) {
        CHECK(CloseOrBothNan(Log(x),   std::log(x)));
        CHECK(CloseOrBothNan(Log10(x), std::log2(x)));   // code 9  -> log2
        CHECK(CloseOrBothNan(Log2(x),  std::log10(x)));  // code 11 -> log10
    }
}

TEST(UtilFpu, LogBaseCodeMapping) {
    CHECK(CloseOrBothNan(LogBase(kLogCode_Log2, 8.0),  std::log2(8.0)));
    CHECK(CloseOrBothNan(LogBase(kLogCode_Log10, 1000.0), std::log10(1000.0)));
    CHECK(CloseOrBothNan(LogBase(kLogCode_Natural, std::exp(2.0)), 2.0));
}

TEST(UtilFpu, LogDomain) {
    CHECK(std::isinf(Log(0.0)) && Log(0.0) < 0.0);           // -inf at 0
    CHECK(std::isnan(Log(-1.0)));                            // NaN for x<0
}

// ------------------------------------------------------------- Classify ----

TEST(UtilFpu, ClassifyInfNanZeroNormal) {
    const double inf = std::numeric_limits<double>::infinity();
    const double qnan = std::numeric_limits<double>::quiet_NaN();
    CHECK_EQ(Classify(inf),  (int)kFpPosInf);
    CHECK_EQ(Classify(-inf), (int)kFpNegInf);
    CHECK_EQ(Classify(qnan), (int)kFpQNan);
    CHECK_EQ(Classify(0.0),  (int)kFpNormal);
    CHECK_EQ(Classify(-0.0), (int)kFpNormal);
    CHECK_EQ(Classify(3.14), (int)kFpNormal);
    CHECK_EQ(Classify(-1e300), (int)kFpNormal);
}

TEST(UtilFpu, ClassifySignallingNan) {
    // Signalling NaN: exponent all ones, top mantissa bit clear, payload != 0.
    std::uint64_t bits = 0x7FF0000000000001ull;
    double snan;
    std::memcpy(&snan, &bits, 8);
    CHECK_EQ(Classify(snan), (int)kFpSNan);
}

// --------------------------------------------------- Control-word helpers ---

TEST(UtilFpu, DecodeEncodeRoundTrip) {
    // For every abstract mask combination, decode(encode(w)) must recover the
    // mask bits (the two helpers are inverses on the 0x1F mask range).
    for (u32 w = 0; w <= 0x1F; ++w) {
        u8 fcw = EncodeControlWord(w);
        u8 back = DecodeStatusWord(fcw);
        CHECK_EQ((u32)back, w);
    }
}

TEST(UtilFpu, EncodeDenormalBit) {
    CHECK((EncodeControlWord(kFpDenormalBit) & 0x02) != 0);
}

TEST(UtilFpu, SetControlWordSpliceAndReturn) {
    SetFcw(0x023F);  // all masked, nearest, 53-bit
    // Clearing the invalid mask (abstract bit 0x10) with mask 0x10.
    u32 r = SetControlWord(0x00, 0x10);
    CHECK((r & 0x10) == 0);
    // Other abstract mask bits preserved (were all set in 0x023F).
    CHECK((r & 0x0F) == 0x0F);
}

TEST(UtilFpu, ControlMaskClearsDenormalSelector) {
    SetFcw(0x023F);
    // ControlMask must strip 0x80000 from the mask, so the denormal bit can't
    // be written even if requested.
    u32 r = ControlMask(0x80000, 0x80000);
    CHECK((r & 0x80000) == 0);
}

TEST(UtilFpu, FcwGetSetPushRoundTrip) {
    SetFcw(0x023F);
    u16 prev = PushFcw(0x0C3F);          // switch to truncate rounding
    CHECK_EQ(prev, (u16)0x023F);
    CHECK_EQ(GetFcw() & kFcw_RC_Mask, (u16)kFcw_RC_Truncate);
    SetFcw(prev);                        // restore
    CHECK_EQ(GetFcw(), (u16)0x023F);
    CHECK_EQ(GetFcw() & kFcw_RC_Mask, (u16)kFcw_RC_Nearest);
}

TEST(UtilFpu, ControlWordIdentity) {
    CHECK_EQ(ControlWord(0x1234), (u16)0x1234);
}

// ----------------------------------------------------- Software-path flag ---

TEST(UtilFpu, X87SoftwarePathFlagAndDispatch) {
    CHECK(!UseX87SoftwarePath());        // default = libm path
    SetX87SoftwarePath(true);
    CHECK(UseX87SoftwarePath());
    // Software path must produce the same values (modeled via <cmath>).
    CHECK(SameBits(Fmod(-5.5, 2.0), std::fmod(-5.5, 2.0)));
    CHECK(CloseOrBothNan(Atan2(1.0, 1.0), std::atan2(1.0, 1.0)));
    SetX87SoftwarePath(false);
    CHECK(!UseX87SoftwarePath());
}

// ----------------------------------------------------------------- Atan2 ----

TEST(UtilFpu, Atan2MatchesCmath) {
    const double vals[] = {-3.0, -1.0, -0.25, 0.0, 0.25, 1.0, 3.0};
    for (double y : vals)
        for (double x : vals)
            CHECK(CloseOrBothNan(Atan2(y, x), std::atan2(y, x)));
    CHECK(CloseOrBothNan(AtanUnary(0.5), std::atan(0.5)));
}
