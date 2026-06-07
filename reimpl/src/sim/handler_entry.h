#pragma once
// handler_entry — the "He" / HandlerEntry pool subsystem of the Guild simulation
// (gilde.exe, VIBE_He_* family). This is the fixed-capacity table of 332-byte
// "handler entry" records (the +82/+112/+172 record that the NpcAction/NpcEvent
// step machines drive — see he.h for the per-record field layout the step
// functions read/write). This file recovers the *table* operations byte-for-byte:
// the allocator, the linear find-by-filter scan, the free path, the per-type
// dispatch tables, the tick/run passes, and the message-box handler run.
//
// Recovered table geometry (from VIBE_He_AllocHandlerEntry @0x4c5f40,
// VIBE_He_FreeHandlerEntry @0x4c6144, VIBE_He_FindFirstHandlerByFilter @0x4c63f8,
// VIBE_He_FindNextMatchingHandler @0x4c6278):
//   * Handler pool  : byte_11D6040 — 332-byte (0x14C) stride, 1024 entries
//                     (339968 = 332 * 1024 bytes). A record is "alive" when its
//                     +0 kind byte is nonzero.
//   * Highest used  : dword_632248 — high-water index into the pool.
//   * Live count    : dword_1229268 — number of allocated records.
//   * Next ordinal  : dword_632244 — monotonic per-record sequence id (-> rec+4).
//   * Icon/MsgBox   : dword_11D5A20 — 256-entry pointer array of records that
//                     carry an event-panel icon (flag 0x08); dword_63224C its
//                     high-water index.
//   * Per-type tables (0x88 = 136 entries each):
//       dword_1229040[type] — the post-alloc "init" callback (run by Alloc).
//       funcs_4C6EE9[type]  — the per-tick "run/step" callback (run by Tick/Run).
//   * Filter state (FindFirst stashes, FindNext reads):
//       byte_632250 kind (-1 = any), dword_632254 id@+4 (-1 = any),
//       word_632258 index@+8 (-1 = any), dword_63225C field@+16 (-1 = any),
//       byte_632260 first-call flag, dword_1229260 scan cursor.
//
// The pool, the per-type callback tables and the filter state are file globals
// in the original (one process-wide instance). We gather them into a single
// HandlerTable instance so the subsystem is testable in isolation; one live
// instance reproduces the original's single global state.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// --- table geometry (the recovered constants) ------------------------------
constexpr u32 kHandlerStride   = 0x14C;   // 332 bytes per record
constexpr u32 kHandlerSlots    = 1024;    // pool capacity
constexpr u32 kHandlerPoolBytes = kHandlerStride * kHandlerSlots; // 339968
constexpr u32 kIconSlots       = 256;     // dword_11D5A20 capacity
constexpr u32 kNumHandlerTypes = 0x88;    // 136 per-type table entries

// Flag bits in the handler record's +120 byte (a2/a1 descriptor +54 -> rec+120).
enum HandlerFlag : u8 {
    kHfHasIcon = 0x08,   // record owns an event-panel icon (-> dword_11D5A20)
};

// The 332-byte handler-entry record. Treated as raw POD bytes (the originals
// address it with explicit `*(T*)(base+off)`); the he.h He_* accessors decode
// the NpcAction-facing fields. The table-level fields recovered here are:
//   +0    kind byte (0 = free slot)
//   +4    handler ordinal / sequence id (dword_632244 stamped at alloc)
//   +8    person marker word (mirror of Person record +0)
//   +12   person/entity id (mirror of Person record +4)
//   +16   cityId / descriptor field (descriptor +12)
//   +116  event-panel slot ptr (read on free to tear the panel slot down)
//   +120  flags byte (0x08 => has icon)
//   +124  gfx/icon allocation ptr (Memory_FreeDebug'd on free)
struct HandlerRecord {
    u8 bytes[kHandlerStride];
};
static_assert(sizeof(HandlerRecord) == kHandlerStride, "handler record 332B");

// table-level field accessors (byte-faithful)
inline u8&  HrKind(HandlerRecord* r)    { return r->bytes[0]; }
inline i32& HrOrdinal(HandlerRecord* r) { return *reinterpret_cast<i32*>(r->bytes + 4); }
inline u16& HrIndex(HandlerRecord* r)   { return *reinterpret_cast<u16*>(r->bytes + 8); }
inline i32& HrId(HandlerRecord* r)      { return *reinterpret_cast<i32*>(r->bytes + 12); }
inline i32& HrField16(HandlerRecord* r) { return *reinterpret_cast<i32*>(r->bytes + 16); }
inline i32& HrPanelSlot(HandlerRecord* r){ return *reinterpret_cast<i32*>(r->bytes + 116); }
inline u8&  HrFlags(HandlerRecord* r)   { return r->bytes[120]; }
inline i32& HrGfx(HandlerRecord* r)     { return *reinterpret_cast<i32*>(r->bytes + 124); }

// Per-type callbacks. The original keeps the post-alloc init in dword_1229040
// and the per-tick step in funcs_4C6EE9; both are indexed by the kind byte.
using HandlerInitFn = void (*)(HandlerRecord* r);
using HandlerRunFn  = i32  (*)(HandlerRecord* r);

// The handler-entry pool + per-type tables + filter scan state.
class HandlerTable {
public:
    HandlerTable();
    ~HandlerTable();
    HandlerTable(const HandlerTable&) = delete;
    HandlerTable& operator=(const HandlerTable&) = delete;

    // VIBE_He_InitHandlerTable @0x4c5248 (table-reset half). The original wires
    // the type-0 fallbacks then calls the CharAction/Event/Building registrars
    // and memsets three side tables. We model the resettable part: zero the
    // pool, reset the high-water/live counters, install the type-0 fallback
    // (LogInvalidAllocType / LogInvalidRunType) into slot 0. Returns 0.
    int Init();

    // VIBE_He_RegisterHandlerByType @0x4c5398 — bind the init + run callbacks for
    // a kind byte. Returns 1 if `type >= 0x88` (rejected), else 0.
    int RegisterHandlerByType(u8 type, HandlerInitFn initFn, HandlerRunFn runFn);

    // VIBE_He_AllocHandlerEntry @0x4c5f40 — allocate a record for the descriptor
    // `desc` (a HeRecord-shaped template; see he.h). Validates the kind byte
    // (< 0x88 and both type callbacks present); if desc->id (+8 in the descriptor
    // = HeRecord +8? no, descriptor +8 is the personId) is not -1 it resolves the
    // Person record (via the injected resolver) and copies its marker/id into the
    // new record; stamps the ordinal; copies the descriptor's payload regions;
    // links the record into the icon array if flag 0x08 is set; runs the per-type
    // init callback; bumps the live count. Returns the record, or nullptr on
    // failure (bad type / pool full / icon array full / person not found).
    //
    // The descriptor is laid out like the original's stack template:
    //   +4  kind byte, +8 personId dword, +12 cityId dword,
    //   +16..+47 8 dwords copied to record dword 4..11,
    //   +40/+44/+48 -> record dword 17/18/19, +52 word -> record word 40,
    //   +54 flags -> record +120, +56..+31? (see .cpp), +88..+0xA0 -> record +172.
    // Tests pass a HeRecord whose first 0xC0 bytes carry these template fields.
    HandlerRecord* AllocHandlerEntry(const HeRecord* desc);

    // VIBE_He_FreeHandlerEntry @0x4c6144 — release a record: free its gfx ptr,
    // unlink it from the icon array (tearing the panel slot if present), memset
    // the 332 bytes to 0, decrement the live count, and walk the high-water
    // indices back down. Null-safe. Returns the original's eax.
    i32 FreeHandlerEntry(HandlerRecord* r);

    // VIBE_He_FindFirstHandlerByFilter @0x4c63f8 — varargs filter. Each filter is
    // a (selector, value) pair: selector 0 => kind (byte), 1 => id@+4 (dword),
    // 2 => index@+8 (word), 3 => field@+16 (dword). `count` filter pairs follow.
    // Resets the scan state and returns the first matching record (or nullptr).
    HandlerRecord* FindFirstHandlerByFilter(int count, ...);

    // VIBE_He_FindNextMatchingHandler @0x4c6278 — continue the scan from the
    // cursor; returns the next record matching the active filter, or nullptr.
    HandlerRecord* FindNextMatchingHandler();

    // VIBE_He_CountMatchingHandlers @0x537358 — count records whose +8 index ==
    // *markerWord (or all if markerWord is null). Uses FindFirst/FindNext.
    int CountMatchingHandlers(const u16* markerWord);

    // VIBE_He_RefreshEntityHandlers @0x4c6e0c — walk all records whose +8 index
    // matches *markerWord and call the (injected) world-pos refresh on each.
    // Returns the last FindNext result (nullptr at the end).
    HandlerRecord* RefreshEntityHandlers(const u16* markerWord);

    // VIBE_He_TickActiveHandlers @0x4c52d8 — iterate the live records up to the
    // high-water index and free each. (The original has a type-13 universe-switch
    // side path delegated through hooks; here it just frees every live record,
    // matching the dominant control flow. Returns the last Free result.)
    i32 TickActiveHandlers();

    // VIBE_He_RunAllHandlers @0x4c6e38 — iterate live records; for each whose
    // flags lack 0x18, if its appointment time (+82) is due vs the supplied clock
    // and its kind < 0x88, run the per-type run callback. Returns the last result.
    i32 RunAllHandlers(const GameTime& clock);

    // VIBE_He_RunMessageBoxHandlers @0x4c6eb4 — iterate the icon array up to its
    // high-water index; for each non-null record with kind < 0x88, run its
    // per-type run callback. Returns the last result.
    i32 RunMessageBoxHandlers();

    // --- injected leaves (default inert) -----------------------------------
    // VIBE_Person_FindRecordById — resolve a person id to a record base. Alloc
    // copies the record's +0 word and +4 id into the new handler. Returns null if
    // absent (which aborts the alloc when desc personId != -1).
    using PersonFindFn = const void* (*)(i32 personId);
    void set_person_find(PersonFindFn fn) { personFind_ = fn; }

    // VIBE_He_UpdateHandlerWorldPos — refresh leaf (renderer); inert by default.
    using WorldPosFn = void (*)(HandlerRecord* r);
    void set_world_pos(WorldPosFn fn) { worldPos_ = fn; }

    // --- inspectable state --------------------------------------------------
    HandlerRecord* pool()       { return pool_; }
    HandlerRecord& slot(u32 i)  { return pool_[i]; }
    i32  high_water() const     { return highWater_; }     // dword_632248
    i32  live_count() const     { return liveCount_; }     // dword_1229268
    i32  next_ordinal() const   { return nextOrdinal_; }   // dword_632244
    i32  icon_high_water() const{ return iconHigh_; }      // dword_63224C
    HandlerRecord* icon_slot(u32 i) const { return icons_[i]; } // dword_11D5A20

private:
    HandlerRecord* pool_;       // byte_11D6040 (332 * 1024)
    HandlerRecord* icons_[kIconSlots] = {}; // dword_11D5A20
    HandlerInitFn  initFns_[kNumHandlerTypes] = {};  // dword_1229040
    HandlerRunFn   runFns_[kNumHandlerTypes]  = {};  // funcs_4C6EE9

    i32 highWater_   = 0;   // dword_632248
    i32 liveCount_   = 0;   // dword_1229268
    i32 nextOrdinal_ = 0;   // dword_632244
    i32 iconHigh_    = 0;   // dword_63224C

    // filter scan state (FindFirst -> FindNext)
    u8  fKind_   = 0xFF;    // byte_632250
    i32 fId_     = -1;      // dword_632254
    u16 fIndex_  = 0xFFFF;  // word_632258
    i32 fField_  = -1;      // dword_63225C
    bool fFirst_ = true;    // byte_632260
    HandlerRecord* cursor_ = nullptr; // dword_1229260

    PersonFindFn personFind_ = nullptr;
    WorldPosFn   worldPos_   = nullptr;
};

// ---------------------------------------------------------------------------
// VIBE_He_SumPlayerHandlerValues @0x4c3a78 — sum, over a separate 45-byte-stride
// "He entity" table (dword_11BC7xx, 512 entries = 23040 bytes), the cost of every
// entry owned by player `playerId` whose state column == 1. The per-entry cost is
// the dword at +12 of the matching 36-byte score-table entry (unk_631E98, 26
// entries) keyed by the entry's action byte (column @+9). The entity table and
// the score table are recovered exactly. The table is supplied by the caller (the
// original keeps it as a file global populated by the unported entity-table
// builders) via HeEntityTable.
//
// The table model here is addressed from the OWNER column (dword_11BC776), which
// is what the Sum loop walks: `*(int*)((char*)&dword_11BC776 + i)` for i in
// 0,45,90,... Relative to that column base, the columns read are:
//   +0   ownerId   (dword, dword_11BC776) — compared to playerId
//   +6   action    (byte,  byte_11BC77C)  — score-table key (< 26)
//   +15  state     (dword, dword_11BC785) — must equal 1
struct HeEntityTable {
    static constexpr u32 kStride = 45;
    static constexpr u32 kCount  = 512;       // 23040 / 45
    static constexpr u32 kBytes  = kStride * kCount; // 23040
    u8 bytes[kBytes];                          // base = owner column (dword_11BC776)

    void set_owner(u32 i, i32 v) { *reinterpret_cast<i32*>(bytes + 45 * i) = v; }
    void set_action(u32 i, u8 v) { bytes[45 * i + 6] = v; }
    void set_state(u32 i, i32 v) { *reinterpret_cast<i32*>(bytes + 45 * i + 15) = v; }
};

// The 36-byte score table (unk_631E98, 26 entries). Entry +12 (dword index 3) is
// the value summed by SumPlayerHandlerValues. Recovered byte-for-byte.
constexpr u32 kHeScoreEntries = 26;
constexpr u32 kHeScoreStride  = 36;
extern const i32 kHeScoreTable[kHeScoreEntries][kHeScoreStride / 4];

// gilde.exe 0x4c3a78 — VIBE_He_SumPlayerHandlerValues(playerId@eax).
int He_SumPlayerHandlerValues(const HeEntityTable& table, i32 playerId);

} // namespace guild::sim
