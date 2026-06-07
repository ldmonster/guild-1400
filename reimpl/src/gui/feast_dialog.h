#pragma once
// guild::gui — VIBE_FeastDialog_InviteGuests @0x549a54.  The "hold a feast" multi-screen
// flow (locations\wohnsitz\fest / fest2).
//
// A feast schedules a future event (GameTime_Advance +24h), then walks several screens:
//
//   SCREEN A — pick a MAIN COURSE (fest, slot 1):  RenderRichString(4997) header, then five
//     "$L%ia[%s]$A" food buttons (text id = 4998 + i, i in [0,5)) and one "$L%in[%s]" final
//     entry (i=5).  childObj[i] captured.  Click -> store choice -> next screen.
//
//   SCREEN B — pick a DRINK (fest, slot 1; only when v120 "has a wine cellar"):
//     RenderRichString(5004) header, five "$L%ia[%s]$A" drink buttons (5005 + i) + final
//     "$L%in[%s]" (5005+5).  Click -> store choice.  If no wine cellar, the drink is fixed
//     to index 5 (no screen).
//
//   SCREEN C — GUEST TABLE (fest, slots 1/2/3):  for up to 4 invited guests render
//     RenderRichString(4989, *guest) and capture a remove-object id; an "$ZZum Fest
//     einladen" add-button (4990) opens the office-overview to add a guest; a 2-button row
//     (Hud_BuildButtonRow: confirm 4991 / cancel 4992).  Group composition is scored via
//     AiMethod_EvalGroupComposition.  Confirm enqueues the feast (slot-reset kind 55 + a
//     delta packet per guest, message kind 54; rank-6/7 guests use kind 9, else 1).
//
//   SCREEN D — REVIEW (fest2): a read-only summary (5011 header, 5012 per guest, 5013 main
//     course label 4998+course, 5014 drink label 5005+drink when wine cellar, 5015 score).
//
// We recover the form names, the per-screen window slots + header/button text bases (4997,
// 4998, 5004, 5005, 4988/4989/4990/4991/4992, 5011..5015), the food/drink button-label
// markup strings, the guest cap (4), the add-guest office-overview header string, the
// command kinds (slot-reset 55, message 54, delta field per guest), and the screen
// transitions / button wiring.  The frame loop, AI scorer, text engine and command codec
// are forward-declared / mocked.

#include "gui/types.h"

namespace guild::gui {

inline constexpr int kFeastClickOK     = 1210;
inline constexpr int kFeastClickCancel = 1155;
inline constexpr int kFeastLoopForm    = 423879;

inline constexpr const char* kFormFeast  = "locations\\wohnsitz\\fest";
inline constexpr const char* kFormFeast2 = "locations\\wohnsitz\\fest2";

// Button-label markup strings (RenderRichString format args).
inline constexpr const char* kFeastBtnMarkup    = "$L%ia[%s]$A"; // a selectable food/drink row
inline constexpr const char* kFeastBtnLastMark  = "$L%in[%s]";   // the final (i==5) entry
inline constexpr const char* kFeastAddGuestHdr  = "$ZZum Fest einladen"; // office-overview header

// Screen-A (main course) text bases.
inline constexpr int kTextFeastCourseHdr  = 4997; // header
inline constexpr int kTextFeastCourseBase = 4998; // 4998 + i food label

// Screen-B (drink) text bases.
inline constexpr int kTextFeastDrinkHdr   = 5004; // header
inline constexpr int kTextFeastDrinkBase  = 5005; // 5005 + i drink label

// Screen-C (table) text ids.
inline constexpr int kTextFeastTableHdr   = 4988; // table header
inline constexpr int kTextFeastGuestRow   = 4989; // per-guest remove row
inline constexpr int kTextFeastAddBtn     = 4990; // add-guest button
inline constexpr int kTextFeastConfirmBtn = 4991; // confirm button label
inline constexpr int kTextFeastCancelBtn  = 4992; // cancel button label

// Screen-D (review, fest2) text ids.
inline constexpr int kTextFeastReviewHdr   = 5011;
inline constexpr int kTextFeastReviewGuest = 5012;
inline constexpr int kTextFeastReviewCourse = 5013; // 4998 + course base offset
inline constexpr int kTextFeastReviewDrink  = 5014; // 5005 + drink base offset
inline constexpr int kTextFeastReviewScore  = 5015;

inline constexpr int kFeastMaxGuests   = 4;  // up to 4 invited guests
inline constexpr int kFeastChoiceCount = 6;  // 5 selectable + 1 final entry per menu
inline constexpr int kFeastNoChoice    = 5;  // index 5 = "none"/final

// Command kinds.
inline constexpr int kFeastResetKind     = 55; // QueueRequestSlotReset28 (start the feast)
inline constexpr int kFeastGuestMsgKind  = 54; // per-guest message slot-reset
inline constexpr int kFeastGuestMsgRank67 = 9; // message kind for rank-6/7 guests
inline constexpr int kFeastGuestMsgOther  = 1; // message kind for other guests

// ---------------------------------------------------------------------------
// Synthetic feast state.
// ---------------------------------------------------------------------------
struct FeastState {
    int self = 0;            // active char entity id
    int building = 0;        // the feast venue building handle
    bool hasWineCellar = false; // v120 — gates the drink screen
    int  course = kFeastNoChoice; // chosen main course index (0..5)
    int  drink  = kFeastNoChoice; // chosen drink index (0..5)
    int  guestEntities[kFeastMaxGuests] = {0, 0, 0, 0}; // invited guests (0 = empty)
    int  guestRanks[kFeastMaxGuests]    = {0, 0, 0, 0}; // *(byte*)guest+2
};

// The widget set one menu screen builds (course or drink).
struct FeastMenuLayout {
    const char* form = nullptr;
    int header = 0;          // header text id
    int childIds[kFeastChoiceCount]{}; // the 6 button object ids
    int base = 0;            // label text base (4998 / 5005)
};

// The guest-table screen layout.
struct FeastTableLayout {
    const char* form = nullptr;
    int header = kTextFeastTableHdr;
    int guestCount = 0;                 // current invited count
    int removeObj[kFeastMaxGuests];     // per-guest remove-row object (-1 if empty)
    int addObj = -1;                    // "$ZZum Fest einladen" add-button object
    int confirmObj = -1;                // confirm button (BuildButtonRow[0])
    int cancelObj  = -1;                // cancel button (BuildButtonRow[1])
    bool addDisabled = false;           // table full (4 guests) -> add disabled
    bool confirmDisabled = false;       // no guests -> confirm disabled
};

struct FeastCommandSink {
    virtual ~FeastCommandSink() = default;
    // Start the feast: a slot-reset (kind 55) carrying the course/drink + a per-guest
    // delta (message kind 9 for rank-6/7 guests, else 1).
    virtual void HoldFeast(int /*building*/, int /*course*/, int /*drink*/,
                           int /*guestCount*/) {}
    // A single guest invite delta (kind 54) appended during HoldFeast.
    virtual void InviteGuest(int /*guest*/, int /*msgKind*/) {}
};
void FeastDialog_SetCommandSink(FeastCommandSink* sink);

// gilde.exe 0x549a54 (course-menu half) — build the main-course screen.
FeastMenuLayout FeastDialog_BuildCourseMenu();
// gilde.exe 0x549a54 (drink-menu half) — build the drink screen (only when hasWineCellar).
FeastMenuLayout FeastDialog_BuildDrinkMenu(const FeastState& s);

// gilde.exe 0x549a54 (table half) — build the guest-table screen for the current guests.
FeastTableLayout FeastDialog_BuildTable(const FeastState& s);

// Menu wiring — returns the chosen index for a clicked menu object (or -1).
int FeastDialog_DispatchMenu(const FeastMenuLayout& l, int clickedObj);

// Table wiring — interpret a click on the guest-table screen.  Returns:
//   >=0  : a guest-remove was clicked (the slot index removed);
//   -2   : the add-guest button (caller opens the office overview);
//   -3   : confirm (feast dispatched via the sink);
//   -4   : cancel;
//   -1   : no-op.
int FeastDialog_DispatchTable(const FeastTableLayout& l, const FeastState& s,
                              int clickedObj);

} // namespace guild::gui
