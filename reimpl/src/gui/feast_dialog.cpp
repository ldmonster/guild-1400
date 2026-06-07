#include "gui/feast_dialog.h"

namespace guild::gui {

namespace {

FeastCommandSink  g_defaultSink;
FeastCommandSink* g_sink = &g_defaultSink;

// Stable child-object id bases per screen (the originals fetch these from
// VIBE_Form_GetChildObjectId; only relative identity matters for the wiring).
constexpr int kCourseObjBase = 2300; // 6 course buttons
constexpr int kDrinkObjBase  = 2310; // 6 drink buttons
constexpr int kRemoveObjBase = 2320; // up to 4 guest-remove rows
constexpr int kAddObj        = 2330;
constexpr int kConfirmObj    = 2331;
constexpr int kCancelObj     = 2332;

} // namespace

void FeastDialog_SetCommandSink(FeastCommandSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

// gilde.exe 0x549a54 (course-menu half).
//   SelectWindow(fest,1); RenderRichString(4997);
//   do { RenderRichString("$L%ia[%s]$A", 4998+i, name); childObj[i] = GetChildObjectId; }
//     while (++i < 5);
//   RenderRichString("$L%in[%s]", 4998+5, name); childObj[5] = GetChildObjectId;
FeastMenuLayout FeastDialog_BuildCourseMenu() {
    FeastMenuLayout l{};
    l.form = kFormFeast;
    l.header = kTextFeastCourseHdr;
    l.base = kTextFeastCourseBase;
    for (int i = 0; i < kFeastChoiceCount; ++i)
        l.childIds[i] = kCourseObjBase + i;
    return l;
}

// gilde.exe 0x549a54 (drink-menu half) — only entered when v120 (wine cellar).
//   SelectWindow(fest,1); RenderRichString(5004); five "$L%ia[%s]$A" (5005+i) + final.
FeastMenuLayout FeastDialog_BuildDrinkMenu(const FeastState& s) {
    FeastMenuLayout l{};
    if (!s.hasWineCellar)
        return l; // no drink screen; drink is fixed to index 5 by the caller
    l.form = kFormFeast;
    l.header = kTextFeastDrinkHdr;
    l.base = kTextFeastDrinkBase;
    for (int i = 0; i < kFeastChoiceCount; ++i)
        l.childIds[i] = kDrinkObjBase + i;
    return l;
}

// gilde.exe 0x549a54 (table half).
//   SelectWindow(fest,1); RenderRichString(4988); for i in [0,4): if guest[i]
//     RenderRichString(4989,*guest); removeObj[i] = GetChildObjectId; else removeObj[i]=-1.
//   SelectWindow(fest,2); RenderRichString(4990) -> addObj; if 4 guests SetEnabled(add,0).
//   SelectWindow(fest,3); BuildButtonRow(confirm 4991 / cancel 4992); if 0 guests
//     SetEnabled(confirm,0).
FeastTableLayout FeastDialog_BuildTable(const FeastState& s) {
    FeastTableLayout l{};
    l.form = kFormFeast;
    int count = 0;
    for (int i = 0; i < kFeastMaxGuests; ++i) {
        if (s.guestEntities[i] != 0) {
            l.removeObj[i] = kRemoveObjBase + i;
            ++count;
        } else {
            l.removeObj[i] = -1;
        }
    }
    l.guestCount = count;
    l.addObj = kAddObj;
    l.confirmObj = kConfirmObj;
    l.cancelObj = kCancelObj;
    l.addDisabled = (count == kFeastMaxGuests);   // table full
    l.confirmDisabled = (count == 0);             // nothing to confirm
    return l;
}

// gilde.exe 0x549a54 — menu click resolution.
int FeastDialog_DispatchMenu(const FeastMenuLayout& l, int clickedObj) {
    if (!l.form)
        return -1;
    for (int i = 0; i < kFeastChoiceCount; ++i) {
        if (l.childIds[i] == clickedObj)
            return i;
    }
    return -1;
}

// gilde.exe 0x549a54 (table wiring).
//   if (dword_62D22C == addObj)     -> open office overview (return -2).
//   else if (dword_62D22C == confirmObj) -> dispatch the feast (return -3).
//   else if (dword_62D22C == cancelObj)  -> cancel (return -4).
//   else (matches a removeObj[i]) -> remove guest i (return i).
int FeastDialog_DispatchTable(const FeastTableLayout& l, const FeastState& s,
                              int clickedObj) {
    if (clickedObj == l.addObj) {
        if (l.addDisabled) return -1;
        return -2;
    }
    if (clickedObj == l.confirmObj) {
        if (l.confirmDisabled) return -1;
        // Dispatch the feast: a slot-reset (kind 55) plus a per-guest invite delta.
        g_sink->HoldFeast(s.building, s.course, s.drink, l.guestCount);
        for (int i = 0; i < kFeastMaxGuests; ++i) {
            if (s.guestEntities[i] != 0) {
                int rank = s.guestRanks[i];
                int kind = (rank == 6 || rank == 7) ? kFeastGuestMsgRank67
                                                    : kFeastGuestMsgOther;
                g_sink->InviteGuest(s.guestEntities[i], kind);
            }
        }
        return -3;
    }
    if (clickedObj == l.cancelObj)
        return -4;
    for (int i = 0; i < kFeastMaxGuests; ++i) {
        if (l.removeObj[i] != -1 && l.removeObj[i] == clickedObj)
            return i;
    }
    return -1;
}

} // namespace guild::gui
