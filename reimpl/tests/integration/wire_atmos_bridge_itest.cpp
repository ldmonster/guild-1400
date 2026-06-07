// tests/integration/wire_atmos_bridge_itest.cpp — atmosphere ON vs OFF on a small
// synthetic live frame. Renders one frame with the atmosphere layer de-inerted and
// one with it inert, and asserts the ON frame GAINS sky + water + particle pixels
// that the OFF frame lacks — and that the ON frame is deterministic across reruns.
#include "test.h"

#include "play/wire_atmos_bridge.h"
#include "render/frame.h"
#include "render/surface.h"
#include "crt/rand.h"

#include <cstring>
#include <vector>

using namespace guild;

namespace {

render::Surface* MakeFb(int W, int H) {
    render::Surface* fb = render::SurfaceCreate(W, H, 16);
    if (fb) std::memset(fb->pixels, 0, (size_t)fb->pitch * fb->height);
    return fb;
}

int NonZero(const render::Surface* fb) {
    if (!fb || !fb->pixels) return 0;
    int n = 0;
    const u16* px = (const u16*)fb->pixels;
    for (int i = 0; i < fb->widthPx * fb->height; ++i) if (px[i] != 0) ++n;
    return n;
}

std::vector<u16> Snapshot(const render::Surface* fb) {
    std::vector<u16> v;
    if (!fb || !fb->pixels) return v;
    v.assign((const u16*)fb->pixels, (const u16*)fb->pixels + fb->widthPx * fb->height);
    return v;
}

// Render one atmosphere frame (compose: sky -> frame(particles+flares) -> water).
// `seed` re-seeds the RNG so the emitter is reproducible.
int RenderAtmosFrame(render::Surface* fb, u32 seed, bool* drewSky, int* waterMoved,
                     int* alive) {
    render::Particle parts[24];
    render::SnowFlake flakes[24];
    play::AtmosBridgeContext ctx{};
    play::AtmosBridgeContext::MakeSynthetic(ctx, fb->width, fb->height,
                                            parts, 24, flakes, 24);
    ctx.fb = fb;
    crt::Srand(seed);
    render::FrameState fs{}; render::FrameHooks hooks{};
    play::ComposeAtmosphereFrame(fs, hooks, &ctx);
    if (drewSky)    *drewSky = ctx.drewRealSky;
    if (waterMoved) *waterMoved = ctx.waterMoved;
    if (alive)      *alive = ctx.particlesAlive;
    return NonZero(fb);
}

} // namespace

// Atmosphere ON paints sky + water + particles that the OFF (inert) frame lacks.
TEST(WireAtmosBridgeITest, AtmosphereOnGainsPixelsOverOff) {
    const int W = 96, H = 72;

    // --- OFF: inert hooks, no sky pass -> blank frame ---
    render::Surface* offFb = MakeFb(W, H);
    CHECK(offFb != nullptr);
    {
        render::Particle parts[24];
        render::SnowFlake flakes[24];
        play::AtmosBridgeContext ctx{};
        play::AtmosBridgeContext::MakeSynthetic(ctx, W, H, parts, 24, flakes, 24);
        ctx.fb = offFb;
        crt::Srand(7);
        render::FrameState fs{}; render::FrameHooks hooks{};
        play::InstallInertAtmosBridge(fs, hooks, &ctx);
        play::InertDrawSky(&ctx);                 // inert sky: nothing
        render::BeginUniverseFrame(fs, hooks, 1); // inert particle/flare hooks
        // (no water animate -> inert floor)
        CHECK(ctx.drewRealSky == false);
        CHECK(ctx.drewRealParticles == false);
    }
    int offNB = NonZero(offFb);

    // --- ON: real atmosphere, full compose ---
    render::Surface* onFb = MakeFb(W, H);
    CHECK(onFb != nullptr);
    bool drewSky = false; int waterMoved = 0; int alive = 0;
    int onNB = RenderAtmosFrame(onFb, 7, &drewSky, &waterMoved, &alive);

    CHECK_EQ(offNB, 0);                 // inert frame is blank
    CHECK(drewSky == true);
    CHECK(waterMoved > 0);              // water vertices animated
    CHECK(alive > 0);                   // particles emitted
    CHECK(onNB > offNB);               // ON gained pixels
    CHECK(onNB > (W * H) / 2);         // sky fill dominates the frame

    render::SurfaceDestroy(offFb);
    render::SurfaceDestroy(onFb);
}

// Determinism: two ON frames with the same seed are byte-identical.
TEST(WireAtmosBridgeITest, AtmosphereOnIsDeterministic) {
    const int W = 80, H = 60;
    render::Surface* a = MakeFb(W, H);
    render::Surface* b = MakeFb(W, H);
    CHECK(a != nullptr); CHECK(b != nullptr);

    RenderAtmosFrame(a, 4242, nullptr, nullptr, nullptr);
    RenderAtmosFrame(b, 4242, nullptr, nullptr, nullptr);

    std::vector<u16> sa = Snapshot(a), sb = Snapshot(b);
    CHECK(sa.size() == sb.size());
    bool identical = (sa.size() == sb.size());
    for (size_t i = 0; i < sa.size() && i < sb.size(); ++i)
        if (sa[i] != sb[i]) { identical = false; break; }
    CHECK(identical == true);

    render::SurfaceDestroy(a);
    render::SurfaceDestroy(b);
}
