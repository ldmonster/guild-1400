#include "gui/object.h"

#include <cstdint>

namespace guild::gui {

// Original BSS bases:
//   dword_69FFB4 = g_widgets   (511 * 740 = 378140 bytes)
//   dword_62D26C = g_widgetCache (512 pointer slots)
//   dword_62D24C = g_widgetHighWater
Widget  g_widgets[kMaxWidgets];
Widget* g_widgetCache[kMaxWidgets + 1];
int     g_widgetHighWater;
void*   g_widgetData[kMaxWidgets];

void ResetWidgets() {
    for (auto& wgt : g_widgets) wgt = Widget{};
    for (auto& p : g_widgetCache) p = nullptr;
    for (auto& d : g_widgetData) d = nullptr;
    g_widgetHighWater = 0;
}

// gilde.exe 0x412dac — VIBE_Widget_AllocSlot
//
// The original walks the widget array (`v3 += 740`) until it finds inUse(+4)==0 or
// runs past 378140 bytes (=> idx -1). It then walks the pointer cache backwards from
// slot 511 (byte offset 2044) looking for the last occupied entry to find the next
// free cache slot, clamps it to >=0, stores the new widget pointer there, updates the
// high-water index, sets inUse=1, and returns the slot index.
int Widget_AllocSlot() {
    // Linear scan for a free slot: original steps `v3 += 740` over the byte array
    // until inUse(+4)==0, or past 378140 bytes (== 740*511) -> idx -1.
    int idx = 0;
    while (g_widgets[idx].inUse()) {
        ++idx;
        if (idx >= kMaxWidgets) { // v3 >= 378140 == 740*511
            idx = -1;
            break;
        }
    }
    if (idx == -1)
        return -1;

    Widget* newPtr = &g_widgets[idx];

    // Find the next free cache slot by scanning backwards from the top (slot 511).
    // v6 starts at 511 (byte 2044); decrement while the cache entry is empty; if the
    // scan falls below 0 every slot was empty -> v6 == -1; otherwise the loop found
    // an occupied slot and v6 is incremented to point one past it.
    int cacheSlot = kMaxWidgets;     // v6 = 511
    int v7 = 4 * kMaxWidgets;        // byte offset 2044
    while (g_widgetCache[v7 / 4] == nullptr) {
        v7 -= 4;
        --cacheSlot;
        if (v7 < 0)
            break;
    }
    if (v7 >= 0)             // loop found an occupied slot
        ++cacheSlot;         // ...point one past it
    if (cacheSlot < 0)
        cacheSlot = 0;

    g_widgetCache[cacheSlot] = newPtr;
    if (idx > g_widgetHighWater)
        g_widgetHighWater = idx;
    g_widgets[idx].inUse() = 1;
    return idx;
}

void Widget_FreeSlot(int idx) {
    if (idx < 0 || idx >= kMaxWidgets)
        return;
    Widget* p = &g_widgets[idx];
    // Remove from the pointer cache (mirrors clearing the cache entry on destroy).
    for (auto& c : g_widgetCache) {
        if (c == p)
            c = nullptr;
    }
    g_widgets[idx] = Widget{}; // clears inUse(+4) so the next AllocSlot scan reuses it
    g_widgetData[idx] = nullptr;
}

// gilde.exe 0x41db9c — VIBE_Object_GetDataPtr
i32 Object_GetDataPtr(int idx) {
    Widget& w = g_widgets[idx]; // dword_69FFB4 + 740*a1

    if (w.type() == kTypeAnim) { // 0x41 'A'
        auto* rec = static_cast<Object3D*>(g_widgetData[idx]); // *(v1+12) -> Object3D*
        u8 fl = rec->flags(); // *(v2+38)
        if (fl & kAnimFlag328) // 0x10
            return rec->ptr328();                                  // *(v2+328)
        if (fl & kAnimFlagInline) // 0x01
            return static_cast<i32>(reinterpret_cast<std::intptr_t>(&rec->inline40())); // v2+40
        if (fl & kAnimFlag296) // 0x02
            return rec->ptr296();                                  // *(v2+296)
    }

    if (w.btnFlagA())             // *(v1+68)
        return w.value();         // *(v1+36)
    if (w.type() == kTypeEdit)    // 0x45 'E'
        return w.editText();      // *(v1+120)
    return -1;
}

} // namespace guild::gui
