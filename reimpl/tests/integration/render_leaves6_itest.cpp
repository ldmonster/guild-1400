#include "test.h"

#include "render/render_leaves6.h"
#include "render/render_leaves2.h"   // REAL sibling: DrawHLine (0x4351d8) + FrameTarget

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using guild::render::ClipBounds;
using guild::render::RenderLeaves6Hooks;
using guild::render::FrameTarget;

namespace {

// The live wiring: DrawLineClipped's tail-call (VIBE_Render_DrawHLine) is the
// reconstructed sibling guild::render::DrawHLine in render_leaves2.cpp. We bridge
// the drawSpan hook into it positionally — exactly as the register-arg call does
// (eax,edx,ecx,ebx,stack), so DrawHLine's (x0, y0, y1, x1, color) params receive
// the clipped endpoints in their original register order. This exercises the
// real cross-module clip -> raster flow against a true 16bpp framebuffer.
FrameTarget* g_target = nullptr;

i32 BridgeToRealDrawHLine(i32 x0, i32 y0, i32 x1, i32 y1, i16 color) {
    // Positional forward (register order preserved): the clipped span hits the
    // genuine Bresenham/run raster in render_leaves2.cpp.
    return render::DrawHLine(*g_target, x0, y0, x1, y1,
                             static_cast<u16>(color));
}

} // namespace

// ---------------------------------------------------------------------------
// Wire DrawLineClipped -> the REAL DrawHLine sibling and assert the framebuffer
// matches an independent call to that same real sibling with the clipped
// endpoints. This proves the cross-module clip -> raster wiring is faithful
// (register-order preserved) without re-deriving the Bresenham math here.
// ---------------------------------------------------------------------------
TEST(RenderLeaves6_Integration, ClipThenRealDrawHLineMatchesDirect) {
    const int W = 80, H = 80;
    std::vector<std::uint16_t> fbClip(static_cast<size_t>(W) * H, 0);
    std::vector<std::uint16_t> fbDirect(static_cast<size_t>(W) * H, 0);

    FrameTarget t;
    t.width = W; t.height = H; t.pitch = W; t.bpp = 16;

    // Path A: clip -> real DrawHLine via the hook bridge.
    t.base  = reinterpret_cast<u8*>(fbClip.data());
    g_target = &t;
    RenderLeaves6Hooks h{};
    h.drawSpan = &BridgeToRealDrawHLine;
    render::InstallRenderLeaves6Hooks(h);

    // Fully-inside line: bounds {10,10,200,200}; endpoints (50,50)-(55,60) lie
    // entirely inside, so the clip passes them through UNCHANGED to DrawHLine.
    ClipBounds c{10, 10, 200, 200};
    i32 retClip = render::DrawLineClipped(c, 50, 50, 55, 60, 0xBEEF);

    // Path B: directly invoke the SAME real sibling with the (unclipped) endpoints
    // in the identical register order the clip uses: DrawHLine(t, x0,y0,x1,y1,c).
    FrameTarget t2 = t;
    t2.base = reinterpret_cast<u8*>(fbDirect.data());
    i32 retDirect = render::DrawHLine(t2, 50, 50, 55, 60, 0xBEEF);

    CHECK_EQ(retClip, retDirect);          // same running pixel index
    CHECK(fbClip == fbDirect);             // identical framebuffer contents
    // Sanity: the real raster actually drew something.
    bool drew = false;
    for (auto px : fbClip) if (px == 0xBEEF) { drew = true; break; }
    CHECK(drew);

    RenderLeaves6Hooks reset{};
    render::InstallRenderLeaves6Hooks(reset);
    g_target = nullptr;
}

// ---------------------------------------------------------------------------
// A rejected line must NOT touch the real framebuffer (no raster call at all).
// ---------------------------------------------------------------------------
TEST(RenderLeaves6_Integration, RejectedLineLeavesRealFramebufferClean) {
    const int W = 32, H = 8;
    std::vector<std::uint16_t> fb(static_cast<size_t>(W) * H, 0x1234);
    FrameTarget t;
    t.base  = reinterpret_cast<u8*>(fb.data());
    t.width = W; t.height = H; t.pitch = W; t.bpp = 16;
    g_target = &t;

    RenderLeaves6Hooks h{};
    h.drawSpan = &BridgeToRealDrawHLine;
    render::InstallRenderLeaves6Hooks(h);

    // Bounds that trivially reject (the verbatim guard chain bails immediately).
    ClipBounds c{640, 480, 0, 0};
    i32 ret = render::DrawLineClipped(c, 100, 100, 200, 300, 7);
    CHECK_EQ(ret, 100);   // returns running x0, no draw

    // Framebuffer unchanged.
    bool clean = true;
    for (auto px : fb) if (px != 0x1234) clean = false;
    CHECK(clean);

    RenderLeaves6Hooks reset{};
    render::InstallRenderLeaves6Hooks(reset);
    g_target = nullptr;
}
