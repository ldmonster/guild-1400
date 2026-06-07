#pragma once
// guild::gui — debug windows: memory-info content build + damage-label slot table.
//
// VIBE_DebugWindow_ShowMemoryInfo @0x536658 creates a 400x640 window and prints one
// labelled line per memory category ("Category:$4T%i Bytes$A"), accumulating a RUNNING
// TOTAL across the size-bearing categories, then a "Total:" line, then two count lines.
// The CONTENT/DATA logic worth recovering is the line list + the running-total math
// (which categories feed the total, and in what order).
//
// VIBE_DamageLabel_RegisterEntry @0x4bad5c maintains a fixed 64-slot table (stride 67
// dwords) keyed by an owner handle; entries carry a timestamp (dword_62EB38) and on a
// full table the LEAST-RECENT entry (smallest timestamp) is evicted.  This recovers
// that slot-table insert/find/LRU-evict logic.
//
// Window creation and text rendering are routed through the existing GUI/text clusters
// (forward-declared) or modelled as plain data here so the content/table logic is
// testable in isolation.

#include "guild/common/types.h"
#include <array>
#include <string>
#include <vector>

namespace guild::gui {

using guild::i32;

// One emitted memory-info line: the localized format id and the byte size argument.
struct MemoryInfoLine {
    std::string label;   // e.g. "Textures", "Object-Instances", … "Total"
    i32 bytes;           // the %i argument
    bool feedsTotal;     // true for the categories that accumulate into "Total"
};

// The memory categories in emission order, exactly as ShowMemoryInfo walks them.  The
// first six feed the running total ("Total:"); the trailing "Animations total" /
// counts do not.
enum class MemCategory {
    kTextures,        // VIBE_Mesh_ComputeTotalMemorySize(511, ?, 0)
    kObjectInstances,
    kStockObjects,
    kSupermap,        // VIBE_Mesh_ComputeBitmapMemorySize(...)
    kFloor,           // VIBE_Mesh_ComputeSurfaceMemorySize(...)
    kAnimations,      // VIBE_Mesh_ComputeTotalMemorySize(511, 40, 0)
};

// Sizes the window reads (in the same order as MemCategory).  AnimationsTotal and the
// counts are reported separately and do NOT contribute to the running "Total".
struct MemoryInfoSizes {
    i32 textures = 0;
    i32 objectInstances = 0;
    i32 stockObjects = 0;
    i32 supermap = 0;
    i32 floor = 0;
    i32 animations = 0;
    i32 animationsTotal = 0;  // separate "Animations total" line
    i32 characterCount = 0;   // VIBE_Character_CountByOwner(-1,1)
    i32 playerCount = 0;      // dword_647724
};

// gilde.exe 0x536658 — build the memory-info content lines + the running total.
// Returns the lines in emission order; the "Total" line's value is the sum of the six
// size categories (textures..animations), matching the original `v17` accumulator.
std::vector<MemoryInfoLine> DebugWindow_BuildMemoryInfo(const MemoryInfoSizes& s);

// The window geometry the debug memory window uses (Window_Create(32, 96, 400, 640)).
inline constexpr int kDebugMemWinX = 32;
inline constexpr int kDebugMemWinY = 96;
inline constexpr int kDebugMemWinW = 400;
inline constexpr int kDebugMemWinH = 640;
inline constexpr i32 kDebugMemWinFlags = 1044;  // 0x414

// ---------------------------------------------------------------------------
// Damage-label slot table (VIBE_DamageLabel_RegisterEntry @0x4bad5c).
//   capacity 64, stride 67 dwords.  Each slot: [0]=text-id payload, [1]=timestamp,
//   [2]=owner handle, [3..]=label text.  Lookup is by owner handle; on a full table
//   the slot with the smallest timestamp (oldest) is evicted.
// ---------------------------------------------------------------------------
inline constexpr int kDamageLabelSlots = 64;
inline constexpr int kDamageLabelStride = 67;  // dwords per slot

struct DamageLabelEntry {
    i32 owner = 0;       // +8  (key)
    i32 timestamp = 0;   // +4  (dword_62EB38 at insert time)
    i32 payload = 0;     // +0  (text/value payload, the `a2` argument)
    std::string text;    // +12 (label string)
    bool used = false;
};

class DamageLabelTable {
public:
    // gilde.exe 0x4bad5c — register/update an entry for `owner`.
    //   - if `owner` already has a slot, returns it (no overwrite),
    //   - else use the first free slot,
    //   - else evict a slot whose stored timestamp is older than `now`.  The original
    //     scans all 64 slots and keeps the LAST index whose timestamp < now (v7), so
    //     ties resolve to the highest such index.
    // `now` is the current timestamp (dword_62EB38).  Returns the slot index, or -1
    // when the table is full and no slot is older than `now` (the `v7 == -1` guard).
    int Register(i32 owner, i32 payload, const std::string& text, i32 now);

    int  Find(i32 owner) const;
    const DamageLabelEntry& Slot(int i) const { return slots_[i]; }
    int  UsedCount() const;

private:
    std::array<DamageLabelEntry, kDamageLabelSlots> slots_{};
};

} // namespace guild::gui
