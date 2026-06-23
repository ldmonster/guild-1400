#pragma once
// gilde.exe — Vegetation model load leaf (namespace guild::sim). MODULE: Plant /
// vegetation (prefix VIBE_Plant_*).
//
// This is the render-coupled model-load leaf that src/sim/plant.cpp's
// IPlantWorld::LoadModel previously DEFERRED. It is reconstructed here 1:1 from
// gilde.exe:
//
//   VIBE_Plant_LoadVegetationModel  0x56ec14  (path-select + load-trigger + place)
//
// The deterministic, golden-pinnable part is the asset-path selection:
//   * species record = VIBE_Amt_FindOfficeTypeRecord(HIWORD(rec[2]))  @0x56e850
//   * level  = max(1, (rec[+0x0A] >> 24) / speciesRecord[+0x43])   (signed idiv)
//   * fall flag selects "_FALL.ogr" suffix vs plain ".ogr"
//   * path   = "<dataDir>vegetation/*pfl_<speciesName>_0<level>[ _FALL].ogr"
// The model load itself (Scene_LoadObjectGroup), the name-suffix append
// ("_ID<recId>"), and the world placement (a 3x3 basis from the placement-ref
// object at dword_64A028 applied to the plant's plot coords rec[+8]/rec[+9]) are
// reconstructed; the cross-module side effects (load, set-position, light-cache)
// are routed through VegetationHooks so the path/level logic stays unit-testable.
//
// The species record layout (the only fields touched, gilde.exe 70-byte/35-word
// stride starting at &word_63D738):
//   +0x02 (rec+1 as char*, i.e. word[1..]) — null-terminated species NAME used in
//         the path (the original passes `v3 + 1`, the record+2 byte address).
//   +0x43 (byte 67) — the per-level divisor for the growth-stage -> level map.
#include "guild/common/types.h"

#include <string>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Plant growth-stage record fields this leaf reads (the 24-byte PlantRec from
// sim/plant.h, addressed here as raw bytes so the exact original offsets are
// visible). Provided to the path-select helper as a small POD so the logic is
// testable without the species-table globals.
// ---------------------------------------------------------------------------
struct VegPlantView {
    i32 recId;        // rec[0]   (a1[0]) — used in the "_ID%i" name suffix
    u8  plotX;        // rec[+8]  (a1+8 byte) — plot column, placement basis col 0
    u8  plotY;        // rec[+9]  (a1+9 byte) — plot row,    placement basis col 1
    i32 stagePacked;  // rec[+0x0A] dword — high byte (>>24, signed) is the stage
    u16 typeId;       // HIWORD(rec[2]) == rec[+0x0A] high word? -> see note
};

// ---------------------------------------------------------------------------
// Species-table record fields the path-select needs. The original walks the raw
// 35-word table; we expose the two touched fields.
// ---------------------------------------------------------------------------
struct VegSpeciesRecord {
    bool        found = false;   // FindOfficeTypeRecord returned non-null
    std::string name;            // record+2 (word[1..]) null-terminated name
    u8          levelDivisor = 1;// byte +0x43 (idiv divisor; 0 would #DE in orig)
};

// Lookup hook: VIBE_Amt_FindOfficeTypeRecord(typeId) @0x56e850.
using VegFindSpeciesFn = VegSpeciesRecord (*)(u16 typeId);

// ---------------------------------------------------------------------------
// 1:1 path-select core (the golden-pinnable arithmetic + sprintf format).
//   level = (plant.stagePacked >> 24) / species.levelDivisor   (signed, trunc)
//   if (level < 1) level = 1;
//   fall ? "%svegetation/*pfl_%s_0%i_FALL.ogr"
//        : "%svegetation/*pfl_%s_0%i.ogr"
// Returns the level actually used (>=1) via *outLevel (optional).
// `dataDir` is the byte_122F098 asset base path; `fall` is the autumn flag (the
// edx argument the original tests at 0x56ec2c).
// ---------------------------------------------------------------------------
i32 Veg_ComputeLevel(const VegPlantView& plant, const VegSpeciesRecord& species);
std::string Veg_BuildModelPath(const std::string& dataDir,
                               const VegSpeciesRecord& species,
                               i32 level, bool fall);

// ---------------------------------------------------------------------------
// Cross-module side-effect hooks. Inert defaults make the load a no-op so the
// path-select logic is testable; the live game installs real backends.
// ---------------------------------------------------------------------------
struct VegetationHooks {
    // Asset base path (byte_122F098). Default "".
    const char* (*dataDir)() = nullptr;
    // Autumn/fall season flag (the edx arg the original threads in). Default false.
    bool (*isFallSeason)() = nullptr;
    // VIBE_Amt_FindOfficeTypeRecord(typeId) @0x56e850.
    VegFindSpeciesFn findSpecies = nullptr;
    // VIBE_Scene_LoadObjectGroup(path,0,0,0) @0x5e84f4 -> model node ptr (0 fail).
    void* (*loadObjectGroup)(const char* path) = nullptr;
    // Append the "_ID%i" suffix onto the loaded node's interleaved name buffer.
    // Models the original's wide-ish byte-pair copy at node end. Default no-op.
    void (*appendNameSuffix)(void* node, const char* suffix) = nullptr;
    // Mark node[+0x218 dword] = 1 (the "is vegetation/static decor" flag).
    void (*markVegetationFlag)(void* node) = nullptr;
    // The placement-reference object (dword_64A028); 0 == none (no placement).
    const void* (*placementRef)() = nullptr;
    // Read a float field at byte offset `off` of the placement-ref object.
    float (*placementFloat)(const void* ref, int off) = nullptr;
    // Read the index-map byte: *(u8*)( ref[0]*plotY + plotX + ref[+0x10] ).
    // (ref[0]==*(i32*)ref is the row stride; ref[+0x10]==*(i32*)(ref+0x10) is the
    // map base ptr.) The same byte feeds all three +0xC0/+0xC4/+0xC8 terms.
    u8 (*placementMapByte)(const void* ref, int plotX, int plotY) = nullptr;
    // VIBE_Object_SetPosition(node, float[3]) @0x5af38c.
    void (*setPosition)(void* node, const float pos[3]) = nullptr;
    // VIBE_Light_RequestObjectCache(node) @0x5c8538.
    void (*lightRequestCache)(void* node) = nullptr;
    // VIBE_ErrorLog_ReportMessage(msg) @0x438da8.
    void (*reportError)(const char* msg) = nullptr;
};
void SetVegetationHooks(const VegetationHooks* hooks);
const VegetationHooks& GetVegetationHooks();
void ResetVegetationHooks();

// ---------------------------------------------------------------------------
// gilde.exe 0x56ec14 — VIBE_Plant_LoadVegetationModel.
// Selects the path (above), loads the model group, on success: stores it back as
// the plant's model token (returned), appends the "_ID<recId>" suffix, sets the
// vegetation flag, computes & applies the world placement from the placement-ref
// basis, and requests the light cache. On failure logs the
// "init_SetPflanzenMap(): Could not load 3D-Objekt-roup '%s'..." error and
// returns nullptr. The original writes the model back into rec[5] (+0x14) itself;
// we return it so plant.cpp's IPlantWorld::LoadModel can store it.
void* Plant_LoadVegetationModel(const VegPlantView& plant);

}  // namespace guild::sim
