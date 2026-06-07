#pragma once
// guild::gui — dialog preflight checks, the simple messagebox variant, and two small
// Book / Window content helpers not yet translated.
//
// The "Dialog_Check*" family are the gate predicates the action dialogs call before they
// build: each verifies a precondition (skill level, active-character busy flag, resource
// amount) and, when it fails, renders a localized error message and shows a modal
// messagebox, returning 0; on success it returns 1.  The text-render + messagebox edges
// (VIBE_Text_RenderFormattedMessage / VIBE_Dialog_ShowMessageBox) are routed through a
// mockable sink so the pure check logic (the comparison + the chosen message id) is
// testable headlessly.
//
//   VIBE_Dialog_CheckSkillRequirement  @0x4ad594  (skill level >= required)
//   VIBE_Dialog_CheckActiveCharFlag    @0x4ad5d4  (active char not flagged busy)
//   VIBE_Dialog_CheckResourceAmount    @0x4ad62c  (own >= needed)
//   VIBE_Dialog_CheckResourceByItem    @0x4ad678  (own >= needed; item picks the message)
//   VIBE_Dialog_ShowMessageBoxSimple   @0x4adea4  (form selection by kind flags)
//   VIBE_Book_HandlePageButton         @0x4be1d0  (page +/- by last-clicked widget id)
//   VIBE_Book_SetPageText              @0x4be588  (refresh the two visible page text panes)
//   VIBE_Window_CreateScrollButtons    @0x419ad8  (up/down scrollbar button pair)

#include "gui/types.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// Sink for the messagebox + formatted-text edges.  The default sink records the last
// shown (messageId, kind) pair for testing.
// ---------------------------------------------------------------------------
struct DialogCheckSink {
    virtual ~DialogCheckSink() = default;
    // VIBE_Text_RenderFormattedMessage(buf, msgId, arg) then VIBE_Dialog_ShowMessageBox(flags,kind)
    virtual void ShowMessage(int /*msgId*/, int /*flags*/, char /*kind*/) {}
};
void DialogChecks_SetSink(DialogCheckSink* sink);

// Localized message ids the checks raise (recovered from the decompiles).
inline constexpr int kMsgSkillTooLow      = 116;
inline constexpr int kMsgCharBusy         = 120;
inline constexpr int kMsgNotEnoughGeneric = 117;
inline constexpr int kMsgNotEnoughItem322 = 118;
inline constexpr int kMsgNotEnoughItem277 = 119;

// gilde.exe 0x4ad594 — VIBE_Dialog_CheckSkillRequirement (skill@eax, required@edx, kind@sil)
// Returns 1 when required == 0 or skill >= required.  Otherwise raises message 116 with a
// plain messagebox (flags 0) and returns 0.
int Dialog_CheckSkillRequirement(int skill, int required, char kind);

// gilde.exe 0x4ad5d4 — VIBE_Dialog_CheckActiveCharFlag (busyBit, charId, kind)
// Returns 1 (and raises message 120 with flags 256) when the resolved active character has
// its busy bit (record[218] & 1) set; otherwise 0.  `busyBit`/`charId` model the resolved
// character: busy != 0 means the flag is set, `charId` is the message argument.
int Dialog_CheckActiveCharFlag(bool busy, int charId, char kind);

// gilde.exe 0x4ad62c — VIBE_Dialog_CheckResourceAmount (needed@edx, owned@eax)
// Returns 1 when needed == 0 or owned >= needed; otherwise raises message 117 (flags 0)
// and returns 0.  (`owned` is GameObject_CountAtLocation(resource); passed directly here.)
int Dialog_CheckResourceAmount(int needed, int owned);

// gilde.exe 0x4ad678 — VIBE_Dialog_CheckResourceByItem (itemId, needed, owned, kind)
// Same comparison; on failure the message id is chosen by `itemId`: 322 -> 118, 277 -> 119,
// else 117; the messagebox is shown with flags 4.  itemId == 0 selects the generic 117.
int Dialog_CheckResourceByItem(int itemId, int needed, int owned, char kind);

// gilde.exe 0x4adea4 — VIBE_Dialog_ShowMessageBoxSimple (kind flags).
// Selects the .form name by the kind flags: 0x10 -> Messagebox_BIG, 0x20 ->
// Messagebox_VERY_BIG, else Messagebox.  Returns the (mock) loaded form id.
const char* Dialog_SimpleFormForFlags(char flags);

// ---------------------------------------------------------------------------
// Book content helpers.
// ---------------------------------------------------------------------------

// gilde.exe 0x4be1d0 — VIBE_Book_HandlePageButton (book@eax)
// Maps the last-clicked widget id (lastClick) to a page turn: 1753 -> +2 (turn forward),
// 1754 -> -2 (turn back); any other id is a no-op.  Returns the page-turn delta applied
// (the original returns the TurnPage result; here we return the delta, 0 if no turn).
inline constexpr int kBookBtnForward = 1753;
inline constexpr int kBookBtnBack    = 1754;
int Book_HandlePageButton(int lastClick);

// gilde.exe 0x4be588 — VIBE_Book_SetPageText (book@eax, firstPage@dl)
// Refreshes the book's page text panes: for each of `pageCount` (record +625) pages, the
// two visible pages [firstPage, firstPage+1] get the book body text, the rest get the
// empty string.  Returns 0 when firstPage > pageCount, else 1.  We model the per-page
// "is visible" decision (the byte-for-byte (v4 >= a2 && v4 <= a2+1) test) into `visible[]`.
// pageCount is the number of pages; `visible` (size pageCount) receives 1 for the two
// shown pages, 0 otherwise.  Returns 1 on success, 0 when firstPage out of range.
int Book_SetPageText(int firstPage, int pageCount, unsigned char* visible);

// ---------------------------------------------------------------------------
// Window helper.
// ---------------------------------------------------------------------------

// gilde.exe 0x419ad8 — VIBE_Window_CreateScrollButtons (x@eax, y@edx, group@ecx, win@ebx, gfx)
// Adds the up/down scrollbar button pair to window `win`: when (x,y)==(-1,-1) they default
// to the window's bottom-right (winW-32, winH-32); the down button gets gfx, the up button
// gfx+1, both with the +444 radio flag = 3 and a +476 group link.  Returns 896*group
// (preserved verbatim).  The two created widget ids are returned via out-params.
struct ScrollButtonIds { int down; int up; };
int Window_CreateScrollButtons(int x, int y, int group, int win, int gfx, ScrollButtonIds* out);

} // namespace guild::gui
