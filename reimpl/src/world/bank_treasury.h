#pragma once
// Guild city-tax-rate seeding — the deterministic finance arithmetic of
// VIBE_Office_ComputeCityTaxRates (gilde.exe 0x5011dc).
//
// This is the lone genuinely-untranslated finance leaf in the Bank/Treasury/
// Loan/Exchange/Tax domain: the entire rest of that domain (the Tax_Collect*
// formulas, the Bank/Credit loan-interest + amortization math, the Money_*Rate
// arithmetic, the GuildTreasury cash sums, the Amt_* prosperity/wage/tax-pass
// cadence) was already reconstructed in earlier waves under bare names
// (TaxCollectTradeIncome, BankInterestBase, LoanComputeInterest,
// AmtComputeOfficeWages, EconomyLookupRateScalar, PersonGetCashAmount, ...).
// See the report for the full done-set survey.
//
// Translated:
//   VIBE_Office_ComputeCityTaxRates  0x5011dc
//
// What the original does, once per "sync buildings and offices" pass
// (sole caller VIBE_Scene_SyncBuildingAndOffices @0x501274): for each city in
// the input list it writes a fresh tax-rate byte. The rate is drawn from a
// 16-entry seed table at a rotating offset (a single RandomModulo(16) chooses
// the starting offset, then it advances one slot per city), then biased down by
// half the city's office-definition "rank/level" field. Concretely:
//
//   base = RandomModulo(16)                     // 0x5011f0 — one roll per pass
//   for (i = 0; i < count; ++i) {
//       entry = GetEntryByCity(cityIds[i])      // 0x47efb4
//       def   = GetDefinition(entry.field0)     // 0x47f008
//       rate  = kSeedRates[(base + i) % 16]     // 0x50123f..0x501256 (>>24 of dword)
//       out[i] = (i8)(rate - (def.byte2 / 2))   // 0x501245..0x501263 (signed /2)
//   }
//   return out[count-1]                         // last byte computed (eax)
//
// The GetEntryByCity / GetDefinition table walks are office-table reads owned by
// the office module; here they are abstracted to a caller-supplied resolver so
// the pure rate arithmetic is testable and the function does not pull the whole
// office subsystem into the link. A default resolver (inert: every city resolves
// to definition-level 0) is defined in the library .cpp.
#include "guild/common/types.h"

#include <vector>

namespace guild::world {

// ===========================================================================
// Recovered constant table — dword_4FFA70 (get_bytes 0x4FFA70, 16 bytes copied
// via four `movsd` at 0x501209..0x50120c into the local rate frame).
//   18 27 33 3F  45 4B 12 39  3F 06 0C 2D  33 1E 45 4B
// (The 16 bytes immediately following — 10 12 14 18 as dwords {16,18,20,24} —
// are a separate table the function never touches; only the first 16 are used.)
// Values are read back a byte at a time via the `[esp+edx+0x15] >> 24` idiom,
// i.e. kSeedRates[index] is the rate for slot `index`.
// ===========================================================================
inline constexpr u8 kCityTaxSeedRates[16] = {
    0x18, 0x27, 0x33, 0x3F, 0x45, 0x4B, 0x12, 0x39,
    0x3F, 0x06, 0x0C, 0x2D, 0x33, 0x1E, 0x45, 0x4B,
};

// gilde.exe 0x501245..0x501250 — the office-level bias. `def.byte2` is the byte
// at offset +2 of the 3-dword OfficeDefinition (var_1E / BYTE2(v16)); it is
// halved with a signed (round-toward-zero) shift: eax -= eax>>31; eax >>= 1.
// Reproduced exactly so negative levels round the C way (toward zero).
i32 CityTaxHalveOfficeLevel(i8 officeDefByte2);

// gilde.exe 0x50123f..0x501263 — the per-city rate cell (without the table walk):
//   out = (i8)( kCityTaxSeedRates[(base + index) % 16] - (defByte2 / 2) )
// `base` is the once-per-pass RandomModulo(16) roll; `index` is the 0-based city
// position. The subtraction and store are 8-bit (`mov [edi-1], al`), so the
// result wraps to a signed byte exactly as the original writes it.
i8 CityTaxRateFor(int base, int index, i8 officeDefByte2);

// ===========================================================================
// Office-table resolver (the GetEntryByCity @0x47efb4 + GetDefinition @0x47f008
// pair). Given a city id it yields the office-definition byte (+2) that biases
// the rate. The default implementation is inert (returns 0 -> no bias), defined
// in the library .cpp. Tests/engine install their own.
// ===========================================================================
using CityOfficeLevelResolver = i8 (*)(i32 cityId, void* ctx);
void CityTaxSetOfficeLevelResolver(CityOfficeLevelResolver resolver, void* ctx);

// gilde.exe 0x5011dc — VIBE_Office_ComputeCityTaxRates.
// Fills `out` (one signed byte per city) with freshly seeded tax rates and
// returns the last byte written (the original's eax). `base` selects the seed
// rotation; pass RandomModulo(16) for the live behaviour or a fixed value for a
// deterministic golden vector. When `cityIds` is empty the original returns the
// raw RandomModulo(16) roll untouched (the loop never runs), so this returns
// `base & 0xFFFF` reduced to int — matching the `and eax,0FFFFh` masking.
int ComputeCityTaxRates(int base, const i32* cityIds, int count, i8* out);

// Convenience overload that draws `base` from the shared RandomModulo(16) RNG
// (guild::util::RandomModulo) exactly as the original does at 0x5011f0, and
// returns the filled vector. Non-deterministic (RNG-driven) by design.
std::vector<i8> ComputeCityTaxRates(const i32* cityIds, int count);

} // namespace guild::world
