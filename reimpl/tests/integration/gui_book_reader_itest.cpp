// Integration tests: the book reader dispatch driven against the REAL sibling modules
// it shares state with — the logical pagination model in gui/book.cpp (Book_TurnPage /
// Book_VisiblePages) and the REAL last-clicked-widget global g_lastClickedId in
// gui/input.cpp (dword_75BF38). This verifies the page-button dispatch reproduces the
// original's "read dword_75BF38, flip the same record the pagination math clamps" wiring
// without a stand-in for either side.
#include "gui/book_reader.h"
#include "gui/book.h"
#include "gui/dialog_checks.h"   // REAL Book_HandlePageButton sibling (0x4be1d0)
#include "gui/input.h"
#include "tests/framework/test.h"

#include <vector>

using namespace guild::gui;

namespace {
// Apply the REAL Book_HandlePageButton id->delta map and the REAL Book_TurnPage clamp.
// Returns the page delta actually applied (0 when clamped or the id was not an arrow).
int FlipArrow(Book& book, int clickId) {
    int delta = Book_HandlePageButton(clickId);   // REAL dialog_checks: 1753->+2, 1754->-2
    if (delta != 0 && Book_TurnPage(book, delta))  // REAL book.cpp clamp+flip
        return delta;
    return 0;
}
} // namespace

// The page arrow writes its id into the REAL g_lastClickedId global (as the click router
// in gui/input.cpp does); the REAL Book_HandlePageButton (gui/dialog_checks.cpp) maps that
// exact value to a delta, and the REAL Book_TurnPage (gui/book.cpp) clamps + flips it.
TEST(GuiBookReaderItest, ArrowGlobalDrivesRealPagination) {
    ResetInputState();                  // real gui/input.cpp state reset
    Book book; book.pageCount = 10; book.currentPage = 0;

    // Forward arrow clicked -> dword_75BF38 == 1753.
    g_lastClickedId = kBookBtnForward;  // 1753, from gui/dialog_checks.h
    CHECK_EQ(FlipArrow(book, g_lastClickedId), 2);
    CHECK_EQ((int)book.currentPage, 2);

    // ...again.
    CHECK_EQ(FlipArrow(book, g_lastClickedId), 2);
    CHECK_EQ((int)book.currentPage, 4);

    // Backward arrow clicked -> dword_75BF38 == 1754.
    g_lastClickedId = kBookBtnBack;     // 1754
    CHECK_EQ(FlipArrow(book, g_lastClickedId), -2);
    CHECK_EQ((int)book.currentPage, 2);

    // Real clamp at the start: another two backward flips hit the floor.
    CHECK_EQ(FlipArrow(book, g_lastClickedId), -2);  // 2 -> 0
    CHECK_EQ((int)book.currentPage, 0);
    CHECK_EQ(FlipArrow(book, g_lastClickedId), 0);   // 0 -> clamped away
    CHECK_EQ((int)book.currentPage, 0);
}

// The visible-spread the reader hides against is exactly the spread the REAL
// Book_VisiblePages model reports for the same current page.
TEST(GuiBookReaderItest, HiddenSetIsComplementOfRealVisibleSpread) {
    BookRecord rec;
    rec.pageCount = 8;
    rec.currentPage = 4;
    rec.pageForms.resize(8);
    for (int i = 0; i < 8; ++i) rec.pageForms[(std::size_t)i] = 200 + i;

    // The real model's visible spread for current=4 is {4,5}.
    Book model; model.pageCount = 8; model.currentPage = 4;
    std::vector<int> visible = Book_VisiblePages(model);
    CHECK_EQ((int)visible.size(), 2);
    CHECK(visible[0] == 4 && visible[1] == 5);

    struct CapHost : BookHost {
        std::vector<int> hiddenIdx;
        std::vector<i32>* forms = nullptr;
        void SetPageVisible(i32 form, int v) override {
            if (v == 0)
                for (std::size_t i = 0; i < forms->size(); ++i)
                    if ((*forms)[i] == form) hiddenIdx.push_back((int)i);
        }
    } host;
    host.forms = &rec.pageForms;
    Book_RefreshVisiblePages(rec, host, -1, 0);

    // Every index NOT in the real visible spread {4,5} is hidden.
    for (int idx : host.hiddenIdx)
        CHECK(idx != 4 && idx != 5);
    CHECK_EQ((int)host.hiddenIdx.size(), 6); // 0,1,2,3,6,7
}

// The cursor-zone interaction hook flips the SAME real Book model the page arrows do.
TEST(GuiBookReaderItest, CursorZoneFlipsRealModel) {
    Book book; book.pageCount = 6; book.currentPage = 0;

    Interaction_HandleBookPageTurn(book, 2, true);  // forward zone
    CHECK_EQ((int)book.currentPage, 2);
    Interaction_HandleBookPageTurn(book, 2, true);
    CHECK_EQ((int)book.currentPage, 4);             // 4+2 == 6 >= 6 next is clamped
    Interaction_HandleBookPageTurn(book, 2, true);
    CHECK_EQ((int)book.currentPage, 4);             // clamped
    Interaction_HandleBookPageTurn(book, 3, true);  // backward zone
    CHECK_EQ((int)book.currentPage, 2);
}
