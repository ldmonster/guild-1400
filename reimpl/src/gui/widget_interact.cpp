#include "gui/widget_interact.h"
#include "gui/object.h"        // g_widgets
#include "gui/slider.h"        // Slider_ComputeStep
#include "gui/input_state.h"   // g_cursorClampY0 / g_cursorClampY1

#include <cstdint>

namespace guild::gui {

// ---- Scrollbar drag-state cluster (BSS; zero at load) ----------------------
ScrollDragRecord* g_activeDrag      = nullptr; // dword_62D328
i32 g_dragLatched     = 0;   // dword_62D34C
i32 g_dragStartValue  = 0;   // dword_75BF50
i32 g_dragOriginX     = 0;   // dword_62D344
i32 g_dragOriginY     = 0;   // dword_62D340
i32 g_savedClampY0    = 0;   // dword_75BEB8
i32 g_savedClampY1    = 0;   // dword_75BEC0
i32 g_dragOwnerWidget = -1;  // dword_62D33C

void ResetWidgetInteract() {
    g_activeDrag      = nullptr;
    g_dragLatched     = 0;
    g_dragStartValue  = 0;
    g_dragOriginX     = 0;
    g_dragOriginY     = 0;
    g_savedClampY0    = 0;
    g_savedClampY1    = 0;
    g_dragOwnerWidget = -1;
}

// gilde.exe 0x41207c — VIBE_Widget_SetScrollLimit
int Widget_SetScrollLimit(int idx, i16 code) {
    int result = 740 * idx;                  // result = 740 * a1
    g_widgets[idx].editFlags() = code;       // *(WORD*)(dword_69FFB4 + result + 132) = a2
    return result;
}

// gilde.exe 0x4202e8 — VIBE_Widget_ClearActiveDrag
int Widget_ClearActiveDrag() {
    int result = 0; // the original leaves `result` undefined when no drag is active;
                    // every caller ignores it then, so 0 is a faithful stand-in.
    if (g_activeDrag) {                                   // if ( dword_62D328 )
        result = 740 * g_activeDrag->owner;              // 740 * *(rec+304)
        int v1 = g_dragOwnerWidget;                      // v1 = dword_62D33C
        // *(dword_69FFB4 + result + 40) = 0 — clear the owning widget's +40 mirror.
        g_widgets[g_activeDrag->owner].valueMirror() = 0;
        g_activeDrag = nullptr;                          // dword_62D328 = 0
        if (v1 != -1) {                                  // if ( v1 != -1 )
            g_cursorClampY0 = g_savedClampY0;            // dword_62D0C8 = dword_75BEB8
            g_dragOwnerWidget = -1;                      // dword_62D33C = -1
            g_cursorClampY1 = g_savedClampY1;            // dword_62D0D0 = dword_75BEC0
            return g_savedClampY1;                       // return dword_75BEC0
        }
    }
    return result;
}

// gilde.exe 0x4208ac — VIBE_Scrollbar_DragThumb
int Scrollbar_DragThumb(int curMouseX, int curMouseY, int edge20, int held67222C) {
    // The original returns the active-record pointer (dword_62D328) in eax on the
    // non-commit early-returns; callers ignore that value, so we return -1 there and
    // the committed scrollbar value on the commit path.
    if (!g_activeDrag)                                   // if ( dword_62D328 )
        return -1;

    ScrollDragRecord& rec = *g_activeDrag;
    if ((rec.v.flags & 2) == 0)                          // (*(rec+38) & 2) != 0
        return -1;

    if (held67222C) {                                    // if ( dword_67222C )
        int v1 = rec.v.max;                              // v1 = *(rec+24)
        if (rec.v.value < v1)                            // if ( *(rec+296) < v1 )
            rec.v.value = v1;                            //   *(rec+296) = v1
    }

    if (!edge20)                                         // if ( dword_672220 )
        return -1;

    if (rec.v.value == -1)                               // if ( *(rec+296) != -1 )
        return -1;

    if (!g_dragLatched) {                                // if ( !dword_62D34C )
        int v2 = Slider_ComputeStep(rec.v.max - rec.v.min); // VIBE_Slider_ComputeStep(dword_62D328)
        g_dragLatched   = 1;                             // dword_62D34C = 1
        g_savedClampY0  = g_cursorClampY0;               // dword_75BEB8 = dword_62D0C8
        g_savedClampY1  = g_cursorClampY1;               // dword_75BEC0 = dword_62D0D0
        g_dragStartValue = rec.v.value;                  // dword_75BF50 = *(rec+296)
        g_dragOriginY   = curMouseY;                     // dword_62D340 = unk_67220E >> 16
        std::int8_t flagByte = static_cast<std::int8_t>(rec.v.flags); // v3 = *(rec+38)
        g_dragOriginX   = curMouseX;                     // dword_62D344 = dword_672210 >> 16
        if (flagByte < 0) {                              // if ( v3 < 0 ) — bit 0x80 set
            // Open the cursor clamp to the thumb's pixel travel for this value range.
            g_cursorClampY0 = g_dragOriginX
                + 4 * (g_dragStartValue - rec.v.min) / v2;
            g_cursorClampY1 = g_dragOriginX
                - 4 * (rec.v.max - g_dragStartValue) / v2;
        }
        // *(rec+32) = (mouseX) + 4*(startValue - min)/step
        rec.track32 = curMouseX + 4 * (g_dragStartValue - rec.v.min) / v2;
    }

    // return VIBE_Scrollbar_SetThumbPosition(rec, startValue, curMouseX, originX)
    return Scrollbar_SetThumbPosition(rec.v, g_dragStartValue, curMouseX, g_dragOriginX);
}

// gilde.exe 0x420a04 — integer button-step core of VIBE_Slider_UpdateFromMouse.
int Slider_StepFromButtons(int widgetIdx, int clickEdge, int autoRepeat) {
    Widget& w = g_widgets[widgetIdx];                    // v14 = dword_69FFB4 + 740*dword_62D22C

    // a1 = editFlags(+132), a2 = editStep(+140): in the full function these are loaded
    // from the widget record in the (mouse-track) block; read them here directly.
    int editFlags = static_cast<std::uint16_t>(w.editFlags()); // a1
    int step      = w.editStep();                              // a2

    // The repeat fires on a fresh click, or under auto-repeat once the range is large.
    if (!(clickEdge || (autoRepeat && w.editMax() >= 100))) // dword_672228 || (672224 && +128>=100)
        return w.editVal();

    int v15 = w.editVal();                               // v15 = *(v14+120)
    if (w.decButtonHeld()) {                             // if ( *(v14+732) )
        --v15;
    } else if (w.incButtonHeld()) {                      // else if ( *(v14+733) )
        ++v15;
    }

    if (v15 >= w.editValue()) {                          // if ( v15 >= *(v14+124) )  [editMin]
        if ((editFlags & 0x10) != 0 && v15 > step) {     // (a1 & 0x10) && v15 > a2
            v15 = step;                                  // v15 = a2
        } else if (v15 > w.editMax()) {                  // else if ( v15 > *(v14+128) )
            v15 = w.editMax();                           // v15 = *(v14+128)
        }
    } else {
        v15 = w.editValue();                             // v15 = *(v14+124)  clamp up to min
    }

    w.dirty()   = 1;                                     // *(...+96) = 1
    w.editVal() = v15;                                   // *(...+120) = v15
    return v15;
}

} // namespace guild::gui
