#include "gui/text/format.h"

#include "crt/printf.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace guild::gui::text {

// ---------------------------------------------------------------------------
// Grouped integer — recovered from the inline 'i' loop in RenderRichString:
//
//   VIBE_Crt_Sprintf_0(v202, "%i", value);
//   outLen = (strlen(v202)-1)/3 + strlen(v202);
//   v146 = outLen;
//   v145 = -1;
//   for (j = strlen(v202); j >= 0; ++v145) {
//       v196[v146--] = v202[j];
//       if (j && v145 > 0 && !((v145 + 1) % 3))
//           v196[v146--] = '.';
//       --j;
//   }
//   v196[outLen] = 0;
//
// The loop copies the decimal string (including its terminating NUL slot, since
// `j` starts at strlen and writes index outLen down to 0) back-to-front and
// injects '.' separators. `v145` counts emitted *digits-from-the-right*; the
// guard `j && v145>0 && (v145+1)%3==0` inserts a separator after every third
// digit but never before the first character (so a leading '-' is never grouped,
// because once `j` reaches 0 the `j &&` guard is false).
// gilde.exe (inline @0x59d6e8) — grouped-int production.
int FormatGroupedInt(i32 value, char* out) {
    char dec[64];
    int len = guild::crt::Sprintf(dec, "%i", value);  // VIBE_Crt_Sprintf_0(v202,"%i",..)

    int outLen = (len - 1) / 3 + len;
    int dst = outLen;
    int rightDigits = -1;  // v145
    for (int j = len; j >= 0; ++rightDigits) {
        out[dst--] = dec[j];
        if (j && rightDigits > 0 && ((rightDigits + 1) % 3) == 0)
            out[dst--] = kSepDot;
        --j;
    }
    out[outLen] = 0;
    return static_cast<int>(std::strlen(out));
}

// ---------------------------------------------------------------------------
// Money — VIBE_Money_FormatWithSeparators @0x58f798.
//
// v25  = (float)abs(a1)
// v6   = (double)v25 / divisor + 0.5        (dbl_6269BC = 0.5)
// v24  = (int)v6                            (truncate => round half down)
// then:
//   if amount<0:
//     if v24==0           -> "0%c"          (off_6269A4 = "0%c", icon 17)
//     elif v24<1000       -> "-%i%c"
//     else                -> "-%s%c" with grouped digits
//   else:
//     if v24<1000:
//       if v24            -> "%i%c"
//       else              -> "0%c"
//     else                -> "%s%c" with grouped digits
//
// The grouping loop is identical to FormatGroupedInt but operates on the
// magnitude string (no sign) and uses flt_6269C4 (1/3) to size the buffer:
//   groupedLen = strlen(mag) + (int)((strlen(mag)-1)/3)
// which is the same value as (len-1)/3 + len.
// gilde.exe 0x58f798 — VIBE_Money_FormatWithSeparators.
int FormatMoney(i32 amount, i32 divisor, char* out) {
    if (divisor <= 0)
        divisor = 1;

    float mag = static_cast<float>(std::abs(amount));
    double scaled = static_cast<double>(mag) / static_cast<double>(divisor) + 0.5;  // dbl_6269BC
    i32 v = static_cast<i32>(scaled);

    if (amount < 0) {
        if (v == 0)
            return guild::crt::Sprintf(out, "0%c", kIconMoney);          // off_6269A4
        if (v < 1000)
            return guild::crt::Sprintf(out, "-%i%c", v, kIconMoney);     // aIC_1
        char grouped[128];
        FormatGroupedInt(v, grouped);
        return guild::crt::Sprintf(out, "-%s%c", grouped, kIconMoney);   // aSC
    } else {
        if (v < 1000) {
            if (v)
                return guild::crt::Sprintf(out, "%i%c", v, kIconMoney);  // aIC_0
            return guild::crt::Sprintf(out, "0%c", kIconMoney);          // off_6269A4
        }
        char grouped[128];
        FormatGroupedInt(v, grouped);
        return guild::crt::Sprintf(out, "%s%c", grouped, kIconMoney);    // aSC_0
    }
}

// ---------------------------------------------------------------------------
// Date record — VIBE_GameTime_PackToRecord @0x583304.
//   *(WORD*)(out+2) = *(WORD*)src + 1400;
//   v3 = *(DWORD*)src % 4;
//   *(BYTE*)out     = 1;
//   *(BYTE*)(out+1) = 3*v3 + 1;
//   *(BYTE*)(out+4) = *(BYTE*)(src+4);
//   *(BYTE*)(out+5) = *(BYTE*)(src+6);
//   *(DWORD*)(out+8)= *(DWORD*)(src+10);
// gilde.exe 0x583304 — VIBE_GameTime_PackToRecord.
void PackDateRecord(const GameTimeSource& src, DateRecord& out) {
    i32 yq = src.yearQuarter;
    out.year      = static_cast<u16>(static_cast<u16>(yq & 0xFFFF) + 1400);  // *(WORD*)src + 1400
    int quarter   = yq % 4;                                                  // *(DWORD*)src % 4
    out.dayMarker = 1;
    out.month     = static_cast<u8>(3 * quarter + 1);                        // 1,4,7,10
    out.hour      = src.hour;
    out.minute    = src.minute;
    out.extra     = static_cast<u32>(src.extra);
    out.pad5 = out.pad6 = out.pad7 = 0;
}

// gilde.exe 0x583384 — VIBE_GameTime_GetSeasonFromYear: (*a1 >> 16) % 4.
int SeasonFromYear(i32 packedYearDword) {
    return (packedYearDword >> 16) % 4;
}

} // namespace guild::gui::text
