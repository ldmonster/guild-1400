#pragma once
// World event-binding table — the per-scene table that maps a named game event
// ("SCENE_ENTER", "ZOOM_IN_OBJECT", ...) to a registered .esc handler and its
// script-engine function pointer. The d3e engine builds one such table per
// owner object; the loader/saver round-trips it through the savegame stream.
//
// Recovered 1:1 from the d3e event-table accessors:
//   VIBE_Event_RegisterEvent        0x5f4980  (owner+468 table; add/remove slot)
//   VIBE_Event_RegisterSceneEvent   0x5f4a70  (owner+968 table; byte-identical)
//   VIBE_Event_WriteEventNames      0x5f4b60  (serialize populated slots)
//   VIBE_Event_LoadEventBindings    0x5f4bc8  (deserialize -> RegisterEvent)
//   VIBE_Event_LoadSceneEventBindings 0x5f4c6c (deserialize -> RegisterSceneEvent)
//   VIBE_EventTable_LookupIdToName  0x5f494c  (id -> canonical name)
//   VIBE_EventTable_LookupNameToId  0x5f4910  (name -> id, case-insensitive)
//
// The table is a fixed 7-slot array (0x39C = 924 bytes, stride 132). Slot i:
//   +0     (dword) handler function pointer (0 == empty slot)
//   +4     (char[127]) handler name, NUL-padded (VIBE_Util_StrNCopyPad)
//   +131   (byte) per-slot flag, cleared on register
// RegisterEvent indexes the slot by event id (the high byte of the id arg in
// the original: 132 * (id >> 24) once the id is loaded into the high byte). We
// expose a normal `u8 eventId` parameter.
//
// The name<->id map (dword_64A819 / aNone_0 @0x64A7FC) is a static 8-entry
// table, stride 33: 32-byte name + 1-byte id. Recovered verbatim from the IDB.
#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "guild/common/types.h"

namespace guild::world {

// ---------------------------------------------------------------------------
// One event-binding slot  (stride 132, matches the original record layout).
// ---------------------------------------------------------------------------
constexpr int kEventSlotStride = 132;
constexpr int kEventSlotCount  = 7;        // 0x39C / 132
constexpr int kEventNameMax    = 127;      // StrNCopyPad cap (+4..+130, +131 flag)

struct EventBindingSlot {
    void* handler = nullptr;               // +0    (0 == empty)
    char  name[kEventNameMax + 1] = {};    // +4    NUL-padded handler name
    u8    flag = 0;                         // +131  per-slot flag (cleared on set)
};

// A whole 7-slot table (one per owner object; allocated lazily in the original).
struct EventBindingTable {
    std::array<EventBindingSlot, kEventSlotCount> slots{};
    bool allocated = false;                // mirrors owner+468 pointer != 0

    int CountPopulated() const;            // number of slots with handler != 0
    bool Empty() const;                    // all slots free
};

// ---------------------------------------------------------------------------
// Name <-> id table  (gilde.exe aNone_0 @0x64A7FC, 8 entries, stride 33).
// ---------------------------------------------------------------------------
struct EventNameEntry {
    const char* name;
    u8          id;
};
const std::vector<EventNameEntry>& EventNameTable();

// gilde.exe 0x5f494c — VIBE_EventTable_LookupIdToName.
//   Linear scan of the 8-entry table; return the first name whose id byte
//   matches, else "NONE". (The original returns aNone for no-match.)
const char* LookupIdToName(u8 id);

// gilde.exe 0x5f4910 — VIBE_EventTable_LookupNameToId.
//   Case-insensitive name scan over the 8 entries; return the matched id, or 0
//   ("NONE") when no entry matches.
u8 LookupNameToId(const char* name);

// ---------------------------------------------------------------------------
// Register / unregister a binding.
// ---------------------------------------------------------------------------
// gilde.exe 0x5f4980 — VIBE_Event_RegisterEvent (owner+468 table) and
// gilde.exe 0x5f4a70 — VIBE_Event_RegisterSceneEvent (owner+968 table) are
// byte-identical apart from the table base, so one routine serves both.
//
// Semantics (faithful to the original):
//   * tableValid == false (owner pointer null) -> return 0  (the !a1 guard).
//   * eventId == 0 ("NONE") -> return 1, no change            (the !a2 guard).
//   * handler != null  -> allocate the table on first use, clear slot.flag,
//       copy the name (StrNCopyPad, 127 chars), store the handler. Returns 1.
//   * handler == null  -> remove: if the table or slot is already empty, return
//       1; else clear the slot's handler, and if ALL 7 slots are now empty free
//       the whole table (allocated=false). Returns 1.
// Returns 1 on success, 0 only when the table object itself is missing.
int RegisterEvent(EventBindingTable* table, u8 eventId,
                  const char* name, void* handler);

// ---------------------------------------------------------------------------
// Serialization stream (the original goes through VIBE_Bio_* / VIBE_Vfs_*; we
// abstract the byte sink/source so the table round-trips without the VFS).
// ---------------------------------------------------------------------------
struct ByteWriter {
    std::vector<u8> bytes;
    void WriteDword(u32 v);                // VIBE_Bio_WriteDwordPair (4 bytes)
    void WriteString(const char* s);       // VIBE_Bio_WriteString (str + NUL)
};
struct ByteReader {
    const u8* data = nullptr;
    std::size_t size = 0;
    std::size_t pos = 0;
    u32  ReadDword();                      // VIBE_Bio_ReadDwordSwapArgs (4 bytes)
    std::string ReadString();              // VIBE_Bio_ReadString (until NUL)
};

// gilde.exe 0x5f4b60 — VIBE_Event_WriteEventNames.
//   Writes the populated-slot count, then for each populated slot the canonical
//   event name (LookupIdToName(slotIndex)) followed by the handler name.
//   A null table writes a single zero count (the `if (!a2)` early path).
void WriteEventNames(ByteWriter& out, const EventBindingTable* table);

// gilde.exe 0x5f4bc8 / 0x5f4c6c — VIBE_Event_Load{,Scene}EventBindings.
//   Reads the count, then count*(name,handler) pairs; resolves each name to an
//   id and registers it into `table` (when non-null). The original wires the
//   handler from off_64A904(); here the caller supplies the handler factory.
//   Returns the number of pairs read.
int LoadEventBindings(ByteReader& in, EventBindingTable* table,
                      void* (*handlerFactory)());

} // namespace guild::world
