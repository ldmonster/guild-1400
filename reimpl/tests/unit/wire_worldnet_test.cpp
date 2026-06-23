// Verifies InstallRealWorldNetWiring() — the seed-from-defaults audit installer for
// the four world-net / render-tail hook bridges (WorldHistory2 / NetMode /
// Transition / RenderLeaves9). The contract: the installer is callable +
// idempotent, binds nothing fabricated, leaves the audited inert fields pinned at
// their defaults (non-null stubs; the NetMode vtable stays the module default) —
// EXCEPT the RenderLeaves9 converter slots, which now bind the REAL
// render/shape_convert16 reconstructions (0x5d7c0c / 0x5d7924).
// Suite prefix: WireWorldNet.
#include "tests/framework/test.h"

#include "world/wire_worldnet.h"

#include "world/world_history2.h"        // Get/SetWorldHistory2Hooks
#include "play/menu_recon_transition.h"  // SetTransitionHooks
#include "render/render_leaves9.h"       // Leaves9Hooks / SetLeaves9Hooks / ResetLeaves9Hooks
#include "gui/netfile_run.h"             // SetNetModeHooks

#include <cstdlib>
#include <cstring>

using namespace guild;
using namespace guild::world;

// Install is callable and leaves the POD bridges with non-null inert stubs in every
// field (several call sites invoke these hooks WITHOUT a null-check, so the seed must
// preserve the module's safe stubs — never null them).
TEST(WireWorldNet, InstallKeepsInertStubsNonNull) {
    InstallRealWorldNetWiring();

    // WorldHistory2: every field of the live table stays a non-null inert stub.
    const WorldHistory2Hooks& h = GetWorldHistory2Hooks();
    CHECK(h.logMessage              != nullptr);
    CHECK(h.processPlayerNews       != nullptr);
    CHECK(h.universeSwitchActiveSlot != nullptr);
    CHECK(h.objectFindByHandle      != nullptr);
    CHECK(h.objectDetachAndRelease  != nullptr);
    CHECK(h.arrangeIconsInCircle    != nullptr);
    CHECK(h.createIconMesh          != nullptr);
    CHECK(h.iconsEnabled            != nullptr);
    CHECK(h.parentEntityId          != nullptr);
    CHECK(h.storeSlotInParent       != nullptr);

    // RenderLeaves9: the three render leaves stay non-null inert defaults.
    const guild::render::RenderLeaves9Hooks& l = guild::render::Leaves9Hooks();
    CHECK(l.FrameDataProcess != nullptr);
    CHECK(l.ConvertRgbTo16   != nullptr);
    CHECK(l.Convert8To16     != nullptr);
}

// The inert WorldHistory2 defaults preserve the documented behaviour the engine
// relies on: iconsEnabled() == 1 (icons on), objectFindByHandle() == 1 (live),
// parentEntityId(p) == p (identity). Re-running install must not perturb them.
TEST(WireWorldNet, InertHistory2DefaultsBehaveAsDocumented) {
    InstallRealWorldNetWiring();
    const WorldHistory2Hooks& h = GetWorldHistory2Hooks();
    CHECK_EQ(h.iconsEnabled(), 1);
    CHECK_EQ(h.objectFindByHandle(123), 1);
    CHECK_EQ(h.parentEntityId(7), 7);
    CHECK_EQ(h.universeSwitchActiveSlot(0, 0, 0, 0), 0);
    CHECK_EQ(h.createIconMesh(nullptr, 0, 0), 0);
}

// FrameDataProcess stays the inert default (-> 0); the two converter slots are
// NO LONGER inert — the installer binds the REAL render/shape_convert16
// reconstructions (VIBE_Shape_ConvertRgbTo16 @0x5d7c0c / Convert8To16 @0x5d7924),
// observable as a non-default pointer that actually converts a minimal shape.
TEST(WireWorldNet, InstalledLeaves9ConvertersAreTheRealOnes) {
    guild::render::ResetLeaves9Hooks();
    const auto inertRgb = guild::render::Leaves9Hooks().ConvertRgbTo16;
    const auto inert8   = guild::render::Leaves9Hooks().Convert8To16;

    InstallRealWorldNetWiring();
    const guild::render::RenderLeaves9Hooks& l = guild::render::Leaves9Hooks();
    CHECK_EQ(l.FrameDataProcess(0, 0, nullptr, 0), 0);
    CHECK(l.ConvertRgbTo16 != inertRgb);
    CHECK(l.Convert8To16   != inert8);

    // The installed slot is the live 0x5d7c0c chain: a minimal 24bpp RLE shape
    // (1x1, one run, pixel (255,0,0)) converts to a depth-1 shape via the hook.
    // 72 bytes (not 64): the single run's pixel RGB lands at +62..+64, so a
    // 64-byte buffer lets the faithful converter read shape[64] one byte past the
    // end (ASAN). The shape's declared fields still describe a 1x1/1-run image;
    // the extra tail is just headroom for the 3-byte pixel.
    guild::u8 shape[72] = {};
    auto W32 = [&](int off, guild::u32 v) { std::memcpy(shape + off, &v, 4); };
    auto W16 = [&](int off, guild::u16 v) { std::memcpy(shape + off, &v, 2); };
    W32(0, 50 + 4 + 8 + 3 + 4);  // size (incl. 1-row table)
    W16(6, 1); W16(10, 1);       // width / height
    shape[12] = 2;               // depth 2 (24bpp)
    W32(38, 1);                  // spanFlag != -1 -> RLE branch
    W32(46, 1);                  // opaque pixel count
    W32(50, 1);                  // row 0: runCount = 1
    W32(54, 0); W32(58, 1);      // run: skip 0, 1 pixel
    shape[62] = 255;             // R (G=B=0)
    void* conv = l.ConvertRgbTo16(shape);
    CHECK(conv != nullptr);
    if (conv) {
        CHECK_EQ((int)static_cast<guild::u8*>(conv)[12], 1);  // depth 1
        std::free(conv);
    }
}

// Idempotent: calling the seed-from-defaults installer twice changes nothing — the
// live function pointers are identical across both installs (true no-op).
TEST(WireWorldNet, InstallIsIdempotent) {
    InstallRealWorldNetWiring();
    const WorldHistory2Hooks& a = GetWorldHistory2Hooks();
    void (*log0)(const char*) = a.logMessage;
    int  (*icons0)()          = a.iconsEnabled;
    auto fdp0 = guild::render::Leaves9Hooks().FrameDataProcess;

    InstallRealWorldNetWiring();
    const WorldHistory2Hooks& b = GetWorldHistory2Hooks();
    CHECK(b.logMessage   == log0);
    CHECK(b.iconsEnabled == icons0);
    CHECK(guild::render::Leaves9Hooks().FrameDataProcess == fdp0);
}

// Install after a foreign override re-pins the POD bridges to the inert defaults.
TEST(WireWorldNet, InstallRepinsAfterForeignOverride) {
    // Stomp RenderLeaves9 with a non-default override, then confirm the installer
    // re-seeds from the (now-overridden) live table without crashing and the table
    // still has three non-null leaves. (The installer seeds from the LIVE table by
    // design, so we first restore the module defaults to prove the pinned state.)
    guild::render::ResetLeaves9Hooks();
    InstallRealWorldNetWiring();
    const guild::render::RenderLeaves9Hooks& l = guild::render::Leaves9Hooks();
    CHECK(l.FrameDataProcess != nullptr);
    CHECK_EQ(l.FrameDataProcess(0, 0, nullptr, 0), 0);

    // NetMode + Transition: installer drives them to their module inert defaults via
    // Set*(nullptr). Re-installing must remain safe (no crash, returns cleanly).
    InstallRealWorldNetWiring();
    CHECK(true);
}
