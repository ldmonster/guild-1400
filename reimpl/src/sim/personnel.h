#pragma once
// Personnel (staff hiring / wages / assignment) for the Guild simulation
// (gilde.exe). The rules core: wage computation by job category and the
// staff-assignment predicates. The staff-book / mercenary-book UI windows
// (VIBE_Personnel_RunStaffBook, RunMercenaryBook, *BookRow, *RowLabel) are
// GUI/widget-coupled and are LISTED AS DEFERRED (see person module report).
//
// Translated functions:
//   VIBE_Personnel_ComputeWageByCategory  0x594d70
#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::sim {

// Building base-value lookup (VIBE_Building_ComputeItemBaseValue @0x58f328) and
// the building-type -> category mapper (VIBE_BuildingType_MapActionToCategory
// @0x58a25c) are owned by the building module. We forward-declare them as
// mockable hooks so the wage formula is testable in isolation.
using BuildingBaseValueFn = double (*)(int building, int actionByte,
                                       int ctx, int arg);
using ActionCategoryFn = u8 (*)(u8 buildingTypeByte);
void PersonnelSetBuildingBaseValueHook(BuildingBaseValueFn fn);
void PersonnelSetActionCategoryHook(ActionCategoryFn fn);

// gilde.exe 0x594d70 — VIBE_Personnel_ComputeWageByCategory
//   (__usercall: st0=ret, eax=(building@eax), dl=(actionByte@dl), ebx=(arg@ebx)).
// Computes the wage for a worker producing in `building` doing the action whose
// faction/type byte selects a job category. Categories 10/11/12 (administrative)
// use the 9.0 multiplier; everything else uses the 3.0 multiplier, applied to the
// building's base item value.
//   wage = baseValue(building, actionByte, arg) * (cat in {10,11,12} ? 9.0 : 3.0)
// `aiTypeByte` is the byte read from dword_13CE294 + 589*(actionByte) in the
// original (the AiPlayer/type-def table); supplied directly here.
double PersonnelComputeWageByCategory(int building, u8 actionByte, int arg,
                                      u8 aiTypeByte);

} // namespace guild::sim
