// =============================================================================
// Golden-vector tests for guild::gui::RenderHSlider — the 1:1 horizontal slider
// draw of gilde.exe VIBE_Entity_InteractionLogic @0x41078c (flags & 2 path).
//
// A recording fake IGuiSurface captures every (shapeNr, x, y, mode) blit and
// (x, y, text) call so the recovered element positions can be asserted exactly.
// =============================================================================
#include "gui/gui_render_iface.h"
#include "test.h"

#include <string>
#include <vector>

using guild::gui::IGuiSurface;
using guild::gui::ShapeMode;
using guild::gui::HSliderWidget;
using guild::gui::RenderHSlider;

namespace {

struct ShapeCall { int gfxId, shapeNr, x, y; ShapeMode mode; };
struct TextCall  { int x, y; std::string s; ShapeMode mode; };

// Fake surface with caller-supplied fixed shape metrics. Records all draws.
struct RecSurface : IGuiSurface {
    std::vector<ShapeCall> shapes;
    std::vector<TextCall>  texts;
    int clipX0 = 0, clipY0 = 0, clipX1 = 0, clipY1 = 0;

    // Per-shape fixed sizes (index -> {w,h}); -1 width means "absent".
    int sw[8], sh[8];
    bool present[8];

    RecSurface() {
        for (int i = 0; i < 8; ++i) { sw[i] = 0; sh[i] = 0; present[i] = false; }
    }
    void set(int n, int w, int h) { sw[n] = w; sh[n] = h; present[n] = true; }

    void SetClip(int x0, int y0, int x1, int y1) override {
        clipX0 = x0; clipY0 = y0; clipX1 = x1; clipY1 = y1;
    }
    bool BlitShape(int gfxId, int shapeNr, int x, int y, ShapeMode mode) override {
        shapes.push_back({gfxId, shapeNr, x, y, mode});
        return shapeNr >= 0 && shapeNr < 8 && present[shapeNr];
    }
    bool ShapeSize(int gfxId, int shapeNr, int* w, int* h) override {
        (void)gfxId;
        if (shapeNr >= 0 && shapeNr < 8 && present[shapeNr]) {
            if (w) *w = sw[shapeNr];
            if (h) *h = sh[shapeNr];
            return true;
        }
        if (w) *w = 0;
        if (h) *h = 0;
        return false;
    }
    int DrawText(int x, int y, const char* s, ShapeMode mode,
                 unsigned char, unsigned char, unsigned char) override {
        texts.push_back({x, y, s ? s : "", mode});
        return (int)(s ? std::string(s).size() * 6 : 0);  // fixed pen width
    }
    int TextWidth(const char* s) override {
        return (int)(s ? std::string(s).size() * 6 : 0);
    }

    // Helpers for assertions.
    const ShapeCall* findShape(int shapeNr) const {
        for (const auto& c : shapes) if (c.shapeNr == shapeNr) return &c;
        return nullptr;
    }
    int countShape(int shapeNr) const {
        int n = 0; for (const auto& c : shapes) if (c.shapeNr == shapeNr) ++n;
        return n;
    }
};

// Standard _SLIDER_GOLD_WAAGERECHT-shaped metrics (brief: confirmed pixel sizes).
void setSliderShapes(RecSurface& s) {
    s.set(0, 68, 18);   // leftcap
    s.set(1, 100, 6);   // track
    s.set(3, 66, 19);   // thumb
    s.set(5, 67, 18);   // rightcap
    s.set(6, 35, 18);   // +/- left btn
    s.set(7, 34, 18);   // +/- right btn
}

// (d - (d>>31)) >> 1 — the engine's round-toward-zero halving.
int half(int d) { int sgn = d >> 31; return (d - sgn) >> 1; }

} // namespace

// -----------------------------------------------------------------------------
// The canonical options value slider: flags 0x882, x=200,y=100,w=120,
// range=100, min=0, max=160, value=80.  filled = 80*100/160 = 50.
// -----------------------------------------------------------------------------
TEST(SliderRender1to1, OptionsValueSlider_882) {
    RecSurface gs;
    setSliderShapes(gs);

    HSliderWidget wgt;
    wgt.gfxId = 42;
    wgt.x = 200; wgt.y = 100; wgt.w = 120;
    wgt.value = 80; wgt.minV = 0; wgt.maxV = 160;
    wgt.range = 100; wgt.target = 80;
    wgt.flags = 0x882;
    wgt.hasOptionText = true;
    wgt.optionText = "ON";

    RenderHSlider(gs, wgt);

    const int W0 = 68, H0 = 18, W3 = 66, H3 = 19;
    const int filled = 50;                       // (80-0)*100/160
    const int capY   = 100 + half(H3 - H0);      // 100 + 0 = 100

    // Leftcap shape 0 at (x, capY), normal.
    const ShapeCall* left = gs.findShape(0);
    CHECK(left != nullptr);
    if (left) {
        CHECK_EQ(left->x, 200);
        CHECK_EQ(left->y, capY);
        CHECK_EQ(left->gfxId, 42);
        CHECK(left->mode == ShapeMode::kNormal);
    }

    // Rightcap shape 5 at (x + W0 + range, capY); present -> no fallback.
    const ShapeCall* right = gs.findShape(5);
    CHECK(right != nullptr);
    if (right) {
        CHECK_EQ(right->x, 200 + W0 + 100);      // 368
        CHECK_EQ(right->y, capY);
        CHECK(right->mode == ShapeMode::kNormal);
    }
    // Shape 5 present -> shape 0 fallback NOT drawn (only the leftcap shape 0).
    CHECK_EQ(gs.countShape(0), 1);

    // No hover -> shapes 6/7 absent.
    CHECK(gs.findShape(6) == nullptr);
    CHECK(gs.findShape(7) == nullptr);

    // Thumb shape 3 at trunc(x + filled + W0 - W3/2), y = node.y.
    const int thumbX = 200 + filled + W0 - half(W3);   // 200+50+68-33 = 285
    const ShapeCall* thumb = gs.findShape(3);
    CHECK(thumb != nullptr);
    if (thumb) {
        CHECK_EQ(thumb->x, thumbX);              // 285
        CHECK_EQ(thumb->y, 100);                 // node.y, not capY
        CHECK(thumb->mode == ShapeMode::kNormal);
    }

    // Option text "ON" on the thumb: x == thumb anchor, mode advanced (mode 8).
    CHECK_EQ((int)gs.texts.size(), 1);
    if (!gs.texts.empty()) {
        CHECK_EQ(gs.texts[0].x, thumbX);         // same anchor as the thumb
        CHECK_EQ(gs.texts[0].y, half(H3) + 100); // 9 + 100 = 109
        CHECK(gs.texts[0].s == "ON");
        CHECK(gs.texts[0].mode == ShapeMode::kAdvanced);
    }

    // flags & 4 clear -> no min/max numbers (only the option text was drawn).
}

// -----------------------------------------------------------------------------
// Right-cap fallback: when shape 5 is absent, shape 0 is re-drawn at the
// right-cap position (engine's `if (!Animation_Basic(...,5)) Animation_Basic(...,0)`).
// -----------------------------------------------------------------------------
TEST(SliderRender1to1, RightCapFallbackToShape0) {
    RecSurface gs;
    setSliderShapes(gs);
    gs.present[5] = false;                       // rightcap missing

    HSliderWidget wgt;
    wgt.gfxId = 7;
    wgt.x = 200; wgt.y = 100; wgt.w = 120;
    wgt.value = 80; wgt.minV = 0; wgt.maxV = 160;
    wgt.range = 100; wgt.target = 80;
    wgt.flags = 0x882;
    wgt.hasOptionText = true; wgt.optionText = "X";

    RenderHSlider(gs, wgt);

    const int W0 = 68;
    const int rightCapX = 200 + W0 + 100;        // 368
    // shape 5 attempted (recorded) and returned absent.
    const ShapeCall* attempt5 = gs.findShape(5);
    CHECK(attempt5 != nullptr);
    // shape 0 drawn twice: leftcap at x, fallback at rightCapX.
    CHECK_EQ(gs.countShape(0), 2);
    bool fallbackAt368 = false;
    for (const auto& c : gs.shapes)
        if (c.shapeNr == 0 && c.x == rightCapX) fallbackAt368 = true;
    CHECK(fallbackAt368);
}

// -----------------------------------------------------------------------------
// Hover buttons: shape 6 (left, advanced) at x; shape 7 (right, advanced) at
// x + w - W6.
// -----------------------------------------------------------------------------
TEST(SliderRender1to1, HoverButtons) {
    RecSurface gs;
    setSliderShapes(gs);

    HSliderWidget wgt;
    wgt.gfxId = 1;
    wgt.x = 200; wgt.y = 100; wgt.w = 120;
    wgt.value = 80; wgt.minV = 0; wgt.maxV = 160;
    wgt.range = 100; wgt.target = 80;
    wgt.flags = 0x882;
    wgt.hoverLeft = true; wgt.hoverRight = true;
    wgt.hasOptionText = false; wgt.numberText = "80";

    RenderHSlider(gs, wgt);

    const int W3 = 66, H3 = 19, H0 = 18, W6 = 35;
    const int capY = 100 + half(H3 - H0);
    (void)W3;

    const ShapeCall* h6 = gs.findShape(6);
    CHECK(h6 != nullptr);
    if (h6) {
        CHECK_EQ(h6->x, 200);                    // x
        CHECK_EQ(h6->y, capY);
        CHECK(h6->mode == ShapeMode::kAdvanced);
    }
    const ShapeCall* h7 = gs.findShape(7);
    CHECK(h7 != nullptr);
    if (h7) {
        CHECK_EQ(h7->x, 200 + 120 - W6);         // x + w - W6 = 285
        CHECK_EQ(h7->y, capY);
        CHECK(h7->mode == ShapeMode::kAdvanced);
    }

    // numeric value text "80" present on the thumb.
    CHECK_EQ((int)gs.texts.size(), 1);
    if (!gs.texts.empty()) CHECK(gs.texts[0].s == "80");
}

// -----------------------------------------------------------------------------
// flags & 4: min/max numbers. minX = x - 4 - TextWidth(min); maxX = x + w + 4.
// flags & 8 set here so the thumb/text block is skipped, isolating min/max.
// -----------------------------------------------------------------------------
TEST(SliderRender1to1, MinMaxNumbers) {
    RecSurface gs;
    setSliderShapes(gs);

    HSliderWidget wgt;
    wgt.gfxId = 9;
    wgt.x = 200; wgt.y = 100; wgt.w = 120;
    wgt.value = 80; wgt.minV = 0; wgt.maxV = 160;
    wgt.range = 100; wgt.target = 80;
    wgt.flags = 0x2 | 0x4 | 0x8 | 0x20;   // horiz + min/max; skip fills/caps/thumb
    wgt.minText = "0";
    wgt.maxText = "160";

    RenderHSlider(gs, wgt);

    // Two text draws: min then max.
    CHECK_EQ((int)gs.texts.size(), 2);
    if (gs.texts.size() == 2) {
        // min "0": width = 1*6 = 6; penX = 200 - 4 - 6 = 190.
        CHECK(gs.texts[0].s == "0");
        CHECK_EQ(gs.texts[0].x, 200 - 4 - 6);
        // max "160": penX = x + w + 4 = 324.
        CHECK(gs.texts[1].s == "160");
        CHECK_EQ(gs.texts[1].x, 200 + 120 + 4);
    }
    // flags & 0x20 set -> no caps, no thumb.
    CHECK(gs.findShape(0) == nullptr);
    CHECK(gs.findShape(3) == nullptr);
}

// -----------------------------------------------------------------------------
// filled snap-to-1: a tiny positive filled (0 < filled < 1) snaps to 1.0 before
// the thumb anchor is computed (engine 0x4108f8). value=1,max=10000,range=100 ->
// filled = 100/10000 = 0.01 -> snapped to 1.
// -----------------------------------------------------------------------------
TEST(SliderRender1to1, FilledSnapToOne) {
    RecSurface gs;
    setSliderShapes(gs);

    HSliderWidget wgt;
    wgt.gfxId = 3;
    wgt.x = 200; wgt.y = 100; wgt.w = 120;
    wgt.value = 1; wgt.minV = 0; wgt.maxV = 10000;
    wgt.range = 100; wgt.target = 1;
    wgt.flags = 0x882;
    wgt.hasOptionText = false; wgt.numberText = "1";

    RenderHSlider(gs, wgt);

    const int W0 = 68, W3 = 66;
    // filled snapped to 1 -> thumbX = 200 + 1 + 68 - 33 = 236.
    const int thumbX = 200 + 1 + W0 - half(W3);
    const ShapeCall* thumb = gs.findShape(3);
    CHECK(thumb != nullptr);
    if (thumb) CHECK_EQ(thumb->x, thumbX);       // 236
}
