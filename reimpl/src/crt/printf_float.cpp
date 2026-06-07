#include "crt/printf_float.h"
#include "crt/dtoa.h"

#include <cstring>

namespace guild::crt {

namespace {

// VIBE_String_ShiftRight @0x1422b1a — move the NUL-terminated string at `s`
// right by `n` bytes (making room), preserving the terminator.
void ShiftRight(char* s, int n) {
    if (n) {
        size_t len = std::strlen(s);
        std::memmove(s + n, s, len + 1);
    }
}

// Module state mirroring the original's locale globals used by the General path
// to cache the converted digits across the Fixed/Exponential sub-call:
//   dword_145A24C — cached StrFlt          (g_cached)
//   dword_145A254 — cached (decpt - 1)     (g_decptm1)
//   byte_145A258  — "rounding grew decpt"  (g_grew)
//   byte_145A250  — "reuse cached digits"  (g_reuse)
StrFlt* g_cached  = nullptr;
int     g_decptm1 = 0;
bool    g_grew    = false;
bool    g_reuse   = false;

// gilde.exe 0x142275e — VIBE_FloatFormat_TrimZeros.  Strip trailing zeros (and a
// trailing decimal point) from the fractional part, leaving any exponent intact.
void TrimZeros(char* a1) {
    char* v1 = a1;
    // Find the decimal point (or stop at NUL).
    while (*v1 && *v1 != kDecimalPoint)
        ++v1;
    if (!*v1)
        return;                       // no point: nothing to trim
    char* tail = v1 + 1;
    // Find the end of the fraction (NUL or exponent marker).
    char* result = tail;
    while (*result && *result != 'e' && *result != 'E')
        ++result;
    char* exp = result;               // start of exponent (or NUL)
    // Walk back over trailing zeros.
    do { --result; } while (*result == '0');
    if (*result == kDecimalPoint)
        --result;                     // also drop the point if no digits remain
    // Shift the exponent (if any) up against the trimmed fraction.
    ++result;
    char* w = result;
    do {
        char c = *exp++;
        *w++ = c;
    } while (w[-1]);
}

// Core %f over an already-converted StrFlt. When `reuse` the `prec` significant
// digits are already laid down at dst[sign]; otherwise we convert+round here.
char* FixedImpl(StrFlt& sf, char* a2, int a3, bool reuse) {
    int v3 = a3;
    int sign_off = (sf.sign == '-') ? 1 : 0;
    if (reuse) {
        // Digits already present (placed by General). The original appends a '0'
        // when the integer part exactly fills the requested significant digits.
        if (g_decptm1 == a3) {
            char* z = a2 + sign_off + sf.decpt;
            *z = '0';
            z[1] = '\0';
        }
    } else {
        FormatDigitsRound(a2 + sign_off, a3 + sf.decpt, &sf);
    }

    char* v6 = a2;
    if (sf.sign == '-') {
        *a2 = '-';
        v6 = a2 + 1;
    }

    char* v8;
    int v7 = sf.decpt;
    if (v7 > 0) {
        v8 = v6 + v7;
    } else {
        ShiftRight(v6, 1);
        *v6 = '0';
        v8 = v6 + 1;
    }

    if (v3 > 0) {
        ShiftRight(v8, 1);
        *v8 = kDecimalPoint;
        int v9 = sf.decpt;
        char* v10 = v8 + 1;
        if (v9 < 0) {
            int v11 = -v9;
            if (!reuse && v3 < v11)
                v11 = v3;             // clamp leading zeros to the precision (%f)
            ShiftRight(v10, v11);
            std::memset(v10, '0', static_cast<size_t>(v11));
        }
    }
    return a2;
}

char* ExpImpl(StrFlt& sf, char* a2, int a3, bool upper, bool reuse) {
    int v4 = a3;
    int sign_off = (sf.sign == '-') ? 1 : 0;
    if (reuse) {
        // Make room for the point after the leading digit (digits already there).
        ShiftRight(a2 + sign_off, a3 > 0 ? 1 : 0);
    } else {
        char* d = a2 + sign_off + (a3 > 0 ? 1 : 0);
        FormatDigitsRound(d, a3 + 1, &sf);
        // An all-9s carry (e.g. 9.999 -> 10.00) renormalises to a3+2 digits;
        // %e keeps exactly a3+1 significant digits, so drop the trailing one.
        if (static_cast<int>(std::strlen(d)) > a3 + 1)
            d[a3 + 1] = '\0';
    }

    char* v6 = a2;
    if (sf.sign == '-') {
        *a2 = '-';
        v6 = a2 + 1;
    }
    if (v4 > 0) {
        v6[0] = v6[1];
        v6[1] = kDecimalPoint;
    }

    char* end = v6 + std::strlen(v6);
    *end = upper ? 'E' : 'e';

    int exp = sf.decpt - 1;
    end[1] = '+';
    if (sf.mantissa[0] == '0')
        exp = 0;                      // value rounded to zero -> e+00
    if (exp < 0) {
        exp = -exp;
        end[1] = '-';
    }
    char e2 = '0', e3, e4;
    if (exp >= 100) { e2 = static_cast<char>('0' + exp / 100); exp %= 100; }
    e3 = static_cast<char>('0' + exp / 10);
    e4 = static_cast<char>('0' + exp % 10);
    char* w = end + 2;
    if (e2 != '0')
        *w++ = e2;                    // C prints 3 exponent digits only if needed
    *w++ = e3;
    *w++ = e4;
    *w = '\0';
    return a2;
}

} // namespace

// gilde.exe 0x1422906 — VIBE_FloatFormat_Fixed
char* FormatFixed(double value, char* dst, int precision) {
    if (g_reuse && g_cached)
        return FixedImpl(*g_cached, dst, precision, true);
    StrFlt sf;
    DoubleToStrFlt(value, &sf);
    return FixedImpl(sf, dst, precision, false);
}

// gilde.exe 0x1422802 — VIBE_FloatFormat_Exponential
char* FormatExponential(double value, char* dst, int precision, bool upper) {
    if (g_reuse && g_cached)
        return ExpImpl(*g_cached, dst, precision, upper, true);
    StrFlt sf;
    DoubleToStrFlt(value, &sf);
    return ExpImpl(sf, dst, precision, upper, false);
}

// gilde.exe 0x14229e4 — VIBE_FloatFormat_General
char* FormatGeneral(double value, char* a2, int a3, bool upper) {
    StrFlt sf;
    DoubleToStrFlt(value, &sf);
    int prec = a3;
    if (prec == 0) prec = 1;          // %g: precision 0 means 1

    g_cached = &sf;
    g_decptm1 = sf.decpt - 1;

    int sign_off = (sf.sign == '-') ? 1 : 0;
    char* digits = a2 + sign_off;
    FormatDigitsRound(digits, prec, &sf);

    // Did rounding push the decimal point right (e.g. 9.99 -> 10.0)?
    g_grew = (g_decptm1 < sf.decpt - 1);
    g_decptm1 = sf.decpt - 1;

    char* result;
    if (g_decptm1 < -4 || g_decptm1 >= prec) {
        // Exponential form, reusing the cached digits.
        g_reuse = true;
        result = FormatExponential(value, a2, prec, upper);
        g_reuse = false;
    } else {
        // (FormatDigitsRound already renormalises an all-9s carry, so the digit
        // string holds exactly `prec` significant digits here; g_grew is tracked
        // only to mirror the original's byte_145A258 bookkeeping.)
        (void)g_grew;
        g_reuse = true;
        result = FormatFixed(value, a2, prec);
        g_reuse = false;
    }
    g_cached = nullptr;

    // %g without '#': strip trailing zeros.
    TrimZeros(a2);
    return result;
}

// gilde.exe 0x1422ac9 — VIBE_FloatFormat_Dispatch
char* FormatDispatch(double value, char* dst, int conv, int precision) {
    if (conv == 'e' || conv == 'E')
        return FormatExponential(value, dst, precision, conv == 'E');
    if (conv == 'f' || conv == 'F')
        return FormatFixed(value, dst, precision);
    return FormatGeneral(value, dst, precision, conv == 'G');
}

// gilde.exe 0x605848 — VIBE_Crt_FormatFixedFloat (16.16 fixed-point, NOT IEEE).
void FormatFixed16_16(char* dst, i32 fixed, int precision) {
    char* p = dst;
    u32 v15;
    if (fixed < 0) {
        *p++ = '-';
        v15 = static_cast<u32>(-fixed);
    } else {
        v15 = static_cast<u32>(fixed);
    }
    if (precision < 0) precision = 4;

    u32 ip = static_cast<u32>(v15 >> 16);
    char tmp[16];
    int tn = 0;
    do { tmp[tn++] = static_cast<char>('0' + ip % 10); ip /= 10; } while (ip);
    for (int i = 0; i < tn; ++i) p[i] = tmp[tn - 1 - i];
    char* end = p + tn;

    if (precision) {
        *end++ = kDecimalPoint;
        u32 frac = v15 & 0xFFFF;
        for (int i = 0; i < precision; ++i) {
            frac = 10 * (frac & 0xFFFF);
            *end++ = static_cast<char>(((frac >> 16) & 0xFF) + '0');
        }
        *end = '\0';
    }
}

} // namespace guild::crt
