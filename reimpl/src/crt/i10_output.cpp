#include "crt/i10_output.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// The 8-digit emitter (VIBE_Text_FormatDigitPair3 @0x5faad1) is recovered once in
// guild::gui::text; reuse it (forward-declare + link) rather than redefining (ODR).
namespace guild::gui::text {
void FormatDigitPair3(guild::u32 value, char*& out);
}

namespace guild::crt {

// ---------------------------------------------------------------------------
// Provenance of the recovered scaling machinery (modeled, see below).
//
// VIBE_Math_FloatToDigits classifies an x87 80-bit extended value via
// VIBE_Math_ClassifyLongDouble @0x606237 (0=zero, 1=normal, 2=nan, 3=inf,
// 4=denormal), scales it toward [1, 1e8) with VIBE_Math_ScaleExponentClamped
// @0x5fa7a3 -> ScaleByPowerOfTen @0x5fa722 -> PowerOfTenScale @0x5fa6c5, then
// repeatedly extracts 8 decimal digits at a time with VIBE_Text_FormatDigitPair3
// (a chain of 16-bit DIVs) into a scratch buffer, strips leading zeros, and rounds
// to the requested significant-digit count.
//
// PowerOfTenScale walks the binary-exponentiation power-of-ten table:
//
//   unk_64A9C0 — array of 10-byte x87 extended floats 10^(2^k):
//     00 00 00 00 00 00 00 a0 02 40  = 1e1
//     00 00 00 00 00 00 00 c8 05 40  = 1e2
//     00 00 00 00 00 40 9c 0c 40     = 1e4
//     00 00 00 04 bf c9 1b 8e 34 40  = 1e16
//     ...                              (1e32, 1e64, ... up to 1e4096)
//
// Per the agent guide ("model x87 80-bit as long double where needed") we
// reproduce the same observable result the big-int extraction yields — the
// correctly-rounded significant-digit string and the decimal exponent — using the
// host long double, then run the byte-faithful FormatFixedPoint / FormatExponent
// layout below. The two layout routines are translated 1:1 from the original.
// ---------------------------------------------------------------------------

// gilde.exe 0x5fac3f — VIBE_Text_FormatFixedPoint (1:1).
i32 FormatFixedPoint(I10Control* a1, const char* a2, i32 a3, i32 a4, char* a5) {
    int v15 = a1->precision;          // *(_DWORD *)a1
    int v12 = a3 + 1;                 // integer-part digit count (decExp + 1)
    int v5 = 0;
    int v7 = 0;
    int result = 0;

    if ((a1->flags & 4) != 0) {       // %e/%g caps the fraction budget
        if (a4 < v15 && (a1->flags & 0x10) == 0)
            v15 = a4;
        v15 -= v12;
        if (v15 < 0)
            v15 = 0;
    }

    if (v12 > 0) {
        if (a4 >= v12) {
            std::memcpy(a5, a2, static_cast<std::size_t>(v12));
            int v9 = a1->flags;
            int v10 = v12;
            int v11 = a4 - v12;
            a1->outLen0 = v12;
            if ((v9 & 8) != 0) {                 // strip ('g' without '#')
                if (*a5 == '0')
                    a1->outLen0 = 0;
            } else if (v15 > 0 || (v9 & 0x10) != 0) {
                v10 = v12 + 1;
                a5[v12] = '.';
            }
            if (v11 > v15)
                v11 = v15;
            result = v11;
            std::memcpy(&a5[v10], &a2[v12], static_cast<std::size_t>(v11));
            v7 = v11 + v10;
            a1->outLen1 = v7;
            a1->outLen2 = v15 - v11;
        } else {
            result = a4;
            std::memcpy(a5, a2, static_cast<std::size_t>(a4));
            a1->outLen1 = a4;
            v7 = a4;
            a1->outLen2 = v12 - a4;
            int v8 = a1->flags;
            a1->outLen0 = v12;
            if ((v8 & 8) == 0 && (v15 > 0 || (v8 & 0x10) != 0)) {
                a5[a4] = '.';
                v7 = a4 + 1;
                a1->outLen3 = 1;
            }
            a1->outLen4 = v15;
        }
    } else {
        // |value| < 1: a leading "0" then "0...digits" after the point.
        if ((a1->flags & 8) == 0) {
            v5 = 1;
            *a5 = '0';
            if (v15 > 0 || (a1->flags & 0x10) != 0) {
                v5 = 2;
                a5[1] = '.';
            }
        }
        a1->outLen1 = v5;
        if (-v12 > v15)
            v12 = -v15;
        a1->outLen0 = v12;
        a1->outLen2 = v12;
        a1->outLen2 = -v12;            // original overwrites: stores -v12
        if (a4 > v12 + v15)
            a4 = v12 + v15;
        result = a4;
        std::memcpy(&a5[v5], a2, static_cast<std::size_t>(a4));
        a1->outLen3 = a4;
        v7 = a4 + v5;
        a1->outLen4 = v12 + v15 - a4;
    }
    a5[v7] = 0;
    return result;
}

// gilde.exe 0x5fae5d — VIBE_Text_FormatExponent (1:1). The original emits runs of
// the fill char '0' via VIBE_Light_SetGrayColorThunk(48, n) which (in this digit
// context) writes `n` '0' bytes at the running cursor; we inline that as memset.
void FormatExponent(I10Control* a1c, const char* a2, i32 a3, i32 a4, char* a5) {
    int* a1 = reinterpret_cast<int*>(a1c);
    int v17 = a4;
    int v20 = a1[0];                  // precision
    int v5 = a1[1];                   // secondary count
    int v21;
    if (v5 > 0)
        v21 = v20 - v5 + 1;
    else
        v21 = v5 + v20;
    if ((a1[2] & 4) != 0) {
        if (a4 < v21)
            v21 = a4;
        if (--v21 < 0)
            v21 = 0;
    }

    int v28;
    int v6 = a1[1];
    if (v6 > 0) {
        int v22 = a1[1];
        if (v6 > a4)
            v22 = a4;
        std::memcpy(a5, a2, static_cast<std::size_t>(v22));
        v28 = v22;
        a2 += v22;
        int v7 = a1[1];
        v17 -= v22;
        if (v22 < v7) {
            int v23 = v7 - v22;
            std::memset(&a5[v28], '0', static_cast<std::size_t>(v23));
            v28 += v23;
        }
    } else {
        v28 = 1;
        *a5 = '0';
    }
    a1[6] = v28;

    if ((a1[2] & 8) == 0 && (v21 > 0 || (a1[2] & 0x10) != 0)) {
        a5[v28++] = '.';
    }
    if (a1[1] < 0) {
        int v24 = -a1[1];
        std::memset(&a5[v28], '0', static_cast<std::size_t>(v24));
        v28 += v24;
    }
    if (v21 > 0) {
        if (v21 < v17)
            v17 = v21;
        if (v17) {
            std::memcpy(&a5[v28], a2, static_cast<std::size_t>(v17));
            v28 += v17;
        }
        a1[7] = v28;
        a1[8] = v21 - v17;
    }
    if (a1[3]) {
        a5[v28++] = *(reinterpret_cast<char*>(a1) + 12);   // exponent marker
    }

    int v11 = v28;
    int v29 = v28 + 1;
    if (a3 < 0) {
        a3 = -a3;
        a5[v11] = '-';
    } else {
        a5[v11] = '+';
    }

    int v12 = a1[4];                  // exponent digit-count mode
    switch (v12) {
        case 0:
            if (a3 >= 1000) { v12 = 4; break; }
            v12 = 3;
            break;
        case 1:
            if (a3 >= 10)  v12 = 2;
            if (a3 >= 100) v12 = 3;
            if (a3 >= 1000) v12 = 4;
            break;
        case 2:
            if (a3 >= 100) v12 = 3;
            if (a3 >= 1000) v12 = 4;
            break;
        case 3:
            if (a3 >= 1000) v12 = 4;
            break;
        default:
            break;
    }
    a1[4] = v12;

    if (v12 >= 4) {
        int d = 0;
        if (a3 >= 1000) { d = a3 / 1000; a3 %= 1000; }
        a5[v29++] = static_cast<char>(d + '0');
    }
    if (v12 >= 3) {
        int d = 0;
        if (a3 >= 100) { d = a3 / 100; a3 %= 100; }
        a5[v29++] = static_cast<char>(d + '0');
    }
    if (v12 >= 2) {
        int d = 0;
        if (a3 >= 10) { d = a3 / 10; a3 %= 10; }
        a5[v29++] = static_cast<char>(d + '0');
    }
    a5[v29] = static_cast<char>(a3 + '0');
    a1[9] = v29 + 1 - a1[7];
    a5[v29 + 1] = 0;
}

// gilde.exe 0x6060be — VIBE_Crt_FormatExponentFloat.
char* FormatExponentFloat(char conv, int precision, bool alt, long double value,
                          char* dst) {
    I10Control ctrl;
    std::memset(&ctrl, 0, sizeof(ctrl));

    int letter = conv & 0x5F;               // fold to uppercase (v5 & 0x5F)
    ctrl.expMarker = static_cast<unsigned char>(conv);   // +0x0C marker char
    int prec = precision;
    if (letter == 'G') {                    // %g/%G
        if (!prec) prec = 1;
        ctrl.flags = 4;
        ctrl.extraCount = 1;
        ctrl.expMarker -= 2;                // 'g'->'e', 'G'->'E' (v18 -= 2)
    } else if (letter == 'E') {             // %e/%E
        ctrl.flags = 1;
        ctrl.extraCount = 1;
    } else {                                // %f/%F
        ctrl.flags = 2;
        ctrl.extraCount = 0;
    }
    if (alt)
        ctrl.flags |= 0x10;                 // '#'
    if (prec == -1)
        prec = 6;
    ctrl.precision = prec;

    // Produce the unsigned field at dst+1, then prepend the sign (or drop the slot).
    FloatToDigits(&value, &ctrl, dst + 1);
    if (ctrl.sign < 0) {
        dst[0] = '-';
        return dst;
    }
    std::memmove(dst, dst + 1, std::strlen(dst + 1) + 1);
    return dst;
}

// ---------------------------------------------------------------------------
// Digit generation (the scaling+extraction core of FloatToDigits, modeled).
//
// Produces `out` = up to 21 significant decimal digits of |value| (no point/sign)
// and *decExp = the position of the decimal point relative to the first digit
// minus one (i.e. decExp == 0 means d.dddd). Matches the v15/v16 the original's
// loop hands to FormatFixedPoint/FormatExponent.
// ---------------------------------------------------------------------------
namespace {

void GenerateDigits(long double mag, int wantDigits, char* out, int* ndigits,
                    int* decExp) {
    if (wantDigits < 1) wantDigits = 1;
    if (wantDigits > 21) wantDigits = 21;   // cap mirrors the original's v10 limits

    // Decimal exponent of the leading digit (floor(log10(mag))).
    char ebuf[48];
    std::snprintf(ebuf, sizeof(ebuf), "%.*Le", wantDigits - 1, mag);

    // ebuf is d.ddd...e±xx — copy the significant digits, capture the exponent.
    char* d = out;
    int n = 0;
    const char* p = ebuf;
    d[n++] = *p++;                          // leading digit
    if (*p == '.') ++p;
    while (*p && *p != 'e' && *p != 'E')
        d[n++] = *p++;
    d[n] = '\0';

    int exp10 = 0;
    if (*p == 'e' || *p == 'E') {
        ++p;
        int es = 1;
        if (*p == '+') ++p;
        else if (*p == '-') { es = -1; ++p; }
        while (*p >= '0' && *p <= '9')
            exp10 = exp10 * 10 + (*p++ - '0');
        exp10 *= es;
    }
    *ndigits = n;
    *decExp = exp10;
}

} // namespace

// gilde.exe 0x5fa7fd — VIBE_Math_FloatToDigits.
i32 FloatToDigits(const long double* value, I10Control* ctrl, char* dst) {
    ctrl->sign = 0;                         // *(a2+20) = 0
    ctrl->outLen0 = 0;
    ctrl->outLen1 = 0;
    ctrl->outLen2 = 0;
    ctrl->outLen3 = 0;
    ctrl->outLen4 = 0;

    long double v = *value;
    if (std::signbit(v))
        ctrl->sign = -1;                    // (v34 & 0x8000) sign bit
    long double mag = std::signbit(v) ? -v : v;

    // ClassifyLongDouble: 0/4 => (sub)normal-zero, 1 => normal, 2 => nan, 3 => inf.
    if (std::isnan(mag)) {
        dst[0] = 'n'; dst[1] = 'a'; dst[2] = 'n'; dst[3] = '\0';
        ctrl->outLen1 = 3;                  // *(a2+28) = 3
        return 0;
    }
    if (std::isinf(mag)) {
        dst[0] = 'i'; dst[1] = 'n'; dst[2] = 'f'; dst[3] = '\0';
        ctrl->outLen1 = 3;
        return 0;
    }

    // Significant-digit budget. The original derives v39 from the precision/flags
    // (fixed vs sci) then caps it at 15+4 / 20+4 / 40+4 by the L/double-wide flags.
    int prec = ctrl->precision;
    int v39;
    if ((ctrl->flags & 2) != 0) {           // fixed (%f): digits left of point + prec
        v39 = ctrl->precision + 10;         // + decExp added below via budget
        if (ctrl->extraCount > 0)
            v39 += ctrl->extraCount;
    } else {
        v39 = ctrl->precision + 7;
    }
    int cap = 15;
    if ((ctrl->flags & 0x20) != 0) cap = 20;
    if ((ctrl->flags & 0x40) != 0) cap *= 2;
    cap += 4;
    if (cap < v39) v39 = cap;
    if (v39 < 1) v39 = 1;
    (void)prec;

    int ndigits = 0;
    int decExp = 0;
    if (mag == 0.0L) {
        dst[0] = '0';
        // decExp stays 0; below dispatches with the single '0' digit.
        char zero[2] = {'0', '\0'};
        int v18 = 1;
        int v16 = 0;
        // Choose fixed vs sci exactly as the original's final branch.
        unsigned f = static_cast<unsigned>(ctrl->flags);
        if ((f & 2) != 0 ||
            ((f & 4) != 0 && ((v16 >= -4 && v16 < ctrl->precision) || (f & 8) != 0)))
            FormatFixedPoint(ctrl, zero, v16, v18, dst);
        else
            FormatExponent(ctrl, zero, v16, v18, dst);
        return 0;
    }

    char digits[64];
    GenerateDigits(mag, v39, digits, &ndigits, &decExp);

    // Strip leading zeros (the original's `while (*v15 == 48)` loop). %Le never
    // emits a leading zero for nonzero mag, but mirror the logic for fidelity.
    const char* v15 = digits;
    int v14 = ndigits;
    int v16 = decExp;                       // running decimal exponent
    while (*v15 == '0' && v14 > 1) { --v14; --v16; ++v15; }

    // Determine the downstream decimal exponent v16 and digit count v18 exactly as
    // the original's fix-up at 0x5fab42..0x5fabda (extra = ctrl->extraCount, the
    // leading mantissa-digit count for %e/%g, 0 for %f).
    int extra = ctrl->extraCount;
    int v18 = ctrl->precision;
    if ((ctrl->flags & 2) != 0) {           // fixed (%f)
        v16 += extra;
        v18 += v16 + 1;
    } else if ((ctrl->flags & 1) != 0) {    // scientific (%e/%E)
        if (extra <= 0) v18 += extra;
        else            ++v18;
        v16 = v16 + 1 - extra;
    }                                       // else %g: v18 = precision, v16 raw
    if (v18 < 0) v18 = 0;
    if (v18 > v14) v18 = v14;
    {                                       // cap to v20+1 by the L/wide flags
        int v20 = 15;
        if ((ctrl->flags & 0x20) != 0) v20 = 20;
        if ((ctrl->flags & 0x40) != 0) v20 *= 2;
        if (v18 > v20) v18 = v20 + 1;
    }

    // Round at v18 using the next discarded digit (the original rounds the tail
    // run, propagating an all-9s carry which can grow decExp).
    char rbuf[64];
    int rn = v18;
    std::memcpy(rbuf, v15, static_cast<std::size_t>(rn));
    rbuf[rn] = '\0';
    if (v14 > v18 && static_cast<unsigned char>(v15[v18]) >= '5') {
        int i = rn - 1;
        while (i >= 0 && rbuf[i] == '9') { rbuf[i] = '0'; --i; }
        if (i < 0) {
            // All-9s rolled over (e.g. 999 -> 1000): prepend the carry '1' and bump
            // the decimal exponent, then keep exactly v18 significant digits.
            std::memmove(rbuf + 1, rbuf, static_cast<std::size_t>(rn) + 1);
            rbuf[0] = '1';
            ++rn;
            ++v16;
            if (rn > v18 && v18 > 0) { rbuf[v18] = '\0'; rn = v18; }
        } else {
            ++rbuf[i];
        }
    }
    if (rn <= 0) { rbuf[0] = '0'; rbuf[1] = '\0'; rn = 1; v16 = 0; }

    // Final dispatch (0x5fabf4): fixed if %f, or %g whose exponent is in range.
    unsigned f = static_cast<unsigned>(ctrl->flags);
    if ((f & 2) != 0 ||
        ((f & 4) != 0 && ((v16 >= -4 && v16 < ctrl->precision) || (f & 8) != 0)))
        FormatFixedPoint(ctrl, rbuf, v16, rn, dst);
    else
        FormatExponent(ctrl, rbuf, v16, rn, dst);
    return 0;
}

} // namespace guild::crt
