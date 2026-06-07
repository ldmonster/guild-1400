#pragma once
// Plant / vegetation growth for the Guild simulation (gilde.exe). MODULE:
// Animal/Plant (namespace guild::sim).
//
// A building's planting plot holds a fixed array of 64 growth-stage records
// (24 bytes each). VIBE_Plant_AdvanceGrowthStage advances every live plant by
// one stage (capped at the species' max stage from the office/plant type table)
// once per growth tick, releasing the old vegetation model and reloading at the
// new stage. The vegetation model load/hide leaves (LoadVegetationModel,
// EnsureModelsLoaded, HideAllModels) are render-coupled and routed through the
// IPlantWorld hook.
//
// Translated functions:
//   VIBE_Plant_AdvanceGrowthStage  0x56eba4  (the growth rule)
//   VIBE_Plant_EnsureModelsLoaded  0x56ef2c  (model-presence pass; via hook)
//   VIBE_Plant_HideAllModels       0x56ef78  (visibility pass; via hook)
// LoadVegetationModel (0x56ec14) is a pure render leaf — LISTED AS DEFERRED.
#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Plant growth-stage record (gilde.exe 24-byte stride, 64 per plot).
//   +0x08  type id (the high word, HIWORD(rec[2]), keys the species table)
//   +0x0D  growth stage byte (-1/0xFF == empty slot; also the alive marker)
//   +0x14  vegetation model ptr (dword [5]; 0 == not loaded)
// (Other fields: transform / plot scratch, untouched by the growth rule.)
// ---------------------------------------------------------------------------
constexpr int kPlantRecordStride = 24;
constexpr int kPlantPlotCapacity = 64;   // a1+384 dwords == +1536 bytes == 64*24

GUILD_PACKED_BEGIN
struct PlantRec {
    u8  pad0[8];   // +0x00..+0x07
    u16 pad8;      // +0x08  (LOWORD of rec[2])
    u16 typeId;    // +0x0A  (HIWORD of rec[2]) species type id
    u8  pad12;     // +0x0C
    u8  stage;     // +0x0D  growth stage / alive marker (0xFF == empty)
    u8  pad14[6];  // +0x0E..+0x13
    i32 model;     // +0x14  vegetation model ptr token (0 == unloaded)
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(PlantRec) == kPlantRecordStride, "PlantRec stride must be 24");

// ---------------------------------------------------------------------------
// Species table leaf: VIBE_Amt_FindOfficeTypeRecord(typeId) @0x56e850 returns a
// type descriptor whose byte +68 is the max growth stage. Modeled as a hook
// returning that max stage directly (0xFF means "type not found" -> the original
// retries once then reads garbage; we clamp to "no growth" by returning the
// current stage). Default backend: a flat per-type max-stage table.
// ---------------------------------------------------------------------------
using PlantMaxStageFn = u8 (*)(u16 typeId);
void PlantSetMaxStageHook(PlantMaxStageFn fn);

// World/render leaf hook for model release/load/visibility.
struct IPlantWorld {
    virtual ~IPlantWorld() = default;
    // VIBE_Object_DetachAndRelease — drop the old vegetation model.
    virtual void ReleaseModel(i32 model) { (void)model; }
    // VIBE_Plant_LoadVegetationModel — load the model for a plant at its stage;
    // returns the new model token (nonzero) or 0. Default 0 (deferred render).
    virtual i32 LoadModel(PlantRec* p) { (void)p; return 0; }
    // VIBE_Universe_RestoreObjectStates(model, visible) — show/hide.
    virtual void SetModelVisible(i32 model, bool visible) {
        (void)model; (void)visible;
    }
};
void SetPlantWorld(IPlantWorld* world);
IPlantWorld* PlantWorld();

void ResetPlant();  // test/setup helper

// gilde.exe 0x56eba4 — VIBE_Plant_AdvanceGrowthStage  (__usercall, eax=plot a1).
// `plot` points at the 64-record growth-stage array. For each live record
// (stage != 0xFF): release its current model, look up the species max stage,
// and if stage < max increment it. Then runs EnsureModelsLoaded over the plot.
void Plant_AdvanceGrowthStage(PlantRec* plot);

// gilde.exe 0x56ef2c — VIBE_Plant_EnsureModelsLoaded. For each live record:
// load the model if absent, then mark it visible.
void Plant_EnsureModelsLoaded(PlantRec* plot);

// gilde.exe 0x56ef78 — VIBE_Plant_HideAllModels. For each live record with a
// loaded model, hide it.
void Plant_HideAllModels(PlantRec* plot);

}  // namespace guild::sim
