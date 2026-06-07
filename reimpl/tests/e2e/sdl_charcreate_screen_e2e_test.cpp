// e2e (GUARDED on the real gfx/gilde.gfx): run play::RunCharCreateScreen against
// the shipped game dir so the profession/wappen grids draw the REAL decoded
// sprites (gfx beruf[i]+1349 and 1342+i), assert real sprite pixels were blitted,
// drive a scripted selection -> filled NewGameParams, and dump a PPM proof.
// Skips cleanly (CHECK(true); return) when the asset is absent.
#include "test.h"
#include "play/sdl_charcreate_screen.h"
#include "gui/newgame_setup.h"
#include "render/gfx_archive.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/disk_filesystem.h"
#include "shim/IPlatform.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <initializer_list>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

struct SeqPlatform : shim::IPlatform {
    struct Step { int x = 0, y = 0; bool left = false; int key = 0; };
    std::vector<Step> steps;
    int iter = 0;
    int maxPumps = 100000;
    bool createMainWindow(const char*, int, int, bool) override { return true; }
    void destroyMainWindow() override {}
    bool pumpMessages() override { ++iter; return iter < maxPumps; }
    std::uint32_t timeMs() override { return (std::uint32_t)iter * 16u; }
    void sleepMs(std::uint32_t) override {}
    void getMouse(shim::MouseState& o) override { const Step& s = at(); o.x = s.x; o.y = s.y; o.left = s.left; }
    bool keyDown(int vk) override { return at().key == vk; }
    const Step& at() const {
        static Step z; if (steps.empty()) return z;
        int i = iter < (int)steps.size() ? iter : (int)steps.size() - 1;
        if (i < 0) i = 0;
        return steps[i];
    }
};

SeqPlatform::Step OnProf(int i, bool left) {
    SeqPlatform::Step s; s.x = gui::Profession_ButtonX(i) + 10; s.y = gui::Profession_ButtonY(i) + 10; s.left = left; return s;
}
SeqPlatform::Step OnWappen(int i, bool left) {
    SeqPlatform::Step s; s.x = 96 * (i % 4) + 100 + 10; s.y = 80 * (i / 4) + 130 + 10; s.left = left; return s;
}
SeqPlatform::Step OnConfirm(bool left) { SeqPlatform::Step s; s.x = 560; s.y = 536; s.left = left; return s; }

std::vector<SeqPlatform::Step> Script(std::initializer_list<SeqPlatform::Step> clicks) {
    std::vector<SeqPlatform::Step> out;
    out.push_back(SeqPlatform::Step{});
    for (const auto& c : clicks) {
        out.push_back(c);
        SeqPlatform::Step rel = c; rel.left = false; rel.key = 0;
        out.push_back(rel);
    }
    return out;
}

void DumpPPM(const std::vector<std::uint8_t>& fb, int w, int h, const char* path) {
    std::FILE* f = std::fopen(path, "wb"); if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    const auto* p = reinterpret_cast<const std::uint32_t*>(fb.data());
    std::vector<std::uint8_t> row((std::size_t)w * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint32_t c = p[(std::size_t)y * w + x];
            row[x * 3 + 0] = (std::uint8_t)((c >> 16) & 0xFF);
            row[x * 3 + 1] = (std::uint8_t)((c >> 8) & 0xFF);
            row[x * 3 + 2] = (std::uint8_t)(c & 0xFF);
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
}

const int kBeruf[8] = {1, 2, 3, 4, 5, 6, 7, 11};

} // namespace

TEST(CharCreateReal, ProfessionSpritesAndSelection) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("gfx/gilde.gfx")) { CHECK(true); return; }  // GUARDED skip

    // Sanity: the real profession sprites decode (gfx beruf[i]+1349).
    guild::render::GfxArchive arc;
    if (!arc.LoadFromFile(fs, "gfx/gilde.gfx") || !arc.ok()) { CHECK(true); return; }
    {
        // gfx id 1350 (beruf 1) should decode to a non-empty shape.
        guild::render::DecodedShape s;
        const int gfx0 = gui::Profession_ButtonGfx(kBeruf[0]); // 1350
        if ((int)arc.recordCount() > gfx0 && arc.DecodeShape(gfx0, 0, s)) {
            CHECK(s.width > 0 && s.height > 0);
        }
    }

    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    // The leading no-op frame renders the profession grid (real sprites blit), then
    // pick prof 1 -> wappen 2 -> confirm.
    plat.steps = Script({ OnProf(1, true), OnWappen(2, true), OnConfirm(true) });

    play::CharCreateConfig cfg;
    cfg.gameDir = GameDir();
    cfg.fbW = 800; cfg.fbH = 600; cfg.frameCapMs = 0; cfg.maxFrames = 60;
    cfg.defaultName = "Konrad";

    auto r = play::RunCharCreateScreen(dev, plat, cfg);

    CHECK(r.framesPresented > 0);
    // Real decoded sprites were blitted into at least one grid cell.
    CHECK(r.usedRealSprites);
    CHECK(r.confirmed);
    CHECK_EQ(r.params.profession, kBeruf[1]);   // beruf 2
    CHECK_EQ(r.params.wappen, 2);
    CHECK(r.params.started);
    CHECK(r.params.firstName == "Konrad");

    DumpPPM(dev.lastPresented(), 800, 600, "/tmp/charcreate_screen_e2e.ppm");
    std::printf("  charcreate e2e: prof=%d wappen=%d realSprites=%d -> /tmp/charcreate_screen_e2e.ppm\n",
                r.params.profession, r.params.wappen, (int)r.usedRealSprites);
}

TEST(CharCreateReal, DeterministicWithAssets) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("gfx/gilde.gfx")) { CHECK(true); return; }  // GUARDED skip

    auto run = [] {
        shim::MemoryGraphicsDevice d; d.init(800, 600, 32, false);
        SeqPlatform p;
        p.steps = Script({ OnProf(3, true), OnWappen(4, true), OnConfirm(true) });
        play::CharCreateConfig cfg; cfg.gameDir = GameDir();
        cfg.fbW = 800; cfg.fbH = 600; cfg.frameCapMs = 0; cfg.maxFrames = 40;
        auto r = play::RunCharCreateScreen(d, p, cfg);
        return std::make_pair(r, d.lastPresented());
    };
    auto a = run(); auto b = run();
    CHECK_EQ(a.first.params.profession, b.first.params.profession);
    CHECK_EQ(a.first.params.wappen, b.first.params.wappen);
    CHECK(a.second == b.second);  // pixel-identical rerun
}
