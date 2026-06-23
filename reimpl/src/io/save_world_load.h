#pragma once
// gilde.exe — guild::io  (MODULE: full-scenario world load driver + missing table
// loaders)
//
// This is the LOAD side of the savegame/city pipeline. The top loader
// VIBE_Save_LoadGameFile @0x5a7604 drives a fixed sequence of per-table loaders
// over the (already gunzipped) VFS stream, populating the live world arrays. The
// recovered order (decompiled @0x5a7604) is:
//
//   1. VIBE_Save_LoadHeaderAndThumbnail   @0x5a7af0  (io/save.cpp — reused)
//   2. <scalar block, version-gated>      @0x5a76d6  (io/save.cpp — reused)
//   3. VIBE_Save_LoadPersonIndexTable     @0x5a7ffc  (map-tile / scene-node table)
//   4. VIBE_Save_LoadPersonTable          @0x5a8190  (object array — io/save_person — reused)
//   5. VIBE_Save_LoadGlobalCounters       @0x5a86d0  (building/counter table, 16x152)
//   6. VIBE_Save_LoadCityRecords          @0x5a8d3c  (PERSON/scene records, 536-stride)
//   7. VIBE_Save_LoadBuildingSlotTables   @0x5aa058  (5 city-slot tables + 4 city-info)
//   8. (VIBE_Amt_LoadAemter if version >= 0x10045)
//   9. VIBE_Save_PostLoadInitScene        @0x5a7ef8
//  10. if (header.flag & 2) PARTIAL: close + relink + return  (the .cty/network case)
//      else FULL: Gesetz/MapTiles/GameGlobals/Avatar/Object/Amt/History/ActionQueues/
//                 Hotkey + LoadCharacters + Mission + Hotkey tables.
//
// The real `Resources/gamedata/Cities/*.cty` city seeds are SAVE-GAME scenarios with
// header.flag == 2 (version 0x1003B for the shipped cities), so loading a `.cty`
// takes the PARTIAL path (steps 1-10): it populates the map-tile/scene table, the
// object array, the building table, and — the heart of the world — the 536-byte
// person/scene record array (g_persons), then relinks pointers and returns.
//
// This module recovers the four loaders that were previously deferred
// (LoadPersonIndexTable, LoadCityRecords, LoadGlobalCounters, LoadBuildingSlotTables)
// plus the live-actor LoadCharacterSlot, byte-for-byte and version-gated, and the
// full load driver. It REUSES the existing serializers (io/save, io/save_person,
// io/save_building, io/save_tables) and the live entity arrays (sim/entity) — it
// does NOT redefine them.
//
// Live-state integration that depends on the render/universe/mesh subsystems
// (PostLoadInitScene scene refresh, LoadCharacters mesh/avatar creation, the
// BuildingSlotTables text-name lookup tail) is out of scope for a portable load and
// is listed in the report; the SERIALIZATION (every byte read in the exact order)
// is reproduced here so the world arrays populate from a real city.
#include "guild/common/types.h"
#include "io/vfs.h"
#include "sim/entity.h"

#include <vector>

namespace guild::io {

// ===========================================================================
// Recovered constants.
// ===========================================================================
// Map-tile / scene-node index table: dword_13CE290, stride 67, 8192 slots.
// LoadPersonIndexTable reads `count`, then per slot 8 fields (2+4+4+4+4+1+1+0x1F),
// and stores `count` into dword_6498C0 (the flat-scan bound).
constexpr int        kSceneTileStride   = 67;
constexpr int        kSceneTileCapacity = 8192;
constexpr guild::u32 kSceneTileScanBytes = 548864; // 67 * 8192

// Building / counter table: word_13C3110, stride 164, 16 records.
// LoadGlobalCounters reads 16 records; at version 0x1003B each record is 152 bytes.
constexpr int kBuildCounterStride   = 164;
constexpr int kBuildCounterCount    = 16;

// City / person-scene records: word_12CE910, stride 536 (268 words), 768 slots.
// LoadCityRecords reads a preamble (marker word, count, two ids, 8 handler ids) then
// `count` records, each scattered into word_12CE910[268 * index] by its leading word.
constexpr int        kCityRecStride   = 536;
constexpr int        kCityRecCapacity = 768;
constexpr guild::i32 kCityCounterBiasA = 1342; // +84 dword += 1342 on load
constexpr guild::i32 kCityCounterBiasB = 1468; // +396 dword += 1468 on load

// Building-slot tables (LoadBuildingSlotTables): 5 city-slot tables of a 16-byte
// header + 62 sub-records (128-byte stride in memory), then 4 city-info records
// (756-byte stride). dword_13C3B50 / byte_13CD6A0 bases.
constexpr int kCitySlotTableCount = 5;
constexpr int kCitySlotSubCount   = 62;
constexpr int kCityInfoRecCount   = 4;

// Live-actor record (LoadCharacterSlot): 0x204 == 516 bytes; allocated by
// VIBE_Character_AllocSlotAtIndex. The serialized fields are a sparse subset.
constexpr int kLiveActorRecSize = 516;

// ===========================================================================
// A self-contained snapshot of the FULL loaded world state (the portable subset).
// Mirrors the live globals the loaders populate; tests inspect it directly.
// ===========================================================================
struct WorldState {
    // --- map-tile / scene-node index table (dword_13CE290) -----------------
    guild::u32 sceneTileCount = 0;                 // dword_6498C0
    guild::u8  sceneTiles[kSceneTileScanBytes] = {}; // 67 * 8192

    // --- object / building array (dword_13CE298, 169-stride) ---------------
    guild::u32 objectCount = 0;                     // first dword of LoadPersonTable

    // --- building / counter table (word_13C3110, 164-stride x16) -----------
    guild::u8  buildCounters[kBuildCounterStride * kBuildCounterCount] = {};

    // --- person / scene records (word_12CE910, 536-stride x768) ------------
    guild::u16 cityMarker = 0;                       // word_63CC5C
    guild::u32 cityRecCount = 0;                      // dword_647724
    guild::u32 playerIdA = 0;                         // dword_6498E8
    guild::u32 playerIdB = 0;                         // dword_6498EC[0]
    guild::u32 handlerIds[8] = {};                    // dword_6498F0[8] (>=0x10017)

    // --- building-slot tables ----------------------------------------------
    bool buildingSlotsLoaded = false;

    // The live person array (word_12CE910) is the canonical sim::g_persons; the
    // loaders write directly into it. `persons()` exposes the raw 536-byte records.
    guild::u8* personBase();
    // The live object array (dword_13CE298) is sim::g_objects.
    guild::u8* objectBase();
    // The live scene-tile array maps to WorldState::sceneTiles (portable copy).
};

// ===========================================================================
// Missing table loaders (byte-exact, version-gated). All take an open VFS stream
// `h` and the file version `version` (== SaveVersionGet()); they read into the
// supplied bases and return true on a fully-parsed table.
// ===========================================================================

// VIBE_Save_LoadPersonIndexTable @0x5a7ffc — read `count`, then per slot the 8
// fields into the 67-byte scene-tile records (consecutively, base + i*67). Stores
// `count` into *countOut (dword_6498C0). The original first clears the whole tile
// array (VIBE_World_RelinkObjectOwners' tail memset); the portable loader takes a
// pre-zeroed `tileBase`.
bool LoadPersonIndexTable(VfsHandle* h, guild::u8* tileBase, guild::u32* countOut);

// VIBE_Save_LoadGlobalCounters @0x5a86d0 — read 16 building/counter records into
// word_13C3110[82*i] (164-stride), version-gated. Returns true on success.
bool LoadGlobalCounters(VfsHandle* h, guild::u8* counterBase, guild::u32 version);

// VIBE_Save_LoadCityRecords @0x5a8d3c — read the preamble then `count` 536-byte
// person/scene records, each scattered into personBase + 536*index by its leading
// marker word. Counter biases (+1342 / +1468) are re-applied. Out-params receive
// the preamble fields. Returns true on success.
//
// `personBase` is the live 536-stride array (sim::g_persons as bytes); records are
// written at personBase + 536 * (leading word value).
bool LoadCityRecords(VfsHandle* h, guild::u8* personBase, guild::u32 version,
                     guild::u16* markerOut, guild::u32* countOut,
                     guild::u32* idAOut, guild::u32* idBOut,
                     guild::u32 handlerIdsOut[8]);

// VIBE_Save_LoadBuildingSlotTables @0x5aa058 — read the 5 city-slot tables (each a
// 16-byte header + 62 sub-records) into `slotBase` (7952-byte stride per table) and
// the 4 city-info records (756-byte stride) into `cityInfoBase`, version-gated. The
// original's trailing text-name lookup (the localized city display name) is omitted.
// Returns true on success.
bool LoadBuildingSlotTables(VfsHandle* h, guild::u8* slotBase,
                            guild::u8* cityInfoBase, guild::u32 version);

// VIBE_Save_LoadCharacterSlot @0x5a96c0 — read one live-actor record's serialized
// fields into the 516-byte `rec` (already allocated at the persisted index). Returns
// the person-id link (dword at +300, i.e. *((DWORD*)rec+75)) via *personIdOut.
// Version-gated: the +424 0x40 block is read only for version >= 0x10013.
bool LoadCharacterSlot(VfsHandle* h, guild::u8* rec, guild::u32 version,
                       guild::i32* slotIndexOut, guild::u32* personIdOut);

// ===========================================================================
// The full load driver.
// ===========================================================================

// gilde.exe 0x5abb84 — VIBE_Save_RelinkLoadedPointers, the person-record COLUMN
// slice (0x5abbe2..0x5abc4f): for every live record, the saved link IDs at
// +364 (homeBld) / +368 (workBld) are resolved against the object/building
// array (-1 or miss -> 0), the +380 He link is cleared (the partial .cty path
// loads no He records, so every id misses) and the +388 live-char pointer is
// zeroed (@0x5abc3d). Gated on resolving the local player record dword_6498E4
// (@0x5abb8e); returns false (nothing relinked) when it does not resolve.
// LoadWorld/LoadWorldEx run this automatically at the original's call position
// (after the partial-path tables / the full tail); exposed for tests.
bool RelinkPersonRecordColumns(guild::i32 playerId);

// VIBE_Save_LoadGameFile @0x5a7604 — open `path` through the VFS (transparent
// gunzip for a `.cty`/`.SAV.gz`), reset the world, load the header + scalar block,
// then the table loaders in the recovered order, populating `world` and the live
// `sim::g_persons` / `sim::g_objects` arrays. For a partial (header.flag & 2) file —
// the city-seed case — the load stops after the scene/building/person tables.
//
// Returns true on a fully-parsed, version-valid load. On any short read the world is
// left partially populated and false is returned (matching the original's
// abort-and-return-0 contract).
bool LoadWorld(const char* path, WorldState& world);

// LoadWorld + capture of the EMBEDDED CITY SCENE STREAM. After the table loaders,
// the original's step 9 (VIBE_Save_PostLoadInitScene @0x5a7ef8) hands the still-open
// save stream to VIBE_Scene_LoadFromStream @0x5e7e38 (edx = the stream handle, not a
// path) — i.e. a full .ed3-grammar scene blob is serialized INSIDE the .cty at that
// position, carrying every city node's world position (+76), rotation euler (+132)
// and the +512 owner-object id that VIBE_Object_RebuildModelByOwner @0x5a8140 later
// matches against the live object records. `embeddedSceneOut` (optional) receives
// the raw remaining stream bytes from exactly that position (the scene tag dword
// onward), so a host scene parser (render/scene_load + play::ParseSceneObjects) can
// decode the REAL city placements. Capture is gated to version < 0x10045 (>= reads
// the Amt table first, which this slice does not parse; the shipped cities are
// 0x1003B). Pass null to behave exactly like LoadWorld.
bool LoadWorldEx(const char* path, WorldState& world,
                 std::vector<guild::u8>* embeddedSceneOut);

} // namespace guild::io
