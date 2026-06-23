// ===========================================================================
// ai_meister — shared foundation: global tables, raw record accessors, the
// command-sink + leaf-hook globals, the game-clock mirror, and the EvaluateMeister
// dispatch shim. The per-routine Calc bodies live in sibling TUs
// (ai_meister_calc_*.cpp / ai_meister_subplanners*.cpp), all #including
// "ai_meister_internal.h" for the shared accessors.
//
// gilde.exe provenance: the globals below mirror the live engine bases named in
// src/sim/ai_meister.h; the dispatch shim mirrors VIBE_Ai_EvaluateMeister's tail
// switch (0x4533a8) which routes each Meister to its Calc by routine.
// ===========================================================================
#include "sim/ai_meister.h"
#include "sim/ai_meister_internal.h"

#include "ai/aiplayer.h"  // guild::ai::MeisterRoutine (dispatch shim only)

namespace guild::sim {

// --- city-tile danger grid (word_12349A0/A2/byte_12349A4) -------------------
u8 g_cityTileGrid[kCityTileBytes] = {};

// --- MeisterAi shared scratch tables (0xB5xxxx) -----------------------------
int g_workOrderCount = 0;   // dword_B56FDC
int g_stockRowCount  = 0;   // dword_B56FE0
u8  g_workOrderTable[kMaxWorkOrders * kWorkOrderStride] = {}; // 0xB56464
u8  g_stockTable[kStockRows * kStockStride] = {};             // 0xB5444E
i32 g_aiSelMeisterBuilding    = 0;  // dword_B53950
i32 g_aiSelMeisterPerson      = 0;  // dword_B53958
i32 g_aiSelMeisterCount       = 0;  // dword_B53964
i32 g_aiSelMeisterBuildingRec = 0;  // dword_B53954
i32 g_aiSelOrderCount         = 0;  // dword_B5396C

// --- command sink + leaf hooks + clock --------------------------------------
MeisterCmdSink*        g_meisterCmdSink = nullptr;
const MeisterAiLeaves* g_meisterLeaves  = nullptr;
GameTime               g_meisterGameTime = {};
i32                    g_meisterTimeExtra = 0;   // unk_13CE85A
u16                    g_meisterTimeTail  = 0;   // unk_13CE85E

// --- type-def / array bases (aimei internal, set by the bridge) -------------
namespace aimei {
u8* g_buildingTypeDefBase = nullptr;  // dword_13CE294 (589-stride)
u8* g_itemTypeDefBase     = nullptr;  // dword_13CE27C (65-stride)
u8* g_objectArrayBase     = reinterpret_cast<u8*>(&g_objects[0]); // dword_13CE298
u8* g_sceneIndexBase      = nullptr;  // dword_13CE290 (67-stride)
i32 g_aiSelStaffSet       = 0;        // dword_B53968
// Type-def table sizes (bound the lookups; the live bridge sets these to the real
// table counts). Generous defaults cover the building-type / item-type id ranges.
int g_buildingTypeDefCount = 256;     // building-type codes are bytes (0..255)
int g_itemTypeDefCount     = 600;     // item/object type ids (e.g. 308..477 used)
} // namespace aimei

void ResetMeisterAiScratch() {
    g_workOrderCount = 0;
    g_stockRowCount  = 0;
    for (auto& b : g_workOrderTable) b = 0;
    for (auto& b : g_stockTable)     b = 0;
    for (auto& b : g_cityTileGrid)   b = 0;
    g_aiSelMeisterBuilding = g_aiSelMeisterPerson = g_aiSelMeisterCount = 0;
    g_aiSelMeisterBuildingRec = g_aiSelOrderCount = 0;
}

// ---------------------------------------------------------------------------
// VIBE_Ai_EvaluateMeister dispatch tail (0x4533a8). ClassifyMeisterRoutine
// (ai/aiplayer.cpp) chose the routine; this shim routes the Meister to the
// matching Calc. CraftProduction / Production / Bank / PlanProduction are owned
// by other modules (production planner cluster) — left to them; this module owns
// the Diebe/Farming/Wache/Ambush brains and the shared CalcAngriff.
// ---------------------------------------------------------------------------
void DispatchMeisterCalc(int routine, u8* meisterRec, int attackBudget) {
    using R = guild::ai::MeisterRoutine;
    switch (static_cast<R>(routine)) {
        case R::kFarming: CalcMeisterFarming(meisterRec); break;
        case R::kWache:   CalcMeisterWache(meisterRec);   break;
        case R::kDiebe:   CalcMeisterDiebe(meisterRec);   break;
        case R::kAmbush:  CalcMeisterAmbush(meisterRec);  break;
        // CalcAngriff is invoked internally by Wache/Diebe/Ambush; it is not a
        // top-level routine of EvaluateMeister. Exposed for direct dispatch/test.
        default:
            (void)attackBudget;
            break;  // kProduction/kCraftProduction/kBank/kPlanProduction: other module
    }
}

} // namespace guild::sim
