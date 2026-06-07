#include "test.h"

#include "crt/i10_output.h"

#include <cmath>
#include <cstring>
#include <string>

using namespace guild::crt;

// ---------------------------------------------------------------------------
// VIBE_Text_FormatFixedPoint @0x5fac3f — direct golden vectors.
//
// These call the layout routine with pre-rounded significant digits (the job
// FloatToDigits does) and assert the exact buffer contents PLUS the field-length
// metadata (outLen0..4) the printf core uses for zero padding. Goldens were
// computed by tracing the decompiled integer/memcpy logic; they are deterministic.
// The full visible string = buffer with the deferred zero runs inserted (see the
// e2e renderer); here we pin the raw production exactly.
// ---------------------------------------------------------------------------

namespace {

struct FpResult {
    char buf[64];
    int  ret;
    int  l0, l1, l2, l3, l4;
};

FpResult RunFp(int flags, int prec, int extra, const char* dig, int decExp,
               int ndigits) {
    I10Control c;
    std::memset(&c, 0, sizeof(c));
    c.precision = prec;
    c.flags = flags;
    c.extraCount = extra;
    FpResult r;
    std::memset(r.buf, '#', sizeof(r.buf));
    r.buf[63] = 0;
    r.ret = FormatFixedPoint(&c, dig, decExp, ndigits, r.buf);
    r.l0 = c.outLen0; r.l1 = c.outLen1; r.l2 = c.outLen2;
    r.l3 = c.outLen3; r.l4 = c.outLen4;
    return r;
}

struct EpResult {
    char buf[64];
    int  l6, l7, l8, l9;
};

EpResult RunEp(int flags, int prec, int extra, int marker, int expd,
               const char* dig, int decExp, int ndigits) {
    I10Control c;
    std::memset(&c, 0, sizeof(c));
    c.precision = prec;
    c.flags = flags;
    c.extraCount = extra;
    c.expMarker = marker;
    c.expDigits = expd;
    EpResult r;
    std::memset(r.buf, '#', sizeof(r.buf));
    r.buf[63] = 0;
    FormatExponent(&c, dig, decExp, ndigits, r.buf);
    r.l6 = c.outLen0; r.l7 = c.outLen1; r.l8 = c.outLen2; r.l9 = c.outLen3;
    return r;
}

} // namespace

TEST(I10FixedPoint, IntegerWithFraction) {
    // %.2f of 12345.67 (digits already rounded): "12345.67".
    FpResult r = RunFp(/*flags=*/2, /*prec=*/2, /*extra=*/0, "1234567", 4, 7);
    CHECK_EQ(std::string(r.buf), std::string("12345.67"));
    CHECK_EQ(r.ret, 2);          // 2 fraction digits emitted
    CHECK_EQ(r.l0, 5);           // 5 integer digits
    CHECK_EQ(r.l1, 8);           // total chars in buffer
    CHECK_EQ(r.l2, 0);           // no deferred trailing zeros
}

TEST(I10FixedPoint, SubOneMagnitude) {
    // %.2f of 0.50: leading "0." then the two digits.
    FpResult r = RunFp(2, 2, 0, "50", -1, 2);
    CHECK_EQ(std::string(r.buf), std::string("0.50"));
    CHECK_EQ(r.ret, 2);
    CHECK_EQ(r.l0, 0);
    CHECK_EQ(r.l1, 2);           // prefix "0." length
    CHECK_EQ(r.l3, 2);           // digit count placed after the point
}

TEST(I10FixedPoint, ZeroPrecisionNoPoint) {
    // %.0f of 4: just "4", no decimal point.
    FpResult r = RunFp(2, 0, 0, "4", 0, 1);
    CHECK_EQ(std::string(r.buf), std::string("4"));
    CHECK_EQ(r.ret, 0);
    CHECK_EQ(r.l0, 1);
    CHECK_EQ(r.l1, 1);
}

TEST(I10FixedPoint, TrailingFractionZerosDeferred) {
    // %.2f of 100.00 from 5 sig digits "10000": buffer is complete here.
    FpResult r = RunFp(2, 2, 0, "10000", 2, 5);
    CHECK_EQ(std::string(r.buf), std::string("100.00"));
    CHECK_EQ(r.l0, 3);
    CHECK_EQ(r.l1, 6);
}

TEST(I10FixedPoint, LeadingFractionZerosDeferred) {
    // %.4f of 0.0001: digit "1" at decExp -4. The 3 leading fraction zeros are
    // recorded in outLen2 (deferred), so the raw buffer is "0.1".
    FpResult r = RunFp(2, 4, 0, "1", -4, 1);
    CHECK_EQ(std::string(r.buf), std::string("0.1"));
    CHECK_EQ(r.l2, 3);           // 3 deferred leading fraction zeros
    CHECK_EQ(r.l3, 1);           // one significant digit
}

TEST(I10Exponent, BasicPositiveExponent) {
    // %.3e of 1.235e4 with MSVC 3-digit exponent.
    EpResult r = RunEp(/*flags=*/1, 3, 1, 'e', 0, "1235", 4, 4);
    CHECK_EQ(std::string(r.buf), std::string("1.235e+004"));
}

TEST(I10Exponent, ZeroPrecisionNoPoint) {
    // %.0e of 5: "5e+000".
    EpResult r = RunEp(1, 0, 1, 'e', 0, "5", 0, 1);
    CHECK_EQ(std::string(r.buf), std::string("5e+000"));
}

TEST(I10Exponent, UppercaseMarker) {
    // %.3E of 1.000e1 -> "1.000E+001".
    EpResult r = RunEp(1, 3, 1, 'E', 0, "1000", 1, 4);
    CHECK_EQ(std::string(r.buf), std::string("1.000E+001"));
}

TEST(I10Exponent, NegativeExponent) {
    // %.3e of 1.235e-4 -> "1.235e-004".
    EpResult r = RunEp(1, 3, 1, 'e', 0, "1235", -4, 4);
    CHECK_EQ(std::string(r.buf), std::string("1.235e-004"));
}

TEST(I10Exponent, FourDigitExponent) {
    // exponent >= 1000 widens to four digits: 6.022e+302 style.
    EpResult r = RunEp(1, 3, 1, 'e', 0, "6022", 1234, 4);
    CHECK_EQ(std::string(r.buf), std::string("6.022e+1234"));
}

// ---------------------------------------------------------------------------
// VIBE_Math_FloatToDigits @0x5fa7fd — complete-buffer cases (nan / inf / %e).
// ---------------------------------------------------------------------------

TEST(I10FloatToDigits, NotANumber) {
    I10Control c;
    std::memset(&c, 0, sizeof(c));
    c.flags = 1;
    c.precision = 6;
    char buf[32];
    long double v = std::nanl("");
    FloatToDigits(&v, &c, buf);
    CHECK_EQ(std::string(buf), std::string("nan"));
}

TEST(I10FloatToDigits, Infinity) {
    I10Control c;
    std::memset(&c, 0, sizeof(c));
    c.flags = 1;
    c.precision = 6;
    char buf[32];
    long double v = HUGE_VALL;
    FloatToDigits(&v, &c, buf);
    CHECK_EQ(std::string(buf), std::string("inf"));
}

TEST(I10FloatToDigits, NegativeSignDetected) {
    I10Control c;
    std::memset(&c, 0, sizeof(c));
    c.flags = 1;            // %e
    c.extraCount = 1;
    c.expMarker = 'e';
    c.precision = 2;
    char buf[64];
    long double v = -3.14L;
    FloatToDigits(&v, &c, buf);
    CHECK_EQ(c.sign, -1);   // *(ctrl+20) set to -1 for negatives
    // The unsigned mantissa field (driver prepends '-'): "3.14e+000".
    CHECK_EQ(std::string(buf), std::string("3.14e+000"));
}
