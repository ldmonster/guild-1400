#pragma once
// gilde.exe — guild::io  (MODULE: per-table savegame serializers, part 3)
//
// Faithful 1:1 reconstruction of the WRITE-side serializers that were still
// untranslated after parts 1/2 and save_world_load. These stream global record
// arrays field-by-field through the VFS stream layer (each field one
// VIBE_Vfs_WriteStream call with an explicit size, in a fixed order). They are
// the byte-exact mirror of the loaders already reconstructed elsewhere:
//
//   VIBE_Save_WriteBuildingSlotTables @0x5a5c1c  — write mirror of the REAL
//       LoadBuildingSlotTables @0x5aa058 (save_world_load.cpp). 5 city-slot
//       tables (16-byte header + 62 * 128-byte sub-records, stride 7952) then
//       4 city-info records (stride 756). Version-gates the +748 8-byte tail.
//
//   VIBE_Save_WriteObjectRecord @0x5a55b0 — write a single live game-object
//       record. Emits a fixed set of fields, then — depending on whether the
//       object owns a "stock" sub-record (the *((DWORD*)rec+34) presence test,
//       gated on a flag byte at sub+981) — either the sub-record's two blocks or
//       two zero-filled template blocks (dword_5A3430 / dword_5A343C, both all
//       zero in the binary). Finishes with the inline-mesh block: either the
//       object's own 64-byte mesh-name buffer (*((DWORD*)rec+25)) or a 64-byte
//       zero template (dword_5A344C, also all zero).
//
// Because the live arrays/records are owned by other modules, every serializer
// here is parameterized over the record/array BASE pointer; strides are the
// recovered constants. The original's stock-presence probe (a raw pointer
// dereference into another module's slab) is routed through an installable hook
// with an inert default so the byte layout is exercised deterministically.
#include "guild/common/types.h"
#include "io/vfs.h"

namespace guild::io {

// ---------------------------------------------------------------------------
// Building-slot tables (write mirror of LoadBuildingSlotTables @0x5aa058).
// ---------------------------------------------------------------------------
constexpr int        kBst_CitySlotTableCount = 5;     // 5 city-slot tables
constexpr int        kBst_CitySlotSubCount    = 62;   // 62 sub-records / table
constexpr int        kBst_CityInfoRecCount    = 4;    // 4 city-info records
constexpr guild::u32 kBst_CitySlotTableStride = 7952; // bytes / city-slot table
constexpr guild::u32 kBst_CityInfoStride      = 756;  // bytes / city-info record

// gilde.exe 0x5a5c1c — VIBE_Save_WriteBuildingSlotTables. Write the 5 city-slot
// tables from `slotBase` (7952-byte stride) then the 4 city-info records from
// `cityInfoBase` (756-byte stride). `version` gates the +748 8-byte city-info
// tail (written only when version >= 0x10037, mirroring the load gate).
// Returns true on full success.
bool SaveWriteBuildingSlotTables(VfsHandle* h, const guild::u8* slotBase,
                                 const guild::u8* cityInfoBase, guild::u32 version);

// ---------------------------------------------------------------------------
// Object-record serializer (VIBE_Save_WriteObjectRecord @0x5a55b0).
// ---------------------------------------------------------------------------
// Recovered zero-template lengths (all source tables are all-zero in the binary):
constexpr guild::u32 kObjRec_StockBlockA = 16;  // dword_5A3430 / sub+76 block
constexpr guild::u32 kObjRec_StockBlockB = 12;  // dword_5A343C / sub+132 block
constexpr guild::u32 kObjRec_MeshNameLen = 64;  // dword_5A344C / rec+100 block

// The original probes another module's slab for the object's "stock" sub-record:
//   v4 = *((DWORD*)rec+34);                 // the stock pointer at rec+136
//   if (v4 && !*(BYTE*)(v4+981)) { write sub+76(16), sub+132(12); }
//   else                        { write 16 zero bytes, 12 zero bytes; }
// In a portable build the slab is owned elsewhere, so the probe is a hook.
// The inert default returns hasStock=false (-> zero templates), which yields a
// fully deterministic, byte-exact record. A test that wires a real sub-record
// installs a hook that points stockBlockA/stockBlockB at the live blocks.
struct ObjectRecordHooks {
    // Resolve the object's stock sub-record. `rec` is the object-record base.
    // On a present, enabled sub-record set *blockA (16 bytes) and *blockB (12
    // bytes) to the source blocks and return true; otherwise return false.
    bool (*resolveStock)(const guild::u8* rec, const guild::u8** blockA,
                         const guild::u8** blockB) = nullptr;
};

// Install hooks (returns the previous set). Passing a default-constructed struct
// restores the inert behaviour.
ObjectRecordHooks SaveSetObjectRecordHooks(const ObjectRecordHooks& hooks);

// gilde.exe 0x5a55b0 — VIBE_Save_WriteObjectRecord. Serialize one object record.
//   meshPtr  == *((DWORD*)rec+25): when non-null, its 64 bytes are written;
//               otherwise a 64-byte zero template is written (dword_5A344C).
//   linkId   == the dword passed in EDX (a2), written verbatim after the flag byte.
// `flagByte` is the byte at sub+533 in the original; in a portable build the
// caller supplies it (the sub-record is owned elsewhere). Returns true on success.
bool SaveWriteObjectRecord(VfsHandle* h, const guild::u8* rec, guild::i32 linkId,
                           const guild::u8* meshPtr, guild::u8 flagByte);

} // namespace guild::io
