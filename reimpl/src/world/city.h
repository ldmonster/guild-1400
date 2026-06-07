#pragma once
// City records + the economy parameter table seed for the Guild simulation.
//
// Translated functions:
//   VIBE_City_InitParameterTable  0x577a9c  (seeds the 28-good economy table)
//   VIBE_City_ParseCsvFieldList   0x50704c  (CSV -> int list)
//   VIBE_Util_ParseInt            0x5dc070  (lenient ascii -> int)
// Plus a self-contained INI-section field mapper that builds a 756-byte
// CityRecord without the Win32 GetPrivateProfileStringA dependency (the real
// VIBE_City_LoadDefinitionIni 0x507144 reads keys via the platform INI API,
// which lives behind the shim; the field layout it writes is reproduced here).
#include "guild/common/types.h"
#include "world/types.h"

namespace guild::world {

// gilde.exe byte_13CD6A0 @0x13CD6A0 — the multi-city record array (stride 756).
extern CityRecord g_cities[kCityMaxCount];

// gilde.exe 0x1234750 — the 28-entry economy good/profession table.
extern GoodSlot g_goods[kGoodCategoryCount];

// gilde.exe flt_641DA8 — the cap/equilibrium divisor seeded by InitParameterTable
// (left at the incoming FP-register value in the original; we model it as a
// settable scalar the price/delta code divides by). Tests set it explicitly.
extern float g_capDivisor;

// gilde.exe flt_641FD4 / flt_641FD8 — city economy totals (money vs goods).
extern float g_cityTotalMoney;
extern float g_cityTotalGoods;

// gilde.exe 0x5dc070 — VIBE_Util_ParseInt (__usercall, eax=(s@eax)).
// Skips leading blanks, optional +/- sign, then base-10 digits; stops at the
// first non-digit. Matches the binary's lenient parser (used by the INI loader).
i32 UtilParseInt(const char* s);

// gilde.exe 0x50704c — VIBE_City_ParseCsvFieldList (__usercall,
//   eax=(text@eax), edx=(out@edx as int*), ebx=(maxFields@ebx)).
// Splits `text` on commas, ParseInt's each token, writes up to `maxFields`
// ints to `out`. Returns the number of fields parsed. When the text runs out
// of commas before maxFields, the remaining slots keep their prior value (the
// original re-scans to a NUL and stops). Returns count written.
int CityParseCsvFieldList(const char* text, i32* out, int maxFields);

// gilde.exe 0x577a9c — VIBE_City_InitParameterTable.
// Zeroes the 28-good accumulator/delta span and seeds every good's drift weight
// (+0), contribution weight (+2) and cap (+4) with the hand-tuned defaults from
// the binary. `capDivisor` is the FP value the original leaves in flt_641DA8.
void CityInitParameterTable(float capDivisor);

// ---------------------------------------------------------------------------
// Self-contained INI field mapper (replaces the Win32 GetPrivateProfileStringA
// path of VIBE_City_LoadDefinitionIni). Given an in-memory INI text, fills the
// CityRecord at g_cities[index]. Faithful to the loader's field placement; the
// 16-good Import/Export and NachbarStadt recursion (which need live object/world
// state) are out of scope and left zero.
// ---------------------------------------------------------------------------
struct IniDocument; // opaque, defined in city.cpp

// Parse a flat INI text (sections in [..], key=value lines) into a doc.
IniDocument* IniParse(const char* text);
void IniFree(IniDocument* doc);
// Look up [section] key, returning `def` if absent. Section/key are matched
// case-sensitively (as the city .ini files are authored).
const char* IniGet(const IniDocument* doc, const char* section,
                   const char* key, const char* def);

// Build g_cities[index] from an already-parsed INI doc. Mirrors the field
// placement of VIBE_City_LoadDefinitionIni 0x507144.
void CityLoadFromIni(const IniDocument* doc, int index);

} // namespace guild::world
