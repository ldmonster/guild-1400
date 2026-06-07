// gilde.exe — guild::world  (MODULE: d3e event-binding table + serialization)
// See event_bindings.h for the recovered addresses and record layout.
#include "world/event_bindings.h"

#include <cstring>

namespace guild::world {

namespace {

// gilde.exe 0x5cb8f0 — VIBE_Util_StrCmpNoCase: ASCII A-Z lowercased on both
// sides; returns (loweredA - loweredB), i.e. 0 on equal. File-local copy (the
// original lives in the util module; other modules keep private copies too).
int StrCmpNoCase(const unsigned char* a, const unsigned char* b) {
    for (;;) {
        unsigned char v3 = *a;
        unsigned char v4 = *b;
        if (v3 >= 0x41u && v3 <= 0x5Au) v3 += 32;
        if (v4 >= 0x41u && v4 <= 0x5Au) v4 += 32;
        if (v3 != v4 || !v4)
            return static_cast<int>(v3) - static_cast<int>(v4);
        ++a;
        ++b;
    }
}

// gilde.exe 0x5d9360 — VIBE_Util_StrNCopyPad: copy up to `max` chars from src,
// then zero-fill the remainder of the `max`-byte field. Stops early at src NUL.
void StrNCopyPad(char* dst, const char* src, int max) {
    int i = 0;
    while (max > 0 && src[i]) {
        dst[i] = src[i];
        ++i;
        --max;
    }
    while (max > 0) {
        dst[i] = 0;
        ++i;
        --max;
    }
}

// ---------------------------------------------------------------------------
// The static name<->id table (gilde.exe aNone_0 @0x64A7FC, 8 entries stride 33).
// Recovered verbatim from get_bytes: 32-byte name + 1-byte id. Note the row at
// index 4 is a second "TEST" with id 1 in the shipped image — preserved here so
// LookupNameToId("TEST") returns the first match (id 1) exactly like the binary.
// ---------------------------------------------------------------------------
const std::vector<EventNameEntry> kEventNames = {
    {"NONE",             0},
    {"TEST",             1},
    {"ZOOM_IN_OBJECT",   2},
    {"ZOOM_OUT_OBJECT",  3},
    {"TEST",             1},
    {"SCENE_ENTER",      4},
    {"SCENE_EXIT",       5},
    {"SCENE_SUPERVISOR", 6},
};

} // namespace

// ===========================================================================
const std::vector<EventNameEntry>& EventNameTable() {
    return kEventNames;
}

int EventBindingTable::CountPopulated() const {
    int n = 0;
    for (const auto& s : slots)
        if (s.handler)
            ++n;
    return n;
}

bool EventBindingTable::Empty() const {
    for (const auto& s : slots)
        if (s.handler)
            return false;
    return true;
}

// ===========================================================================
// gilde.exe 0x5f494c — VIBE_EventTable_LookupIdToName.
//   while ( id != table[i].idByte ) { i += 33; if (i >= 264) return "NONE"; }
//   return name. We iterate the 8-entry vector equivalently.
// ===========================================================================
const char* LookupIdToName(u8 id) {
    for (const auto& e : kEventNames)
        if (e.id == id)
            return e.name;
    return "NONE";
}

// ===========================================================================
// gilde.exe 0x5f4910 — VIBE_EventTable_LookupNameToId.
//   Case-insensitive scan of the 8 names; on match return the id byte, else 0.
// ===========================================================================
u8 LookupNameToId(const char* name) {
    const unsigned char* key = reinterpret_cast<const unsigned char*>(name);
    for (const auto& e : kEventNames) {
        if (StrCmpNoCase(key, reinterpret_cast<const unsigned char*>(e.name)) == 0)
            return e.id;
    }
    return 0;
}

// ===========================================================================
// gilde.exe 0x5f4980 — VIBE_Event_RegisterEvent / 0x5f4a70 RegisterSceneEvent.
//
// Original control flow (a1=owner, a2=eventId(<<24), a3=name, a4=handler):
//   if (!a1) return 0;                              // no owner object
//   if (!a2) return 1;                              // id 0 == NONE, no-op
//   if (a4) {                                       // install handler
//     if (!owner.table) owner.table = Alloc(0x39C); // 7 slots, lazily
//     slot = table + 132*(id);
//     slot[+131] = 0;                               // clear per-slot flag
//     StrNCopyPad(slot+4, name, 127);
//     slot[+0] = a4;                                // store handler
//     return 1;
//   }
//   // a4 == 0: remove
//   if (!owner.table) return 1;
//   slot = table + 132*(id);
//   if (!slot[+0]) return 1;                        // already empty
//   slot[+0] = 0;                                   // clear handler
//   // if all 7 slots now empty, free the table
//   for (i=0,n=0; i<924; i+=132) { if (table+i populated) break; ++n; }
//   if (n != 7) return 1;
//   Free(table); owner.table = 0;
//   return 1;
// ===========================================================================
int RegisterEvent(EventBindingTable* table, u8 eventId,
                  const char* name, void* handler) {
    if (!table)                     // !a1: no owner object
        return 0;
    if (eventId == 0)               // !a2: NONE -> no-op
        return 1;

    const int idx = static_cast<int>(eventId);
    if (idx < 0 || idx >= kEventSlotCount)
        return 1;                   // out of the 7-slot range; original would OOB
    EventBindingSlot& slot = table->slots[static_cast<std::size_t>(idx)];

    if (handler) {
        table->allocated = true;    // lazy alloc on first install
        slot.flag = 0;              // slot[+131] = 0
        StrNCopyPad(slot.name, name ? name : "", kEventNameMax);
        slot.handler = handler;     // slot[+0] = a4
        return 1;
    }

    // remove path
    if (!table->allocated)
        return 1;
    if (!slot.handler)              // already empty
        return 1;
    slot.handler = nullptr;         // clear handler

    // count leading-empty slots: the original breaks at the first populated slot
    int empties = 0;
    for (int i = 0; i < kEventSlotCount; ++i) {
        if (table->slots[static_cast<std::size_t>(i)].handler)
            break;
        ++empties;
    }
    if (empties != kEventSlotCount) // not all 7 empty -> keep table
        return 1;
    table->allocated = false;       // Free + owner.table = 0
    return 1;
}

// ===========================================================================
// ByteWriter / ByteReader — mirror the 4 VIBE_Bio_* primitives the originals use.
// ===========================================================================
void ByteWriter::WriteDword(u32 v) {        // VIBE_Bio_WriteDwordPair: 4 bytes LE
    bytes.push_back(static_cast<u8>(v & 0xFF));
    bytes.push_back(static_cast<u8>((v >> 8) & 0xFF));
    bytes.push_back(static_cast<u8>((v >> 16) & 0xFF));
    bytes.push_back(static_cast<u8>((v >> 24) & 0xFF));
}

void ByteWriter::WriteString(const char* s) {   // VIBE_Bio_WriteString: str + NUL
    for (const char* p = s; *p; ++p)
        bytes.push_back(static_cast<u8>(*p));
    bytes.push_back(0);
}

u32 ByteReader::ReadDword() {                // VIBE_Bio_ReadDwordSwapArgs: 4 bytes
    u32 v = 0;
    for (int i = 0; i < 4; ++i) {
        u8 b = (pos < size) ? data[pos] : 0;
        ++pos;
        v |= static_cast<u32>(b) << (8 * i);
    }
    return v;
}

std::string ByteReader::ReadString() {       // VIBE_Bio_ReadString: bytes until NUL
    std::string s;
    while (pos < size) {
        u8 b = data[pos++];
        if (b == 0)
            break;
        s.push_back(static_cast<char>(b));
    }
    return s;
}

// ===========================================================================
// gilde.exe 0x5f4b60 — VIBE_Event_WriteEventNames.
//   if (!table) { WriteDword(0); return; }   // null-table early path
//   count populated slots; WriteDword(count);
//   for i in 0..6: if slot[i] populated:
//       WriteString(LookupIdToName(i)); WriteString(slot[i].name);
// ===========================================================================
void WriteEventNames(ByteWriter& out, const EventBindingTable* table) {
    if (!table) {
        out.WriteDword(0);
        return;
    }
    out.WriteDword(static_cast<u32>(table->CountPopulated()));
    for (int i = 0; i < kEventSlotCount; ++i) {
        const EventBindingSlot& slot = table->slots[static_cast<std::size_t>(i)];
        if (slot.handler) {
            out.WriteString(LookupIdToName(static_cast<u8>(i)));
            out.WriteString(slot.name);
        }
    }
}

// ===========================================================================
// gilde.exe 0x5f4bc8 / 0x5f4c6c — VIBE_Event_Load{,Scene}EventBindings.
//   count = ReadDword();
//   for i in 0..count-1:
//       name    = ReadString();  id = LookupNameToId(name);
//       handler = ReadString();
//       if (table) RegisterEvent(table, id, handler, factory());
//   return count read.
// (The original reads name THEN handler; the scene variant reads handler into
// the same scratch order. We register the handler name as the slot name, which
// is what the original stores via StrNCopyPad of the handler string a3.)
// ===========================================================================
int LoadEventBindings(ByteReader& in, EventBindingTable* table,
                      void* (*handlerFactory)()) {
    u32 count = in.ReadDword();
    int read = 0;
    for (u32 i = 0; i < count; ++i) {
        std::string nameStr = in.ReadString();
        u8 id = LookupNameToId(nameStr.c_str());
        std::string handlerStr = in.ReadString();
        ++read;
        if (table) {
            void* fn = handlerFactory ? handlerFactory() : reinterpret_cast<void*>(1);
            RegisterEvent(table, id, handlerStr.c_str(), fn);
        }
    }
    return read;
}

} // namespace guild::world
