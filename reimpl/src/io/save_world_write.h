#pragma once
// gilde.exe — guild::io  (MODULE: full-scenario world WRITE driver + the missing
// Amt (Aemter / office-holder) table serializer pair)
//
// This is the WRITE side of the savegame/city pipeline — the mirror of
// io/save_world_load (VIBE_Save_LoadGameFile @0x5a7604). The top writer
// VIBE_Save_WriteGameFile @0x5a348c drives a fixed sequence of per-table writers
// over an open VFS stream (recovered from the decompiled driver):
//
//   1.  VIBE_Save_WriteScenarioBlock     @0x5a372c  (io/save.cpp — reused;
//                                                    forces version 0x10045)
//   2.  <scalar block, 12/13 fields>     @0x5a3501  (io/save.cpp — reused)
//   3.  VIBE_Save_WriteMapTileTable      @0x5a3fa4  (io/save_tables — reused)
//   4.  VIBE_Save_WritePersonTable       @0x5a4134  (io/save_person — reused)
//   5.  VIBE_Save_WriteBuildingTable     @0x5a45bc  (io/save_building — reused)
//   6.  VIBE_Save_WriteGameStateHeader   @0x5a4938  (io/save_person preamble +
//                                                    per-record writer — reused)
//   7.  VIBE_Save_WriteBuildingSlotTables@0x5a5c1c  (io/save_serial3 — reused)
//   8.  VIBE_Amt_SaveAemter              @0x483198  (NEW — this module)
//   9.  VIBE_Save_RelinkPersonExtraData  @0x5a3f14  (scene-object sidecar save via
//       VIBE_WorldIo_SaveSceneObjects @0x5e65b8 — does NOT write into the save
//       stream; deferred, see the module report)
//   10. if (flags & 2) PARTIAL: close + relink + return  (the .cty / .NET /
//       network-session case — VIBE_Map_LoadCityFile @0x528c39 passes flags=2)
//       else FULL (.SAV): Gesetz/MapTiles/GameGlobals/Avatar/Object/Amt/History/
//       ActionQueues/Hotkey/CityAndPersonTables/Mission/Hotkey tail.
//
// The PARTIAL path (steps 1-10) is exactly the byte stream the reconstructed
// io::LoadWorld consumes — the shipped `.cty` city seeds are partial saves
// (header flag bit 1). This module reconstructs that partial write driver
// (SaveWriteGameFilePartial) by composing the already-reconstructed per-table
// writers in the recovered order, parameterized over the array bases exactly
// like its siblings. The FULL `.SAV` tail is NOT reconstructed here (its load
// mirror in io::LoadWorld is equally deferred); see the report.
//
// NEW 1:1 reconstructions in this module:
//   VIBE_Amt_SaveAemter @0x483198 — write the 37-entry office-holder table
//       (byte_B59848, stride 24 == world::g_officeHolders): count word 37 (u32),
//       then per record +0(1) +4(4) +8(1) +12(4) +16(1) +20(4)  (15 bytes each).
//   VIBE_Amt_LoadAemter @0x4832d0 — the read mirror; REQUIRES the count word to
//       equal 37 (else error + return 0), then the same 15-byte field sequence.
//   The load driver reads it right after LoadBuildingSlotTables when the file
//   version >= 0x10045 (the writer always writes 0x10045, so every save written
//   by this driver carries it); older files carry it in the full tail only.
#include "guild/common/types.h"
#include "io/save.h"          // SaveHeader / SaveScalarBlock / version table
#include "io/save_person.h"   // GameStateHeaderPreamble / PersonSceneLinks
#include "io/vfs.h"

namespace guild::io {

// --- Amt (Aemter) office-holder table ---------------------------------------
// byte_B59848 @0xB59848 — 37 records, stride 24 (== world::OfficeHolder /
// world::g_officeHolders). Serialized fields per record (15 bytes on disk):
//   +0 (1) holder   +4 (4) city   +8 (1) type   +12 (4) rank
//   +16 (1) state   +20 (4) secondary
constexpr guild::u32 kAemterCount  = 37;  // fixed count word (v5[0] = 37)
constexpr guild::u32 kAemterStride = 24;  // in-memory record stride
constexpr guild::u32 kAemterDiskRecBytes = 15;          // serialized bytes/record
constexpr guild::u32 kAemterDiskBytes = 4 + 37 * 15;    // 559 total stream bytes

// gilde.exe 0x483198 — VIBE_Amt_SaveAemter. Write the fixed count word (37),
// then the 37 records' serialized fields from `holderBase` (24-byte stride).
// Returns true (1) on full success, false on any stream failure (matching the
// original's BOOL; the original also returns 0 when the handle is null).
bool AmtSaveAemter(VfsHandle* h, const guild::u8* holderBase);

// gilde.exe 0x4832d0 — VIBE_Amt_LoadAemter. Read the count word; if it is not
// exactly 37 the original logs "amt_fio_LoadAemter(): count read is %i,
// expected %i" and returns 0 — reproduced (sans the sprintf side effect, which
// writes only a stack scratch buffer). Then read the 37 records' fields into
// `holderBase`. Non-serialized pad bytes of the records are left untouched.
bool AmtLoadAemter(VfsHandle* h, guild::u8* holderBase);

// --- the partial-path write driver ------------------------------------------
// All table bases the driver writes from. The live arrays are owned by sim/world
// (rule: parameterize, never redefine); the side tables (tiles / counters / slot
// tables / city-info / preamble ids) are the load-time captures of the caller
// (play/session_save owns a capture of the last LoadWorld-style load).
struct PartialSaveEnv {
    // 1. header (+ optional 0xE100 thumbnail bytes; null -> zero thumbnail, the
    //    live writer regenerates it from the framebuffer which is out of scope
    //    for a headless save). header.flagByte must carry bit 1 (value 2) for a
    //    partial save — the original passes the WriteGameFile `flags` arg through
    //    to the header byte (VIBE_Map_LoadCityFile passes 2).
    SaveHeader        header;
    const guild::u8*  thumbnail = nullptr;     // 0xE100 bytes or null
    // 2. scalar block (the 12/13 live globals @0x5a3501..).
    SaveScalarBlock   scalars;
    // 3. map-tile / scene-node index table (dword_13CE290, 67 x 8192).
    guild::u8*        tileBase = nullptr;
    // 4. object / building array (dword_13CE298, 169 x 256) + trailing extra-96
    //    table (dword_1234600 / dword_1234604).
    guild::u8*        objBase = nullptr;
    const guild::u8*  extra96Base = nullptr;
    guild::u32        extra96Count = 0;
    // 5. building / counter table (word_13C3110, 164 x 16).
    const guild::u8*  counterBase = nullptr;
    // 6. person/scene records (word_12CE910, 536 x 768) + the preamble ids
    //    (word_63CC5C / dword_6498E8 / dword_6498EC / dword_6498F0[8]). The live
    //    count is re-counted from the array exactly as the original does.
    GameStateHeaderPreamble preamble;
    guild::u8*        personBase = nullptr;
    // 7. building-slot tables (5 x 7952) + city-info records (4 x 756).
    const guild::u8*  slotBase = nullptr;
    const guild::u8*  cityInfoBase = nullptr;
    // 8. Amt office-holder table (byte_B59848, 24 x 37 == world::g_officeHolders).
    const guild::u8*  officeBase = nullptr;
};

// gilde.exe 0x5a4938 — VIBE_Save_WriteGameStateHeader, composed from its
// reconstructed pieces (io/save_person): the preamble (marker word, RE-COUNTED
// live-slot count, idA/idB-or-(-1), 8 handler ids), then for every live slot
// (marker word @+0 != -1) in slot order the 536-byte record body via
// SaveWritePersonSceneRecord. The four pointer->id link slots (+364/+368/+380/
// +388 — *((DWORD*)r+91/92/95/97)) are read from the record's current values:
// the portable record model stores the on-disk 32-bit ids in those slots
// (LoadCityRecords wrote them there), so `ptr ? *(ptr+4) : -1` degenerates to
// the stored id (-1 already encodes "null").
bool SaveWriteGameStateSection(VfsHandle* h, const GameStateHeaderPreamble& p,
                               const guild::u8* personBase);

// gilde.exe 0x5a348c — VIBE_Save_WriteGameFile, PARTIAL path (flags & 2).
// Writes steps 1..8 in the recovered order to the open stream `h` at the current
// writer version (SaveWriteScenarioBlock forces 0x10045 and sets the module
// version word, so every gated writer downstream emits its full field set).
// The original's surrounding RelinkPersonObjects / RelinkPersonRecords passes
// convert live heap pointers to ids and back; the portable record model stores
// 32-bit ids in those slots already, so the conversion is the identity here
// (see types.h: serialized paths store ids, not pointers).
// Returns true (1) on full success, false (0) on any stream failure.
bool SaveWriteGameFilePartial(VfsHandle* h, const PartialSaveEnv& env);

} // namespace guild::io
