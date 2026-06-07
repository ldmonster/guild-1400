#include "world/bank_treasury.h"

#include "util/math_random.h"

namespace guild::world {

// gilde.exe 0x501245..0x501250:
//   movzx eax, var_1E   ; def byte +2, sign-extended via the moves below
//   mov   eax, edx      ; (edx = sign-extended byte)
//   sar   edx, 1Fh      ; edx = sign bits
//   sub   eax, edx      ; eax = byte - (byte>>31)
//   sar   eax, 1        ; eax = (byte + (byte<0)) >> 1  == byte/2 toward zero
// i.e. a signed division by two that truncates toward zero (C's `/ 2`).
i32 CityTaxHalveOfficeLevel(i8 officeDefByte2) {
    int x = static_cast<int>(officeDefByte2);
    return x / 2;  // truncates toward zero, matching (x - (x>>31)) >> 1
}

// gilde.exe 0x50123f..0x501263 (the rate cell). The seed byte is fetched as the
// top byte of the dword spanning the rate frame (`[esp+edx+0x15] >> 24`), which
// is exactly kCityTaxSeedRates[(base+index) % 16]. The bias is subtracted and
// the low byte stored: `sub ebp, eax` then `mov [edi-1], al`.
i8 CityTaxRateFor(int base, int index, i8 officeDefByte2) {
    int slot = ((base + index) % 16 + 16) % 16;  // C-mod can be negative; the
                                                 // original's idiv keeps it in
                                                 // [0,16) for the in-range base.
    int rate = static_cast<int>(kCityTaxSeedRates[slot]);
    int biased = rate - CityTaxHalveOfficeLevel(officeDefByte2);
    return static_cast<i8>(biased);  // 8-bit store (mov [edi-1], al)
}

// ---------------------------------------------------------------------------
// Office-table resolver — inert default (no office bias). The real engine wires
// VIBE_Office_GetEntryByCity (0x47efb4) -> VIBE_Office_GetDefinition (0x47f008)
// and returns the definition's +2 byte; here we keep a definition so the module
// links standalone and tests install their own.
// ---------------------------------------------------------------------------
namespace {
CityOfficeLevelResolver g_officeLevelResolver = nullptr;
void*                    g_officeLevelCtx      = nullptr;

i8 ResolveOfficeLevel(i32 cityId) {
    if (g_officeLevelResolver)
        return g_officeLevelResolver(cityId, g_officeLevelCtx);
    return 0;  // GetEntryByCity miss / inert default -> no rate bias
}
}  // namespace

void CityTaxSetOfficeLevelResolver(CityOfficeLevelResolver resolver, void* ctx) {
    g_officeLevelResolver = resolver;
    g_officeLevelCtx      = ctx;
}

// gilde.exe 0x5011dc — VIBE_Office_ComputeCityTaxRates.
//   base = RandomModulo(16); result = base & 0xFFFF;
//   for (i=0; i<count; ++i) { resolve def; out[i] = rate-half; result = full int; }
//   return result;
int ComputeCityTaxRates(int base, const i32* cityIds, int count, i8* out) {
    int result = base & 0xFFFF;  // and eax, 0FFFFh
    if (count <= 0)              // test edx,edx ; jle loc_50126C
        return result;

    for (int i = 0; i < count; ++i) {
        i8 defByte2 = (cityIds ? ResolveOfficeLevel(cityIds[i]) : i8{0});
        // The original keeps the FULL (untruncated) bias int in eax across the
        // loop (mov eax, ebp at 0x50125d) and stores only the low byte; the
        // returned eax is therefore the last full value, not the stored byte.
        int rate   = static_cast<int>(kCityTaxSeedRates[((base + i) % 16 + 16) % 16]);
        int biased = rate - CityTaxHalveOfficeLevel(defByte2);
        if (out)
            out[i] = static_cast<i8>(biased);
        result = biased;
    }
    return result;
}

std::vector<i8> ComputeCityTaxRates(const i32* cityIds, int count) {
    int base = guild::util::RandomModulo(16);  // 0x5011f0
    std::vector<i8> out(count > 0 ? static_cast<size_t>(count) : 0);
    ComputeCityTaxRates(base, cityIds, count, out.empty() ? nullptr : out.data());
    return out;
}

} // namespace guild::world
