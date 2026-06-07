#include "test.h"

// E2E (GUARDED real assets): load the REAL shipped AUGSBURG city into the live sim
// arrays through app::MountRealGameAssets + io::LoadWorld (via play::LoadRealCity),
// then compose the FULL city-view frame with play::RenderCityFrame — terrain floor,
// the REAL loaded objects/scene nodes at their placements, and the in-game HUD —
// dump it to a BMP through the FileDumpGraphicsDevice, and assert a recognizable
// city view: terrain GROUND pixels + N OBJECT pixels (from the real world) + the
// HUD overlay all present in the finished frame, and the BMP round-trips.
//
// Skips cleanly when the real install is absent (honors GUILD_GAME_DIR).
#include "play/city_frame.h"
#include "play/real_session.h"
#include "render/surface.h"
#include "gui/menu_render.h"
#include "io/vfs.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "shim/IFileSystem.h"
#include "shim_impl/filedump_graphics.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;
using namespace guild::sim;

namespace {

// Read-only disk filesystem rooted at the real install (serves the real .cty bytes).
class DiskFs : public shim::IFileSystem {
public:
    explicit DiskFs(std::string root) : root_(std::move(root)) {}
    shim::IFile* open(const char* path, const char* mode) override {
        if (mode && (std::strchr(mode, 'w') || std::strchr(mode, 'W'))) return nullptr;
        std::ifstream f(full(path), std::ios::binary);
        if (!f) return nullptr;
        std::vector<u8> data((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        return new DiskFile(std::move(data));
    }
    void close(shim::IFile* f) override { delete f; }
    bool exists(const char* path) override {
        std::ifstream f(full(path), std::ios::binary); return (bool)f;
    }
private:
    struct DiskFile : public shim::IFile {
        explicit DiskFile(std::vector<u8> d) : data_(std::move(d)) {}
        std::size_t read(void* dst, std::size_t n) override {
            std::size_t a = data_.size() - pos_; if (n > a) n = a;
            if (n) std::memcpy(dst, data_.data() + pos_, n); pos_ += n; return n;
        }
        std::size_t write(const void*, std::size_t) override { return 0; }
        std::int64_t seek(std::int64_t off, int whence) override {
            std::int64_t len = (std::int64_t)data_.size();
            std::int64_t base = whence == 1 ? (std::int64_t)pos_ : whence == 2 ? len : 0;
            std::int64_t t = base + off; if (t < 0) return -1; pos_ = (std::size_t)t; return pos_;
        }
        std::int64_t tell() override { return (std::int64_t)pos_; }
        std::int64_t size() override { return (std::int64_t)data_.size(); }
        std::vector<u8> data_; std::size_t pos_ = 0;
    };
    std::string full(const char* path) const {
        std::string p = path; for (char& c : p) if (c == '\\') c = '/';
        return root_ + "/" + p;
    }
    std::string root_;
};

const char* FindAssetRoot() {
    static std::string root;
    if (const char* env = std::getenv("GUILD_GAME_DIR")) {
        std::string cty = std::string(env) + "/Resources/gamedata/Cities/AUGSBURG.cty";
        std::ifstream f(cty, std::ios::binary);
        if (f) { root = env; return root.c_str(); }
    }
    static const char* candidates[] = {
        "europe_guild_1400_original/Resources/gamedata/Cities/AUGSBURG.cty",
        "reimpl/europe_guild_1400_original/Resources/gamedata/Cities/AUGSBURG.cty",
    };
    for (const char* c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (f) { std::string s = c; auto pos = s.find("/Resources/");
                 root = s.substr(0, pos); return root.c_str(); }
    }
    return nullptr;
}

bool IsGround(u8 r, u8 g, u8 b) { return g > r + 10 && g > b + 10; }
bool IsObject(u8 r, u8 g, u8 b) { return r > g && g >= b && r > 60; }

} // namespace

TEST(CityFrameE2E, AugsburgRenderCityFrameToBmp) {
    const char* root = FindAssetRoot();
    if (!root) { CHECK(true); return; }   // assets absent — skip
    std::printf("[city_frame][e2e] asset root: %s\n", root);

    DiskFs fs(root);
    std::uint32_t persons = 0, objects = 0;
    bool loaded = LoadRealCity(&fs, root, "Augsburg", &persons, &objects);
    CHECK(loaded);
    std::printf("[city_frame][e2e] loaded=%d persons=%u objects=%u sceneNodes=%d\n",
                (int)loaded, persons, objects, g_sceneNodeCount);

    if (loaded) {
        const int W = 192, H = 144;

        CityFrameScene scene;
        // Terrain floor (the terrain_render module's real-format heightfield; the
        // .cty floor heightfield decode into a Heightfield is not yet wired, so the
        // ground is the engine's real-format synthetic floor seeded by the city).
        scene.terrain = Heightfield::MakeSynthetic(64, /*seed*/ 0xA0);
        scene.terrainView = TerrainView::MakeTopDown(scene.terrain.size,
                                                     scene.terrain.tileSpan, W, H);
        // Objects: the REAL loaded live arrays.
        scene.worldOpt.scanScene   = true;
        scene.worldOpt.scanObjects = true;
        scene.worldOpt.scanPersons = false;
        scene.worldOpt.maxObjects  = 8;

        // HUD: a real money/date caption + bar slots drawn from the loaded objects.
        scene.hud.money = 100000; scene.hud.gameDay = 1; scene.hud.clockTick = 4321;
        for (int i = 0; i < kObjectCapacity && (int)scene.hud.barObjects.size() < 4; ++i)
            if (g_objects[i].alive)
                scene.hud.barObjects.push_back({ (u16)g_objects[i].id, 0.5 });
        scene.hudBarOriginX = 4; scene.hudBarOriginY = 4;
        scene.hudCaptionX   = 4; scene.hudCaptionY   = 4;
        scene.drawHud = true;

        CameraControl cam{};

        shim::FileDumpGraphicsDevice dev;
        CHECK(dev.init(W, H, 32, /*fullscreen=*/false));
        dev.configureDump("/tmp", "city_augsburg",
                          shim::FileDumpGraphicsDevice::kBmp);

        render::Surface* frame = nullptr;
        CityFrameStats st = RenderCityFrame(cam, scene, W, H, dev, &frame);

        std::printf("[city_frame][e2e] terrain(px=%d) objects(built=%d drawn=%d px=%d) "
                    "hud(slots=%d glyphs=%d px=%d) nonBlank=%d presented=%d bmp=%s\n",
                    st.terrainPixels, st.objectsBuilt, st.objectsDrawn, st.objectPixels,
                    st.hudBarSlots, st.hudCaptionGlyphs, st.hudPixels,
                    st.nonBlankPixels, (int)st.presented,
                    dev.framePath(0, shim::FileDumpGraphicsDevice::kBmp).c_str());

        // recognizable city view: all three layers present.
        CHECK(st.terrainDrawn);
        CHECK(st.terrainPixels > 0);          // ground filled
        CHECK(st.objectsBuilt >= 1);          // the real world produced scene objects
        CHECK(st.objectsDrawn >= 1);
        CHECK(st.objectPixels > 0);           // N object pixels over terrain
        CHECK(st.hudDrawn);
        CHECK(st.hudPixels > 0);              // HUD overlay present
        CHECK(st.presented);

        // colour-class coexistence in the finished surface.
        if (frame) {
            int ground = 0, object = 0;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    // 32bpp ARGB: decode R@16,G@8,B@0 (SurfaceGetPixelRgb returns
                    // raw [B,G,R] on the 32bpp read path).
                    std::uint32_t px = reinterpret_cast<const std::uint32_t*>(
                        frame->pixels)[(std::size_t)frame->widthPx * y + x];
                    u8 r = (u8)((px >> 16) & 0xff), g = (u8)((px >> 8) & 0xff),
                       b = (u8)(px & 0xff);
                    if (IsGround(r, g, b)) ++ground;
                    else if (IsObject(r, g, b)) ++object;
                }
            std::printf("[city_frame][e2e] groundPixels=%d objectPixels=%d\n",
                        ground, object);
            CHECK(ground > 0);
            CHECK(object > 0);
            render::SurfaceDestroy(frame);
        }

        // the BMP was written + round-trips through the standard decoder.
        std::string bmpPath = dev.framePath(0, shim::FileDumpGraphicsDevice::kBmp);
        std::ifstream bf(bmpPath, std::ios::binary);
        CHECK((bool)bf);
        if (bf) {
            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(bf)),
                                            std::istreambuf_iterator<char>());
            shim::RgbImage img =
                shim::FileDumpGraphicsDevice::DecodeBmp24(bytes);
            CHECK(img.ok);
            CHECK_EQ(img.width, W);
            CHECK_EQ(img.height, H);
        }
    }

    io::VfsShutdown();
}
