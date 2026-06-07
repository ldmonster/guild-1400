// E2E test for play::RunCityScreen with the REAL shipped city list + the real
// _MENUE_BACKGROUND from gfx/gilde.gfx.  GUARDED: skips cleanly when the game
// dir is absent.  Dumps the rendered screen to /tmp as a PPM.
#include "test.h"
#include "play/sdl_city_screen.h"
#include "shim_impl/memory_graphics.h"
#include "shim/IPlatform.h"
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <filesystem>

using namespace guild;

namespace {
struct SeqPlatform : shim::IPlatform {
    struct Step { int x = 0, y = 0; bool left = false; int key = 0; std::uint32_t t = 0; };
    std::vector<Step> steps; int iter = 0, maxPumps = 100000;
    bool createMainWindow(const char*, int, int, bool) override { return true; }
    void destroyMainWindow() override {}
    bool pumpMessages() override { ++iter; return iter < maxPumps; }
    std::uint32_t timeMs() override { return at().t; }
    void sleepMs(std::uint32_t) override {}
    void getMouse(shim::MouseState& o) override { const Step& s = at(); o.x = s.x; o.y = s.y; o.left = s.left; }
    bool keyDown(int vk) override { return at().key == vk; }
    const Step& at() const { static Step z; if (steps.empty()) return z;
        int i = iter < (int)steps.size() ? iter : (int)steps.size() - 1; return steps[i < 0 ? 0 : i]; }
};
SeqPlatform::Step OnCityRow(int i, bool left) {
    SeqPlatform::Step s; s.x = 220 + 16 + 20; s.y = (70 + 56) + i * (40 + 8) + 20; s.left = left; return s;
}
int ActionY(int n) { int nn = n > 0 ? n : 1; return (70 + 56) + (nn - 1) * (40 + 8) + 40 + 24; }
SeqPlatform::Step OnConfirm(int n, bool left) {
    SeqPlatform::Step s; s.x = 220 + 24 + 20; s.y = ActionY(n) + 18; s.left = left; return s;
}
std::string GameDir() {
    if (const char* e = std::getenv("GUILD_GAME_DIR")) return e;
    return "europe_guild_1400_original";
}
void DumpPpm(const char* path, const std::vector<std::uint8_t>& fb, int W, int H) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", W, H);
    const auto* p = reinterpret_cast<const std::uint32_t*>(fb.data());
    for (int i = 0; i < W * H; ++i) {
        std::uint32_t c = p[i];
        std::uint8_t rgb[3] = { (std::uint8_t)(c >> 16), (std::uint8_t)(c >> 8), (std::uint8_t)c };
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}
int NonBlank(const std::vector<std::uint8_t>& fb) {
    int n = 0; const auto* p = reinterpret_cast<const std::uint32_t*>(fb.data());
    for (std::size_t i = 0; i < fb.size() / 4; ++i) if ((p[i] & 0x00FFFFFFu) != 0) ++n;
    return n;
}
} // namespace

TEST(SdlCityScreenE2E, RealCitiesRealBackgroundPickAndDump) {
    namespace fsx = std::filesystem;
    std::error_code ec;
    const fsx::path cdir = fsx::path(GameDir()) / "Resources" / "gamedata" / "Cities";
    if (!fsx::exists(cdir, ec)) {
        std::printf("[ SKIP ] real game dir not found (%s); set GUILD_GAME_DIR.\n", cdir.string().c_str());
        CHECK(true); return;
    }
    play::CityScreenConfig cfg;
    cfg.fbW = 800; cfg.fbH = 600; cfg.frameCapMs = 0; cfg.maxFrames = 80;
    cfg.gameDir = GameDir();
    for (fsx::directory_iterator it(cdir, ec), end; !ec && it != end; ++it) {
        if (!it->is_regular_file()) continue;
        std::string ext = it->path().extension().string();
        for (char& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext != ".cty") continue;
        cfg.cities.emplace_back(it->path().stem().string(),
                                std::string("Resources/gamedata/Cities/") + it->path().filename().string());
    }
    std::sort(cfg.cities.begin(), cfg.cities.end());
    if (cfg.cities.empty()) { std::printf("[ SKIP ] no .cty cities found.\n"); CHECK(true); return; }
    std::printf("[city-e2e] %zu real cities:", cfg.cities.size());
    for (auto& c : cfg.cities) std::printf(" %s", c.first.c_str());
    std::printf("\n");
    CHECK(cfg.cities.size() >= 1);

    // Render a few frames hovering the first city (no pick yet) so we can dump
    // the screen WITH the real background and verify the backdrop is present.
    {
        shim::MemoryGraphicsDevice dev;
        CHECK(dev.init(800, 600, 32, false));
        SeqPlatform plat;
        plat.steps = { OnCityRow(0, false) };
        play::CityScreenConfig rcfg = cfg; rcfg.maxFrames = 3;
        play::CityScreenResult rr = play::RunCityScreen(dev, plat, rcfg);
        CHECK(rr.framesPresented == 3);
        // The real _MENUE_BACKGROUND drew -> sawBackground + lots of non-blank px.
        CHECK(rr.sawBackground);
        const auto& fb = dev.lastPresented();
        CHECK(NonBlank(fb) > 100000);   // a full-screen 800x600 backdrop
        DumpPpm("/tmp/sdl_city_screen_e2e.ppm", fb, 800, 600);
        std::printf("[city-e2e] dumped /tmp/sdl_city_screen_e2e.ppm (nonblank=%d)\n", NonBlank(fb));
    }

    // Now drive a real pick: select the first city + Confirm.
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    const int n = (int)cfg.cities.size();
    plat.steps = {
        OnCityRow(0, false), OnCityRow(0, true), OnCityRow(0, false),
        OnConfirm(n, false), OnConfirm(n, true), OnConfirm(n, false),
    };
    play::CityScreenResult r = play::RunCityScreen(dev, plat, cfg);
    CHECK(r.confirmed);
    CHECK(r.cityIndex == 0);
    CHECK(r.cityName == cfg.cities[0].first);
    CHECK(r.cityPath == cfg.cities[0].second);
    // The picked .cty must actually exist on disk.
    CHECK(fsx::exists(fsx::path(GameDir()) / r.cityPath, ec));
    std::printf("[city-e2e] picked '%s' -> %s (exists)\n", r.cityName.c_str(), r.cityPath.c_str());

    // ESC -> back works on the real-size screen.
    shim::MemoryGraphicsDevice dev2; dev2.init(800, 600, 32, false);
    SeqPlatform p2; SeqPlatform::Step esc; esc.key = 0x1B; p2.steps = { esc };
    play::CityScreenConfig bcfg = cfg; bcfg.maxFrames = 10;
    play::CityScreenResult rb = play::RunCityScreen(dev2, p2, bcfg);
    CHECK(rb.back);
    CHECK(!rb.confirmed);
}
