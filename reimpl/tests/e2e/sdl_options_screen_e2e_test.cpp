// E2E test for play::RunOptionsScreen with the REAL gilde.INI + menu background.
// GUARDED: skips cleanly when the game dir / assets are absent.
#include "test.h"
#include "play/sdl_options_screen.h"
#include "config/ini.h"
#include "shim_impl/memory_graphics.h"
#include "shim/IPlatform.h"
#include <vector>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace guild;

namespace {
struct SeqPlatform : shim::IPlatform {
    struct Step { int x = 0, y = 0; bool left = false; int key = 0; };
    std::vector<Step> steps; int iter = 0, maxPumps = 100000;
    bool createMainWindow(const char*, int, int, bool) override { return true; }
    void destroyMainWindow() override {}
    bool pumpMessages() override { ++iter; return iter < maxPumps; }
    std::uint32_t timeMs() override { return (std::uint32_t)iter * 16u; }
    void sleepMs(std::uint32_t) override {}
    void getMouse(shim::MouseState& o) override { const Step& s = at(); o.x = s.x; o.y = s.y; o.left = s.left; }
    bool keyDown(int vk) override { return at().key == vk; }
    const Step& at() const { static Step z; if (steps.empty()) return z;
        int i = iter < (int)steps.size() ? iter : (int)steps.size() - 1; return steps[i < 0 ? 0 : i]; }
};
constexpr int kRowY0 = 90, kRowH = 30, kValueX = 360;
SeqPlatform::Step OnRow(int i, bool left) {
    SeqPlatform::Step s; s.x = kValueX + 20; s.y = kRowY0 + i * kRowH + 4; s.left = left; return s;
}
SeqPlatform::Step OnBack(int rc, bool left) {
    SeqPlatform::Step s; s.x = 60 + 10; s.y = kRowY0 + rc * kRowH + 16 + 8; s.left = left; return s;
}
std::string GameDir() {
    if (const char* e = std::getenv("GUILD_GAME_DIR")) return e;
    return "europe_guild_1400_original";
}
void DumpPpm(const char* path, const shim::MemoryGraphicsDevice& dev, int w, int h) {
    const auto& fb = dev.lastPresented();
    if (fb.empty()) return;
    const auto* p = reinterpret_cast<const std::uint32_t*>(fb.data());
    FILE* fp = std::fopen(path, "wb");
    if (!fp) return;
    std::fprintf(fp, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) {
        const std::uint32_t c = p[i];
        std::uint8_t rgb[3] = { (std::uint8_t)(c >> 16), (std::uint8_t)(c >> 8), (std::uint8_t)c };
        std::fwrite(rgb, 1, 3, fp);
    }
    std::fclose(fp);
}
} // namespace

TEST(OptionsE2E, RealSfxPageTogglesRealIniSetting) {
    namespace fsx = std::filesystem;
    std::error_code ec;
    const fsx::path gdir = GameDir();
    const fsx::path ini  = gdir / "Gilde.INI";
    const fsx::path gfx  = gdir / "gfx" / "gilde.gfx";
    if (!fsx::exists(ini, ec) || !fsx::exists(gfx, ec)) {
        std::printf("[ SKIP ] real game dir not found (%s); set GUILD_GAME_DIR.\n", gdir.string().c_str());
        CHECK(true); return;
    }

    // Seed the page from the REAL gilde.INI via the reconstructed reader.
    config::IniFile profile;
    CHECK(profile.loadFile(ini.string()));
    config::GfxSettings g; config::SoundSettings s; config::GameSettings m;
    config::ReadGfxAndSoundSettings(profile, g, s, m);
    std::printf("[options-e2e] real Gilde.INI: master_vol=%d sfx_vol=%d msx_vol=%d msx_freq=%d\n",
                (int)s.masterVol, (int)s.sfxVol, (int)s.msxVol, (int)s.msxFreq);
    CHECK(s.masterVol > 0);   // the shipped INI has real volumes

    play::OptionsConfig cfg;
    cfg.page = play::OptionsPage::kSfx;
    cfg.gameDir = gdir.string();
    cfg.fbW = 800; cfg.fbH = 600; cfg.frameCapMs = 0; cfg.maxFrames = 60;
    cfg.gfx = g; cfg.sound = s; cfg.game = m;

    const int beforeMaster = (int)cfg.sound.masterVol;

    shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    // Toggle master_vol (row 0; from 127 -> wraps to min 0), then apply.
    plat.steps = { OnRow(0, false), OnRow(0, true), OnRow(0, false),
                   OnBack(5, false), OnBack(5, true) };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);

    CHECK(r.framesPresented > 0);
    CHECK(!r.cancelled);
    CHECK(r.changed);
    CHECK((int)r.sound.masterVol != beforeMaster);   // a real INI-backed value changed
    std::printf("[options-e2e] master_vol %d -> %d (rendered %d frames over real bg)\n",
                beforeMaster, (int)r.sound.masterVol, r.framesPresented);

    DumpPpm("/tmp/options_sfx_e2e.ppm", dev, 800, 600);
    std::printf("[options-e2e] dumped /tmp/options_sfx_e2e.ppm\n");
}

TEST(OptionsE2E, RealGamePageOverBackgroundIsNonBlank) {
    namespace fsx = std::filesystem;
    std::error_code ec;
    const fsx::path gdir = GameDir();
    if (!fsx::exists(gdir / "gfx" / "gilde.gfx", ec)) {
        std::printf("[ SKIP ] gilde.gfx not found; set GUILD_GAME_DIR.\n");
        CHECK(true); return;
    }
    config::IniFile profile;
    profile.loadFile((gdir / "Gilde.INI").string());
    config::GfxSettings g; config::SoundSettings s; config::GameSettings m;
    config::ReadGfxAndSoundSettings(profile, g, s, m);

    play::OptionsConfig cfg;
    cfg.page = play::OptionsPage::kGame;
    cfg.gameDir = gdir.string();
    cfg.fbW = 800; cfg.fbH = 600; cfg.frameCapMs = 0; cfg.maxFrames = 3;
    cfg.gfx = g; cfg.sound = s; cfg.game = m;

    shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat; plat.steps = { OnRow(0, false) };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(r.framesPresented > 0);

    const auto& fb = dev.lastPresented();
    const auto* p = reinterpret_cast<const std::uint32_t*>(fb.data());
    int lit = 0; for (int i = 0; i < 800 * 600; ++i) if (p[i] & 0x00FFFFFFu) ++lit;
    std::printf("[options-e2e] game page over real bg: %d lit px\n", lit);
    CHECK(lit > 10000);   // the real background fills the frame
    DumpPpm("/tmp/options_game_e2e.ppm", dev, 800, 600);
}
