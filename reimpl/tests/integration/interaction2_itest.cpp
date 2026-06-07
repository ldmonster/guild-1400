// Integration test: the interaction2 book-page-turn handler wired against the
// REAL reconstructed book-pagination sibling (guild::gui::Book_TurnPageForward /
// Book_TurnPageBackward in gui/book.cpp — NOT a mock). This is exactly the live
// wiring: VIBE_Interaction_HandleBookPageTurn (0x596ee8) reads the panel's
// bookMode (index 15) and bookObj (index 19) and dispatches the page flip through
// the Interaction2Hooks.bookTurnForward / bookTurnBackward leaves; production binds
// those to VIBE_Book_TurnPageForward/Backward (0x4be8b8/0x4be8c8). We forward them
// into the genuine Book_TurnPage clamp/visibility math and assert the cross-module
// flow: a panel page-turn event advances the real book spread and respects the
// real clamp bounds at the ends of the book.
#include "test.h"

#include "sim/interaction2.h"
#include "gui/book.h"      // REAL pagination sibling: Book / Book_TurnPage*

using namespace guild;
using guild::sim::g_i2;
using guild::sim::g_i2Hooks;
using guild::gui::Book;

namespace {

// The single live book the interaction panel is reading. The hooks are plain C
// function pointers (no captures), so they forward into this one global Book —
// the genuine reconstructed pagination sibling, exercised end to end.
Book g_liveBook;
bool g_realFlipReturn = false;   // last return of the REAL Book_TurnPage*
bool g_forwardHookFired = false; // proves whether the leaf was reached at all

// Production binds these to VIBE_Book_TurnPageForward/Backward; the bookObj arg
// mirrors *((dword*)panel+19). We assert the leaf is reached and forward the flip
// into the REAL sibling on the live book.
void RealBookForward(int bookObj) {
    g_forwardHookFired = true;
    if (bookObj)                 // mirrors the original's non-null book guard
        g_realFlipReturn = guild::gui::Book_TurnPageForward(g_liveBook);
}
void RealBookBackward(int bookObj) {
    if (bookObj)
        g_realFlipReturn = guild::gui::Book_TurnPageBackward(g_liveBook);
}

void WireRealBookHooks() {
    guild::sim::ResetInteraction2State();
    guild::sim::ResetInteraction2Hooks();
    g_i2Hooks.bookTurnForward = &RealBookForward;
    g_i2Hooks.bookTurnBackward = &RealBookBackward;
    g_realFlipReturn = false;
    g_forwardHookFired = false;
}

// Build a panel reading the live book in the given cursor zone (bookMode). The
// bookObj need only be non-zero to dispatch; we use a stable sentinel address.
guild::sim::PanelObject MakeBookPanel(int bookMode) {
    guild::sim::PanelObject p;
    p.bookMode = bookMode;       // index 15 : 2 == forward, 3 == backward
    p.bookObj = 0x1000;          // index 19 : non-null "open book" handle
    return p;
}

} // namespace

// A forward page-turn event drives the REAL Book_TurnPage forward by one spread.
TEST(Interaction2Itest, ForwardEventAdvancesRealBookSpread) {
    WireRealBookHooks();
    g_liveBook = Book{};
    CHECK(guild::gui::Book_Open(g_liveBook, /*pageCount=*/6));
    CHECK_EQ((int)g_liveBook.currentPage, 0);

    guild::sim::PanelObject panel = MakeBookPanel(/*bookMode=*/2);
    g_i2.panel = &panel;

    // HandleBookPageTurn always returns 0 (the original return), but must have
    // routed into the REAL sibling and flipped the book by the spread step (2).
    CHECK_EQ(guild::sim::HandleBookPageTurn(), 0);
    CHECK(g_forwardHookFired);                     // the leaf was reached
    CHECK(g_realFlipReturn);                        // real flip succeeded
    CHECK_EQ((int)g_liveBook.currentPage, 2);      // real clamp math advanced +2
}

// A backward event flips the REAL book back by one spread.
TEST(Interaction2Itest, BackwardEventRewindsRealBookSpread) {
    WireRealBookHooks();
    g_liveBook = Book{};
    CHECK(guild::gui::Book_Open(g_liveBook, /*pageCount=*/6));
    // Advance two spreads first so there is room to go back.
    CHECK(guild::gui::Book_TurnPageForward(g_liveBook));
    CHECK(guild::gui::Book_TurnPageForward(g_liveBook));
    CHECK_EQ((int)g_liveBook.currentPage, 4);

    guild::sim::PanelObject panel = MakeBookPanel(/*bookMode=*/3);
    g_i2.panel = &panel;

    CHECK_EQ(guild::sim::HandleBookPageTurn(), 0);
    CHECK(g_realFlipReturn);
    CHECK_EQ((int)g_liveBook.currentPage, 2);      // real backward clamp: -2
}

// The REAL book clamp gates the page-turn at the end of the book: a forward
// event on the last spread leaves the real currentPage unchanged.
TEST(Interaction2Itest, ForwardEventClampedAtRealBookEnd) {
    WireRealBookHooks();
    g_liveBook = Book{};
    CHECK(guild::gui::Book_Open(g_liveBook, /*pageCount=*/4));
    // Move to the last legal spread (current+2 >= pageCount rejects further).
    CHECK(guild::gui::Book_TurnPageForward(g_liveBook));
    CHECK_EQ((int)g_liveBook.currentPage, 2);

    guild::sim::PanelObject panel = MakeBookPanel(/*bookMode=*/2);
    g_i2.panel = &panel;
    g_realFlipReturn = true;   // seed opposite of expected

    CHECK_EQ(guild::sim::HandleBookPageTurn(), 0);
    CHECK(g_forwardHookFired);                   // leaf still reached
    CHECK(!g_realFlipReturn);                    // real clamp rejected the flip
    CHECK_EQ((int)g_liveBook.currentPage, 2);    // unchanged
}

// bookObj == 0 (no open book) must NOT route into the real sibling at all: the
// interaction handler's bookObj guard short-circuits before the hook fires.
TEST(Interaction2Itest, NoBookObjectSkipsRealSibling) {
    WireRealBookHooks();
    g_liveBook = Book{};
    CHECK(guild::gui::Book_Open(g_liveBook, /*pageCount=*/6));

    guild::sim::PanelObject panel;
    panel.bookMode = 2;
    panel.bookObj = 0;                  // no open book
    g_i2.panel = &panel;

    CHECK_EQ(guild::sim::HandleBookPageTurn(), 0);
    CHECK(!g_forwardHookFired);         // hook never reached
    CHECK_EQ((int)g_liveBook.currentPage, 0);  // real book untouched
}
