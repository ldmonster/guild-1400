#pragma once
// guild::gui — save/load browser: enumerate save files and build the slot list.
//
// VIBE_SaveBrowser_EnumerateSaveFiles @0x569530 walks a VFS directory listing,
// matches files by extension (case-insensitive), and produces a record list: each
// entry holds the bare display name (the file name with its ".ext" stripped) and the
// full "dir/name" path (also extension-stripped).  The list stride is 528 bytes.
//
// VIBE_SaveBrowser_LoadSlotMetadata @0x569d00 enumerates ".SAV" files (capped at 16),
// loads each one's header+thumbnail, drops corrupt/excluded saves (header flag bit
// 0x2), assigns each surviving save to a slot via VIBE_SaveBrowser_FindSaveSlot, and
// finally fills every still-empty slot (of the 16) with a default placeholder entry.
// Slot records have a 544-byte stride; the slot grid lays them at y = 130*slot.
//
// VIBE_SaveBrowser_FindSaveSlot @0x569c50 maps a save to its target slot.
//
// This module recovers the LIST-BUILD logic (extension match, name/path build,
// extension strip, the 16-slot fill-empty pass, slot Y layout) from synthetic file
// metadata.  The VFS directory read and the per-slot widget/thumbnail creation are
// routed through inputs / a command hook so the list build is testable in isolation.

#include "guild/common/types.h"
#include <string>
#include <vector>

namespace guild::gui {

using guild::i32;

inline constexpr int kSaveMaxSlots = 16;     // saves enumerated are capped at 16
inline constexpr int kSaveSlotPitchY = 130;  // 130 * slot  (child-window Y)
inline constexpr int kSaveListStride = 528;  // enumerate record stride (bytes)
inline constexpr int kSaveSlotStride = 544;  // slot record stride (bytes)

// Header flag bit that excludes a save from the browser (v56[4] & 2 in LoadSlotMetadata).
inline constexpr int kSaveExcludeFlag = 0x2;

// One enumerated save file.
struct SaveFileEntry {
    std::string displayName;  // file name with extension stripped (StripPathAndExt)
    std::string fullPath;     // "dir/name" with the ".ext" stripped
};

// gilde.exe 0x569530 — VIBE_SaveBrowser_EnumerateSaveFiles.
// `dirName` is the directory; `files` is its raw listing (file names as found); `ext`
// is the extension to match (e.g. ".SAV"), matched case-insensitively against the
// portion from the first '.'.  Returns the matching entries (display name + full path,
// extension stripped from the path) in listing order.
std::vector<SaveFileEntry> SaveBrowser_EnumerateSaveFiles(
    const std::string& dirName, const std::vector<std::string>& files,
    const std::string& ext);

// Synthetic save metadata fed to the slot builder (the bits LoadSlotMetadata reads
// out of the on-disk header+thumbnail).
struct SaveMetadata {
    std::string path;       // matched file path (from enumerate)
    std::string label;      // the display label drawn in the slot
    bool excluded = false;  // header flag bit 0x2 set -> dropped
    bool loadOk = true;     // VIBE_Save_LoadHeaderAndThumbnail succeeded
    int preferredSlot = -1; // FindSaveSlot result (-1 -> append to next free slot)
};

// A built slot in the 16-slot grid.
struct SaveSlot {
    bool occupied = false;  // true for a real save, false for the placeholder fill
    std::string label;      // slot label ("" placeholder uses the default text)
    std::string path;       // backing file path ("" for placeholder)
    int y = 0;              // child-window Y == 130 * slotIndex
};

// gilde.exe 0x569d00 — VIBE_SaveBrowser_LoadSlotMetadata (the slot-list build).
// Builds the 16-entry slot grid:
//   1. drop entries with !loadOk or excluded (header bit 0x2),
//   2. place each surviving save into its preferredSlot (or the next free slot when
//      preferredSlot < 0), first-come within the 16 slots,
//   3. fill every still-empty slot with an empty placeholder.
// Slot i sits at y = 130*i.  Returns the 16 slots in index order.
std::vector<SaveSlot> SaveBrowser_BuildSlots(const std::vector<SaveMetadata>& saves);

} // namespace guild::gui
