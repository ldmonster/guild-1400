#include "sim/plant.h"

namespace guild::sim {

// Faithful 1:1 port of the plant growth + model-presence passes from gilde.exe.
// The growth rule (stage++ capped at species max) is exact; the render leaves
// (model release/load/visibility, species-table lookup) are behind hooks.

// ---------------------------------------------------------------------------
// Species max-stage hook + default flat table.
// ---------------------------------------------------------------------------
static u8 DefaultMaxStage(u16 typeId) {
    // Default backend: a small per-type cap so growth saturates. The live game
    // reads typeRecord[+68]; absent the data files we cap every species at 4.
    (void)typeId;
    return 4;
}
static PlantMaxStageFn g_maxStageFn = &DefaultMaxStage;
void PlantSetMaxStageHook(PlantMaxStageFn fn) {
    g_maxStageFn = fn ? fn : &DefaultMaxStage;
}

// ---------------------------------------------------------------------------
// World hook.
// ---------------------------------------------------------------------------
static IPlantWorld g_defaultWorld;
static IPlantWorld* g_world = &g_defaultWorld;
void SetPlantWorld(IPlantWorld* world) { g_world = world ? world : &g_defaultWorld; }
IPlantWorld* PlantWorld() { return g_world; }

void ResetPlant() {
    g_maxStageFn = &DefaultMaxStage;
    g_world = &g_defaultWorld;
}

// A record is live iff its stage byte (+0x0D) is not -1 (0xFF). The original
// tests *(int*)(rec+10) >> 24 != -1, i.e. the sign-extended high byte of the
// dword at +10, which is the byte at +13.
static bool PlantAlive(const PlantRec* p) {
    return static_cast<i8>(p->stage) != -1;
}

// ===========================================================================
// VIBE_Plant_AdvanceGrowthStage  0x56eba4
//   for each record in plot[0..63]:
//     if (alive):
//       if (rec.model) { DetachAndRelease(rec.model); rec.model = 0; }
//       def = FindOfficeTypeRecord(rec.typeId);  max = def[+68];
//       if (rec.stage < max) ++rec.stage;
//   EnsureModelsLoaded(plot);
// ===========================================================================
void Plant_AdvanceGrowthStage(PlantRec* plot) {
    if (!plot)
        return;
    for (int i = 0; i < kPlantPlotCapacity; ++i) {
        PlantRec* p = &plot[i];
        if (!PlantAlive(p))
            continue;
        if (p->model) {
            g_world->ReleaseModel(p->model);
            p->model = 0;
        }
        u8 maxStage = g_maxStageFn(p->typeId);
        if (p->stage < maxStage)
            p->stage = static_cast<u8>(p->stage + 1);
    }
    Plant_EnsureModelsLoaded(plot);
}

// ===========================================================================
// VIBE_Plant_EnsureModelsLoaded  0x56ef2c
//   for each live record: if (!model) LoadVegetationModel(rec);
//                         if (model) RestoreObjectStates(model, 1 /*visible*/);
// ===========================================================================
void Plant_EnsureModelsLoaded(PlantRec* plot) {
    if (!plot)
        return;
    for (int i = 0; i < kPlantPlotCapacity; ++i) {
        PlantRec* p = &plot[i];
        if (!PlantAlive(p))
            continue;
        if (!p->model)
            p->model = g_world->LoadModel(p);
        if (p->model)
            g_world->SetModelVisible(p->model, true);
    }
}

// ===========================================================================
// VIBE_Plant_HideAllModels  0x56ef78
//   for each live record with a loaded model: RestoreObjectStates(model, 0).
// ===========================================================================
void Plant_HideAllModels(PlantRec* plot) {
    if (!plot)
        return;
    for (int i = 0; i < kPlantPlotCapacity; ++i) {
        PlantRec* p = &plot[i];
        if (PlantAlive(p) && p->model)
            g_world->SetModelVisible(p->model, false);
    }
}

}  // namespace guild::sim
