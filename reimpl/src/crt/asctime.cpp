#include "crt/asctime.h"

namespace guild::crt {

namespace {
// Column-wise 3-letter name tables, recovered verbatim from gilde.exe:
//   byte_64A54C[12] / byte_64A558[12] / byte_64A564[12]  — month letters 0..2
//   byte_64A570[7]  / byte_64A577[7]  / byte_64A57E[7]   — weekday letters 0..2
// (i.e. month m's name is {monC0[m], monC1[m], monC2[m]} = "Jan".."Dec".)
const u8 kMonC0[12] = {'J','F','M','A','M','J','J','A','S','O','N','D'};
const u8 kMonC1[12] = {'a','e','a','p','a','u','u','u','e','c','o','e'};
const u8 kMonC2[12] = {'n','b','r','r','y','n','l','g','p','t','v','c'};
const u8 kDayC0[7]  = {'S','M','T','W','T','F','S'};
const u8 kDayC1[7]  = {'u','o','u','e','h','r','a'};
const u8 kDayC2[7]  = {'n','n','e','d','u','i','t'};
} // namespace

// gilde.exe 0x5e55c0 — VIBE_Crt_FormatTwoDigits (__usercall a1@eax, a2@edx, a3@ebx)
char FormatTwoDigits(int value, int pos, char* buf) {
    int ones = value % 10;                       // v4
    buf[pos]     = static_cast<char>(value / 10 + '0');
    buf[pos + 1] = static_cast<char>(ones + '0');
    return static_cast<char>(value % 10 + '0');
}

// gilde.exe 0x5e55f0 — VIBE_Crt_FormatAsctime (__usercall a1@eax(tm), a2@edx(buf))
char* FormatAsctime(const TmRec* tm, char* buf) {
    // Weekday name (a1[6])
    int wd = tm->wday;
    buf[0] = static_cast<char>(kDayC0[wd]);
    buf[1] = static_cast<char>(kDayC1[wd]);
    buf[2] = static_cast<char>(kDayC2[wd]);
    buf[3] = ' ';
    // Month name (a1[4])
    int mo = tm->mon;
    buf[4] = static_cast<char>(kMonC0[mo]);
    buf[5] = static_cast<char>(kMonC1[mo]);
    buf[6] = static_cast<char>(kMonC2[mo]);
    buf[7] = ' ';
    // Day of month (a1[3]) at [8..9]; leading zero -> space
    FormatTwoDigits(tm->mday, 8, buf);
    if (buf[8] == '0')
        buf[8] = ' ';
    buf[10] = ' ';
    // hh:mm:ss  (a1[2], a1[1], a1[0])
    FormatTwoDigits(tm->hour, 11, buf);
    buf[13] = ':';
    FormatTwoDigits(tm->min, 14, buf);
    buf[16] = ':';
    FormatTwoDigits(tm->sec, 17, buf);
    buf[19] = ' ';
    // Year (a1[5], since 1900): century = year/100 + 19, yy = year % 100
    int year = tm->year;                          // v10
    int yy = year % 100;                          // v12
    FormatTwoDigits(year / 100 + 19, 20, buf);
    FormatTwoDigits(yy, 22, buf);
    buf[24] = '\n';
    buf[25] = '\0';
    return buf;
}

// gilde.exe 0x606d3c — VIBE_Time_CompareDateFields (__usercall a1@eax, a2@edx)
int CompareDateFields(const int* a, const int* b) {
    int ay = a[2], by = b[2];
    if (ay < by)
        return 1;
    if (ay == by) {
        int am = a[1], bm = b[1];
        if (am < bm || (am == bm && a[0] < b[0]))
            return 1;
    }
    return 0;
}

} // namespace guild::crt
