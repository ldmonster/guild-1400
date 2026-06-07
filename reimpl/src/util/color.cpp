#include "util/color.h"

namespace guild::util {

// gilde.exe 0x4226e0 — VIBE_Color_SetRgb
//   *result = a2(dl);  result[1] = a4(bl);  result[2] = a3(cl);
void ColorSetRgb(u8* dst, u8 a, u8 b, u8 c) {
    dst[0] = a;
    dst[1] = c;
    dst[2] = b;
}

// gilde.exe 0x4226bc — VIBE_Color_NotEqualRgb
//   return *a1 != *a2 || a1[1] != a2[1] || a1[2] != a2[2];
int ColorNotEqualRgb(const u8* a, const u8* b) {
    return a[0] != b[0] || a[1] != b[1] || a[2] != b[2];
}

} // namespace guild::util
