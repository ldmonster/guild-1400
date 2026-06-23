// Golden tests for the wave-22 GUI core mini-cluster:
//   widget_layout.{h,cpp}    — VIBE_Widget_LayoutBounds / Property_Get / State_GetCurrent
//                              / Coord_Transform
//   gui_object_state.{h,cpp} — VIBE_State_Update / ResolveObjectState / MarkObjectUsed
//
// Pins reproduce the exact arithmetic of the Hex-Rays pseudocode (gilde.exe imagebase
// 0x400000) against synthetic records — no game assets required.
#include "test.h"
#include "gui/widget_layout.h"
#include "gui/gui_object_state.h"
#include "gui/object.h"

#include <cstring>
#include <vector>

using namespace guild::gui;

// ===========================================================================
// 0x5d8b00 — VIBE_Coord_Transform
// ===========================================================================
TEST(GuiCore, CoordTransformNullAndOffset) {
    // rec==0 returns 0 regardless of base.
    CHECK_EQ(CoordTransform(nullptr, 0, 5), 0);

    // rec!=0, base==null -> handle unchanged (inert default).
    CHECK_EQ(CoordTransform(nullptr, 100, 3), 100);

    // rec!=0, base provided: result = rec + *(u32*)(base + 4*idx + 69).
    unsigned char buf[256];
    std::memset(buf, 0, sizeof(buf));
    // idx=2 -> offset 4*2+69 = 77. Store 0x40 there.
    int add = 0x40;
    std::memcpy(buf + 77, &add, sizeof(add));
    CHECK_EQ(CoordTransform(buf, 7, 2), 7 + 0x40);
}

// ===========================================================================
// 0x4152cc — VIBE_Property_Get  (synthetic monospace font)
// ===========================================================================
namespace {
// A glyph record where +26 (advance) = 10 for every glyph, +22 (kern) = 1.
u16 monoGlyphWord(void*, i32 /*g*/, int byteOff) {
    if (byteOff == 26) return 10;  // advance
    if (byteOff == 22) return 1;   // kern pullback
    return 0;
}
i32 idCoordTransform(void*, i32 state, u8) { return state ? state : 1; }
i32 fontStateUpdate(void*, int font)       { return font; }
} // namespace

TEST(GuiCore, PropertyGetMonospaceWidth) {
    TextMetricEnv env;
    env.stateUpdate   = fontStateUpdate;
    env.glyphWord     = monoGlyphWord;
    env.coordTransform= idCoordTransform;
    env.letterSpacing = 2;   // dword_62D274
    env.spaceExtra    = 8;   // dword_62D270
    // font handle 1 so coordTransform yields nonzero glyph.

    // "AB": i=0 'A': not space, i==0 so no kern; w += 2 + 10 = 12.
    //       i=1 'B': prev 'A' != ' ', kern: w -= 1 (=11); w += 2 + 10 = 23.
    // return w + 2 = 25.
    CHECK_EQ(PropertyGet(env, "AB", 1), 25);

    // Single char "A": w = 2+10 = 12; return 12 + 2 = 14.
    CHECK_EQ(PropertyGet(env, "A", 1), 14);

    // Empty string: loop bound n-1 = 0, return 0 + letterSpacing = 2.
    CHECK_EQ(PropertyGet(env, "", 1), 2);

    // '~' is skipped entirely (no width, no kern). "A~B":
    //   'A' (i0): +12  (w=12)
    //   '~' (i1): skipped  (w=12)
    //   'B' (i2): prev char is '~' (!=' '), kern -1 (w=11); +12 (w=23)
    //   return 23 + 2 = 25.
    CHECK_EQ(PropertyGet(env, "A~B", 1), 25);

    // Space adds spaceExtra: "A B":
    //   'A' (i0): +12 (w=12)
    //   ' ' (i1): prev 'A'!=' ' -> kern -1 (w=11); space => + (2+8+10)=20 (w=31)
    //   'B' (i2): prev ' '==' ' -> NO kern; +12 (w=43)
    //   return 43 + 2 = 45.
    CHECK_EQ(PropertyGet(env, "A B", 1), 45);
}

// ===========================================================================
// 0x40e728 — VIBE_State_GetCurrent  (dirty-rect clamp + append)
// ===========================================================================
TEST(GuiCore, StateGetCurrentAppendAndClamp) {
    DirtyRect buf[16];
    DirtyRectState st;
    st.rects = buf;
    st.count = 0;
    st.capacity = 512;
    st.clipLeft = 0; st.clipTop = 0; st.clipRight = 640; st.clipBottom = 480;
    // Scroll window 0 => any positive rect pokes outside (added).
    st.scrollLeft = 0; st.scrollTop = 0; st.scrollRight = 0; st.scrollBottom = 0;

    // Even x=10, y=20, h=30, w=40 -> appended unchanged.
    i32 rx = StateGetCurrent(st, 10, 20, 30, 40);
    CHECK_EQ(st.count, 1);
    CHECK_EQ(buf[0].x, 10);
    CHECK_EQ(buf[0].y, 20);
    CHECK_EQ(buf[0].w, 40);
    CHECK_EQ(buf[0].h, 30);
    CHECK_EQ(rx, 1);   // result becomes new count

    // Odd-x alignment: x=11 -> w+=2 (=42), x-- (=10), w &= ~1 (=42 already even).
    st.count = 0;
    StateGetCurrent(st, 11, 0, 5, 40);
    CHECK_EQ(buf[0].x, 10);
    CHECK_EQ(buf[0].w, 42);

    // Bottom clamp: y=470, h=30 -> y+h=500 > 480 -> h = 480-470 = 10.
    st.count = 0;
    StateGetCurrent(st, 0, 470, 30, 40);
    CHECK_EQ(buf[0].h, 10);

    // Right clamp on x: x=700 > 640 -> x=640. Then x even, w=40, x+w=680>640 -> w=0.
    // w<=0 => NOT appended. count stays 0; returns the clamped x (640).
    st.count = 0;
    i32 r2 = StateGetCurrent(st, 700, 10, 10, 40);
    CHECK_EQ(st.count, 0);
    CHECK_EQ(r2, 640);
}

TEST(GuiCore, StateGetCurrentScrollWindowSkip) {
    DirtyRect buf[16];
    DirtyRectState st;
    st.rects = buf; st.count = 0; st.capacity = 512;
    st.clipLeft = 0; st.clipTop = 0; st.clipRight = 640; st.clipBottom = 480;
    // Inner scroll-safe window covering [100,100]-[300,300]. A rect fully inside is
    // skipped (already covered); one poking outside is added.
    st.scrollLeft = 100; st.scrollTop = 100; st.scrollRight = 300; st.scrollBottom = 300;

    // Rect [150,150] w=20 h=20 -> wholly inside (>scrollLeft, <scrollRight, etc) -> skip.
    StateGetCurrent(st, 150, 150, 20, 20);
    CHECK_EQ(st.count, 0);

    // Rect at x=50 (<= scrollLeft 100) pokes outside -> added.
    StateGetCurrent(st, 50, 150, 20, 20);
    CHECK_EQ(st.count, 1);
}

TEST(GuiCore, StateGetCurrentHardCap) {
    DirtyRectState st;
    DirtyRect buf[1];
    st.rects = buf; st.capacity = 512;
    st.count = 512;   // at the cap
    i32 r = StateGetCurrent(st, 10, 20, 30, 40);
    CHECK_EQ(st.count, 512);  // nothing appended
    CHECK_EQ(r, 10);          // returns the original x unchanged (early return)
}

// ===========================================================================
// 0x40e9e8 / 0x40eaf0 / 0x412ea4 — object-state machine
// ===========================================================================
namespace {
int g_realiseCalls;
int g_lastRealised;
int countingRealise(StateContext* ctx, int idx, int) {
    ++g_realiseCalls;
    g_lastRealised = idx;
    ctx->records[idx].stateHandle = 0x1000 + idx;  // realise -> nonzero handle
    return 1;
}
} // namespace

TEST(GuiCore, StateUpdateSimpleRealise) {
    std::vector<ObjectStateRecord> recs(8);
    std::memset(recs.data(), 0, recs.size() * sizeof(ObjectStateRecord));
    StateContext ctx;
    ctx.records = recs.data();
    ctx.recordCount = 8;
    ctx.gridOffset = 0;
    ctx.realiseHook = countingRealise;

    g_realiseCalls = 0;
    recs[3].kind = 1;             // not a 5/8 pair
    recs[3].stateHandle = 0;      // unrealised
    i32 h = StateUpdate(ctx, 3);
    CHECK_EQ(g_realiseCalls, 1);
    CHECK_EQ(g_lastRealised, 3);
    CHECK_EQ(h, 0x1000 + 3);
    CHECK_EQ(ctx.redirectDelta, 0);

    // Already realised -> no realise call, returns existing handle.
    g_realiseCalls = 0;
    recs[4].kind = 1;
    recs[4].stateHandle = 0x77;
    CHECK_EQ(StateUpdate(ctx, 4), 0x77);
    CHECK_EQ(g_realiseCalls, 0);
}

TEST(GuiCore, StateUpdateLinkedPairRedirect) {
    std::vector<ObjectStateRecord> recs(8);
    std::memset(recs.data(), 0, recs.size() * sizeof(ObjectStateRecord));
    StateContext ctx;
    ctx.records = recs.data();
    ctx.realiseHook = countingRealise;

    // idx 2 is a kind-5 pair with loadOffset==0, partner=5.
    recs[2].kind = 5;
    recs[2].loadOffset = 0;
    recs[2].linkIndex = 5;
    recs[2].stateHandle = 0;
    recs[5].stateHandle = 0;   // partner unrealised

    g_realiseCalls = 0;
    i32 h = StateUpdate(ctx, 2);
    // Realise called on the PARTNER (5), redirectDelta = 2 - 5 = -3, returns partner handle.
    CHECK_EQ(g_realiseCalls, 1);
    CHECK_EQ(g_lastRealised, 5);
    CHECK_EQ(ctx.redirectDelta, 2 - 5);
    CHECK_EQ(h, 0x1000 + 5);

    // If the pair's loadOffset != 0, it does NOT redirect (falls to the else branch).
    recs[2].loadOffset = 1;
    recs[2].stateHandle = 0;
    g_realiseCalls = 0;
    StateUpdate(ctx, 2);
    CHECK_EQ(g_lastRealised, 2);          // realised self, not partner
    CHECK_EQ(ctx.redirectDelta, 0);
}

TEST(GuiCore, StateUpdateGridOffset) {
    std::vector<ObjectStateRecord> recs(16);
    std::memset(recs.data(), 0, recs.size() * sizeof(ObjectStateRecord));
    StateContext ctx;
    ctx.records = recs.data();
    ctx.realiseHook = countingRealise;
    ctx.gridOffset = 4;   // byte_62D220

    // idx 1 has flag68 bit 0x02 set -> working index becomes 1 + 4 = 5.
    recs[1].flag68 = 0x02;
    recs[5].kind = 1;
    recs[5].stateHandle = 0;
    g_realiseCalls = 0;
    StateUpdate(ctx, 1);
    CHECK_EQ(g_lastRealised, 5);   // realised the grid-offset target
}

TEST(GuiCore, ResolveObjectStateOutputs) {
    std::vector<ObjectStateRecord> recs(8);
    std::memset(recs.data(), 0, recs.size() * sizeof(ObjectStateRecord));
    StateContext ctx;
    ctx.records = recs.data();
    ctx.realiseHook = countingRealise;

    // Simple non-redirect: idx 3, kind 1.
    recs[3].kind = 1;
    recs[3].stateHandle = 0;
    i32 outState = -1; int outIndex = -1;
    int ret = ResolveObjectState(ctx, 3, &outState, &outIndex);
    CHECK_EQ(ret, 1);
    CHECK_EQ(outIndex, 3);              // v3
    CHECK_EQ(outState, 0x1000 + 3);    // record[v6=3].stateHandle after realise
    CHECK_EQ(ctx.redirectDelta, 0);

    // Linked pair redirect: idx 2 -> partner 6. outState = partner handle, outIndex = v3=2.
    recs[2].kind = 8;
    recs[2].loadOffset = 0;
    recs[2].linkIndex = 6;
    recs[2].stateHandle = 0;
    recs[6].stateHandle = 0x900;       // already realised partner
    outState = -1; outIndex = -1;
    ResolveObjectState(ctx, 2, &outState, &outIndex);
    CHECK_EQ(outIndex, 2);             // v3 (pre-redirect index)
    CHECK_EQ(outState, 0x900);         // record[partner].stateHandle
    CHECK_EQ(ctx.redirectDelta, 2 - 6);
}

TEST(GuiCore, MarkObjectUsedBumpAndStamp) {
    std::vector<ObjectStateRecord> recs(8);
    std::memset(recs.data(), 0, recs.size() * sizeof(ObjectStateRecord));
    StateContext ctx;
    ctx.records = recs.data();
    ctx.frameStamp = 0x555;

    // Non-pair idx 3: bumps record 3.
    recs[3].kind = 1;
    recs[3].useCount = 7;
    int r = MarkObjectUsed(ctx, 3);
    CHECK_EQ(r, 3);
    CHECK_EQ(recs[3].useCount, 8);
    CHECK_EQ(recs[3].frameStamp, 0x555);

    // kind-5 pair with loadOffset==0 -> resolves to partner and bumps THAT.
    recs[2].kind = 5;
    recs[2].loadOffset = 0;
    recs[2].linkIndex = 6;
    recs[6].useCount = 0;
    int r2 = MarkObjectUsed(ctx, 2);
    CHECK_EQ(r2, 6);
    CHECK_EQ(recs[6].useCount, 1);
    CHECK_EQ(recs[6].frameStamp, 0x555);

    // kind-5 pair but loadOffset != 0 -> does NOT redirect; bumps self.
    recs[2].loadOffset = 1;
    recs[2].useCount = 0;
    int r3 = MarkObjectUsed(ctx, 2);
    CHECK_EQ(r3, 2);
    CHECK_EQ(recs[2].useCount, 1);
}

// ===========================================================================
// 0x413220 — VIBE_Widget_LayoutBounds
// ===========================================================================
TEST(GuiCore, LayoutBoundsWindowBackingNoParent) {
    ResetWidgets();
    // Widget 1 is a window-backing widget ('@' = 0x40) with w=100,h=50, no parent (+44=0),
    // no children.
    Widget& w = g_widgets[1];
    w = Widget{};
    w.type() = 0x40;
    w.at<i16>(20) = 100;   // w
    w.at<i16>(22) = 50;    // h
    w.at<i32>(44) = 0;     // no parent link
    w.at<i32>(116) = 0;    // ownerWindow

    LayoutEnv env;   // all hooks null; childCount null -> 0 children.
    // Screen extents = the shipped 640x480 (high words 480) so the '@'-path pre-recursion
    // clamps (+30 <= BC.hi, +34 <= B8.hi) do NOT trigger. (With the BSS-zero default the
    // original genuinely zeroes +30/+34 here — covered by the else-branch / runtime path.)
    env.screenExtBC = (480 << 16) | 640;   // dword_69FFBC
    env.screenExtB8 = (480 << 16) | 0;     // dword_69FFB8 (hi word = top/bottom extent)

    u8 t = WidgetLayoutBounds(env, 30, 40, 1);
    CHECK_EQ((int)t, 0x40);
    // Position written.
    CHECK_EQ(w.at<i16>(16), 30);   // x
    CHECK_EQ(w.at<i16>(18), 40);   // y
    // '@' bound writes: +32=y(40), +34=h+y(90), +28=x(30), +30=w+x(130).
    CHECK_EQ(w.at<i16>(32), 40);
    CHECK_EQ(w.at<i16>(34), 90);
    CHECK_EQ(w.at<i16>(28), 30);
    CHECK_EQ(w.at<i16>(30), 130);
    // Always-stamped metric bytes.
    CHECK_EQ((int)w.at<u8>(104), 8);
    CHECK_EQ((int)w.at<u8>(105), 8);
}

TEST(GuiCore, LayoutBoundsNonWindowElseBranch) {
    ResetWidgets();
    // A non-'@', non-4, non-'A' type (e.g. 'C' label 0x43) with no parent: the else
    // branch sets +32=0, +28=0, +34=LOWORD(BC), +30=HIWORD(BC). Returns the type byte.
    Widget& w = g_widgets[2];
    w = Widget{};
    w.type() = 0x43;       // 'C'
    w.at<i32>(44) = 0;
    LayoutEnv env;
    // screenExtBC packs (right=200 in low word, bottom=150 in high word).
    env.screenExtBC = (150 << 16) | 200;
    env.screenExtB8 = 0;

    u8 t = WidgetLayoutBounds(env, 5, 6, 2);
    CHECK_EQ((int)t, 0x43);
    CHECK_EQ(w.at<i16>(16), 5);
    CHECK_EQ(w.at<i16>(18), 6);
    CHECK_EQ(w.at<i16>(32), 0);
    CHECK_EQ(w.at<i16>(28), 0);
    CHECK_EQ(w.at<i16>(34), 200);  // LOWORD(BC)
    CHECK_EQ(w.at<i16>(30), 150);  // HIWORD(BC)
}

TEST(GuiCore, LayoutBoundsType4Anchor) {
    ResetWidgets();
    Widget& w = g_widgets[3];
    w = Widget{};
    w.type() = 4;
    w.at<i32>(44) = 0;
    w.at<i32>(116) = 7;   // ownerWindow

    static int s_anchorWin = -1; static i16 s_ax = -1, s_ay = -1;
    LayoutEnv env;
    env.anchorType4 = [](void*, int win, i16 x, i16 y) {
        s_anchorWin = win; s_ax = x; s_ay = y;
    };
    env.screenExtBC = 0; env.screenExtB8 = 0;

    WidgetLayoutBounds(env, 12, 34, 3);
    CHECK_EQ(s_anchorWin, 7);
    CHECK_EQ((int)s_ax, 12);   // *(v4+16)
    CHECK_EQ((int)s_ay, 34);   // *(v4+18)
}

TEST(GuiCore, LayoutBoundsWindowChildRecursion) {
    ResetWidgets();
    // Parent window-backing widget 1 owns window 0 with one child (widget 5).
    Widget& parent = g_widgets[1];
    parent = Widget{};
    parent.type() = 0x40;
    parent.at<i16>(20) = 200;
    parent.at<i16>(22) = 100;
    parent.at<i32>(44) = 0;
    parent.at<i32>(116) = 0;   // ownerWindow 0

    Widget& child = g_widgets[5];
    child = Widget{};
    child.type() = 0x43;       // a label
    child.at<i16>(16) = 10;    // child local x
    child.at<i16>(18) = 20;    // child local y
    child.at<i32>(44) = 0;

    LayoutEnv env;
    env.screenExtBC = 0; env.screenExtB8 = 0;
    env.windowChildCount = [](void*, int) { return 1; };
    env.windowChildId    = [](void*, int, int n) { return n == 0 ? 5 : 0; };
    env.windowOriginX    = [](void*, int) -> guild::i32 { return 0; };  // old origin
    env.windowOriginY    = [](void*, int) -> guild::i32 { return 0; };
    static int s_stampWin = -1; static i16 s_sx = -1, s_sy = -1;
    env.windowStampOrigin = [](void*, int win, i16 x, i16 y) {
        s_stampWin = win; s_sx = x; s_sy = y;
    };

    WidgetLayoutBounds(env, 100, 50, 1);
    // The window origin was stamped with the parent's (x,y).
    CHECK_EQ(s_stampWin, 0);
    CHECK_EQ((int)s_sx, 100);
    CHECK_EQ((int)s_sy, 50);
    // Child re-laid: new x = a1(100) + child.x(10) - oldOriginX(0) = 110.
    //                new y = a2(50)  + child.y(20) - oldOriginY(0) = 70.
    CHECK_EQ(child.at<i16>(16), 110);
    CHECK_EQ(child.at<i16>(18), 70);
}
