#pragma once
// gilde.exe — guild::io  (MODULE: savegame browser — file enumeration + slot map)
//
// The save/load screen builds a list of on-disk savegames and maps each one into
// a fixed-size slot table. Two load-bearing primitives drive that:
//
//   VIBE_SaveBrowser_EnumerateSaveFiles @0x569530  scan a VFS directory for files
//       whose extension matches a filter (".SAV"), emitting one 528-byte browser
//       record per match (name field + full path field).
//   VIBE_SaveBrowser_FindSaveSlot       @0x569c50  pick the slot index a browser
//       record belongs in (QUICKSAVE -> 0, AUTOSAVE -> 0, else next free), copy the
//       record into that slot, and mark it used.
//
// These are pure string/array operations over the already-built VFS tree
// (io/vfs_tree) — they touch no GUI/render globals — so they are reconstructed
// 1:1 and round-tripped in tests. The GUI-coupled caller
// (VIBE_SaveBrowser_LoadSlotMetadata @0x569d00) that drives window/label creation
// is out of scope for this slice (it is listed in the module report).
#include "guild/common/types.h"
#include "io/vfs_tree.h"
#include <cstddef>

namespace guild::io {

// --- recovered browser-record layout ---------------------------------------
// Both functions work on a flat 528-byte (0x210) record, but they read its +9 name
// field with DIFFERENT semantics (recovered from the disassembly):
//   * EnumerateSaveFiles @0x569530 fills +9 with the loose FILE NAME truncated at its
//     '.' (extension dropped) and +265 with the verbatim "basePath/name" (extension
//     kept; NOT truncated). NOTE: the original truncates the +9 NAME field, not +265.
//   * FindSaveSlot @0x569c50 reads +9 as the in-game SAVE NAME (the user-entered
//     name, e.g. "QUICKSAVE"/"AUTOSAVE") — in the real flow this record is the
//     loaded save HEADER metadata (see VIBE_Save_LoadHeaderAndThumbnail), a SEPARATE
//     528-byte buffer that FindSaveSlot then copies into the chosen slot (record to
//     slot+16, slot-index marker to slot+0; a slot is free when its id @+8 == -1).
// Only the two string fields the enumerator fills are modelled; the gap fields are
// GUI scratch the metadata loader populates and are left zeroed by the enumerator.
struct SaveBrowserRecord {
    guild::u8  pad0[9];        // +0x000  (slot marker lives at +0 once placed)
    char       name[256];      // +0x009  raw file name (copy of the VFS entry name)
    char       fullPath[263];  // +0x109  "basePath/name", truncated at first '.'
};
static_assert(sizeof(SaveBrowserRecord) == 0x210,
              "SaveBrowserRecord must be 528 bytes (0x210 copy stride @0x569c50)");

// --- slot-table layout (FindSaveSlot) --------------------------------------
// The slot table is an array of 544-byte slots. Slot 0 is reserved for the
// quick/auto save. A slot is FREE when its id dword @+8 == -1. FindSaveSlot writes
// the chosen index byte to slot+0 (marker) and copies the 528-byte record to
// slot+16.
constexpr guild::u32 kSlotStride     = 544;  // 0x220 bytes per browser slot
constexpr guild::u32 kSlotMarkerOff  = 0;    // *(slot+0)  = slot index (byte)
constexpr guild::u32 kSlotIdOff      = 8;    // *(slot+8)  = id (-1 == free)
constexpr guild::u32 kSlotRecordOff  = 16;   // record copied to slot+16
constexpr guild::u32 kSlotRecordSize = 0x210;// 528-byte record copy

// Extension filter the save screen passes (string @0x624f08).
extern const char kSaveExt[5];        // ".SAV"
// Reserved-name tags compared against record+9 (strings @0x624ef0 / @0x624efc).
extern const char kQuickSaveTag[10];  // "QUICKSAVE"
extern const char kAutoSaveTag[9];    // "AUTOSAVE"

// VIBE_SaveBrowser_EnumerateSaveFiles @0x569530
//   (__usercall, eax=basePath, ecx=caseMode, ebx=outRecords, edx=extFilter)
// Resolve `basePath` to a VFS directory node, then for every file entry whose
// extension (the substring from its first '.') case-insensitively equals
// `extFilter`, emit a SaveBrowserRecord into `outRecords[i]`: the name (truncated at
// its '.') at +9 and the verbatim "basePath/name" (extension kept) at +265. Returns
// the count emitted. `caseMode` is forwarded to NormalizeDirPath. Returns 0 if the
// path does not resolve.
// `maxRecords` bounds how many records are written into `outRecords` (the
// original was unbounded — it trusted the saves dir to fit the caller's fixed
// buffer; a real install with >capacity .SAV files would overflow it). Default
// -1 keeps the unbounded 1:1 behavior for callers with a big-enough buffer;
// pass the buffer capacity to fail safe (extra files skipped, return clamped).
int SaveBrowserEnumerateSaveFiles(const char* basePath, VfsNode* startDir,
                                  const char* extFilter,
                                  SaveBrowserRecord* outRecords,
                                  int maxRecords = -1);

// VIBE_SaveBrowser_FindSaveSlot @0x569c50  (__usercall, edx=slotTable, ecx=record,
//   ebx=preferredSlot)
// Decide which slot `record` belongs to. If its name (record+9) is "QUICKSAVE" or
// "AUTOSAVE" the slot is forced to 0; otherwise `preferredSlot` is used. Returns -1
// (and copies nothing) if the chosen slot is already occupied (id@+8 != -1) or the
// name is reserved but `preferredSlot` disagrees. On success it stamps the slot
// marker (slot+0) and copies the 528-byte record to slot+16, returning the index.
int SaveBrowserFindSaveSlot(guild::u8* slotTable, const SaveBrowserRecord* record,
                            int preferredSlot);

} // namespace guild::io
