#include "gui/hud.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// HUD slot table (dword_11CB560 / dword_11CB564, stride 3, 16 slots).
// ---------------------------------------------------------------------------
HudSlot g_hudSlots[kHudSlotCount];

void ResetHudSlots() {
    for (int i = 0; i < kHudSlotCount; ++i) {
        g_hudSlots[i].owner  = 0;
        g_hudSlots[i].widget = -1;
        g_hudSlots[i].window = 0;
    }
}

// gilde.exe 0x4bc280 — the hot-spot scan:
//   v7 = 0; while(1){ if(dword_11CB564[v7]!=-1 && ...) { if(hover==*(widget+0)) break; }
//                     v7 += 3; if(v7>=48) goto LABEL_31; }
// In this model each slot's stored widget index is g_hudSlots[i].widget; a click over
// `hoverWidget` matches the slot whose widget == hoverWidget.
int Hud_FindSlotForWidget(int hoverWidget) {
    for (int i = 0; i < kHudSlotCount; ++i) {
        if (g_hudSlots[i].widget != -1 && g_hudSlots[i].widget == hoverWidget)
            return i;
    }
    return -1;
}

// gilde.exe 0x4c5460 — first-free scan:
//   slot = 0; v8 = 0; if(dword_11CB564[0]!=-1){ do{ v8+=3; ++slot; }
//                       while(v8<48 && dword_11CB564[v8]!=-1); }
//   if(slot>=16) slot = FindNearestEventPanelSlot();
int Hud_FindFreeSlot() {
    int slot = 0; // result slot index
    int v8 = 0;   // dword offset = 3*slot
    if (g_hudSlots[0].widget != -1) {
        do {
            v8 += kHudSlotStride;
            ++slot;
        } while (v8 < kHudSlotStride * kHudSlotCount && g_hudSlots[slot].widget != -1);
    }
    if (slot >= kHudSlotCount)
        return -1; // (original calls FindNearestEventPanelSlot, which can yield -1)
    return slot;
}

// ---------------------------------------------------------------------------
// Click-mode flag dispatch (the v21 bit tests in HandleMouseClick).
// ---------------------------------------------------------------------------
HudClickAction Hud_ClassifyClick(int clickMode) {
    if (clickMode & kClickFlagSelection)
        return HudClickAction::kSelection;
    if (clickMode & kClickFlagInfoPanel)
        return HudClickAction::kInfoPanel;
    if (clickMode & kClickFlagStatusBanner)
        return HudClickAction::kStatusBanner;
    return HudClickAction::kNone;
}

namespace {
HudCommandSink g_defaultSink;       // no-op sink (mock seam)
HudCommandSink* g_sink = &g_defaultSink;
} // namespace

void Hud_SetCommandSink(HudCommandSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

HudClickAction Hud_DispatchClick(int clickMode, int hoverWidget) {
    // The original resolves the hovered widget against the slot table first (this is
    // what the dword_75BF10 / hot-spot loop does) before applying the flag-bit action.
    (void)Hud_FindSlotForWidget(hoverWidget);
    HudClickAction action = Hud_ClassifyClick(clickMode);
    switch (action) {
        case HudClickAction::kSelection:    g_sink->OnSelection();    break;
        case HudClickAction::kInfoPanel:    g_sink->OnInfoPanel();    break;
        case HudClickAction::kStatusBanner: g_sink->OnStatusBanner(); break;
        case HudClickAction::kNone:         break;
    }
    return action;
}

// ---------------------------------------------------------------------------
// HUD label layout.
// ---------------------------------------------------------------------------
HudLabelLayout Hud_LabelLayout(HudLabelAlign align, int anchorX, i16 width) {
    HudLabelLayout out{};
    out.width  = width;
    out.flag88 = 0;
    out.flag92 = 0;
    switch (align) {
        case HudLabelAlign::kCentered: // 0x552778: x = anchorX - width/2 ; +88 = 1
            out.x      = anchorX - width / 2;
            out.flag88 = 1;
            break;
        case HudLabelAlign::kRight:    // 0x552858: x = anchorX - width ; +92 = 1
            out.x      = anchorX - width;
            out.flag92 = 1;
            break;
        case HudLabelAlign::kLeft:     // 0x5528b0: x = anchorX ; +92 = 0
            out.x      = anchorX;
            out.flag92 = 0;
            break;
    }
    return out;
}

// ---------------------------------------------------------------------------
// HUD button row layout — gilde.exe 0x4bcdfc.
// ---------------------------------------------------------------------------
int Hud_ButtonRowLayout(const int* widths, int count, int windowWidth, int* outX) {
    int maxW = 0; // v5
    for (int i = 0; i < count; ++i) {
        int wv = widths[i]; // VIBE_Property_Get(text,..) + 32 in the original
        if (wv > maxW)
            maxW = wv;
    }

    int cell = windowWidth / (count + 1); // v14/(a4+1)
    int v26;                              // start-offset accumulator (result)
    int pitch;                            // v16 / v27
    if (maxW <= cell) {
        pitch = cell;
        v26   = cell;
    } else {
        pitch = maxW + 8;                                  // v5+8
        int total = (maxW + 8) * count;                    // v17
        int spread = (total <= windowWidth) ? (windowWidth - total) : (total - windowWidth); // v18
        v26 = (maxW + 8) / 2 + spread / 2;                 // (v5+8)/2 + v18/2
    }

    int x = maxW / -2 + v26; // v20 = v5/-2 + v26
    for (int i = 0; i < count; ++i) {
        outX[i] = x;
        // The original stores x into the low 16 bits (LOWORD) of the running x, so the
        // accumulator wraps as a 16-bit value; reproduce that wraparound.
        x = (i16)(pitch + x);
    }
    return pitch;
}

// gilde.exe 0x4bea2c — VIBE_Hud_FindModeIndex.
int Hud_FindModeIndex(int modeId, int curIndex, const int* modeTable) {
    int tag = modeId * 4;       // v1 = result*4
    int v2  = curIndex;         // dword_631610
    if (curIndex >= 0) {
        int result = 2 * curIndex; // index into the stride-2 table
        while (tag != modeTable[result]) {
            result -= 2;
            --v2;
            // 0x4bea4f: not-found exit returns `result * 4` where `result` is now the
            // negative loop counter (e.g. -2 -> -8), NOT 4*modeId.  (disasm-confirmed)
            if (result < 0)
                return result * 4;
        }
        // 0x4bea5b: side effect dword_631614 = dword_631610 - v2 (the depth); the live
        // process shares one global, modeled by the return value here.
        return curIndex - v2;      // depth from the top
    }
    // curIndex < 0: `result` (eax) is still the input modeId, so this returns 4*modeId.
    return modeId * 4;
}

// ---------------------------------------------------------------------------
// Status-text table — gilde.exe 0x4bcc80.
// ---------------------------------------------------------------------------
StatusTextEntry g_statusText[kStatusTextCount];

void ResetStatusText() {
    for (int i = 0; i < kStatusTextCount; ++i) {
        g_statusText[i].inUse = false;
        g_statusText[i].key   = 0;
        g_statusText[i].tag   = 0;
    }
}

int StatusText_Register(int key, int tag) {
    // De-dup pass: i in [0,1600) step 50 -> slots 0..31.
    for (int i = 0; i < kStatusTextCount; ++i) {
        if (g_statusText[i].inUse && g_statusText[i].key == key) {
            // *(entry+536) = 1 (mark active) -> already in use; return slot.
            return i;
        }
    }
    // Count occupied slots (the original walks until the first empty slot).
    int count = 0;
    if (g_statusText[0].inUse) {
        int v7 = 0;
        do {
            v7 += kStatusTextStride;
            ++count;
        } while (v7 < kStatusTextStride * kStatusTextCount && g_statusText[count].inUse);
    }
    if (count >= kStatusTextCount)
        return -1; // "Too many Objects"
    g_statusText[count].inUse = true;
    g_statusText[count].key   = key;
    g_statusText[count].tag   = tag;
    return count;
}

// ---------------------------------------------------------------------------
// Damage-label table — gilde.exe 0x4bad5c.
// ---------------------------------------------------------------------------
DamageLabelEntry g_damageLabels[kDamageLabelCount];

void ResetDamageLabels() {
    for (int i = 0; i < kDamageLabelCount; ++i) {
        g_damageLabels[i].amount    = 0;
        g_damageLabels[i].timestamp = 0;
        g_damageLabels[i].source    = 0;
        g_damageLabels[i].inUse     = false;
    }
}

int DamageLabel_Register(int source, int amount, int now) {
    // De-dup by source handle: scan all 64 slots (v5 step 67).
    for (int i = 0; i < kDamageLabelCount; ++i) {
        if (g_damageLabels[i].inUse && g_damageLabels[i].source == source)
            return i;
    }
    // Find first free slot (the original counts occupied until empty).
    int slot = 0;
    if (g_damageLabels[0].inUse) {
        int v6 = 0;
        do {
            v6 += kDamageLabelStride;
            ++slot;
        } while (v6 < kDamageLabelStride * kDamageLabelCount && g_damageLabels[slot].inUse);
    }
    if (slot >= kDamageLabelCount) {
        // Full: evict the entry with the smallest timestamp strictly older than `now`.
        int best = -1; // v7
        for (int i = 0; i < kDamageLabelCount; ++i) {
            if (now > g_damageLabels[i].timestamp)
                best = i;
        }
        slot = best;
        if (slot == -1)
            return -1;
    }
    g_damageLabels[slot].inUse     = true;
    g_damageLabels[slot].timestamp = now;   // *(entry+4) = dword_62EB38
    g_damageLabels[slot].source    = source; // *(entry+8) = v3
    g_damageLabels[slot].amount    = amount; // *(entry+0) = a2
    return slot;
}

// ---------------------------------------------------------------------------
// On-screen clock — gilde.exe 0x527778.
// ---------------------------------------------------------------------------
int g_dayLengthSeconds = 100; // dword_63CC60 (recovered value)

ClockTime Clock_ComputeTimeOfDay(int tickAccumulator) {
    // v1 = (tickAccum * 0.00625 + 0.5) * dayLength   (computed in x87 double)
    double v1 = ((double)tickAccumulator * (double)kClockTickScale + kClockTickBias) *
                (double)g_dayLengthSeconds;
    int seconds = (int)v1; // truncation toward zero (x87 -> int)

    ClockTime t{};
    t.totalSeconds = seconds;
    t.h = seconds / 3600;
    t.m = seconds % 3600 / 60;
    t.s = seconds % 3600 % 60;
    return t;
}

} // namespace guild::gui
