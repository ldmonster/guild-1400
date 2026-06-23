// =============================================================================
// guild::gui — menu-frame supporting leaves (see menu_frame_leaves.h).
// Faithful 1:1 reconstructions of the genuinely-missing leaves the main menu and
// its sub-screens call each frame.  Host/DDraw edges go through inert-default hooks.
// =============================================================================
#include "gui/menu_frame_leaves.h"

#include "gui/gui_dialogs7.h"  // g_fadeSlots (dword_672280) — REUSED, not redefined
#include "gui/object.h"        // g_widgets, Widget, Widget_AllocSlot
#include "gui/window.h"        // g_windows, g_defaultFont (dword_62D2B0)
#include "gui/widget_create.h" // g_defaultCtrlH(69FFB0), g_screenClipExt(69FFBC), Property_Get
#include "gui/radiogroup.h"    // g_radioGroups, Selection_Update

#include <cstdlib>
#include <cstring>

namespace guild::gui {

// ---- module-owned state ----------------------------------------------------
MenuInputState g_menuInput = {};
u8 g_radioGroupFlags[8] = {0};

// ---------------------------------------------------------------------------
// Hooks (inert defaults).
// ---------------------------------------------------------------------------
namespace {

FadeRecord* DefaultAllocFade(int /*size*/, const char* /*tag*/) {
    // VIBE_Memory_AllocDebug(0x64, "d2:fadeinfo") -> a real zeroed 100-byte record.
    FadeRecord* r = static_cast<FadeRecord*>(std::calloc(1, sizeof(FadeRecord)));
    return r;
}
void DefaultFreeFade(FadeRecord* block) { std::free(block); }
void DefaultReleaseTexture(i32 /*tex*/) {}
i32  DefaultFillBackBuffer(int /*mode*/, const u32* /*pix*/, int /*color*/) { return 0; }

void DefaultGameLogicEntities(int, int, i32) {}
void DefaultResultHandlerInteraction(int, int, i32, i32, i32, int, int, i32) {}
void DefaultFadeUpdate(i32, i32) {}
void DefaultRenderPresentFrame(void*) {}
void DefaultGuiNop() {}
void DefaultCoordWarp(int, int) {}

const MenuFrameLeavesHooks kDefaultHooks = {
    &DefaultAllocFade,
    &DefaultFreeFade,
    &DefaultReleaseTexture,
    &DefaultFillBackBuffer,
    &DefaultGameLogicEntities,
    &DefaultResultHandlerInteraction,
    &DefaultFadeUpdate,
    &DefaultRenderPresentFrame,
    &DefaultGuiNop,
    &DefaultCoordWarp,
};

const MenuFrameLeavesHooks* g_hooks = &kDefaultHooks;

} // namespace

const MenuFrameLeavesHooks* SetMenuFrameLeavesHooks(const MenuFrameLeavesHooks* hooks) {
    const MenuFrameLeavesHooks* prev = g_hooks;
    g_hooks = hooks ? hooks : &kDefaultHooks;
    return prev;
}

void ResetMenuFrameLeaves() {
    g_menuInput = MenuInputState{};
    g_menuInput.popupGate = -1;        // dword_62D294 default == -1 (no popup)
    g_menuInput.selectedWidget = 0;
    std::memset(g_radioGroupFlags, 0, sizeof(g_radioGroupFlags));
    std::memset(g_fadeSlots, 0, sizeof(g_fadeSlots)); // dword_672280[32]
}

// The fade slots store synthetic handles; we keep the FadeRecord pointers in a parallel
// owned table so g_fadeSlots can stay an i32-handle array exactly as the original BSS is
// (the original stores the raw 32-bit record pointer there). Handle == 1-based slot+1.
namespace {
FadeRecord* g_fadeRecords[kFadeSlotCount] = {nullptr};
} // namespace

// ===========================================================================
// gilde.exe 0x41f0e8 — VIBE_Fade_Register
// ===========================================================================
i32 Fade_Register(i32 x, i32 y, i32 h, i32 w, u8 flags, i32 duration,
                  const u32* pixels, i32 now) {
    // Scan g_fadeSlots for the first empty entry. v10/v11 walk together; the loop
    // continues while v11<32 AND the slot is occupied. v10 ends == first-free index.
    int idx = 0;            // v10
    if (g_fadeSlots[0]) {   // if ( dword_672280[0] )
        int i = 0;          // v11
        do {
            ++i;            // ++v11
            ++idx;          // ++v10
        } while (i < kFadeSlotCount && g_fadeSlots[i]);
    }
    if (idx >= kFadeSlotCount)   // if ( v10 >= 32 ) return 0
        return 0;

    // VIBE_Memory_AllocDebug(0x64, "d2:fadeinfo")
    FadeRecord* rec = g_hooks->allocFade(kFadeRecordBytes, "d2:fadeinfo");
    if (!rec)
        return 0;

    rec->rectX()    = x;        // *((DWORD*)v13 + 17) = a1
    rec->rectY()    = y;        // *((DWORD*)v13 + 18) = a2
    rec->rectW()    = w;        // *((DWORD*)v13 + 19) = a4
    rec->flags()    = flags;    // *v13 = a7
    rec->duration() = duration; // *((DWORD*)v13 + 21) = a6
    rec->rectH()    = h;        // *((DWORD*)v13 + 20) = a3
    // *((DWORD*)v13 + 24) = VIBE_Render_FillBackBuffer(1, a5, a3)
    rec->texture()  = g_hooks->fillBackBuffer(1, pixels, h);
    rec->startTick()= now;      // *(v14 + 88) = dword_62EB44
    rec->lastTick() = now;      // *(v14 + 92) = dword_62EB44

    // dword_672280[idx] = record (handle). Store a non-null 1-based handle and keep the
    // record pointer in the parallel table.
    i32 handle = idx + 1;
    g_fadeSlots[idx]   = handle;
    g_fadeRecords[idx] = rec;
    return handle;          // return v14 (the record handle)
}

// ===========================================================================
// gilde.exe 0x41f18c — VIBE_Fade_Unregister
// ===========================================================================
i32 Fade_Unregister(i32 handle) {
    int result = 0;
    bool found = (handle == g_fadeSlots[0]); // a1 == dword_672280[0]
    if (!found) {
        while (++result < kFadeSlotCount) {       // while ( ++result < 32 )
            if (handle == g_fadeSlots[result]) {  // a1 == dword_672280[result]
                found = true;
                break;
            }
        }
    }
    if (!found)
        return result * 4;   // return result*4 (== 32*4 == 128, the not-found sentinel)

    // LABEL_4: clear the slot, release the texture (+96), free the record.
    g_fadeSlots[result] = 0;                       // dword_672280[result] = 0
    FadeRecord* rec = g_fadeRecords[result];
    g_fadeRecords[result] = nullptr;
    if (rec)
        g_hooks->releaseTexture(rec->texture());   // VIBE_Surface_ReleaseTexture(*(a1+96))
    g_hooks->freeFade(rec);                         // VIBE_Memory_FreeDebug(...)
    return 0;
}

// ===========================================================================
// gilde.exe 0x4134f0 — VIBE_Window_RenderEntityList
//   VIBE_GameLogic_Entities(0, 0, a1)
//   VIBE_Result_Handler_Interaction(0,0, dword_62D210[+8], dword_62D210[+4],
//                                   dword_62D210, 0, 0, dword_62D218)
//   for (i=0; i!=32; ++i) if (dword_672280[i]) VIBE_Fade_Update(dword_672280[i], 62D210)
//   VIBE_Render_PresentFrame(0x80)
//   VIBE_Result_Handler_Interaction(0,0, 62D210[+8], 62D210[+4], 62D218, 0,0, 62D210)
//   VIBE_Gui_Nop()
// dword_62D210 = back surface, dword_62D218 = work surface (modeled as ids 0/1).
// ===========================================================================
void Window_RenderEntityList(i32 arg) {
    const MenuFrameLeavesHooks& h = *g_hooks;
    const i32 back = 0;   // dword_62D210
    const i32 work = 1;   // dword_62D218
    // The surface dims (*(dword_62D210+8)==w, *(dword_62D210+4)==h) are renderer-owned;
    // we pass 0/0 here (the host edge supplies real dims). The arg ordering is preserved.
    const i32 srcW = 0;
    const i32 srcH = 0;

    h.gameLogicEntities(0, 0, arg);
    h.resultHandlerInteraction(0, 0, srcW, srcH, back, 0, 0, work);
    for (int i = 0; i != kFadeSlotCount; ++i) {
        if (g_fadeSlots[i])
            h.fadeUpdate(g_fadeSlots[i], back);
    }
    h.renderPresentFrame(reinterpret_cast<void*>(0x80));
    h.resultHandlerInteraction(0, 0, srcW, srcH, work, 0, 0, back);
    h.guiNop();
}

// ===========================================================================
// gilde.exe 0x41b494 — VIBE_Object_CreateTextLabel  (a1=x@ax, a2=y@dx, a3=text@ebx)
//   VIBE_Widget_AllocSlot()
//   v3 = VIBE_Memory_AllocDebug(0x101, "d2:ShowTxt")   ; 257-byte text buffer
//   *(740*v4 + dword_69FFB4 + 116) = v3   ; +116 = text buffer ptr
//   if (!v3) return -1
//   <byte-pair strcpy text -> v3>
//   *(+112) = dword_62D2B0   ; colour/font word
//   *(+16)  = a1 (x)         *(+18) = a2 (y)
//   *(+24)  = 67 ('C')       ; type tag
//   v13 = VIBE_Property_Get(a3, *(+110)>>16)
//   *(+26) = 2   *(+32) = 0   *(+28) = 0
//   *(+20) = v13 + 96        ; width
//   *(+22) = dword_69FFB0    ; default ctrl height
//   *(+34) = dword_69FFBC    ; LOWORD screen clip ext
//   *(+30) = HIWORD(dword_69FFBC)
// ===========================================================================
int Object_CreateTextLabel(i16 x, i16 y, const char* text) {
    int slot = Widget_AllocSlot();   // VIBE_Widget_AllocSlot()  (REUSED)
    if (slot < 0)
        return -1;
    Widget& w = g_widgets[slot];     // 740*v4 + dword_69FFB4

    // VIBE_Memory_AllocDebug(0x101, "d2:ShowTxt") — a 257-byte owned text buffer kept in
    // the parallel data table (the +116 field cannot hold a 64-bit ptr on the host).
    char* buf = static_cast<char*>(std::calloc(0x101, 1));
    SetWidgetDataOwned(slot, buf);   // *(+116) = owned block (freed on slot recycle/reset)
    if (!buf)
        return -1;                   // if (!v3) return -1

    // byte-pair strcpy of text into the buffer (the original's unrolled copy).
    std::strncpy(buf, text ? text : "", 0x100);
    buf[0x100] = '\0';

    w.at<u16>(112) = static_cast<u16>(g_defaultFont); // +112 = dword_62D2B0 (colour/font)
    w.at<i16>(16)  = x;                                // +16 = a1
    w.at<i16>(18)  = y;                                // +18 = a2
    int prop = w.ld<i32>(110) >> 16;                   // *(+110) >> 16
    w.type()       = kTypeLabel;                       // +24 = 67 ('C')
    i16 v13 = Property_Get(text, prop);                // VIBE_Property_Get(a3, *(+110)>>16)
    w.at<u16>(26)  = 2;                                // +26 = 2
    w.at<u16>(32)  = 0;                                // +32 = 0
    w.at<u16>(28)  = 0;                                // +28 = 0
    w.at<u16>(20)  = static_cast<u16>(v13 + 96);       // +20 = v13 + 96 (width)
    w.at<u16>(22)  = static_cast<u16>(g_defaultCtrlH); // +22 = dword_69FFB0
    w.at<u16>(34)  = static_cast<u16>(g_screenClipExt & 0xFFFF);          // +34 = LOWORD(69FFBC)
    w.at<u16>(30)  = static_cast<u16>((g_screenClipExt >> 16) & 0xFFFF);  // +30 = HIWORD(69FFBC)
    return slot;                                       // v15 (the widget slot)
}

// ===========================================================================
// gilde.exe 0x41e614 — VIBE_Object_SetColor  (a1=idx@eax, a2=color@edx)
//   v3 = 740*a1 + dword_69FFB4
//   LOBYTE(a1) = *(v3 + 24)        ; the widget type tag
//   *(WORD*)(v3 + 112) = a2        ; colour word
//   if (type >= 0x40) {
//       if (type <= 0x40)          ; type == 64 ('@')
//           word_67EDFC[17*(56 * *(v3+116))/2] = a2   ; owning-window title colour
//       else if (type == 65)       ; 'A'
//           dword_6951B4[(348 * *(v3+116))/4] = a2     ; object colour dword
//   }
//   return a1   ; (al == resolved value)
// word_67EDFC / dword_6951B4 are the window/object colour tables keyed by the widget's
// owning-window slot (+116); modeled as the live Window record fields where present.
// ===========================================================================
u8 Object_SetColor(i32 idx, i32 color) {
    Widget& w = g_widgets[idx];                        // v3 = 740*a1 + dword_69FFB4
    u8 type = w.type();                                // LOBYTE(a1) = *(v3+24)
    // The return register (al) starts as the type byte and is only overwritten in the
    // type==0x40 branch (LOBYTE(a1) = 16*v4). All other branches return the type byte.
    u8 ret = type;
    w.at<u16>(112) = static_cast<u16>(color);          // *(WORD*)(v3+112) = a2

    if (type >= kTypeWindow) {                          // >= 0x40
        i32 owner = w.ownerWindow();                    // *(v3+116)
        if (type <= kTypeWindow) {                       // == 64 ('@')
            // v4 = 56 * owner ; word_67EDFC[17*v4/2] = a2 ; LOBYTE(a1) = 16*v4.
            // word_67EDFC[17*(56*owner)/2] — the owning window's title-colour word. The
            // 56*owner index resolves into the per-window colour-word table; on the live
            // model that table is keyed by the owning window slot, so we write the colour
            // into the Window record's title-colour word.
            i32 v4 = 56 * owner;
            if (owner >= 0 && owner < kMaxWindows)
                g_windows[owner].at<u16>(636) = static_cast<u16>(color); // word @ +636
            ret = static_cast<u8>(16 * v4);             // LOBYTE(a1) = 16*v4
        } else if (type == kTypeAnim) {                  // == 65 ('A')
            // a1 = 348*owner ; dword_6951B4[a1/4] = a2. al == LOBYTE(348*owner).
            if (owner >= 0 && owner < kMaxWindows)
                g_windows[owner].at<i32>(904) = color;   // object colour dword @ +904
            ret = static_cast<u8>(348 * owner);          // a1 = 348*owner -> al
        }
        // type > 0x41 (and >= 0x40): al stays the type byte.
    }
    return ret;                                          // al (resolved value byte)
}

// ===========================================================================
// gilde.exe 0x41dd98 — VIBE_Object_SetVisibleRecursive  (a1=idx@eax, a2=vis@edx)
//   v3 = 740*a1 + dword_69FFB4
//   *(v3 + 52) = (a2 == 0)              ; +52 holds the HIDDEN flag
//   if (*(v3 + 24) == 64) {            ; type '@' window-backing -> recurse children
//       v6 = &dword_67EB80[238 * *(v3+116)]   ; the owning window record
//       n  = *(v6 + 26) >> 16          ; objCount (word @ +28)
//       for (i = 0; i < n; ++i)
//           VIBE_Object_SetVisibleRecursive(childList[i], a2)
//   }
//   return n
// childList == *(v6 + 24) (the window's object-id list, dword[6]).
// ===========================================================================
int Object_SetVisibleRecursive(int idx, int vis) {
    Widget& w = g_widgets[idx];                 // v3
    int result = (vis == 0) ? 1 : 0;            // a2 == 0
    w.at<i32>(52) = result;                     // *(v3+52) = (a2==0)  (hidden flag)

    if (w.type() == kTypeWindow) {              // *(v3+24) == 64
        int owner = w.ownerWindow();            // *(v3+116)
        if (owner < 0 || owner >= kMaxWindows)
            return result;
        Window& win = g_windows[owner];         // &dword_67EB80[238*owner]
        result = win.objCount();                // *(v6+26)>>16 == word @ +28
        if (result > 0) {
            const i32* children = WindowChildList(owner); // *(v6+24) object-id list
            for (int i = 0; i < result; ++i) {
                if (children)
                    Object_SetVisibleRecursive(children[i], vis); // recurse
                result = win.objCount();        // re-read each iteration (matches orig)
            }
        }
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x412970 — VIBE_InitStateReader  (a1=group@eax)
// Radio-group keyboard/mouse state reader.  See header for the global map.
// ===========================================================================
int InitStateReader(int group) {
    MenuInputState& s = g_menuInput;
    RadioGroup& g = g_radioGroups[group];       // 35*group dwords (== 140*group bytes)

    int result = 140 * group;                   // result = 140*a1
    if (g.count <= 0)                            // *(int*)((char*)dword_676584 + 140*a1) <= 0
        return result;

    int v3 = g.selected;                        // v3 = *(int*)((char*)dword_676588 + 140*a1)

    // --- focus-latch / mouse-still block ---
    // if ( dword_62D2FC || (mouse hasn't moved) ) { if (!dword_62D2FC) goto LABEL_13; }
    // else dword_62D2FC = 1;
    bool mouseStill = ((s.mouseX1616 >> 16) == s.lastMouseX) &&
                      ((s.mouseY1616 >> 16) == s.lastMouseY);
    if (s.focusLatch || mouseStill) {
        if (!s.focusLatch) {
            // goto LABEL_13 (skip the focus-scan block)
        } else {
            // focusLatch set -> if no external override, scan buttons for the focused id
            if (s.selectionOverride == 0) {  // !dword_62D328
                int v4 = 0;                  // v4 = dword_62D328 (== 0 here)
                for (int i = 0; i < g.count; ++i, ++v4) {
                    // dword_67658C[35*a1 + i] == g.button[i] == dword_62D22C
                    // AND ((byte_676580[140*a1] & 1) == 0 || dword_67221C)
                    if (g.button[i] == s.selectedWidget &&
                        (((g_radioGroupFlags[group] & 1) == 0) || s.wrapAllowed)) {
                        v3 = v4;
                    }
                }
                if (v3 == g.selected)        // v3 == dword_676588[35*a1]
                    s.focusLatch = 0;        // dword_62D2FC = 0
            }
        }
    } else {
        s.focusLatch = 1;                    // dword_62D2FC = 1
    }

    // --- LABEL_13: key handling (UP / DOWN) ---
    // UP: byte_67225C == -56 || (dword_672254 && dword_62D294 == -1)
    if (s.lastKey == kKeyUp || (s.upHeld && s.popupGate == -1)) {
        s.focusLatch = 0;                                  // dword_62D2FC = 0
        s.lastMouseX = s.mouseX1616 >> 16;                 // dword_69FFC0 = unk_67220E >> 16
        int v6 = v3 - 1;                                   // v6 = v3 - 1
        s.lastMouseY = s.mouseY1616 >> 16;                 // dword_69FFC4 = dword_672210 >> 16
        if (v6 < 0)
            v6 = g.count - 1;                              // wrap to last
        v3 = (g.count != 0) ? (v6 % g.count) : v6;         // v3 = v6 % count
        if (s.upHeld) {                                    // if (dword_672254) warp cursor
            // gilde.exe 0x412ab8-0x412aec: warp to the button centre. The original
            // builds X = trunc(x + dbl_610E84*w), Y = trunc(y + dbl_610E84*h) on the
            // x87 stack (dbl_610E84 == 0.5, get_bytes 0x610E84,8), each truncated via
            // VIBE_Coord_ConvertX @0x5c6b08, then ConvertY(X,Y). Since x/y are integers
            // and w/h are non-negative, trunc(x + 0.5*w) == x + w/2 (integer div toward
            // zero) exactly, so the integer centre below is value-identical. VERIFIED 1:1.
            if (v3 >= 0 && v3 < g.count) {
                Widget& b = g_widgets[g.button[v3]];
                g_hooks->coordWarp(b.x() + b.w() / 2, b.y() + b.h() / 2);
            }
        }
        // else fall through to LABEL_18
    } else if (s.lastKey == kKeyDown || (s.downHeld && s.popupGate == -1)) {
        // DOWN: byte_67225C == -48 || (dword_672250 && dword_62D294 == -1)
        int v15 = v3 + 1;                                  // v15 = v3 + 1
        s.lastMouseX = s.mouseX1616 >> 16;                 // dword_69FFC0
        s.focusLatch = 0;                                  // dword_62D2FC = 0
        s.lastMouseY = s.mouseY1616 >> 16;                 // dword_69FFC4
        if (v15 < 0)
            v15 = g.count - 1;
        v3 = (g.count != 0) ? (v15 % g.count) : v15;       // v3 = v15 % count
        if (s.downHeld) {                                  // if (dword_672250) warp cursor
            if (v3 >= 0 && v3 < g.count) {
                Widget& b = g_widgets[g.button[v3]];
                g_hooks->coordWarp(b.x() + b.w() / 2, b.y() + b.h() / 2);
            }
        }
    }

    // --- LABEL_18: commit selection + ENTER activation ---
    result = 140 * group;
    if (v3 == -1) {
        if (g.selected != -1)                              // dword_676588[35*a1] != -1
            return Selection_Update(group, -1);            // VIBE_Selection_Update(a1, -1)
        return result;
    }

    if (v3 < 0)
        v3 = g.count - 1;
    int sel = (g.count != 0) ? (v3 % g.count) : v3;        // v12 = v3 % count
    result  = (g.count != 0) ? (v3 / g.count) : 0;         // result = v3 / count
    if (sel != g.selected || !s.focusLatch)                // v12 != selected || !dword_62D2FC
        result = Selection_Update(group, sel);             // VIBE_Selection_Update(a1, v12)

    if (s.lastKey == kKeyEnter) {                          // byte_67225C == 28
        int v14 = g.button[sel];                           // dword_67658C[35*a1 + v13]
        s.clickFlag = 1;                                   // dword_672228 = 1
        s.lastKey   = 0;                                    // byte_67225C = 0
        Widget& bw = g_widgets[v14];                        // dword_69FFB4 + 740*v14
        if (bw.type() == 9) {                               // *(result+24) == 9
            if (bw.radioFlag() & 0x10) {                    // *(result+444) & 0x10
                s.selectedWidget = v14;                     // dword_62D22C = v14
                s.hoverHelpId    = 1155;                    // dword_75BF38 = 1155
            } else {
                s.selectedWidget = v14;
                s.hoverHelpId    = 1210;                    // dword_75BF38 = 1210
            }
        } else {
            int payload = bw.id();                          // result = *(result+8)
            s.selectedWidget = v14;                         // dword_62D22C = v14
            s.hoverHelpId    = payload;                      // dword_75BF38 = result
            result = payload;
        }
    }
    return result;
}

} // namespace guild::gui
