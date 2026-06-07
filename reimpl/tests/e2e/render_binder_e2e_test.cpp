#include "test.h"

// E2E: render a full frame of a LOADED WORLD through the REAL render path and
// prove it RUNS — not just compiles. The binder drives:
//   render::RenderMainViewFrame  (frame entry: gate + clear + universe walk)
//     -> clearRect    : real Surface clear to the world's clear colour
//     -> sceneWalk     : real ProjectVerticesToScreen + RadixSortDrawList
//     -> flushDrawList : real RasterizeMeshList (software triangle rasterizer)
//   -> HUD overlay blit (real 2D surface ops)
//   -> render::PresentFrame -> shim::MemoryGraphicsDevice (headless capture)
//
// Assertions:
//   * the device actually PRESENTED a frame (present path ran),
//   * the captured framebuffer is NON-BLANK (pixels changed from the clear colour),
//   * the render is DETERMINISTIC (two full renders produce identical framebuffers),
//   * the scene walk + rasterize + HUD each did real work (non-zero counts).
#include "play/render_binder.h"
#include "render/surface.h"
#include "render/colorformat.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/filedump_graphics.h"

#include <cstdint>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

// Snapshot the binder's software framebuffer bytes for an exact compare.
std::vector<std::uint8_t> SnapFb(const render::Surface* s) {
    std::vector<std::uint8_t> out;
    if (!s || !s->pixels) return out;
    std::size_t bytes = (std::size_t)s->pitch * (std::size_t)s->height;
    out.assign(s->pixels, s->pixels + bytes);
    return out;
}

} // namespace

// --- the full real render path runs and produces a NON-BLANK, presented frame --
TEST(RenderBinderE2E, RendersNonBlankWorldFrameAndPresents) {
    LoadedWorld w = LoadedWorld::MakeDefault();

    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(w.fbW, w.fbH, 16, /*fullscreen=*/false));

    RenderBinder rb;
    CHECK(rb.load(w));

    RenderStats st = rb.renderFrame(dev);

    // The scene-graph project+sort appended draw-list entries (real walk ran).
    CHECK(st.appendedPolys > 0);
    // The software rasterizer flushed triangles into the framebuffer.
    CHECK(st.rasterTris > 0);
    // The HUD overlay painted pixels.
    CHECK(st.hudPixels > 0);
    // The present dispatch succeeded into the device.
    CHECK(st.presented);
    CHECK(dev.presentCount() == 1);

    // NON-BLANK: the rendered framebuffer differs from the clear colour in many
    // pixels (the scene quads + HUD painted real content).
    int changed = rb.nonClearPixels();
    CHECK(changed > 0);
    // A meaningful fraction of the frame changed (terrain + objects + HUD), not a
    // stray pixel — guards against a near-blank "ran but drew nothing" result.
    CHECK(changed > 100);

    // The DEVICE captured a NON-BLANK frame too (the present copied real pixels).
    const std::vector<std::uint8_t>& shown = dev.lastPresented();
    CHECK(!shown.empty());
    bool deviceNonBlank = false;
    for (std::uint8_t b : shown)
        if (b != 0) { deviceNonBlank = true; break; }
    CHECK(deviceNonBlank);
}

// --- two full renders are byte-identical (DETERMINISM) -------------------------
TEST(RenderBinderE2E, FrameIsDeterministicAcrossRenders) {
    LoadedWorld w = LoadedWorld::MakeDefault();

    shim::MemoryGraphicsDevice dev1, dev2;
    CHECK(dev1.init(w.fbW, w.fbH, 16, false));
    CHECK(dev2.init(w.fbW, w.fbH, 16, false));

    RenderBinder a, b;
    CHECK(a.load(w));
    CHECK(b.load(w));

    RenderStats sa = a.renderFrame(dev1);
    RenderStats sb = b.renderFrame(dev2);

    // Same counts.
    CHECK_EQ(sa.appendedPolys, sb.appendedPolys);
    CHECK_EQ(sa.rasterTris, sb.rasterTris);
    CHECK_EQ(sa.hudPixels, sb.hudPixels);

    // Identical software framebuffers, byte for byte.
    std::vector<std::uint8_t> fa = SnapFb(a.framebuffer());
    std::vector<std::uint8_t> fb = SnapFb(b.framebuffer());
    CHECK(!fa.empty());
    CHECK(fa.size() == fb.size());
    CHECK(fa == fb);

    // Identical presented frames captured by the two devices.
    CHECK(dev1.lastPresented() == dev2.lastPresented());

    // Re-rendering the SAME binder a second time is also stable (self-consistent).
    RenderStats sa2 = a.renderFrame(dev1);
    CHECK_EQ(sa.appendedPolys, sa2.appendedPolys);
    CHECK_EQ(sa.rasterTris, sa2.rasterTris);
    std::vector<std::uint8_t> fa2 = SnapFb(a.framebuffer());
    CHECK(fa == fa2);
}

// --- the framebuffer is inspectable headless via FileDumpGraphicsDevice --------
// Proves the output can be dumped/decoded (the inspectable-headless requirement).
TEST(RenderBinderE2E, FrameInspectableViaFileDumpDevice) {
    LoadedWorld w = LoadedWorld::MakeDefault();

    shim::FileDumpGraphicsDevice dev;
    CHECK(dev.init(w.fbW, w.fbH, 16, false));
    // No configureDump dir => present still snapshots in-memory (file write is a
    // no-op when unconfigured); we inspect the decoded RGB snapshot directly.

    RenderBinder rb;
    CHECK(rb.load(w));
    RenderStats st = rb.renderFrame(dev);
    CHECK(st.presented);

    shim::RgbImage img = dev.snapshotRgb();
    CHECK(img.width == w.fbW);
    CHECK(img.height == w.fbH);
    CHECK((int)img.rgb.size() == w.fbW * w.fbH * 3);

    // The decoded image is non-blank (some pixel has a non-zero channel).
    bool nonBlank = false;
    for (std::uint8_t c : img.rgb)
        if (c != 0) { nonBlank = true; break; }
    CHECK(nonBlank);
}
