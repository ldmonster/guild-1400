#include "sim/personnel.h"

namespace guild::sim {

// gilde.exe 0x594d70 — VIBE_Personnel_ComputeWageByCategory.
//   HIBYTE(v9) = a2; cat = MapActionToCategory(*(dword_13CE294 + 589*(v9>>24)));
//   if (cat==10||cat==11||cat==12) return base(...) * flt_626B30; // 9.0
//   else                            return base(...) * flt_626B2C; // 3.0
// flt_626B2C = 3.0 (0x40400000), flt_626B30 = 9.0 (0x41100000). The original
// narrows each product to float. `aiTypeByte` is the type-table byte the original
// fetches; SHIBYTE(v9) is the sign-extended action byte passed to base().
constexpr float kWageMultLow  = 3.0f;  // flt_626B2C — production/craft
constexpr float kWageMultHigh = 9.0f;  // flt_626B30 — administrative (cat 10/11/12)

static BuildingBaseValueFn g_baseValueFn = nullptr;
static ActionCategoryFn    g_categoryFn  = nullptr;
void PersonnelSetBuildingBaseValueHook(BuildingBaseValueFn fn) { g_baseValueFn = fn; }
void PersonnelSetActionCategoryHook(ActionCategoryFn fn) { g_categoryFn = fn; }

double PersonnelComputeWageByCategory(int building, u8 actionByte, int arg,
                                      u8 aiTypeByte) {
    // v9>>24 sign-extends actionByte; here we pass it as the action byte directly.
    int sActionByte = static_cast<i8>(actionByte);

    u8 cat = g_categoryFn ? g_categoryFn(aiTypeByte) : 0;
    double base = g_baseValueFn
                      ? g_baseValueFn(building, sActionByte, 0, arg)
                      : 0.0;
    float mult = (cat == 10 || cat == 11 || cat == 12) ? kWageMultHigh
                                                       : kWageMultLow;
    return static_cast<float>(base * mult);
}

} // namespace guild::sim
