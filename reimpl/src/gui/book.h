#pragma once
// guild::gui — the in-world book widget: page record + pagination/flip logic.
//
// VIBE_Book_Open @0x4be204 allocates a 0x274-byte book record and fills a per-page
// "form" array (one entry every 4 bytes starting at +552 / dword index 138), one per
// page, up to `pageCount` (the `a3` argument, stored as a byte at +625).  The current
// page byte lives at +624.  A book is displayed as a two-page SPREAD: the visible
// pages are `current` and `current+1`; turning flips by ±2.
//
// VIBE_Book_TurnPage    @0x4be60c   flip by ±delta with clamping + visibility update.
// VIBE_Book_RefreshVisiblePages @0x4be494  recompute which page forms are visible.
// VIBE_Book_TurnPageForward/Backward @0x4be8b8/0x4be8c8  thin ±2 wrappers.
//
// This module recovers the page RECORD layout and the pagination MATH (clamp bounds,
// visible-spread computation, flip step) byte-for-byte.  The mesh/animation/audio side
// effects (Buch_vorblaettern.baf etc.) and the per-page form rendering are routed
// through a command hook so the pagination logic is testable in isolation.

#include "guild/common/types.h"
#include <cstdint>
#include <vector>

namespace guild::gui {

using guild::i32;

// Book page record offsets (from VIBE_Book_Open / TurnPage; the live record is a
// 0x274-byte malloc'd blob).  Only the fields the pagination logic touches are named.
//   +0    (dword)  mesh/object handle (VIBE_Character_RunMeshCallback result)
//   +4    (dword)  attached-node handle (0 when no suspend state)
//   +552  (dword)  per-page form array base (entry stride 4; one form id per page)
//   +616  (dword)  "next page" button widget id (-1 == none)
//   +620  (dword)  "prev page" button widget id (-1 == none)
//   +624  (byte)   current page index
//   +625  (byte)   page count
//   +626  (byte)   saved z-enable flag (byte_64A351)
inline constexpr int kBookFlipStep = 2;  // a spread is two pages; flip moves by 2

// Logical book state (the bytes at +624/+625 plus the per-page form list at +552).
struct Book {
    std::uint8_t currentPage = 0;   // +624
    std::uint8_t pageCount = 0;     // +625
    std::vector<i32> pageForms;     // +552[k] : form id for page k (size == pageCount)
    i32 nextButtonId = -1;          // +616
    i32 prevButtonId = -1;          // +620
};

// gilde.exe 0x4be204 — VIBE_Book_Open: initialise a book with `pageCount` pages.
// Mirrors: current=0; pageCount=a3; for k<pageCount allocate a page form; pages > 2
// start hidden.  `makeForm` supplies the per-page form id (the renderer side); when
// null the form ids are left 0.  Returns false if pageCount is 0.
bool Book_Open(Book& book, int pageCount, i32 (*makeForm)(int page) = nullptr);

// gilde.exe 0x4be60c — VIBE_Book_TurnPage (clamp + flip).
// delta > 0 flips forward, delta < 0 flips backward.  The original guards:
//   backward: reject when (current - 2) < 0
//   forward : reject when (current + 2) >= pageCount
// On success current += delta and visibility is recomputed.  Returns true when the
// flip happened, false when it was clamped away (or the book is empty).
bool Book_TurnPage(Book& book, int delta);

// gilde.exe 0x4be8b8 / 0x4be8c8 — flip one spread forward / backward (delta = ±2).
inline bool Book_TurnPageForward(Book& book)  { return Book_TurnPage(book, kBookFlipStep); }
inline bool Book_TurnPageBackward(Book& book) { return Book_TurnPage(book, -kBookFlipStep); }

// gilde.exe 0x4be494 — which page indices are currently visible.  A book shows the
// spread {current, current+1}; pages outside that range are hidden.  Returns the (up
// to two) visible page indices, clamped to [0, pageCount).
std::vector<int> Book_VisiblePages(const Book& book);

// True when page `p` is part of the currently displayed spread.
bool Book_IsPageVisible(const Book& book, int p);

} // namespace guild::gui
