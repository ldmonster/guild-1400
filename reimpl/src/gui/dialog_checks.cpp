#include "gui/dialog_checks.h"

#include "gui/window.h"
#include "gui/object.h"

namespace guild::gui {

namespace {
DialogCheckSink  g_defaultSink;
DialogCheckSink* g_sink = &g_defaultSink;
} // namespace

void DialogChecks_SetSink(DialogCheckSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

// ===========================================================================
// Preflight checks.
// ===========================================================================

// gilde.exe 0x4ad594 — VIBE_Dialog_CheckSkillRequirement.
//   if (required <= skill || !required) return 1;
//   RenderFormattedMessage(buf, 116, required); ShowMessageBox(0, kind); return 0;
// (skill == *(a1+404) in the original; passed in directly here.)
int Dialog_CheckSkillRequirement(int skill, int required, char kind) {
    if (required <= skill || !required)
        return 1;
    g_sink->ShowMessage(kMsgSkillTooLow, 0, kind); // msg 116, flags 0
    return 0;
}

// gilde.exe 0x4ad5d4 — VIBE_Dialog_CheckActiveCharFlag.
//   c = Person_FindActiveByEntity(a1);
//   if (!c || !(c[218] & 1)) return 0;
//   RenderFormattedMessage(buf, 120, *c); ShowMessageBox(256, kind); return 1;
int Dialog_CheckActiveCharFlag(bool busy, int charId, char kind) {
    (void)charId; // the message argument (*c) — carried for fidelity; the sink omits args
    if (!busy)        // !c || (c[218] & 1) == 0
        return 0;
    g_sink->ShowMessage(kMsgCharBusy, 256, kind); // msg 120, flags 256
    return 1;
}

// gilde.exe 0x4ad62c — VIBE_Dialog_CheckResourceAmount.
//   if (CountAtLocation(*(a2+376)) >= needed || !needed) return 1;
//   RenderFormattedMessage(buf, 117, ...); ShowMessageBox(0, needed); return 0;
// (The original passes `needed` as the messagebox kind arg — preserved.)
int Dialog_CheckResourceAmount(int needed, int owned) {
    if (owned >= needed || !needed)
        return 1;
    g_sink->ShowMessage(kMsgNotEnoughGeneric, 0, static_cast<char>(needed)); // msg 117, flags 0
    return 0;
}

// gilde.exe 0x4ad678 — VIBE_Dialog_CheckResourceByItem.
//   if (CountAtLocation(loc) >= needed || !needed) return 1;
//   if (item && *item == 322)      RenderFormattedMessage(buf, 118, ...);
//   else if (item && *item == 277) RenderFormattedMessage(buf, 119, ...);
//   else                           RenderFormattedMessage(buf, 117, ...);
//   ShowMessageBox(4, (char)item); return 0;
int Dialog_CheckResourceByItem(int itemId, int needed, int owned, char kind) {
    if (owned >= needed || !needed)
        return 1;
    int msg;
    if (itemId == 322)
        msg = kMsgNotEnoughItem322;   // 118
    else if (itemId == 277)
        msg = kMsgNotEnoughItem277;   // 119
    else
        msg = kMsgNotEnoughGeneric;   // 117
    g_sink->ShowMessage(msg, 4, kind); // flags 4
    return 0;
}

// gilde.exe 0x4adea4 — VIBE_Dialog_ShowMessageBoxSimple (form selection half).
//   if (kind & 0x10) name = "misc\\Messagebox_BIG";
//   else if (kind & 0x20) name = "misc\\Messagebox_VERY_BIG";
//   else name = "misc\\Messagebox";
const char* Dialog_SimpleFormForFlags(char flags) {
    if (flags & 0x10)
        return "misc\\Messagebox_BIG";
    if (flags & 0x20)
        return "misc\\Messagebox_VERY_BIG";
    return "misc\\Messagebox";
}

// ===========================================================================
// Book content helpers.
// ===========================================================================

// gilde.exe 0x4be1d0 — VIBE_Book_HandlePageButton.
//   if (lastClick == 1753) return TurnPage(book, 2);
//   if (lastClick == 1754) return TurnPage(book, -2);
//   return book;   (no turn)
int Book_HandlePageButton(int lastClick) {
    if (lastClick == kBookBtnForward)
        return 2;
    if (lastClick == kBookBtnBack)
        return -2;
    return 0;
}

// gilde.exe 0x4be588 — VIBE_Book_SetPageText.
//   if (!book) return book;
//   if (firstPage > book[625]) return 0;
//   for (v4=0; v4 < book[625]; ++v4) {
//     if (v4 >= firstPage && v4 <= firstPage+1) body = book[552], pane = book+8+((v4&3)<<7);
//     else                                      body = book[552], pane = "" ;
//     Form_RefreshIfVisible(form, pane, "");
//   }
//   return 1;
// The visible/empty decision per page is the (v4 >= a2 && v4 <= a2+1) test; we expose it.
int Book_SetPageText(int firstPage, int pageCount, unsigned char* visible) {
    if (firstPage > pageCount)
        return 0;
    for (int v4 = 0; v4 < pageCount; ++v4) {
        bool shown = (v4 >= firstPage && v4 <= firstPage + 1);
        if (visible)
            visible[v4] = shown ? 1 : 0;
        // Form_RefreshIfVisible(book[552], pane, "") — the pop-out form refresh edge.
    }
    return 1;
}

// ===========================================================================
// Window helper.
// ===========================================================================

// gilde.exe 0x419ad8 — VIBE_Window_CreateScrollButtons.
//   v7 = &g_windows[win];
//   if (x==-1 && y==-1) { y = winH-32; x = (winW>>16)-32; }   // bottom-right default
//   down = Object_AddToWindow(win, y, x, gfx);    v7[235]=down;
//     widget[down][+476]=group;  widget[down][+444]=3;
//   up   = Object_AddToWindow(win, y, x-32, gfx+1); v7[234]=up;
//     widget[up][+476]=group;    widget[up][+444]=3;
//   dword_67EDC8[238*win]=0;  dword_67EDE4[238*group]=48;
//   return 896*group;
// (The original reads x/y from packed 16.16 geometry; here the Window record stores pixel
// x/y/w/h directly, so the >>16 / HIWORD reads become plain field reads.)
int Window_CreateScrollButtons(int x, int y, int group, int win, int gfx, ScrollButtonIds* out) {
    Window& w = g_windows[win];
    if (x == -1 && y == -1) {
        y = w.h() - 32;     // HIWORD(v7[2]) - 32  (window height)
        x = w.w() - 32;     // (v7+6 >> 16) - 32   (window width)
    }

    // The binary writes the +476/+444 fields UNCONDITIONALLY (no -1 guard): it indexes
    // dword_69FFB4 + 740*v8 with whatever Object_AddToWindow returned. 0x419b42-0x419b8c
    // (down) and 0x419ba8-0x419bf0 (up). Object_AddToWindow always returns a valid slot
    // here, so no guard is taken in practice.
    int down = Object_AddToWindow(win, static_cast<i16>(y), static_cast<i16>(x), gfx);
    w.at<i32>(940) = down;  // v7[235]
    g_widgets[down].at<i32>(476) = group; // +476 group link
    g_widgets[down].at<u8>(444)  = 3;     // +444 radio/scroll flag

    int up = Object_AddToWindow(win, static_cast<i16>(y), static_cast<i16>(x - 32), gfx + 1);
    w.at<i32>(936) = up;    // v7[234]
    g_widgets[up].at<i32>(476) = group;
    g_widgets[up].at<u8>(444)  = 3;

    // dword_67EDC8[238*win] = 0; dword_67EDE4[238*group] = 48 — scroll-state globals owned
    // by the window cluster (offsets +584/+656 inside the window record region); the slot
    // wiring above is what this helper owns.
    if (out) {
        out->down = down;
        out->up = up;
    }
    return 896 * group;
}

} // namespace guild::gui
