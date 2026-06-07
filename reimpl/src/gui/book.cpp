#include "gui/book.h"

namespace guild::gui {

bool Book_Open(Book& book, int pageCount, i32 (*makeForm)(int page)) {
    if (pageCount <= 0)
        return false;
    book.currentPage = 0;                    // v6[624] = 0
    book.pageCount = static_cast<std::uint8_t>(pageCount);  // v6[625] = a3
    book.nextButtonId = -1;                  // *((dword*)v6 + 154) = -1  (+616)
    book.prevButtonId = -1;                  // *((dword*)v6 + 155) = -1  (+620)
    book.pageForms.assign(static_cast<std::size_t>(pageCount), 0);
    for (int k = 0; k < pageCount; ++k)
        book.pageForms[k] = makeForm ? makeForm(k) : 0;
    // (Pages with index > 2 start hidden in the original; visibility is derived from
    //  currentPage via Book_VisiblePages, so nothing to store here.)
    return true;
}

bool Book_TurnPage(Book& book, int delta) {
    if (book.pageCount == 0)
        return false;
    const int current = book.currentPage;
    if (delta < 0) {
        // backward: reject when (current - 2) < 0
        if (current - kBookFlipStep < 0)
            return false;
    } else {
        // forward: reject when (current + 2) >= pageCount
        if (current + kBookFlipStep >= book.pageCount)
            return false;
    }
    book.currentPage = static_cast<std::uint8_t>(current + delta);  // *(+624) += delta
    return true;
}

std::vector<int> Book_VisiblePages(const Book& book) {
    // RefreshVisiblePages: page i is visible iff (i >= current && i <= current+1).
    std::vector<int> out;
    const int current = book.currentPage;
    for (int i = current; i <= current + 1; ++i)
        if (i >= 0 && i < book.pageCount)
            out.push_back(i);
    return out;
}

bool Book_IsPageVisible(const Book& book, int p) {
    const int current = book.currentPage;
    return p >= current && p <= current + 1 && p >= 0 && p < book.pageCount;
}

} // namespace guild::gui
