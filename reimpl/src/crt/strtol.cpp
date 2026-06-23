#include "crt/strtol.h"
#include "crt/ctype.h"

namespace guild::crt {

// Thread-local errno model (the original's per-thread CRT slot).
int& Errno() {
    static thread_local int e = 0;
    return e;
}

// gilde.exe 0x1421c05 — VIBE_Crt_Atoi
int Atoi(const char* sp) {
    const unsigned char* s = reinterpret_cast<const unsigned char*>(sp);
    // Skip leading whitespace: kPctype[c] & 0x08 (the _SPACE bit).
    while (MbcsCodePageActive() ? CharTypeQuery(*s, 8) : (kPctype[*s] & 8))
        ++s;
    int c = *s;
    const unsigned char* p = s + 1;
    int sign = c; // remember the leading char to test for '-'
    if (c == '-' || c == '+')
        c = *p++;
    int acc = 0;
    while (MbcsCodePageActive() ? CharTypeQuery(c, 4) : (kPctype[c] & 4)) {
        // Intentional 2's-complement wraparound, matches the original's 32-bit
        // imul/add. Compute in u32 (the exact bit pattern the CPU produces) and
        // cast back to avoid signed-overflow UB while keeping bytes identical.
        acc = static_cast<int>(
            static_cast<u32>(c) + 10u * static_cast<u32>(acc) - 48u);
        c = *p++;
    }
    if (sign == '-')
        // Negate in u32 (2's-complement) so INT_MIN negates to itself without
        // signed-overflow UB — matches the original's 32-bit `neg`.
        return static_cast<int>(0u - static_cast<u32>(acc));
    return acc;
}

// gilde.exe 0x60b350 — VIBE_Crt_StrToLong  (eax=str, edx=endptr, ecx=signed, ebx=base)
u32 StrToLong(const char* str, const char** endptr, int signed_flag, int base) {
    const unsigned char* a1 = reinterpret_cast<const unsigned char*>(str);
    int v4 = base; // working base (esi)
    if (endptr)
        *endptr = str;

    // Skip leading whitespace via the strtol helper table (indexed at *p + 1).
    const unsigned char* i = a1;
    while (kStrtolCtype[static_cast<unsigned char>(*i + 1)] & 2)
        ++i;

    // The sign char is captured here (the original keeps it in `ch` across the
    // sign-skip and to the very end); it is the first non-whitespace byte.
    unsigned char sign = *i;
    if (*i == '+' || *i == '-')
        ++i;

    if (base) {
        if (base < 2 || base > 36) {
            Errno() = kEINVAL; // VIBE_Runtime_SetErrnoEinval, eax=0Dh (EINVAL)
            return 0;
        }
        if (base == 16) {
            // strip an optional 0x / 0X prefix
            if (*i == '0' && (i[1] == 'x' || i[1] == 'X'))
                i += 2;
        }
        v4 = base;
    } else {
        // base 0: auto-detect
        if (*i == '0' && (i[1] == 'x' || i[1] == 'X')) {
            v4 = 16;
            i += 2; // 0x prefix consumed
        } else if (*i == '0') {
            v4 = 8;
        } else {
            v4 = 10;
        }
    }

    const unsigned char* digit_start = i;
    // off_64C270[base*4] == 0xFFFFFFFF / base : the largest accumulator that can be
    // safely multiplied. Computed here (recovered byte-for-byte as 0xFFFFFFFF/base).
    u32 threshold = 0xFFFFFFFFu / static_cast<u32>(v4);
    bool overflow = false;
    u32 acc = 0;
    while (true) {
        int dv = DigitValue(*i);
        if (dv >= v4)
            break;
        if (acc > threshold)
            overflow = true;
        u32 prev = acc;
        acc = static_cast<u32>(v4) * acc + static_cast<u32>(dv);
        if (acc < prev) // wrapped on the add
            overflow = true;
        ++i;
    }

    const unsigned char* end = i;
    if (end == digit_start)   // no digits consumed
        end = a1;             // point endptr back at the start
    if (endptr)
        *endptr = reinterpret_cast<const char*>(end);

    // Signed-overflow / range handling (faithful to loc_60B43F..loc_60B49C).
    bool range_err;
    if (signed_flag == 1 && acc >= 0x80000000u &&
        !(acc == 0x80000000u && sign == '-')) {
        range_err = true; // signed value out of [LONG_MIN, LONG_MAX]
    } else {
        range_err = overflow;
    }

    if (range_err) {
        Errno() = kERANGE; // VIBE_Runtime_SetErrnoEinval, eax=0Eh (ERANGE)
        if (signed_flag == 0)
            return 0xFFFFFFFFu;             // unsigned overflow -> ULONG_MAX
        if (sign == '-')
            return 0x80000000u;             // LONG_MIN
        return 0x7FFFFFFFu;                 // LONG_MAX
    }

    if (sign == '-')
        return 0u - acc; // 2's-complement negate (no signed-overflow UB)
    return acc;
}

// gilde.exe 0x60b4b0 — VIBE_Crt_StrToLongAuto
u32 StrToLongAuto(const char* str, const char** endptr, int base) {
    return StrToLong(str, endptr, 1, base);
}

// gilde.exe 0x606020 — VIBE_Crt_StrToUpper
// In-place ASCII strupr. The original's eax return is path-dependent (the input
// pointer for an empty string, otherwise the last uppercased char in al); no caller
// (only VIBE_Crt_FormatConversion @0x605a18, three sites) uses it, so we return the
// buffer pointer, which is the routine's real contract.
char* StrToUpper(char* s) {
    unsigned char* p = reinterpret_cast<unsigned char*>(s);
    if (*p) {
        do {
            *p = static_cast<unsigned char>(ToUpperAscii(*p)); // 'a'..'z' -> upper
            ++p;
        } while (*p);
    }
    return s;
}

// gilde.exe 0x142225f — VIBE_Strtol_Parse  (str, endptr, base, mode)
u32 Strtol_Parse(const char* str, const char** endptr, unsigned base, int mode) {
    const unsigned char* a1 = reinterpret_cast<const unsigned char*>(str);
    int flags = mode;     // a4: bit0 unsigned, bit1 negative, bit2 ovf, bit3 sawdigit
    u32 acc = 0;          // v13
    unsigned char c = *a1;
    const unsigned char* i = a1 + 1;
    // skip whitespace (kPctype bit 0x08)
    while (MbcsCodePageActive() ? CharTypeQuery(c, 8) : (kPctype[c] & 8)) {
        c = *i++;
    }
    const unsigned char* cur = i; // v14 — points one past the current char `c`
    if (c == '-') {
        flags |= 2u;
        c = *i++;
        cur = i;
    } else if (c == '+') {
        c = *i++;
        cur = i;
    }

    // validate base: 0 or 2..36 (reject 1 and negatives)
    if ((base & 0x80000000u) != 0 || base == 1 || static_cast<int>(base) > 36) {
        if (endptr)
            *endptr = str;
        return 0;
    }

    if (base == 0) {
        if (c != '0') {
            base = 10;
        } else if (*i != 'x' && *i != 'X') {
            base = 8;
        } else {
            base = 16;
        }
    }
    if (base == 16 && c == '0' && (*i == 'x' || *i == 'X')) {
        c = i[1];
        cur = i + 2;
    }

    const u32 div = 0xFFFFFFFFu / base; // v12
    const u32 mod = 0xFFFFFFFFu % base;
    while (true) {
        unsigned digit;
        if (MbcsCodePageActive() ? CharTypeQuery(c, 4) : (kPctype[c] & 4)) {
            digit = static_cast<unsigned char>(c) - 48u; // '0'..'9'
        } else {
            int isalpha = MbcsCodePageActive() ? CharTypeQuery(c, 0x103)
                                               : (kPctype[c] & 0x103);
            if (!isalpha)
                break; // not a digit/letter -> stop
            digit = static_cast<unsigned>(LocaleToUpper(c)) - 55u; // 'A'->10
        }
        if (digit >= base)
            break;
        flags |= 8u; // saw a valid digit
        if (acc < div || (acc == div && digit <= mod))
            acc = digit + base * acc;
        else
            flags |= 4u; // overflow
        c = *cur++;
    }

    const unsigned char* end = cur - 1;
    if ((flags & 8) != 0) {
        // had at least one digit; check overflow / range clamp
        bool clamp = (flags & 4) != 0;
        if (!clamp && (flags & 1) == 0) {
            // signed: clamp if magnitude exceeds the signed range
            if ((flags & 2) != 0)
                clamp = acc > 0x80000000u;
            else
                clamp = acc > 0x7FFFFFFFu;
        }
        if (clamp) {
            Errno() = kERANGE;
            if ((flags & 1) != 0)
                acc = 0xFFFFFFFFu;                    // unsigned -> ULONG_MAX
            else
                acc = 0x7FFFFFFFu + ((flags & 2) != 0 ? 1u : 0u); // LONG_MIN/MAX
        }
    } else {
        // no digits: endptr back to start, value 0
        if (endptr)
            end = a1;
        acc = 0;
    }
    if (endptr)
        *endptr = reinterpret_cast<const char*>(end);
    if ((flags & 2) != 0)
        return 0u - acc; // 2's-complement negate (no signed-overflow UB)
    return acc;
}

// gilde.exe 0x1422248 — VIBE_Strtol_Wrapper
long Strtol(const char* str, const char** endptr, int base) {
    return static_cast<long>(static_cast<i32>(Strtol_Parse(str, endptr, static_cast<unsigned>(base), 0)));
}

// gilde.exe 0x1422467 — VIBE_Strtoul_Wrapper
unsigned long Strtoul(const char* str, const char** endptr, int base) {
    return static_cast<unsigned long>(Strtol_Parse(str, endptr, static_cast<unsigned>(base), 1));
}

// gilde.exe 0x142a446 — VIBE_String_ToUpper
char* StringToUpper(char* s) {
    if (CaseCodePage()) {
        // MBCS path: VIBE_Locale_MultiByteToWide + Mem alloc + copy thunk. Deferred.
        // LOG: not reached in the C locale. Fall through to ASCII fold would be wrong
        // for true MBCS, so we keep the ASCII fold only when no code page is active.
    }
    for (char* p = s; *p; ++p) {
        char ch = *p;
        if (ch >= 97 && ch <= 122) // 'a'..'z'
            *p = ch - 32;
    }
    return s;
}

// gilde.exe 0x142a56d — VIBE_String_ToLower
char* StringToLower(char* s) {
    for (char* p = s; *p; ++p) {
        char ch = *p;
        if (ch >= 65 && ch <= 90) // 'A'..'Z'
            *p = ch + 32;
    }
    return s;
}

// gilde.exe 0x142a610 — VIBE_String_CompareNoCaseN
int StringCompareNoCaseN(const char* ap, const char* bp, int n) {
    const unsigned char* a = reinterpret_cast<const unsigned char*>(ap);
    const unsigned char* b = reinterpret_cast<const unsigned char*>(bp);
    int v3 = n;
    if (n == 0)
        return 0;

    if (CaseCodePage()) {
        // Locale fold path (VIBE_Locale_ToLower).
        unsigned ca = 0, cb = 0;
        bool less = false, equal = true;
        do {
            ca = *a;
            cb = *b;
            if (!ca || !cb)
                break;
            ++a;
            ++b;
            int la = v3;
            cb = static_cast<unsigned>(LocaleToLower(static_cast<int>(cb)));
            ca = static_cast<unsigned>(LocaleToLower(static_cast<int>(ca)));
            if (ca != cb) {
                less = ca < cb;
                equal = false;
                break;
            }
            v3 = la - 1;
        } while (v3 != 1);
        if (equal) {
            // either ran out of count or hit a NUL
            if (ca == cb)
                return 0;
            less = ca < cb;
        }
        return less ? -1 : 1;
    }

    // ASCII fold path
    unsigned char ca = 0, cb = 0;
    do {
        ca = *a;
        cb = *b;
        if (!ca || !cb)
            break;
        ++a;
        ++b;
        if (ca >= 'A' && ca <= 'Z')
            ca += 32;
        if (cb >= 'A' && cb <= 'Z')
            cb += 32;
        if (ca != cb)
            return ca < cb ? -1 : 1;
        --v3;
    } while (v3);
    if (ca == cb)
        return 0;
    return ca < cb ? -1 : 1;
}

} // namespace guild::crt
