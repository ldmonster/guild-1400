#pragma once
// data_load — the building/object type-table loaders for the Guild game bring-up
// (gilde.exe). MODULE: world (namespace guild::world).
//
// This module ports, byte-for-byte, the two functions that read the shipped
// type-definition data files into the in-memory type tables at session start:
//
//   VIBE_World_LoadBuildingAndObjectData 0x5835f8
//       Allocates the building-type table (dword_13CE294, 72 records * 589 bytes)
//       and the scene/object-type table (dword_13CE27C, 731 records * 65 bytes),
//       reads them RAW from "<dir>A_Geb.dat" / "<dir>A_Obj.dat" (fixed-stride
//       fread), then runs a per-building room-count fixup and the derived-table
//       builder VIBE_World_InitBuildingTypeTable, and reseeds the RNG.
//
//   VIBE_World_InitBuildingTypeTable 0x5833b4
//       Derives the per-object-type "worth/security remap" byte table
//       (byte_13CE862 == sim::g_sceneTypeRemap) by propagating each building
//       group's kind through its room list, falling back to a fixed kind->worth
//       lookup (byte_13CEB3C/byte_13CEB3D).
//
// The .dat FILE FORMAT is a flat binary array of fixed-stride records with NO
// header:
//   A_Geb.dat : 589 bytes/record (BuildingTypeDef). Record +0 = kind byte,
//               +1.. = ASCII name (0-terminated), +33 = derived room count,
//               +34 = active-room count, +35 = u16 roomList[64] (object-type
//               ids, high bit = "present" flag, 0xFFFF = end), +36 = a signed
//               "kind-flag" byte the fixup tests (<0 => counts toward +33).
//   A_Obj.dat : 65 bytes/record (SceneTypeDef). Record +0 = kind byte,
//               +1.. = ASCII name, +33 = subtype byte (used as kind->worth idx).
// The on-disk files are larger than the loaded record counts (A_Geb.dat has 192
// records on disk, A_Obj.dat 1024); only the first 72 / 731 are read, exactly as
// the engine's fread call counts.
//
// Tables/accessors are REUSED from sim (g_buildingTypes / g_sceneTypes /
// g_sceneTypeRemap) — this module only fills them. Render/scene + production-slot
// seeding leaves are out of scope (see report); the loader runs the data path.
#include "guild/common/types.h"
#include "shim/IFileSystem.h"

namespace guild::world {

// Record counts actually loaded (the fread `count` arguments in the original).
constexpr int kBuildingTypeLoadCount = 72;    // 0xA5A8 / 589
constexpr int kSceneTypeLoadCount    = 731;   // 0xB99B / 65

// gilde.exe 0x5833b4 — VIBE_World_InitBuildingTypeTable.
// Builds the per-object-type remap byte table (sim::g_sceneTypeRemap, the
// original byte_13CE862[731]) from the loaded building/scene type tables. Must be
// called AFTER both tables are populated. Returns the number of object types
// processed (731), matching the original.
int WorldInitBuildingTypeTable();

// gilde.exe 0x5835f8 — VIBE_World_LoadBuildingAndObjectData (__usercall, eax=dir).
// Reads "<dir>A_Geb.dat" and "<dir>A_Obj.dat" through `fs` into the building-type
// and scene-type tables, runs the room-count fixup + InitBuildingTypeTable, and
// marks the tables loaded. `dir` is prefixed verbatim to the file names (the
// original does `sprintf("%s%s", dir, "A_Geb.dat")`).
// Returns 0 on success, or the original's negative error code:
//   -1 build-table alloc, -2 object-table alloc (n/a here, fixed arrays),
//   -3 A_Geb.dat open, -4/-5 type/scene alloc (n/a), -6 A_Obj.dat open.
int WorldLoadBuildingAndObjectData(guild::shim::IFileSystem* fs, const char* dir);

// Full 731-entry remap result (the original byte_13CE862[731]); for tests. The
// shared sim::g_sceneTypeRemap only holds the low 256 entries.
guild::u8 WorldTypeRemapAt(int objType);

} // namespace guild::world
