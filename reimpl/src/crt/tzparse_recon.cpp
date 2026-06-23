#include "crt/tzparse_recon.h"

#include <cstring>

namespace guild::crt {

namespace {

// Month cumulative-day tables exactly as in the binary (dword_62CEC6 normal,
// dword_62CEE0 leap). The original reads *(int*)(base + 2*month) and
// *(int*)(base + 2*month + 2) and takes the high 16 bits of each 32-bit window;
// the difference is the number of days in `month`. We store the raw bytes and
// reproduce that exact (unaligned, >>16) read.
const u8 kDaysNormal[26] = {
    0x40,0x80,0x00,0x00, 0x1f,0x00, 0x3b,0x00, 0x5a,0x00, 0x78,0x00,
    0x97,0x00, 0xb5,0x00, 0xd4,0x00, 0xf3,0x00, 0x11,0x01, 0x30,0x01, 0x4e,0x01,
};
const u8 kDaysLeap[26] = {
    0x6d,0x01,0x00,0x00, 0x1f,0x00, 0x3c,0x00, 0x5b,0x00, 0x79,0x00,
    0x98,0x00, 0xb6,0x00, 0xd5,0x00, 0xf4,0x00, 0x12,0x01, 0x31,0x01, 0x4f,0x01,
};

// *(int*)(base + byteOff) >> 16  (signed 32-bit load, arithmetic shift).
inline i32 windowHi16(const u8* base, std::size_t byteOff) {
    i32 v;
    std::memcpy(&v, base + byteOff, sizeof(v));
    return v >> 16;
}

} // namespace

// gilde.exe 0x6070b0 — VIBE_Time_ParseDecimal.
const char* TimeParseDecimal(const char* p, i32* out) {
    const unsigned char* s = reinterpret_cast<const unsigned char*>(p);
    int i = 0;
    while (*s >= 0x30u) {
        if (*s > 0x39u)
            break;
        int v4 = *s++;
        // 2's-complement wrap, matches the original 32-bit accumulate; compute
        // in u32 to avoid signed-overflow UB while keeping bytes identical.
        i = static_cast<int>(
            static_cast<u32>(v4) + 10u * static_cast<u32>(i) - 48u);
    }
    *out = i;
    return reinterpret_cast<const char*>(s);
}

// gilde.exe 0x6070dc — VIBE_Time_ParseTzName.
const char* TimeParseTzName(const char* p, char* nameOut, i32* offsetOut) {
    const unsigned char* v3 = reinterpret_cast<const unsigned char*>(p);
    if (*v3 == 58)                    // ':'
        ++v3;
    const unsigned char* v4 = v3;     // start of name
    unsigned char v5;
    while (true) {
        v5 = *v3;
        if (!*v3 || v5 == 44 || v5 == 45 || v5 == 43
            || (v5 >= 0x30u && v5 <= 0x39u))
            break;                    // ',' '-' '+' or digit terminate the name
        ++v3;
    }
    std::size_t v6 = static_cast<std::size_t>(v3 - v4);
    if (static_cast<long>(v6) > 128)
        v6 = 128;
    std::memcpy(nameOut, v4, v6);
    int v7 = 0;                       // sign flag
    nameOut[v6] = 0;
    if (v5 == 45) {                   // '-'
        v7 = 1;
        ++v3;
    } else if (v5 == 43) {            // '+'
        ++v3;
    }
    if (*v3 >= 0x30u && *v3 <= 0x39u) {
        i32 v14 = 0, v15 = 0, v16 = 0;       // ss, mm, hh
        const char* q = TimeParseDecimal(reinterpret_cast<const char*>(v3), &v16);
        v3 = reinterpret_cast<const unsigned char*>(q);
        if (*v3 == 58) {                      // ':'
            q = TimeParseDecimal(reinterpret_cast<const char*>(v3 + 1), &v15);
            v3 = reinterpret_cast<const unsigned char*>(q);
            if (*v3 == 58) {
                q = TimeParseDecimal(reinterpret_cast<const char*>(v3 + 1), &v14);
                v3 = reinterpret_cast<const unsigned char*>(q);
            }
        }
        i32 v10 = 60 * (60 * v16 + v15) + v14;  // total seconds
        *offsetOut = v10;
        if (v7)
            *offsetOut = -v10;
    }
    return reinterpret_cast<const char*>(v3);
}

// gilde.exe 0x6071f4 — VIBE_Time_ParseTzRule.
const char* TimeParseTzRule(const char* p, TzRule* rule) {
    const unsigned char* v2 = reinterpret_cast<const unsigned char*>(p);
    int v3 = -1;                       // type flag (esi): -1 none, 1 J, 0 M
    if (*v2 == 74) {                   // 'J'
        v3 = 1;
        ++v2;
    }
    if (*v2 == 77) {                   // 'M'
        ++v2;
        v3 = 0;
    }
    rule->type = v3;                   // *(a2+32)

    i32 num = 0;
    const char* cur = TimeParseDecimal(reinterpret_cast<const char*>(v2), &num);
    if (v3) {
        rule->jday = num;              // v5[7] = v14[0]   (J or n day-of-year)
    } else {
        rule->month = num - 1;         // v5[4] = v14[0]-1
        if (*reinterpret_cast<const unsigned char*>(cur) == 46) {  // '.'
            cur = TimeParseDecimal(cur + 1, &num);
            rule->week = num;          // v5[3]
            if (*reinterpret_cast<const unsigned char*>(cur) == 46) {
                cur = TimeParseDecimal(cur + 1, &num);
                rule->wday = num;      // v5[6]
            }
        }
        rule->jday = 0;                // v5[7] = 0
    }

    i32 hh = 2, mm = 0, ss = 0;        // v13=2, v11=0, v12=0
    if (*reinterpret_cast<const unsigned char*>(cur) == 47) {  // '/'
        cur = TimeParseDecimal(cur + 1, &hh);
        if (*reinterpret_cast<const unsigned char*>(cur) == 58) {  // ':'
            cur = TimeParseDecimal(cur + 1, &mm);
            if (*reinterpret_cast<const unsigned char*>(cur) == 58)
                cur = TimeParseDecimal(cur + 1, &ss);
        }
    }
    rule->sec  = ss;   // *v5     = v11
    rule->min  = mm;   // v5[1]   = v12
    rule->hour = hh;   // v5[2]   = v13
    return cur;
}

// gilde.exe 0x606f10 — VIBE_Time_ClearTzInitFlag.
i32 TimeClearTzInitFlag(u32* flagWord) {
    i32 result = static_cast<i32>(*flagWord & 1u);
    // LOBYTE(dword) = dword & 0xFC : clear low byte's bits 0..1, keep upper bytes.
    *flagWord = (*flagWord & 0xFFFFFF00u) | ((*flagWord & 0xFCu));
    return result;
}

// gilde.exe 0x606f2c — VIBE_Time_SetTzInitFlag.
i32 TimeSetTzInitFlag(u32* flagWord) {
    i32 result = static_cast<i32>(*flagWord & 1u);
    *flagWord = (*flagWord & 0xFFFFFF00u) | ((*flagWord & 0xFCu) | 1u);
    return result;
}

// gilde.exe 0x60693c — VIBE_Time_DstTransitionDayOfYear.
i32 TimeDstTransitionDayOfYear(const TzRule* rule, int yearMinus1900) {
    int v3 = rule->type;                       // a1[8]  (+0x20)
    if (v3) {
        if (v3 == 1)
            return rule->jday - 1;             // J-rule: a1[7] - 1
        else
            return rule->jday;                 // n-rule: a1[7]
    }

    // M-rule (type 0).
    const u8* table = IsLeapYear(static_cast<u32>(yearMinus1900 + 1900))
                          ? kDaysLeap : kDaysNormal;
    int month = rule->month;                    // a1[4] (+0x10)
    int v6 = windowHi16(table, 2 * static_cast<std::size_t>(month) + 2);
    int v7 = windowHi16(table, 2 * static_cast<std::size_t>(month));
    int v9 = v6 - v7;                           // days in `month`

    // Build the 1st-of-month tm. The original calls VIBE_Crt_MakeTimeFromTm,
    // which (via VIBE_Time_ConvertToTmFields) writes the normalized wday(+6) and
    // yday(+7) BACK into the tm in place. Our MakeTimeUtc takes a const tm and
    // returns the time_t, so we recover the same normalized wday/yday by feeding
    // the returned time_t back through Gmtime (== ConvertToTmFields). This is
    // behavior-identical to the original's in-place mutation.
    TmFields tm{};
    tm.mday = 1;                                // v14[3] = 1
    tm.mon  = rule->month;                      // v14[4] = a1[4]
    tm.year = yearMinus1900;                    // v14[5] = v4 (yearMinus1900)
    tm.isdst = 0;                               // v14[8] = 0
    i32 t = MakeTimeUtc(&tm);                   // VIBE_Crt_MakeTimeFromTm
    TmFields norm = Gmtime(static_cast<u32>(t)); // recover wday/yday

    int v10 = (rule->wday - norm.wday + 7) % 7; // (a1[6] - v14[6] + 7) % 7
    int v11 = rule->week;                       // a1[3] (+0x0C)
    int v12;
    if (v11 == 5) {
        if (v10 + 29 <= v9)
            v12 = 4;
        else
            v12 = rule->week - 2;               // a1[3] - 2
    } else {
        v12 = v11 - 1;
    }
    return v10 + norm.yday + 7 * v12;           // v10 + v14[7] + 7*v12
}

// gilde.exe 0x606a34 — VIBE_Time_IsAfterDstStart.
bool TimeIsAfterDstStart(const TzRule* endRule, const TzRule* startRule,
                         int yearMinus1900) {
    // if (!endRule->type && !startRule->type) { compare months }
    //   a1[8] == endRule.type (+0x20), *(a2+32) == startRule.type (+0x20).
    if (!endRule->type && !startRule->type) {
        int v3 = endRule->month;                // a1[4]
        int v4 = startRule->month;              // *(a2+16)
        if (v3 > v4)
            return true;
        if (v3 < v4)
            return false;
        // equal months: fall through to day-of-year comparison
    }
    int endDoy   = TimeDstTransitionDayOfYear(endRule, yearMinus1900);
    int startDoy = TimeDstTransitionDayOfYear(startRule, yearMinus1900);
    return endDoy > startDoy;
}

} // namespace guild::crt
