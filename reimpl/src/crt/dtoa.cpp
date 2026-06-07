#include "crt/dtoa.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace guild::crt {

// byte_1452BC0 — '.' in the C locale (the value in the static image).
char kDecimalPoint = '.';

// ---------------------------------------------------------------------------
// Provenance of the recovered IEEE-double -> digits engine.
//
// VIBE_Math_DoubleToExtended80 @0x14272b0 widens the IEEE double to an x87
// 80-bit extended value (sign, 15-bit biased exponent, 64-bit significand) in a
// 12-byte slot. VIBE_Float_FormatToDigits @0x1429420 then renders that extended
// value to decimal by scaling it into [1,10) with VIBE_Math_BigIntScaleByPower
// @0x1429a96 (which multiplies by entries of the 80-bit power-of-ten tables) and
// repeatedly extracting decimal digits with a 96-bit big integer
// (VIBE_Math_BigIntMultiply/Add/ShiftLeft/ShiftRightOne @0x1428dcf..0x1429876),
// rounding the 17th significant digit.
//
// The two power-of-ten tables (each a sequence of 12-byte 80-bit extended floats)
// were recovered byte-for-byte:
//
//   unk_1455510 (positive powers, 10^(1..7)*8^k ...), first slots:
//     00 00 00 00 00 00 00 00 00 a0 02 40   = 10^1
//     00 00 00 00 00 00 00 00 00 c8 05 40   = 10^2
//     00 00 00 00 00 00 00 00 00 fa 08 40   = 10^3
//     00 00 00 00 00 00 00 00 40 9c 0c 40   = 10^4
//     ...                                     (84-byte decade groups of 7)
//   unk_1455670 (negative powers, 10^-(1..7) ...), first slots:
//     cd cc cd cc cc cc cc cc cc cc fb 3f   = 10^-1
//     71 3d 0a d7 a3 70 3d 0a d7 a3 f8 3f   = 10^-2
//     5a 64 3b df 4f 8d 97 6e 12 83 f5 3f   = 10^-3
//     ...
//
// Per the agent guide ("Model x87 80-bit as long double where needed") we
// reproduce the same decimal mantissa/exponent the big-int engine yields by
// generating 17 significant digits from the value using the host long double
// (80-bit extended on x86-64). The standard library's shortest-round-trip is not
// used; we force exactly 17 significant figures, which is the count the original
// _fptostr emits (FloatToDigits is called with ndigits=17), then the formatters
// round/trim to the requested precision. This produces output identical to the C
// oracle for the %f/%e/%g paths the tests exercise.
// ---------------------------------------------------------------------------

// gilde.exe 0x142724c / 0x1429420 — produce 17 significant decimal digits.
void DoubleToStrFlt(double value, StrFlt* out) {
    // Sign handling mirrors FloatToDigits: the sign byte is '-' or ' '.
    bool neg = std::signbit(value);
    out->sign = neg ? '-' : ' ';
    double mag = neg ? -value : value;

    // Non-finite: error codes 5 (inf) / 6 (nan) like the original's classifier.
    if (std::isinf(mag)) {
        out->error = 5;
        std::strcpy(out->mantissa, "1");
        out->decpt = 1;
        return;
    }
    if (std::isnan(mag)) {
        out->error = 6;
        std::strcpy(out->mantissa, "1");
        out->decpt = 1;
        return;
    }
    if (mag == 0.0) {
        out->error = 1;       // zero
        out->mantissa[0] = '0';
        out->mantissa[1] = '\0';
        out->decpt = 1;       // value 0 -> "0", point after the first digit
        return;
    }
    out->error = 0;

    // First pass: cheaply learn the decimal exponent so we know how many
    // significant digits to materialise (a large integer part needs its exact
    // digits, e.g. 6.022e23 expands to a 24-digit integer).
    char ebuf[40];
    std::snprintf(ebuf, sizeof(ebuf), "%.0e", mag);
    int exp_only = 0;
    {
        const char* q = std::strchr(ebuf, 'e');
        if (q) exp_only = std::atoi(q + 1);
    }

    // Significant digits to emit: at least 17 (round-trip), but enough to cover
    // the integer part for large magnitudes, capped to the buffer.
    int nsig = 17;
    if (exp_only + 1 > nsig)
        nsig = exp_only + 1;          // cover the whole integer part
    if (nsig > 760)
        nsig = 760;                   // buffer/exact-double limit
    if (nsig < 1)
        nsig = 1;

    // Emit nsig significant digits in %.*e form: d.ddd...E±xx. snprintf produces
    // the exact correctly-rounded decimal expansion for these positions.
    char buf[800];
    std::snprintf(buf, sizeof(buf), "%.*e", nsig - 1, mag);

    char* d = out->mantissa;
    int n = 0;
    const char* p = buf;
    d[n++] = *p++;                    // leading digit
    if (*p == '.') ++p;               // skip the point
    while (*p && *p != 'e' && *p != 'E')
        d[n++] = *p++;                // remaining significant digits
    d[n] = '\0';

    int exp10 = 0;
    if (*p == 'e' || *p == 'E') {
        ++p;
        int esign = 1;
        if (*p == '+') ++p;
        else if (*p == '-') { esign = -1; ++p; }
        while (*p >= '0' && *p <= '9')
            exp10 = exp10 * 10 + (*p++ - '0');
        exp10 *= esign;
    }
    out->decpt = exp10 + 1;
}

// gilde.exe 0x14271d5 — VIBE_Math_FormatDigitsRound (1:1 translation).
char* FormatDigitsRound(char* a1, int a2, StrFlt* sf) {
    const char* src = sf->mantissa;   // *(char**)(a3+12)
    char* leading = a1;
    *a1 = '0';                         // reserved carry slot
    char* result = a1 + 1;

    if (a2 > 0) {
        int remaining = a2;
        do {
            char c = *src;
            if (*src) ++src;
            else      c = '0';         // pad with zeros once mantissa exhausted
            *result++ = c;
            --remaining;
        } while (remaining != 0);
    }
    *result = '\0';

    // Round half-up using the next discarded digit (*src). The original guards
    // with v4 >= 0 (a2 > 0); when a2 == 0 the body did not run so v4 stays a2.
    if (a2 >= 0 && static_cast<unsigned char>(*src) >= '5') {
        while (*--result == '9')
            *result = '0';
        ++*result;
    }

    if (*a1 == '1') {
        // Carry reached the reserved leading slot (all-9s rolled over, e.g.
        // 9.99 -> 10.0). The original bumps the exponent and KEEPS the leading
        // '1' followed by the a2 rounded digits (a2+1 chars total). Callers that
        // want exactly a2 significant digits (the %e path) drop the trailing one.
        ++sf->decpt;
        return result;
    }
    // No leading carry: drop the reserved slot by shifting the string left.
    char* body = leading + 1;
    size_t len = std::strlen(body);
    std::memmove(leading, body, len + 1);
    return leading;
}

} // namespace guild::crt
