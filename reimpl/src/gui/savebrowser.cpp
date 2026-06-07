#include "gui/savebrowser.h"

#include <cctype>

namespace guild::gui {

namespace {
// VIBE_Util_StripPathAndExt-style: drop everything from the first '.' onward.
std::string StripExt(const std::string& s) {
    auto dot = s.find('.');
    return dot == std::string::npos ? s : s.substr(0, dot);
}

// The extension portion of a name (from the first '.'), or "" when none.
std::string ExtOf(const std::string& s) {
    auto dot = s.find('.');
    return dot == std::string::npos ? std::string() : s.substr(dot);
}

bool EqualNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}
} // namespace

std::vector<SaveFileEntry> SaveBrowser_EnumerateSaveFiles(
    const std::string& dirName, const std::vector<std::string>& files,
    const std::string& ext) {
    std::vector<SaveFileEntry> out;
    for (const std::string& name : files) {
        // Match the file's extension (from the first '.') against `ext`, no-case.
        // The original compares a 32-char zero-padded copy of the ext substring.
        if (!EqualNoCase(ExtOf(name), ext))
            continue;
        SaveFileEntry e;
        e.displayName = name;  // the original copies the full name into the slot buffer
        // full path "dir/name" then strip the extension off the path.
        e.fullPath = StripExt(dirName + "/" + name);
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<SaveSlot> SaveBrowser_BuildSlots(const std::vector<SaveMetadata>& saves) {
    std::vector<SaveSlot> slots(kSaveMaxSlots);
    for (int i = 0; i < kSaveMaxSlots; ++i)
        slots[i].y = kSaveSlotPitchY * i;  // 130 * slot

    // Pass 1: place surviving saves (cap at 16 — the enumerate count is clamped).
    int placed = 0;
    for (const SaveMetadata& s : saves) {
        if (placed >= kSaveMaxSlots)
            break;
        if (!s.loadOk || s.excluded)  // dropped by !load or header flag 0x2
            continue;
        int slot = s.preferredSlot;
        if (slot < 0 || slot >= kSaveMaxSlots || slots[slot].occupied) {
            // FindSaveSlot returned no usable slot -> next free slot.
            slot = -1;
            for (int i = 0; i < kSaveMaxSlots; ++i)
                if (!slots[i].occupied) { slot = i; break; }
            if (slot < 0)
                break;  // grid full
        }
        slots[slot].occupied = true;
        slots[slot].label = s.label;
        slots[slot].path = s.path;
        ++placed;
    }

    // Pass 2: every still-empty slot gets the placeholder (the `*(v42+12)=0;` empty
    // entry + default "%i" label in LoadSlotMetadata's tail loop).
    for (int i = 0; i < kSaveMaxSlots; ++i)
        if (!slots[i].occupied) {
            slots[i].occupied = false;
            slots[i].label.clear();
            slots[i].path.clear();
        }

    return slots;
}

} // namespace guild::gui
