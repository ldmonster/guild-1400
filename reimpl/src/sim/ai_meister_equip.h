#pragma once
// ===========================================================================
// ai_meister_equip.h — supplemental leaves + exported globals for the
// MeisterAi equipment/workstation supply sub-planners
// (ai_meister_equip.cpp, gilde.exe 0x45a62c / 0x45ba84 / 0x45bd68 / 0x45c10c / 0x45e350).
//
// This header declares the MeisterEquipLeaves struct (function-pointer table
// for engine leaves that are NOT yet in MeisterAiLeaves) and the two scene-type
// word-lists (word_B56FAC / word_B56FC8) that the CollectStorageItems /
// GatherRequiredItems / EquipStaffWeapon sub-planners consume.
//
// The live bridge wires these before dispatch; tests inject deterministic
// stand-ins. All guarded: null hook == same path original takes on null/zero.
// ===========================================================================
#include "guild/common/types.h"

namespace guild::sim {

// Maximum entries in the scene-type lists (matches kWorkOrderStride = 88 entries):
constexpr int kSceneTypeListCap = 88;

// ---------------------------------------------------------------------------
// Supplemental engine leaves used by the equip/supply cluster.
// Not yet in MeisterAiLeaves (src/sim/ai_meister.h) — surfaced here.
// ---------------------------------------------------------------------------
struct MeisterEquipLeaves {
    // gilde.exe 0x58658c — VIBE_MeisterAi_EvaluateStockNeeds(itemId)
    // Fills seller-list arrays. After call: *outCount entries in
    // outBuilding[]/outHasObj[]/outDeficit[]. On null: *outCount = 0.
    void (*evaluateStockNeeds)(i16 itemId,
                               int* outCount,
                               i32  outBuilding[],
                               i32  outHasObj[],
                               i32  outDeficit[]) = nullptr;
    // gilde.exe 0x583a70 — VIBE_Object_FindObjectById(id) -> object rec ptr or null
    u8*  (*objectFindById)(i32 id) = nullptr;
    // gilde.exe 0x5922d4 — VIBE_Inventory_CollectProductionSlots(objRec, outBuf46dw)
    // Writes production slot info into a 46-dword (184-byte) buffer.
    // outBuf[0] = slot count; type words at &outBuf[18]+stride; counts at &outBuf[26]+stride.
    void (*collectProductionSlots)(u8* objRec, i32* outBuf) = nullptr;
    // gilde.exe 0x592428 — VIBE_Inventory_FindItemStock(containerRec, itemTypeHi) -> count
    i32  (*findItemStock)(u8* containerRec, i16 itemTypeHi) = nullptr;
    // gilde.exe 0x58f6b8 — VIBE_Building_LookupCachedMarketPrice(itemId, currency) -> double
    double (*lookupCachedMarketPrice)(i16 itemId, u8 currency) = nullptr;
    // gilde.exe 0x5878b0 — VIBE_Building_MapTypeToCategory(typeByte) -> int
    int  (*mapTypeToCategory)(u8 typeByte) = nullptr;
};

// Active supplemental leaf table for the equip/supply cluster.
// Defined in ai_meister_equip.cpp; zero-init (all hooks null) by default.
extern MeisterEquipLeaves g_meisterEquipLeaves;

// Scene-type ID word lists — word_B56FAC (production slots) and word_B56FC8 (storage slots).
// Written by VIBE_GameLogic_InitGuardState (0x4520d0); consumed by CollectStorageItems /
// GatherRequiredItems / EquipStaffWeapon. Zero-terminated word arrays, kSceneTypeListCap entries.
extern i16 g_sceneTypeListA[kSceneTypeListCap];  // word_B56FAC
extern i16 g_sceneTypeListB[kSceneTypeListCap];  // word_B56FC8

} // namespace guild::sim
