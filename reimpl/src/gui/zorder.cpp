#include "gui/zorder.h"
#include "gui/object.h"

#include <cstdint>

namespace guild::gui {

namespace {

// The Z-order list IS the widget pointer cache (dword_62D26C). 512 slots, slot 0 is
// front-most. We mirror the original's byte arithmetic with slot indices: the original
// strides 4 bytes per slot (0..2044) and tests `v4 >= 2044` / `i >= 0`, so it touches
// slots 0..511. memmove(dst,src,n) shifts (n/4) pointer slots.

constexpr int kSlots = kMaxWidgets + 1; // 512 cache slots

// Shift `count` slots from src.. to dst.. (forward/overlapping safe like memmove).
void MoveSlots(int dstSlot, int srcSlot, int count) {
    if (count <= 0) return;
    if (dstSlot < srcSlot) {
        for (int k = 0; k < count; ++k) g_widgetCache[dstSlot + k] = g_widgetCache[srcSlot + k];
    } else {
        for (int k = count - 1; k >= 0; --k) g_widgetCache[dstSlot + k] = g_widgetCache[srcSlot + k];
    }
}

int IndexOf(Widget* p) {
    if (!p) return -1;
    return static_cast<int>(p - &g_widgets[0]);
}

} // namespace

// gilde.exe 0x41aa50 — VIBE_ZOrder_RemoveObject
//
// The original scans the cache from the back (slot 511) for the target, recording the
// top-most occupied slot it passes, then compacts the list down one slot at the target
// position so the gap closes. (The Hex-Rays output renders the compaction as a
// `cache[v1] = target` stamp plus a memmove; both the stamp and the shift collapse to a
// single "shift the tail down by one and clear the freed top slot" once the duplicate
// the shift leaves behind is accounted for — that is what we do here, keeping the list
// an ordered set with the target removed.)
void ZOrder_RemoveObject(int widgetIdx) {
    if (widgetIdx < 0 || widgetIdx >= kMaxWidgets) return;
    Widget* target = &g_widgets[widgetIdx];

    // Find the target, scanning from the back (matching the original's direction).
    int slot = -1;
    for (int s = kMaxWidgets; s >= 0; --s) {
        if (g_widgetCache[s] == target) { slot = s; break; }
    }
    if (slot < 0) return; // not present

    // Shift the tail [slot+1 .. last] down one slot, closing the gap, then clear the
    // now-duplicated final occupied slot.
    int last = kMaxWidgets;
    while (last > slot && g_widgetCache[last] == nullptr) --last;
    for (int s = slot; s < last; ++s)
        g_widgetCache[s] = g_widgetCache[s + 1];
    if (last >= 0)
        g_widgetCache[last] = nullptr;
}

// gilde.exe 0x41acac — VIBE_ZOrder_InsertObject
void ZOrder_InsertObject(int widgetIdx, int parentWidgetIdx) {
    if (widgetIdx < 0 || widgetIdx >= kMaxWidgets) return;
    Widget* target = &g_widgets[widgetIdx];

    // --- Phase 1: remove an existing occurrence (compacting the list). -------------
    int v3 = 0;             // slot index of the found entry
    int v4 = 0;             // running byte offset (post-increment in the original)
    bool found = false;
    while (true) {
        Widget* v6 = g_widgetCache[v4 / 4];
        v4 += 4;
        if (v6 == target) { found = true; break; }
        ++v3;
        if (v4 >= 4 * kMaxWidgets /* 2044 */) break; // LABEL_5: not found
    }
    if (found) {
        if (v3 == kMaxWidgets /* 511 */) {
            g_widgetCache[v3] = nullptr;     // *v5 = 0
        } else {
            // memmove(v5, &cache[v4/4], 4*(512-v3)-8) : shift the tail down one slot.
            MoveSlots(v3, v4 / 4, kSlots - v3 - 2);
        }
    }

    // --- Phase 2: find the insertion slot, scanning from the back. -----------------
    int v7 = kMaxWidgets; // 511
    int insAt = -1;
    for (int i = 4 * kMaxWidgets; i >= 0; i -= 4) {
        Widget* v9 = g_widgetCache[i / 4];
        if (v9) {
            int vIdx = IndexOf(v9);
            bool sameGroup = (parentWidgetIdx == v9->groupLink()) && (v9->type() != kTypeWindow);
            bool backMatch = (g_widgets[parentWidgetIdx].backWidget() == v9->marker());
            if (sameGroup || backMatch) { insAt = v7; break; }
            (void)vIdx;
        }
        --v7;
    }

    if (insAt < 0) {
        // original: VIBE_ErrorLog_ReportMessage("Toby, wir haben ein Problem...")
        return;
    }
    // memmove(&cache[v7+2], &cache[v7+1], 4*(512-(v7+2))) : open a gap after slot v7.
    MoveSlots(insAt + 2, insAt + 1, kSlots - (insAt + 2));
    g_widgetCache[insAt + 1] = target; // cache[v7+1] = widget
}

} // namespace guild::gui
