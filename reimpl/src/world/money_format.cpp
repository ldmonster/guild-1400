#include "world/money_format.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace guild::world {

// Groups the decimal digits of `magnitude` with '.' every three digits, exactly
// as VIBE_Money_FormatWithSeparators (0x58f798) does in its >= 1000 branch.
//
// Original (positive branch, dest buffer v17, source itoa buffer v19):
//   VIBE_AnimationState_Update(v24, v19, 10);   // v19 = decimal digits, no sign
//   v22 = (int)((double)strlen(v19) + (double)(strlen(v19) - 1) * (1/3));
//   v12 = -1; v14 = strlen(v19);                 // v14 indexes the source NUL
//   for (j = v22; v14 >= 0; ++v12) {
//       v17[j--] = v19[v14];                     // copy char (first iter: '\0')
//       if (v14 && v12 > 0 && !((v12 + 1) % 3))
//           v17[j--] = 46;                       // '.'
//       --v14;
//   }
//   v17[v22] = 0;
// flt_6269C4 == 0.333333343f (1.0/3); VIBE_Coord_ConvertX truncates toward zero,
// so v22 == trunc(len + (len-1)/3).
std::string MoneyGroupThousands(u32 magnitude) {
    // VIBE_Util_IntToStringRadix(v24, buf, 10): plain decimal, no sign.
    std::string digits = std::to_string(magnitude);
    const int srcLen = static_cast<int>(digits.size());

    // The original only groups when v24 >= 1000; below that it prints the bare
    // number ("%i").
    if (magnitude < 1000)
        return digits;

    // v22 = trunc(len + (len-1) * (1/3))  — total length including separators.
    const float kThird = 0.333333343f;  // flt_6269C4
    const int outLen = static_cast<int>(
        static_cast<double>(srcLen) + static_cast<double>(srcLen - 1) * kThird);

    // Build back-to-front into a fixed scratch (matches the original's v17[256]).
    std::string dest(static_cast<size_t>(outLen) + 1, '\0');

    int i = outLen;          // j
    int v8 = -1;             // v12
    int v10 = srcLen;        // v14 — starts at the NUL terminator index
    // `buf` = digits followed by an implicit '\0' at index srcLen.
    while (v10 >= 0) {
        char ch = (v10 < srcLen) ? digits[static_cast<size_t>(v10)] : '\0';
        if (i >= 0)
            dest[static_cast<size_t>(i)] = ch;
        --i;
        if (v10 != 0 && v8 > 0 && ((v8 + 1) % 3) == 0) {
            if (i >= 0)
                dest[static_cast<size_t>(i)] = '.';   // 46
            --i;
        }
        --v10;
        ++v8;
    }
    // v17[v22] = 0; — terminate at outLen (truncate any leading scratch).
    dest.resize(static_cast<size_t>(outLen));
    return dest;
}

// gilde.exe 0x58f798 — VIBE_Money_FormatWithSeparators.
std::string MoneyFormatWithSeparators(i32 amount, i32 rate) {
    if (rate == 0)
        rate = 1;  // original guarantees rate != 0 (dword_649A88 entry)

    // v25 = abs32(a1) read back as a SIGNED dword (0x58f7ab: the abs result is
    // stored and reloaded via SLODWORD(v25)), so INT_MIN stays INT_MIN.
    // v6 = (double)mag / rate + 0.5; (int)v6 with trunc-toward-zero
    // == round-half-up of |amount|/rate.
    const std::int32_t mag32 = static_cast<std::int32_t>(
        amount < 0 ? 0u - static_cast<std::uint32_t>(amount)
                   : static_cast<std::uint32_t>(amount));
    const double scaled = static_cast<double>(mag32) / static_cast<double>(rate)
                        + kMoneyFormatRoundBias;
    const std::int32_t v24 =
        static_cast<std::int32_t>(std::trunc(scaled));  // VIBE_Coord_ConvertX

    std::string out;
    if (v24 == 0) {
        // Both sign branches: VIBE_Crt_Sprintf_0(dest, "0%c", 17) — no '-'.
        out = "0";
        out.push_back(kCurrencyGlyph);
        return out;
    }

    if (amount < 0)
        out.push_back('-');

    if (v24 < 1000) {
        // "-%i%c" / "%i%c"
        out += std::to_string(v24);
    } else {
        out += MoneyGroupThousands(v24);
    }
    out.push_back(kCurrencyGlyph);
    return out;
}

} // namespace guild::world
