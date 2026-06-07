#include "gui/debugwindow.h"

namespace guild::gui {

std::vector<MemoryInfoLine> DebugWindow_BuildMemoryInfo(const MemoryInfoSizes& s) {
    std::vector<MemoryInfoLine> lines;
    // The six size categories accumulate into the running total (v17 in the original:
    //   v8=textures+objInst; v11=+stock; v13=+supermap; v15=+floor; v17=+animations).
    i32 total = 0;
    auto add = [&](const char* label, i32 bytes, bool feeds) {
        lines.push_back({label, bytes, feeds});
        if (feeds)
            total += bytes;
    };
    add("Textures", s.textures, true);
    add("Object-Instances", s.objectInstances, true);
    add("StockObjects", s.stockObjects, true);
    add("Supermap", s.supermap, true);
    add("Floor", s.floor, true);
    add("Animations", s.animations, true);
    // "Animations total" is reported but does not feed the running total.
    add("Animations total", s.animationsTotal, false);
    // The grand "Total" line carries the accumulator.
    lines.push_back({"Total", total, false});
    // Counts.
    add("Charactercount", s.characterCount, false);
    add("Playercount", s.playerCount, false);
    return lines;
}

// gilde.exe 0x4bad5c — VIBE_DamageLabel_RegisterEntry.
int DamageLabelTable::Find(i32 owner) const {
    for (int i = 0; i < kDamageLabelSlots; ++i)
        if (slots_[i].used && slots_[i].owner == owner)
            return i;
    return -1;
}

int DamageLabelTable::Register(i32 owner, i32 payload, const std::string& text,
                               i32 now) {
    // 1. existing owner -> return its slot (no overwrite).
    int existing = Find(owner);
    if (existing != -1)
        return existing;

    // 2. first free slot.
    int slot = -1;
    for (int i = 0; i < kDamageLabelSlots; ++i)
        if (!slots_[i].used) { slot = i; break; }

    // 3. full: evict the last slot whose timestamp is older than `now`.
    if (slot == -1) {
        int victim = -1;
        for (int i = 0; i < kDamageLabelSlots; ++i)
            if (now > slots_[i].timestamp)
                victim = i;   // keep the LAST qualifying index (matches v7)
        if (victim == -1)
            return -1;        // nothing older than now -> give up
        slot = victim;
    }

    DamageLabelEntry& e = slots_[slot];
    e.used = true;
    e.owner = owner;       // +8
    e.timestamp = now;     // +4
    e.payload = payload;   // +0
    e.text = text;         // +12
    return slot;
}

int DamageLabelTable::UsedCount() const {
    int n = 0;
    for (const auto& e : slots_)
        if (e.used)
            ++n;
    return n;
}

} // namespace guild::gui
