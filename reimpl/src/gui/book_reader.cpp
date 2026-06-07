#include "gui/book_reader.h"
#include "gui/book.h"   // REUSED: Book / Book_TurnPage clamp+flip math (not redefined)

namespace guild::gui {

// Per-spread page-animation names are formatted from kBookPageTopFmt / kBookPageBottomFmt
// ("Buch_Seite%i_oben_ns_nm" / "..._unten_ns_nm") with ((page & 3) + 1) in the original
// TurnPage path; the page-form text refresh here is driven by caller-supplied text, so
// the format strings are retained as recovered constants in the header.

// VIBE_Book_HandlePageButton @0x4be1d0 and VIBE_Book_SetPageText @0x4be588 are translated
// in gui/dialog_checks.cpp (Book_HandlePageButton / Book_SetPageText) and reused from
// there — not redefined here (ODR).

// gilde.exe 0x4be494 — VIBE_Book_RefreshVisiblePages (__usercall, eax = result/record).
// for (i = 0; i < pageCount; ++i)
//     v6 = current;
//     if (i < v6 || i > v6 + 1) SetChildrenVisible(pageForm[i], 0);   // hide off-spread
// // auto-flip when the live arrow is BOTH hovered and a click edge fired:
// if (prevButtonId != -1 && prevButtonId == dword_62D22C && dword_67221C) TurnPage(rec,-2);
// if (nextButtonId != -1 && nextButtonId == dword_62D22C && dword_67221C) TurnPage(rec,+2);
// // re-show the spread {current + flip .. current + flip + 1} (byte_631E60 toggles a
// // one-page parity each call; the show window is [current+p .. current+p+1]):
// v9 = current + byte_631E60;
// while (v9 < pageCount && v9 <= current + byte_631E60) DecompressGameState(pageForm[v9++]);
// byte_631E60 ^= 1;
void Book_RefreshVisiblePages(BookRecord& rec, BookHost& host, int hoverObject, int clickEdge) {
    // byte_631E60 — the one-bit parity that alternates the re-show window each call.
    // It is a module-private static (the original is a single global toggled here).
    static std::uint8_t s_showParity = 0;

    const int count = rec.pageCount;

    // Hide every page outside the current two-page spread.
    for (int i = 0; i < count; ++i) {
        const int cur = rec.currentPage;
        if (i < cur || i > cur + 1)
            host.SetPageVisible(rec.pageForms[static_cast<std::size_t>(i)], 0);
    }

    // Auto-flip when the live arrow widget is both hovered and clicked this frame. The
    // original turns the underlying record; we mirror the same clamp on a logical Book.
    Book model;
    model.currentPage = rec.currentPage;
    model.pageCount   = rec.pageCount;

    if (rec.prevButtonId != -1 && rec.prevButtonId == hoverObject && clickEdge) {
        if (Book_TurnPage(model, -kBookSpreadFlip))
            rec.currentPage = model.currentPage;
    }
    if (rec.nextButtonId != -1 && rec.nextButtonId == hoverObject && clickEdge) {
        if (Book_TurnPage(model, kBookSpreadFlip))
            rec.currentPage = model.currentPage;
    }

    // Re-show window: [current + parity .. current + parity] (a single page; the parity
    // alternates which of the two spread pages is "decompressed"/shown each call).
    const int parity = s_showParity;
    int v9 = rec.currentPage + parity;
    while (v9 < rec.pageCount && v9 <= rec.currentPage + parity) {
        host.SetPageVisible(rec.pageForms[static_cast<std::size_t>(v9)], 1);
        ++v9;
    }
    s_showParity ^= 1u;
}

// gilde.exe 0x4be3d0 — VIBE_Book_Close (__usercall, eax = record, edi = audio arg).
//   DrawSubMeshes(rec.mesh);
//   if (rec.attachNode) { ToggleSuspendStateNamed(attachNode,1); DetachAndRelease(mesh);
//                         mesh = attachNode; }
//   for (i = 0; i < pageCount; ++i) { BroadcastClickResult(pageForm[i]); Destroy(pageForm[i]); }
//   if (nextButtonId != -1) DestroyByType(nextButtonId);
//   if (prevButtonId != -1) DestroyByType(prevButtonId);
//   if (savedZEnable) SetZEnable(1);
//   FreeDebug(rec);
//   StartVoiceSample(...);
void Book_Close(BookRecord& rec, BookHost& host) {
    host.ReleaseMesh(rec);

    const int count = rec.pageCount;
    for (int i = 0; i < count; ++i)
        host.DestroyPageForm(rec.pageForms[static_cast<std::size_t>(i)]);

    if (rec.nextButtonId != -1)
        host.DestroyButton(rec.nextButtonId);
    if (rec.prevButtonId != -1)
        host.DestroyButton(rec.prevButtonId);

    if (rec.savedZEnable)
        host.SetZEnable(1);

    host.PlayPageSound();   // VIBE_Audio_StartVoiceSample close sample

    rec = BookRecord{};     // VIBE_Memory_FreeDebug — the record is gone
}

// gilde.exe 0x596ee8 — VIBE_Interaction_HandleBookPageTurn.
//   if (cursorZone == 2 && book) { TurnPageForward(book); return 0; }
//   if (cursorZone == 3 && book) { TurnPageBackward(book); return 0; }
//   return 0;
int Interaction_HandleBookPageTurn(Book& book, int cursorZone, bool bookOpen) {
    if (cursorZone == 2 && bookOpen) {
        Book_TurnPageForward(book);   // REUSED: gui/book.h +2 wrapper
        return 0;
    }
    if (cursorZone == 3 && bookOpen) {
        Book_TurnPageBackward(book);  // REUSED: gui/book.h -2 wrapper
        return 0;
    }
    return 0;
}

// ===========================================================================
// SCROLL reader.
// ===========================================================================

// gilde.exe 0x4be8d8 — VIBE_Scroll_Open (__thiscall).
//   if (dword_631E64 != -1) return -1;
//   dword_631E64 = GameTick_Finalize(0, 0, "misc\\scroll_perga");
//   CenterChildWindows(dword_631E64);
//   if (dword_631E64 == -1) return -1;
//   SelectWindow(dword_631E64, 0);
//   dword_631E68 = AddToWindow(curWin, 0,   0, 1618);
//   dword_631E6C = AddToWindow(curWin, 465, 0, 1618);
//   return dword_631E64;
i32 Scroll_Open(ScrollReaderState& st, ScrollHost& host) {
    if (st.form != -1)
        return -1;

    st.form = host.OpenForm(kScrollFormAsset);
    host.CenterChildWindows(st.form);
    if (st.form == -1)
        return -1;

    host.SelectWindow(st.form, 0);
    st.leftSprite  = host.AddSprite(0, kScrollSpriteGfx);
    st.rightSprite = host.AddSprite(kScrollRightX, kScrollSpriteGfx);
    return st.form;
}

// gilde.exe 0x4be960 — VIBE_Scroll_Close.
//   if (dword_631E64 != -1) {
//     result = Form_Destroy(dword_631E64);   // -1 on success
//     dword_631E64 = dword_631E68 = dword_631E6C = result;
//   }
void Scroll_Close(ScrollReaderState& st, ScrollHost& host) {
    if (st.form != -1) {
        host.DestroyForm(st.form);
        // The original assigns the (discarded ecx) destroy result to all three; on the
        // close path that value is -1 (the "no scroll" sentinel).
        st.form        = -1;
        st.leftSprite  = -1;
        st.rightSprite = -1;
    }
}

// gilde.exe 0x4be990 — VIBE_Scroll_UpdateAnimation.
//   if (dword_631E64 != -1) {
//     SelectWindow(dword_631E64, 1);
//     v0 = *(dword_62D298 + 584) / 6 % 12;
//     if (v0 < 0) v0 = 0;
//     *(base + 740*dword_631E68 + 116) = v0;
//     *(base + 740*dword_631E6C + 116) = v0;
//   }
int Scroll_UpdateAnimation(ScrollReaderState& st, ScrollHost& host, int timer) {
    if (st.form == -1)
        return -1;

    host.SelectWindow(st.form, 1);

    int frame = timer / kScrollAnimDivisor % kScrollAnimFrames;
    if (frame < 0)
        frame = 0;

    host.SetSpriteFrame(st.leftSprite, frame);
    host.SetSpriteFrame(st.rightSprite, frame);
    return frame;
}

} // namespace guild::gui
