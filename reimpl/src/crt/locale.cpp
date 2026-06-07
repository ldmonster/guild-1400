#include "crt/locale.h"
#include "crt/ctype.h" // kPctype (word_14529BA) — ANSI class table, reused (ODR)

namespace guild::crt {

namespace {
LocaleCType g_locale;
bool        g_built = false;

void ensureBuilt() {
    if (!g_built) {
        BuildCTypeTablesC();
        g_built = true;
    }
}
} // namespace

LocaleCType& Locale() {
    ensureBuilt();
    return g_locale;
}

// VIBE_Locale_BuildCTypeTables @0x1428a7f — the C-locale fallback loop:
//   for (result = 0; result < 0x100; ++result) {
//     if (result in 'A'..'Z') { classBits[result] |= 0x10; caseFold[result] = result+32; }
//     else if (result in 'a'..'z') { classBits[result] |= 0x20; caseFold[result] = result-32; }
//     else caseFold[result] = 0;
//   }
//   return result;   // 0x100
unsigned BuildCTypeTablesC() {
    unsigned result;
    for (result = 0; result < 0x100; ++result) {
        if (result >= 0x41 && result <= 0x5A) { // 'A'..'Z'
            g_locale.classBits[result] |= kLocaleUpper;
            g_locale.caseFold[result] = static_cast<u8>(result + 32);
        } else if (result >= 0x61 && result <= 0x7A) { // 'a'..'z'
            g_locale.classBits[result] |= kLocaleLower;
            g_locale.caseFold[result] = static_cast<u8>(result - 32);
        } else {
            g_locale.caseFold[result] = 0;
        }
    }
    g_built = true;
    return result;
}

// VIBE_Locale_IsCType @0x142880f:
//   if (localeMask & classBits[ch]) return 1;
//   r = ansiMask ? (ansiMask & kPctype[ch]) : 0;
//   return r ? 1 : 0;
int LocaleIsCType(u8 ch, int ansiMask, u8 localeMask) {
    ensureBuilt();
    if ((localeMask & g_locale.classBits[ch]) != 0)
        return 1;
    int r = ansiMask ? (ansiMask & static_cast<u16>(kPctype[ch])) : 0;
    return r ? 1 : 0;
}

// VIBE_Locale_IsSpace @0x14287fe — IsCType(ch, 0, 4). NOTE: with ansiMask 0 and
// the C-locale class table only carrying bits 0x10/0x20, this returns 0 for all
// bytes in the C locale, reproducing the original exactly.
int LocaleIsSpace(u8 ch) { return LocaleIsCType(ch, 0, 4u); }

} // namespace guild::crt
