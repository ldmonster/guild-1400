// Unit tests for the book reader (raw-record) + scroll reader. Golden vectors for the
// deterministic dispatch / spread-visibility / page-text / scroll-frame math, all
// computed by hand from the gilde.exe pseudocode (0x4be1d0..0x4be990).
#include "gui/book_reader.h"
#include "tests/framework/test.h"

#include <string>
#include <vector>

using namespace guild::gui;

namespace {

// A recording host that captures every page-form side effect so the dispatch logic can
// be asserted without an engine.
struct RecBookHost : BookHost {
    std::vector<std::pair<i32,int>> visibility;             // (form, visible)
    std::vector<std::pair<i32,std::string>> textTop;        // (form, top text)
    std::vector<std::string> animations;                    // flip anims played
    int pageSounds = 0;
    std::vector<i32> destroyedForms;
    std::vector<i32> destroyedButtons;
    int zEnableRestores = 0;

    void SetPageVisible(i32 form, int v) override { visibility.push_back({form, v}); }
    void RefreshPageText(i32 form, const std::string& top, const std::string&) override {
        textTop.push_back({form, top});
    }
    void PlayFlipAnimation(i32, const std::string& a) override { animations.push_back(a); }
    void PlayPageSound() override { ++pageSounds; }
    void DestroyPageForm(i32 f) override { destroyedForms.push_back(f); }
    void DestroyButton(i32 b) override { destroyedButtons.push_back(b); }
    void SetZEnable(int e) override { if (e) ++zEnableRestores; }
};

BookRecord MakeRecord(int pages) {
    BookRecord r;
    r.pageCount = static_cast<std::uint8_t>(pages);
    r.pageForms.resize(static_cast<std::size_t>(pages));
    for (int i = 0; i < pages; ++i)
        r.pageForms[static_cast<std::size_t>(i)] = 100 + i; // distinct form ids
    return r;
}

} // namespace

// ---- RefreshVisiblePages: hide everything off-spread {current, current+1} ----------
TEST(GuiBookReader, RefreshHidesOffSpread) {
    BookRecord r = MakeRecord(6);
    r.currentPage = 2;             // spread {2,3}; pages 0,1,4,5 hidden
    RecBookHost host;
    Book_RefreshVisiblePages(r, host, /*hover*/-1, /*clickEdge*/0);

    // Hidden set: every form with index < 2 or > 3.
    std::vector<i32> hidden;
    for (auto& v : host.visibility) if (v.second == 0) hidden.push_back(v.first);
    CHECK_EQ((int)hidden.size(), 4);
    CHECK(hidden[0] == 100 && hidden[1] == 101 && hidden[2] == 104 && hidden[3] == 105);
}

TEST(GuiBookReader, RefreshAutoFlipForwardOnClick) {
    BookRecord r = MakeRecord(8);
    r.currentPage = 0;
    r.nextButtonId = 55;
    RecBookHost host;
    // next button is hovered (55) AND click edge set -> auto flip +2.
    Book_RefreshVisiblePages(r, host, /*hover*/55, /*clickEdge*/1);
    CHECK_EQ((int)r.currentPage, 2);
}

TEST(GuiBookReader, RefreshNoFlipWithoutClickEdge) {
    BookRecord r = MakeRecord(8);
    r.currentPage = 0;
    r.nextButtonId = 55;
    RecBookHost host;
    Book_RefreshVisiblePages(r, host, /*hover*/55, /*clickEdge*/0); // hovered but no edge
    CHECK_EQ((int)r.currentPage, 0);
}

TEST(GuiBookReader, RefreshNoFlipWrongHover) {
    BookRecord r = MakeRecord(8);
    r.currentPage = 0;
    r.nextButtonId = 55;
    RecBookHost host;
    Book_RefreshVisiblePages(r, host, /*hover*/77, /*clickEdge*/1); // edge but wrong widget
    CHECK_EQ((int)r.currentPage, 0);
}

// ---- Book_Close: destroy forms + set buttons + restore z + sound -------------------
TEST(GuiBookReader, CloseTearsDownEverything) {
    BookRecord r = MakeRecord(4);
    r.nextButtonId = 70;
    r.prevButtonId = 71;
    r.savedZEnable = 1;
    RecBookHost host;
    Book_Close(r, host);

    CHECK_EQ((int)host.destroyedForms.size(), 4);
    CHECK_EQ((int)host.destroyedButtons.size(), 2);
    CHECK_EQ(host.zEnableRestores, 1);
    CHECK_EQ(host.pageSounds, 1);
    CHECK_EQ((int)r.pageCount, 0); // record cleared
}

TEST(GuiBookReader, CloseNoButtonsNoZ) {
    BookRecord r = MakeRecord(2); // default buttons -1, savedZEnable 0
    RecBookHost host;
    Book_Close(r, host);
    CHECK_EQ((int)host.destroyedButtons.size(), 0);
    CHECK_EQ(host.zEnableRestores, 0);
    CHECK_EQ(host.pageSounds, 1);
}

// ---- Interaction cursor-zone hook --------------------------------------------------
TEST(GuiBookReader, InteractionZone2Forward) {
    Book b; b.pageCount = 6; b.currentPage = 0;
    CHECK_EQ(Interaction_HandleBookPageTurn(b, 2, true), 0);
    CHECK_EQ((int)b.currentPage, 2);
}

TEST(GuiBookReader, InteractionZone3Backward) {
    Book b; b.pageCount = 6; b.currentPage = 4;
    CHECK_EQ(Interaction_HandleBookPageTurn(b, 3, true), 0);
    CHECK_EQ((int)b.currentPage, 2);
}

TEST(GuiBookReader, InteractionNoBookNoFlip) {
    Book b; b.pageCount = 6; b.currentPage = 0;
    CHECK_EQ(Interaction_HandleBookPageTurn(b, 2, false), 0);
    CHECK_EQ((int)b.currentPage, 0);
}

TEST(GuiBookReader, InteractionOtherZone) {
    Book b; b.pageCount = 6; b.currentPage = 2;
    CHECK_EQ(Interaction_HandleBookPageTurn(b, 0, true), 0);
    CHECK_EQ((int)b.currentPage, 2);
}

// ===================================================================================
// SCROLL reader.
// ===================================================================================
namespace {

struct RecScrollHost : ScrollHost {
    std::string openedAsset;
    int centerCalls = 0;
    std::vector<std::pair<i32,int>> selects;  // (form, slot)
    std::vector<std::pair<int,int>> sprites;  // (x, gfx)
    int destroyCalls = 0;
    std::vector<std::pair<i32,int>> frames;   // (spriteId, frame)
    i32 formToReturn = 9;
    i32 nextSpriteId = 30;

    i32 OpenForm(const std::string& a) override { openedAsset = a; return formToReturn; }
    void CenterChildWindows(i32) override { ++centerCalls; }
    void SelectWindow(i32 f, int s) override { selects.push_back({f, s}); }
    i32 AddSprite(int x, int gfx) override { sprites.push_back({x, gfx}); return nextSpriteId++; }
    void DestroyForm(i32) override { ++destroyCalls; }
    void SetSpriteFrame(i32 id, int fr) override { frames.push_back({id, fr}); }
};

} // namespace

TEST(GuiScrollReader, OpenBuildsSprites) {
    ScrollReaderState st;
    RecScrollHost host;
    i32 form = Scroll_Open(st, host);
    CHECK_EQ(form, 9);
    CHECK(host.openedAsset == "misc\\scroll_perga");
    CHECK_EQ(host.centerCalls, 1);
    // Selected window slot 0, then two sprites at x=0 and x=465 with gfx 1618.
    CHECK_EQ((int)host.selects.size(), 1);
    CHECK_EQ(host.selects[0].second, 0);
    CHECK_EQ((int)host.sprites.size(), 2);
    CHECK_EQ(host.sprites[0].first, 0);
    CHECK_EQ(host.sprites[1].first, 465);
    CHECK_EQ(host.sprites[0].second, 1618);
    CHECK_EQ(st.leftSprite, 30);
    CHECK_EQ(st.rightSprite, 31);
}

TEST(GuiScrollReader, OpenWhenAlreadyOpenReturnsMinus1) {
    ScrollReaderState st;
    st.form = 5; // already open
    RecScrollHost host;
    CHECK_EQ(Scroll_Open(st, host), -1);
    CHECK(host.openedAsset.empty()); // never reached OpenForm
}

TEST(GuiScrollReader, OpenFormFailureReturnsMinus1) {
    ScrollReaderState st;
    RecScrollHost host;
    host.formToReturn = -1;
    CHECK_EQ(Scroll_Open(st, host), -1);
    CHECK_EQ(host.centerCalls, 1);     // CenterChildWindows is called before the -1 check
    CHECK((int)host.sprites.empty());  // bailed before adding sprites
}

TEST(GuiScrollReader, CloseResetsGlobals) {
    ScrollReaderState st;
    st.form = 9; st.leftSprite = 30; st.rightSprite = 31;
    RecScrollHost host;
    Scroll_Close(st, host);
    CHECK_EQ(host.destroyCalls, 1);
    CHECK_EQ(st.form, -1);
    CHECK_EQ(st.leftSprite, -1);
    CHECK_EQ(st.rightSprite, -1);
}

TEST(GuiScrollReader, CloseNoopWhenClosed) {
    ScrollReaderState st; // form == -1
    RecScrollHost host;
    Scroll_Close(st, host);
    CHECK_EQ(host.destroyCalls, 0);
}

// frame = (timer / 6) % 12, clamped >= 0. Golden vectors:
//   timer=0   -> 0
//   timer=6   -> 1
//   timer=72  -> (12 % 12) == 0
//   timer=78  -> (13 % 12) == 1
//   timer=863 -> 143 % 12 == 11
TEST(GuiScrollReader, AnimationFrameVectors) {
    ScrollReaderState st; st.form = 9; st.leftSprite = 30; st.rightSprite = 31;
    RecScrollHost host;
    CHECK_EQ(Scroll_UpdateAnimation(st, host, 0), 0);
    CHECK_EQ(Scroll_UpdateAnimation(st, host, 6), 1);
    CHECK_EQ(Scroll_UpdateAnimation(st, host, 72), 0);
    CHECK_EQ(Scroll_UpdateAnimation(st, host, 78), 1);
    CHECK_EQ(Scroll_UpdateAnimation(st, host, 863), 11);
    // Both curl sprites receive the same frame each call.
    // (Last call: timer=863 -> frame 11, written to sprite 30 and 31.)
    auto& f = host.frames;
    CHECK(f.size() >= 2);
    CHECK_EQ(f[f.size()-2].first, 30);
    CHECK_EQ(f[f.size()-1].first, 31);
    CHECK_EQ(f[f.size()-1].second, 11);
}

TEST(GuiScrollReader, AnimationNoScrollReturnsMinus1) {
    ScrollReaderState st; // form == -1
    RecScrollHost host;
    CHECK_EQ(Scroll_UpdateAnimation(st, host, 42), -1);
    CHECK(host.frames.empty());
}
