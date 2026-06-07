#pragma once
// guild::gui — the in-world BOOK reader (raw record) + the parchment SCROLL reader.
//
// This module is the faithful reconstruction of the engine-facing half of the book
// reader cluster at gilde.exe 0x4be1d0..0x4be990, complementing the pure pagination
// MODEL in gui/book.h (Book / Book_Open / Book_TurnPage / Book_VisiblePages). Where
// book.h works on a tidy logical Book, this module works on the real 0x274-byte malloc'd
// record (BookRecord) and the page-form array at +552, mirroring the original control
// flow byte-for-byte (button-id dispatch, two-page-spread visibility, per-page text
// refresh, the "buch\Buch_*.baf" animation routing, the page-turn audio sample).
//
// Functions translated here:
//   VIBE_Book_Close               @0x4be3d0  tear down the record + buttons + audio
//   VIBE_Book_RefreshVisiblePages @0x4be494  recompute visible-spread + auto page-flip
//   VIBE_Interaction_HandleBookPageTurn @0x596ee8  cursor-zone -> forward/backward flip
//   VIBE_Scroll_Open              @0x4be8d8  open the misc\scroll_perga parchment form
//   VIBE_Scroll_Close             @0x4be960  destroy the scroll form (reset its globals)
//   VIBE_Scroll_UpdateAnimation   @0x4be990  drive the 12-frame curl animation
//
// Two sibling functions in this same cluster are ALREADY translated and are REUSED, not
// redefined: VIBE_Book_HandlePageButton @0x4be1d0 and VIBE_Book_SetPageText @0x4be588
// live in gui/dialog_checks.{h,cpp} (Book_HandlePageButton / Book_SetPageText). The pure
// TurnPage clamp/visibility MATH + the +/-2 forward/backward wrappers live in gui/book.h
// and are REUSED (Book_TurnPage on the logical model). Engine leaves (mesh callbacks,
// form create/destroy/refresh, animation load, audio) are routed through BookHost /
// ScrollReaderHost so the dispatch + bookkeeping logic is testable with no Win32.

#include "gui/types.h"
#include "gui/book.h"   // the logical Book model (Book / Book_TurnPage) — reused here

#include <cstdint>
#include <string>
#include <vector>

namespace guild::gui {

using guild::i32;

// ---------------------------------------------------------------------------
// Recovered constants.
// ---------------------------------------------------------------------------
// The page-arrow widget ids (dword_75BF38 == 1753 forward / 1754 backward) are declared
// once in gui/dialog_checks.h as kBookBtnForward / kBookBtnBack and reused from there.

inline constexpr int kBookSpreadFlip = 2;       // a flip moves the cursor by two pages

// The four "Buch_*" asset strings the page-turn / open paths reference (byte-for-byte).
inline constexpr const char* kBookAnimForward   = "buch\\Buch_vorblaettern.baf";     // aBuchBuchVorbla @0x61e27c
inline constexpr const char* kBookAnimBackward  = "buch\\Buch_zurueckblaettern.baf"; // aBuchBuchZuruec @0x61e298
inline constexpr const char* kBookPageTopFmt    = "Buch_Seite%i_oben_ns_nm";         // aBuchSeiteIOben @0x61e22c
inline constexpr const char* kBookPageBottomFmt = "Buch_Seite%i_unten_ns_nm";        // aBuchSeiteIUnte @0x61e244

// The parchment-scroll form asset (misc\scroll_perga..; the path is truncated at the
// NUL in the binary so the engine appends the real extension) — aMiscScrollPerg @0x61e2c8.
inline constexpr const char* kScrollFormAsset = "misc\\scroll_perga";
inline constexpr int kScrollSpriteGfx = 1618;  // VIBE_Object_AddToWindow gfx id (both curls)
inline constexpr int kScrollRightX    = 465;   // x of the second (right) curl sprite

// Scroll curl animation: frame = (timer / 6) % 12, clamped to >= 0.
inline constexpr int kScrollAnimDivisor = 6;
inline constexpr int kScrollAnimFrames  = 12;

// The page-turn click sound (VIBE_Audio_StartVoiceSample(dword_63C754, 63, 1, ...)).
inline constexpr int kBookPageSoundChannel = 63;

// ---------------------------------------------------------------------------
// BookRecord — the live 0x274-byte book record (the malloc in VIBE_Book_Open). Only
// the fields the reader logic touches are named; offsets are byte offsets into the blob.
// ---------------------------------------------------------------------------
struct BookRecord {
    i32 meshHandle  = 0;            // +0    object/mesh handle (RunMeshCallback result)
    i32 attachNode  = 0;            // +4    attached-node handle (0 == no suspend state)
    std::vector<i32> pageForms;     // +552[k]  per-page form id (stride 4; size==pageCount)
    i32 nextButtonId = -1;          // +616  ((dword*)+154) next-page button widget id
    i32 prevButtonId = -1;          // +620  ((dword*)+155) prev-page button widget id
    std::uint8_t currentPage = 0;   // +624  current page index
    std::uint8_t pageCount   = 0;   // +625  page count
    std::uint8_t savedZEnable = 0;  // +626  saved z-enable flag (byte_64A351)
};

// ---------------------------------------------------------------------------
// Engine edges for the book reader (mesh / form / animation / audio leaves).
// ---------------------------------------------------------------------------
struct BookHost {
    virtual ~BookHost() = default;

    // VIBE_Form_SetChildrenVisible(form, visible) — show/hide a page form's children.
    virtual void SetPageVisible(i32 form, int visible) { (void)form; (void)visible; }
    // VIBE_Form_RefreshIfVisible(form, top, bottom) — refresh a page form's two text
    // surfaces with the named animation strings ("" == clear).
    virtual void RefreshPageText(i32 form, const std::string& top, const std::string& bottom) {
        (void)form; (void)top; (void)bottom;
    }
    // VIBE_Character_LoadObjectAnimation(mesh, anim, 18) — play the flip animation.
    virtual void PlayFlipAnimation(i32 mesh, const std::string& anim) { (void)mesh; (void)anim; }
    // VIBE_Audio_StartVoiceSample(dword_63C754, 63, 1, ...) — the page-turn click.
    virtual void PlayPageSound() {}
    // VIBE_Form_BroadcastClickResult + VIBE_Form_Destroy — tear down one page form.
    virtual void DestroyPageForm(i32 form) { (void)form; }
    // VIBE_Widget_DestroyByType(buttonId, ...) — destroy a page-arrow button.
    virtual void DestroyButton(i32 buttonId) { (void)buttonId; }
    // VIBE_Render_SetZEnable(enable) — restore z-enable on close.
    virtual void SetZEnable(int enable) { (void)enable; }
    // VIBE_Character_DrawSubMeshes / detach / free — close the mesh side (no-op model).
    virtual void ReleaseMesh(const BookRecord& rec) { (void)rec; }
};

// NOTE: VIBE_Book_HandlePageButton @0x4be1d0 (Book_HandlePageButton) and
// VIBE_Book_SetPageText @0x4be588 (Book_SetPageText) are translated in
// gui/dialog_checks.{h,cpp} and reused from there — they are NOT redefined here.

// gilde.exe 0x4be494 — VIBE_Book_RefreshVisiblePages.
// Hide every page form outside the current two-page spread {current, current+1}; if the
// prev/next arrow is both the hovered widget (== hoverObject) AND a click edge is set,
// auto-flip in that direction; then re-show the spread pages. `hoverObject` mirrors
// dword_62D22C, `clickEdge` mirrors dword_67221C (the bit-0x10 edge). `host` receives
// the SetPageVisible / RefreshPageText / DecompressGameState (model: show) calls.
void Book_RefreshVisiblePages(BookRecord& rec, BookHost& host, int hoverObject, int clickEdge);

// gilde.exe 0x4be3d0 — VIBE_Book_Close. Tear the record down: release the mesh, destroy
// every page form, destroy the two arrow buttons (when set), restore z-enable (when it
// was saved), and play the close audio sample. `rec` is cleared to an empty record.
void Book_Close(BookRecord& rec, BookHost& host);

// gilde.exe 0x596ee8 — VIBE_Interaction_HandleBookPageTurn. The cursor-zone hook: zone
// 2 over an open book flips forward, zone 3 flips backward. `cursorZone` mirrors
// *((dword*)off_5953F0+15); `bookOpen` mirrors the non-null book pointer at +19. Returns
// 0 always (the original return), and flips `book` via the page-button arrows.
int Interaction_HandleBookPageTurn(Book& book, int cursorZone, bool bookOpen);

// ===========================================================================
// SCROLL reader (parchment) — globals dword_631E64/68/6C.
// ===========================================================================
struct ScrollHost {
    virtual ~ScrollHost() = default;
    // VIBE_GameTick_Finalize(0, 0, "misc\\scroll_perga") -> form id (or -1).
    virtual i32 OpenForm(const std::string& asset) { (void)asset; return -1; }
    virtual void CenterChildWindows(i32 form) { (void)form; }
    virtual void SelectWindow(i32 form, int slot) { (void)form; (void)slot; }
    // VIBE_Object_AddToWindow(curWin, x, 0, gfx) -> sprite widget id.
    virtual i32 AddSprite(int x, int gfx) { (void)x; (void)gfx; return 0; }
    virtual void DestroyForm(i32 form) { (void)form; }
    // Per-sprite animation frame write (*(base + 740*spriteId + 116) = frame).
    virtual void SetSpriteFrame(i32 spriteId, int frame) { (void)spriteId; (void)frame; }
};

// The parchment-scroll session state (the three dword_631E6x globals: -1 == none).
// (Named ScrollReaderState to avoid clashing with gui/scrollbar.h's scrollbar ScrollState.)
struct ScrollReaderState {
    i32 form        = -1;  // dword_631E64
    i32 leftSprite  = 0;   // dword_631E68
    i32 rightSprite = 0;   // dword_631E6C
};

// gilde.exe 0x4be8d8 — VIBE_Scroll_Open. If a scroll is already open (form != -1) returns
// -1. Otherwise opens the parchment form, centres its child windows, and (on success)
// selects window slot 0 and adds the two curl sprites at x=0 and x=465. Returns the form
// id, or -1 on failure.
i32 Scroll_Open(ScrollReaderState& st, ScrollHost& host);

// gilde.exe 0x4be960 — VIBE_Scroll_Close. Destroy the scroll form and reset all three
// globals to -1 (the original sets them to the destroy return, which is -1 on success).
void Scroll_Close(ScrollReaderState& st, ScrollHost& host);

// gilde.exe 0x4be990 — VIBE_Scroll_UpdateAnimation. Compute the curl frame from a timer
// (frame = (timer / 6) % 12, clamped >= 0) and write it into both curl sprites. `timer`
// mirrors *(dword_62D298 + 584). Returns the computed frame (or -1 when no scroll open).
int Scroll_UpdateAnimation(ScrollReaderState& st, ScrollHost& host, int timer);

} // namespace guild::gui
