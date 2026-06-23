// tests/e2e/groundplan_blueprint_e2e_test.cpp — GUARDED real-asset blueprint
// pipeline. Loads a shipped Riss_*.bmp from the real game's
// gfx/BMP/GroundPlans, decodes it with the reconstructed render::BmpLoadBuffer
// (the same 24bpp codec VIBE_Picture_CreateSurfaceFromBmp uses), then drives
// VIBE_Groundplan_LoadBlueprintBmp (0x4aea5c) end-to-end through a backend
// backed by the decoded pixels — exercising the surface copy + per-pixel
// collision-map scan + room-hotspot spawn loop over a genuine asset.
//
// GUARDED: skips cleanly (zero checks) when the real game dir is absent.
// Override the directory with GUILD_GAME_DIR.
#include "test.h"

#include "gui/groundplan.h"
#include "render/bmp.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/guild-1400/reimpl/europe_guild_1400_original";
}

std::vector<u8> ReadFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::vector<u8>((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
}

// A software surface backed by a decoded 24bpp RGB buffer, exposed to the
// groundplan backend exactly as the engine's surface leaves would.
struct SoftSurface {
    int w = 0, h = 0;
    std::vector<u8> rgb;   // w*h*3, top-down R,G,B
};
SoftSurface g_blueprint;   // surf631640 stand-in
SoftSurface g_dest;        // surf631644 stand-in
int g_setPixels = 0;
int g_spawned = 0;

i32 BE_Create(const char*) { return g_blueprint.rgb.empty() ? 0 : 1; }
int BE_W(i32) { return g_blueprint.w; }
int BE_H(i32) { return g_blueprint.h; }
void BE_Get(int x, int y, u8 out[3], i32) {
    if (x < 0 || y < 0 || x >= g_blueprint.w || y >= g_blueprint.h) {
        out[0]=out[1]=out[2]=0; return;
    }
    const u8* p = &g_blueprint.rgb[3 * (y * (size_t)g_blueprint.w + x)];
    out[0]=p[0]; out[1]=p[1]; out[2]=p[2];
}
void BE_Set(int, int, u8, u8, u8, i32) { g_setPixels++; }
int BE_NotEqual(const u8* a, const u8* b) {
    return (a[0]!=b[0] || a[1]!=b[1] || a[2]!=b[2]) ? 1 : 0;
}
void BE_OnRoom(int, int) { g_spawned++; }

} // namespace

TEST(GroundplanBlueprintE2E, DecodeRealRissAndScan) {
    const std::string dir = GameDir();
    // Try a couple of canonical shipped blueprints (case varies on disk).
    const char* candidates[] = {
        "/gfx/BMP/GroundPlans/Riss_Kirche.BMP",
        "/gfx/BMP/GroundPlans/Riss_Wirtshaus.BMP",
        "/gfx/BMP/GroundPlans/Riss_Rathaus.BMP",
    };
    std::vector<u8> file;
    std::string used;
    for (const char* c : candidates) {
        file = ReadFile(dir + c);
        if (!file.empty()) { used = dir + c; break; }
    }
    if (file.empty()) {
        std::printf("  [skip] GroundplanBlueprintE2E: real blueprint assets absent (%s)\n",
                    dir.c_str());
        return;
    }
    std::printf("  blueprint: %s (%zu bytes)\n", used.c_str(), file.size());

    // Decode with the reconstructed BMP codec -> 24bpp RGB, top-down.
    int w = 0, h = 0;
    std::vector<u8> rgb = render::BmpLoadBuffer(file, /*wantBpp=*/24, w, h);
    CHECK(!rgb.empty());
    CHECK(w > 0);
    CHECK(h > 0);
    CHECK_EQ((int)rgb.size(), w * h * 3);
    std::printf("  decoded %dx%d (%zu px)\n", w, h, (size_t)w * h);

    // Back the blueprint surface with the decoded pixels and drive the loader.
    g_blueprint = SoftSurface{w, h, rgb};
    g_dest = SoftSurface{w, h, std::vector<u8>((size_t)w * h * 3, 0)};
    g_setPixels = 0;
    g_spawned = 0;

    gui::GroundplanBackend be;
    be.PictureCreateSurfaceFromBmp = &BE_Create;
    be.SurfaceWidth = &BE_W;
    be.SurfaceHeight = &BE_H;
    be.GetPixelRgb = &BE_Get;
    be.SetPixelRgb = &BE_Set;
    be.ColorNotEqualRgb = &BE_NotEqual;
    be.OnRoomHotspot = &BE_OnRoom;
    be.assetBaseDir = "";

    gui::GroundplanState st;
    st.screenH = 600;
    st.surf631644 = 9;          // dest surface is "created" already
    st.buildingPtr748 = 0x2000; // an active building record

    std::string chosen = gui::Groundplan_LoadBlueprintBmp(st, be);
    std::printf("  chosen path: %s ; setPixels=%d spawned=%d\n",
                chosen.c_str(), g_setPixels, g_spawned);

    // The surface copy pass must have visited every blueprint pixel once (the
    // G,R,B channel-swap blit), proving the w*h scan ran over the real asset.
    CHECK_EQ(g_setPixels, w * h);
    // The collision-map "_C.bmp" derivation appended correctly.
    CHECK(chosen.find("_C.bmp") != std::string::npos);
}
