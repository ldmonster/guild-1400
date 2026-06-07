#include "crt/string.h"

// Translations of the game's string/memory routines (namespace guild::crt).
//
// DEFERRED to other agents (depend on Locale / Mbcs / Mem / Math / Runtime modules
// or on platform segment trickery — NOT pure string/mem/char):
//   0x142a446 VIBE_String_ToUpper          (Locale_MultiByteToWide + Mem alloc/free)
//   0x142a56d VIBE_String_ToLower          (Locale_MultiByteToWide + Mem alloc/free)
//   0x142a610 VIBE_String_CompareNoCaseN   (Locale_ToLower + dword_145A3C4)
//   0x142a398 VIBE_String_FindCharMbcs     (Mbcs lead-byte tables)
//   0x609e30  VIBE_String_MbsCompareN      (Mbcs_AdvanceChar/IsStringEnd/CompareTwoChars)
//   0x1422b1a VIBE_String_ShiftRight       (Util_MemMove_27370)
//   0x142a40b VIBE_String_Duplicate        (Mem_Alloc + String copy thunk)
//   0x609550  VIBE_String_UInt64ToString   (Math_UnsignedLongLongDivide)
//   0x6095e8  VIBE_String_Int64ToString    (-> UInt64ToString)
//   0x60a130  VIBE_String_WideToBytesBuffer / 0x609510 VIBE_String_WideCharToBytes (Locale)
//   0x1421c05 VIBE_Crt_Atoi                / 0x1421c9b VIBE_Crt_CharTypeQuery (Locale tables)
//   0x60b350  VIBE_Crt_StrToLong / 0x60b4b0 StrToLongAuto / 0x60b4c0 DigitValue
//             (Runtime_SetErrnoEinval + Util_ToLower + byte_64A208 ctype table)
//   0x605754  VIBE_Crt_StrNLen             (MK_FP far-pointer form)
//   0x606020  VIBE_Crt_StrToUpper          (Util_CharToUpper)
// (All other VIBE_Crt_* are printf/scanf/file-IO/time/exception/TLS/startup/heap and
//  belong to other agents.)

namespace guild::crt {

// gilde.exe 0x1421d10 — VIBE_String_Length  (MSVC word-at-a-time strlen)
// Faithful clone of strlen; behavior verified against std::strlen.
u32 StringLength(const char* s) {
    const char* p = s;
    while (*p)
        ++p;
    return static_cast<u32>(p - s);
}

// gilde.exe 0x1421ef0 — VIBE_String_Concat  (entry/thunk 0x1421ee0 -> 0x1421f51)
// MSVC word-at-a-time strcat. Returns dst.
char* StringConcat(char* dst, const char* src) {
    char* d = dst;
    while (*d)
        ++d;
    while ((*d = *src) != 0) {
        ++d;
        ++src;
    }
    return dst;
}

// gilde.exe 0x14220c0 — VIBE_String_Compare  (MSVC word-at-a-time strcmp)
// Returns the sign (-1/0/+1) of the first differing unsigned byte, like the original
// (-2*carry + 1). Equivalent in sign to std::strcmp.
int StringCompare(const char* a, const char* b) {
    const unsigned char* pa = reinterpret_cast<const unsigned char*>(a);
    const unsigned char* pb = reinterpret_cast<const unsigned char*>(b);
    while (*pa == *pb) {
        if (*pa == 0)
            return 0;
        ++pa;
        ++pb;
    }
    return (*pa < *pb) ? -1 : 1;
}

// gilde.exe 0x1422490 — VIBE_String_FindChar  (MSVC word-at-a-time strchr)
// Returns pointer to first byte equal to c (c==0 matches the NUL), else nullptr.
char* StringFindChar(char* s, unsigned char c) {
    for (char* p = s;; ++p) {
        if (static_cast<unsigned char>(*p) == c)
            return p;
        if (*p == 0)
            return nullptr;
    }
}

// gilde.exe 0x4418ec — VIBE_String_MatchPrefix  (__usercall, eax = (a@eax, b@edx))
int StringMatchPrefix(const char* a, const char* b) {
    int n = 0;
    if (!*b)
        return n;
    while (*a == *b) {
        ++b;
        ++n;
        ++a;
        if (n >= 128 || !*b)
            return n;
    }
    return 0;
}

// gilde.exe 0x44ad48 — VIBE_String_GetDelimitedField
//   (__usercall, eax = (start@eax, n@edx, mode@bl, fallback@edi))
// Walks `start` as a run of consecutive NUL-terminated fields. 1:1 with the original.
char* StringGetDelimitedField(char* start, int n, char mode, char* fallback) {
    char* cur = start;
    int idx = 0;
    if (mode == 8) {
        fallback = start;
        n += 4;
    }
    while (idx < n - 1) {
        int len = 0;
        while (cur[len++])
            ;
        char* nul = &cur[len - 1];
        if (!nul) // original keeps this (never true), preserved verbatim
            return start;
        ++idx;
        cur = nul + 1;
        if (idx == 4) {
            fallback = cur;
        } else if (idx == 6) {
            if (*cur)
                fallback = cur;
        }
    }
    if (!*cur && mode == 4)
        return start;
    if (*cur || mode != 8)
        return cur;
    return fallback;
}

// gilde.exe 0x44b268 — VIBE_String_SkipLeadingSpaces  (__usercall, eax = (s@eax))
const char* StringSkipLeadingSpaces(const char* s) {
    u32 i = 0;
    char ch = 0;
    do {
        if (i >= StringLength(s))
            break;
        ch = s[i++];
    } while (ch == 32);
    if (i == StringLength(s))
        return nullptr;
    return &s[i - 1];
}

// gilde.exe 0x44b2ac — VIBE_String_TrimTrailingSpaces  (__usercall, eax = (s@eax))
const char* StringTrimTrailingSpaces(char* s) {
    int i = static_cast<int>(StringLength(s)) - 1;
    char* result = &s[i];
    if (s[i] == 32) {
        while (i > 0) {
            if (*result != 32) {
                *(result + 1) = 0;
                return result;
            }
            --result;
            --i;
        }
    }
    return result;
}

// gilde.exe 0x142a511 — VIBE_String_IntToRadix  (al return = last char of reversal)
char StringIntToRadix(u32 value, char* buf, u32 radix, int negative) {
    char* w = buf;
    u32 v;
    if (negative) {
        *buf = '-';
        w = buf + 1;
        v = 0u - value;
    } else {
        v = value;
    }
    char* lo = w;
    do {
        u32 digit = v % radix;
        bool isDecimal = (v % radix) <= 9;
        v /= radix;
        *w++ = isDecimal ? static_cast<char>(digit + 48) : static_cast<char>(digit + 87);
    } while (v);
    *w = 0;
    char* hi = w - 1;
    char result;
    do {
        result = *hi;
        *hi = *lo;
        *lo = result;
        --hi;
        ++lo;
    } while (lo < hi);
    return result;
}

// gilde.exe 0x142a4e4 — VIBE_String_IntToAscii  (itoa)
char* StringIntToAscii(int value, char* buf, u32 radix) {
    if (radix == 10 && value < 0)
        StringIntToRadix(static_cast<u32>(value), buf, 10u, 1);
    else
        StringIntToRadix(static_cast<u32>(value), buf, radix, 0);
    return buf;
}

// gilde.exe 0x609640 — VIBE_String_UIntToString  (__usercall, ultoa core)
// Digit table at 0x64c1c0 is "0123456789abcdefghijklmnopqrstuvwxyz".
static const char kDigits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
char* StringUIntToString(u32 value, char* buf, u32 radix) {
    char tmp[33]; // matches the original's stack scratch (max 32 digits + NUL)
    char* w = tmp;
    do {
        u32 d = value % radix;
        value /= radix;
        *w++ = kDigits[d];
    } while (value);
    // Reverse-emit into buf, terminating with the NUL slot reached at the bottom.
    char* out = buf;
    char ch;
    do {
        ch = *--w;
        *out++ = ch;
    } while (w != tmp);
    *out = 0;
    return buf;
}

// gilde.exe 0x609040 — VIBE_String_WideEnvLength  (__usercall, eax = (s@eax))
// Preserves the original's a1[1] probe: empty -> 0, else index of terminator.
int StringWideEnvLength(const u16* s) {
    const u16* base = s;
    if (*s) {
        u16 next;
        do {
            next = s[1];
            ++s;
        } while (next);
    }
    return static_cast<int>(s - base);
}

// gilde.exe 0x60c000 — VIBE_Crt_WcsChr  (__usercall, eax = (s@eax, c@dx))
u16* WcsChr(u16* s, u16 c) {
    if (c == *s)
        return s;
    while (*s++) {
        if (c == *s)
            return s;
    }
    return nullptr;
}

} // namespace guild::crt
