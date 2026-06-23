#pragma once
// gilde.exe 0x4c2ba0 — VIBE_Gesetz_ComputeMaxWantedLevel.
//
// Computes a perpetrator's *maximum wanted level* — the probability the city
// guard catches a law-breaker — from the strongest guard station owned by the
// same faction. The original scans the 256-slot building array (stride 169) for
// buildings that are (a) alive, (b) owned by the same faction word (+39 == *a1),
// and (c) of building type 7 (the type byte at dword_13CE294 + 589*buildingId).
// For each such guard building it sums workstation category-10 capacity
// (VIBE_Building_SumWorkstationByCategory(b, 10, 1)); a building whose sum is
// exactly 75 short-circuits the whole scan to 0.75, otherwise it tracks the max.
// After the scan the level is
//
//     level = (float)( (double)maxSum * 0.01f )      // flt_61E588 == 0.01f
//
// (computed in double, then narrowed to float exactly as the original's
// `return (float)(...)`).
//
// world/law.{h,cpp} already carries the *scalar* tail of this (the
// (sum==75)?0.75:sum*0.01 formula, abstracted, used by EvaluateViolation). This
// module is the faithful 0x4c2ba0 reconstruction including the building scan;
// the building enumeration is routed through a settable accessor so the scan is
// standalone-testable. The two coexist (different symbols, no ODR clash).
#include "guild/common/types.h"

namespace guild::world {

// flt_61E588 @0x61E588 = 0A D7 23 3C = 0.01f (the per-unit wanted scalar).
extern const float kWantedLevelScalar;

// ---------------------------------------------------------------------------
// One building slot, as the scan reads it. The original walks dword_13CE298
// (the 169-byte-stride building array) for indices 0..255; for each it needs the
// alive byte (*b), the owner word (*(u16*)(b+39)), the building id (== *b, used
// to index the type table), and the workstation category-10 sum.
// ---------------------------------------------------------------------------
struct GuardBuildingAccessor {
    int count = 0; // number of slots to scan (the original caps at 256)
    // alive(i): *b — non-zero when the slot holds a live building.
    bool (*alive)(int i, void* ctx) = nullptr;
    // owner(i): *(u16*)(b+39) — the faction/owner word compared to perpFaction.
    u16  (*owner)(int i, void* ctx) = nullptr;
    // type(i): *(byte*)(dword_13CE294 + 589 * *b) — building type (7 == guard).
    int  (*type)(int i, void* ctx) = nullptr;
    // sumCat10(i): VIBE_Building_SumWorkstationByCategory(b, 10, 1) for slot i.
    int  (*sumCat10)(int i, void* ctx) = nullptr;
    void* ctx = nullptr;
};

// ---------------------------------------------------------------------------
// The scalar tail: (sum == 75) ? 0.75 : (float)((double)sum * 0.01f). Returned
// as a float-narrowed double, matching the original's `return (float)(...)` /
// the literal `return 0.75;` short-circuit.
// ---------------------------------------------------------------------------
double WantedLevelFromMaxSum(int maxSum);

// ===========================================================================
// gilde.exe 0x4c2ba0 — VIBE_Gesetz_ComputeMaxWantedLevel (__usercall: eax = a1,
// st0 = result). `perpFaction` is *a1 (the perpetrator's owner/faction word the
// scan matches buildings against). Scans `acc` for guard buildings (alive, owner
// == perpFaction, type == 7); if any has cat-10 sum exactly 75 the result is
// immediately 0.75, else the result is (float)(maxSum * 0.01f) (0 if none found).
// ===========================================================================
double ComputeMaxWantedLevel(u16 perpFaction, const GuardBuildingAccessor& acc);

} // namespace guild::world
