#include "util/string_ops.h"

namespace guild::util {

// gilde.exe 0x1427fd0 — VIBE_Util_StrStr  (__cdecl(char* hay, _BYTE* needle))
//   Classic two-pointer strstr: lock the first needle byte, scan `hay` for it,
//   then verify the rest of the needle. The original is hand-unrolled (it peels
//   the first two needle bytes), but the observable behaviour is plain strstr.
char* StrStr(char* hay, const char* needle) {
    char first = needle[0];          // v2 = *a2
    if (!first)                      // empty needle
        return hay;                  // return a1
    for (char* p = hay; *p; ++p) {   // scan each start position
        if (*p != first)
            continue;
        const char* n = needle;      // try to match the whole needle here
        char* h = p;
        while (*n && *h == *n) {
            ++h;
            ++n;
        }
        if (!*n)                     // reached needle terminator -> full match
            return p;
    }
    return nullptr;
}

// gilde.exe 0x14287d3 — VIBE_Util_StrnLen  (__cdecl(_BYTE* s, int cap))
//   Walk at most `cap` bytes looking for the NUL. The original's loop runs while
//   `cap-1` (post-decrement) stays non-negative AND the byte is non-zero; on exit
//   it returns `cap` if the stopped-at byte is still non-zero (no terminator seen
//   within range), else the offset to the terminator.
std::size_t StrnLen(const char* s, int cap) {
    const char* v = s;               // v2
    int remaining = cap - 1;         // v3 = a2 - 1
    if (cap) {                       // if (a2)
        do {
            if (!*v)                 // hit terminator
                break;
            ++v;
        } while (remaining--);       // while (v3--)
    }
    if (*v)                          // no terminator within range
        return static_cast<std::size_t>(cap); // return a2
    return static_cast<std::size_t>(v - s);   // return (v - s)
}

// gilde.exe 0x1428cb0 — VIBE_Util_StrnCpy  (__cdecl(_BYTE* dst, char* src, uint n))
//   strncpy: copy up to n bytes; if src ends early, NUL-pad the rest of dst; if
//   src is >= n bytes, dst is left un-terminated. The original is the word-at-a-
//   time MSVC strncpy (the 0x7EFEFEFF/0x81010100 has-zero-byte trick + 4-byte
//   stores); this byte loop is behaviour-identical.
char* StrnCpy(char* dst, const char* src, std::size_t n) {
    if (!n)                          // if (!a3) return a1
        return dst;
    char* d = dst;
    std::size_t i = 0;
    // Copy bytes until either the source NUL or the count is reached.
    for (; i < n; ++i) {
        char c = src[i];
        d[i] = c;
        if (!c) {                    // hit source terminator -> pad remainder
            for (++i; i < n; ++i)
                d[i] = '\0';
            return dst;
        }
    }
    return dst;                      // filled n bytes, no terminator written
}

// gilde.exe 0x5d3ef0 — VIBE_Util_StrChr  (__usercall, eax = (s@eax, c@dl))
//   strrchr: scan the whole string (including the terminator on the final
//   iteration) and remember the LAST position where the byte equals `c`.
char* StrChrLast(char* s, char c) {
    char* result = nullptr;          // v2 = 0
    do {
        if (c == *s)                 // record every match; last one wins
            result = s;
    } while (*s++);                  // continue through (and including) the NUL
    return result;
}

// gilde.exe 0x5e9f50 — VIBE_Util_StrToUpper  (__usercall, eax = (s@eax))
//   In-place ASCII upper-case. The original computes (*i - 'a') and upper-cases
//   only when the unsigned result is <= 25 ('a'..'z').
char* StrToUpper(char* s) {
    for (char* i = s; *i; ++i) {
        unsigned char v = static_cast<unsigned char>(*i) - 97u; // *i - 'a'
        if (v <= 0x19u)              // 'a'..'z'
            *i = static_cast<char>(v + 65);                     // -> 'A'..'Z'
    }
    return s;
}

// gilde.exe 0x5e9ee0 — VIBE_Util_StrncmpN  (__usercall, eax = (a@eax, b@edx, n@ebx))
//   strncmp: compare up to `n` bytes, returning the unsigned-byte difference at
//   the first mismatch, or 0 if equal within the limit / either string ended.
int StrncmpN(const char* a, const char* b, int n) {
    if (!n)                          // if (!a3) return 0
        return 0;
    for (;;) {
        unsigned char ca = static_cast<unsigned char>(*a);
        unsigned char cb = static_cast<unsigned char>(*b);
        if (ca != cb)
            return static_cast<int>(ca) - static_cast<int>(cb);
        if (!ca)                     // both terminated equal
            return 0;
        ++a;
        ++b;
        if (!--n)                    // exhausted the budget
            return 0;
    }
}

// gilde.exe 0x5cb8f0 — VIBE_Util_StrCmpNoCase  (__usercall, eax = (a@eax, b@edx))
//   stricmp folding 'A'..'Z' -> 'a'..'z'. Returns the folded-byte difference.
int StrCmpNoCase(const char* a, const char* b) {
    for (;;) {
        unsigned char v3 = static_cast<unsigned char>(*a);
        unsigned char v4 = static_cast<unsigned char>(*b);
        if (v3 >= 0x41u && v3 <= 0x5Au) // 'A'..'Z'
            v3 += 32;
        if (v4 >= 0x41u && v4 <= 0x5Au)
            v4 += 32;
        if (v3 != v4 || !v4)
            return static_cast<int>(v3) - static_cast<int>(v4);
        ++a;
        ++b;
    }
}

// gilde.exe 0x5e0db0 — VIBE_Util_StrCmpNoCaseN  (__usercall, eax=(a@eax,b@edx,n@ebx))
//   strnicmp folding 'A'..'Z' -> 'a'..'z' over at most `n` bytes.
int StrCmpNoCaseN(const char* a, const char* b, int n) {
    if (!n)                          // if (!a3) return 0
        return 0;
    for (;;) {
        unsigned char v5 = static_cast<unsigned char>(*a);
        unsigned char v6 = static_cast<unsigned char>(*b);
        if (v5 >= 0x41u && v5 <= 0x5Au)
            v5 += 32;
        if (v6 >= 0x41u && v6 <= 0x5Au)
            v6 += 32;
        if (v5 != v6)
            return static_cast<int>(v5) - static_cast<int>(v6);
        if (!v6)                     // both terminated equal
            return 0;
        ++a;
        ++b;
        if (!--n)
            return 0;
    }
}

// gilde.exe 0x5ea788 — VIBE_Util_StrCmpNoCaseInline  (__usercall, eax=(a@eax,b@edx))
//   A second case-insensitive compare that folds 'a'..'z' -> UPPER and normalises
//   the result to exactly -1 / 0 / +1:
//     - if both folded bytes are 0  -> return (v4==0)-1 == 0
//     - if a ended first            -> return -1
//     - if b ended first            -> return +1
//     - else sign of (foldA - foldB)
int StrCmpNoCaseSign(const char* a, const char* b) {
    for (;;) {
        unsigned char v3 = static_cast<unsigned char>(*a++);
        unsigned char v4 = static_cast<unsigned char>(*b++);
        if (v3 >= 0x61u && v3 <= 0x7Au) // 'a'..'z'
            v3 -= 32;
        if (v4 >= 0x61u && v4 <= 0x7Au)
            v4 -= 32;
        if (!v3)                     // a ended
            return (v4 == 0) - 1;    // 0 if both ended, else -1
        if (!v4)                     // b ended, a did not
            return 1;
        if (v3 < v4)
            return -1;
        if (v3 > v4)
            return 1;
    }
}

} // namespace guild::util
