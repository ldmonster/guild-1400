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
// Geometry mirror of sdl_options_screen.cpp BuildLayout (e2e runs at 800x600 -> ox=oy=0).
constexpr int kDesignW = 800, kDesignH = 600;
struct Geom { int win0X, win0Y, win0W, win0H, win1X, win1Y, sliderX; const int* rowY; };
const int kGfxRowY[9]   = { 16, 48, 96, 160, 128, 192, 240, 272, 304 };
const int kSfxRowY[5]   = { 32, 120, 160, 200, 280 };
const int kGameRowY[11] = { 8, 72, 96, 120, 144, 168, 208, 232, 256, 280, 304 };
Geom GeomFor(play::OptionsPage p) {
    switch (p) {
        case play::OptionsPage::kGfx:  return { 112,120,452,574, 146,171, 208, kGfxRowY };
        case play::OptionsPage::kSfx:  return { 112,120,449,575, 144,168, 208, kSfxRowY };
        case play::OptionsPage::kGame: default: return { 104,120,449,575, 136,168, 208, kGameRowY };
    }
}
constexpr int kBtnW = 110, kBtnH = 33;
play::OptionsPage gPage = play::OptionsPage::kSfx;
int gFbW = 800, gFbH = 600;
SeqPlatform::Step OnRow(int i, bool left) {
    const Geom g = GeomFor(gPage);
    const int ox = (gFbW - kDesignW) / 2, oy = (gFbH - kDesignH) / 2;
    SeqPlatform::Step s; s.x = ox + g.win1X + g.sliderX + 4; s.y = oy + g.win1Y + g.rowY[i]; s.left = left; return s;
}
SeqPlatform::Step OnBack(int /*rc*/, bool left) {
    const Geom g = GeomFor(gPage);
    const int ox = (gFbW - kDesignW) / 2, oy = (gFbH - kDesignH) / 2;
    const int btnY = oy + g.win0Y + g.win0H - kBtnH - 24;
    const int totalW = kBtnW * 2 + 24, firstX = ox + g.win0X + (g.win0W - totalW) / 2;
    SeqPlatform::Step s; s.x = firstX + kBtnW / 2; s.y = btnY + kBtnH / 2; s.left = left; return s;
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

TEST(OptionsE2E, RealSfxPageTogglesRealIniSettingAndPersists) {
    namespace fsx = std::filesystem;
    std::error_code ec;
    const fsx::path gdir = GameDir();
    const fsx::path ini  = gdir / "Gilde.INI";
    const fsx::path gfx  = gdir / "gfx" / "gilde.gfx";
    if (!fsx::exists(ini, ec) || !fsx::exists(gfx, ec)) {
        std::printf("[ SKIP ] real game dir not found (%s); set GUILD_GAME_DIR.\n", gdir.string().c_str());
        CHECK(true); return;
    }

    // SANDBOX the INI: copy the real Gilde.INI into a temp dir and bind the
    // screen to it (cfg.iniDir) — the live app binds straight to <gameDir>
    // and writes the real file (1:1 with WritePrivateProfileStringA), but the
    // test must never mutate the user's INI.
    const fsx::path sandbox = fsx::temp_directory_path() / "guild_opts_e2e_ini";
    fsx::create_directories(sandbox, ec);
    fsx::copy_file(ini, sandbox / "Gilde.INI", fsx::copy_options::overwrite_existing, ec);
    CHECK(!ec);

    // Read the REAL gilde.INI via the reconstructed reader (for the baseline).
    config::IniFile profile;
    CHECK(profile.loadFile(ini.string()));
    config::GfxSettings g; config::SoundSettings s; config::GameSettings m;
    config::ReadGfxAndSoundSettings(profile, g, s, m);
    std::printf("[options-e2e] real Gilde.INI: master_vol=%d sfx_vol=%d msx_vol=%d msx_freq=%d\n",
                (int)s.masterVol, (int)s.sfxVol, (int)s.msxVol, (int)s.msxFreq);
    CHECK(s.masterVol > 0);   // the shipped INI has real volumes

    play::OptionsConfig cfg;
    cfg.page = play::OptionsPage::kSfx;
    cfg.gameDir = gdir.string();        // real assets (menu background)
    cfg.iniDir  = sandbox.string();     // sandboxed persistence target
    cfg.fbW = 800; cfg.fbH = 600; cfg.frameCapMs = 0; cfg.maxFrames = 60;
    gPage = cfg.page; gFbW = cfg.fbW; gFbH = cfg.fbH;

    const int beforeMaster = (int)s.masterVol;

    shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    // Toggle master_vol (row 0; from 127 -> wraps to min 0), then apply.
    plat.steps = { OnRow(0, false), OnRow(0, true), OnRow(0, false),
                   OnBack(5, false), OnBack(5, true) };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);

    CHECK(r.framesPresented > 0);
    CHECK(!r.cancelled);
    CHECK(r.changed);
    CHECK(r.persisted);                              // the bound INI was written
    CHECK((int)r.sound.masterVol != beforeMaster);   // a real INI-backed value changed
    std::printf("[options-e2e] master_vol %d -> %d (rendered %d frames over real bg)\n",
                beforeMaster, (int)r.sound.masterVol, r.framesPresented);

    // The sandbox INI now reloads with the new value through the REAL reader,
    // and the foreign sections of the real file survived the merge.
    config::IniFile after;
    CHECK(after.loadFile((sandbox / "Gilde.INI").string()));
    config::GfxSettings g2; config::SoundSettings s2; config::GameSettings m2;
    config::ReadGfxAndSoundSettings(after, g2, s2, m2);
    CHECK((int)s2.masterVol == (int)r.sound.masterVol);
    CHECK(after.getString("General", "Bildmodus", "") ==
          profile.getString("General", "Bildmodus", "?"));
    CHECK(after.getInt("Network", "Port", -1) == profile.getInt("Network", "Port", -2));
    // The real file itself is untouched.
    config::IniFile realAgain;
    CHECK(realAgain.loadFile(ini.string()));
    CHECK(realAgain.getInt("Sound", "master_vol", -1) == beforeMaster);

    DumpPpm("/tmp/options_sfx_e2e.ppm", dev, 800, 600);
    std::printf("[options-e2e] dumped /tmp/options_sfx_e2e.ppm\n");
    fsx::remove_all(sandbox, ec);
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
    gPage = cfg.page; gFbW = cfg.fbW; gFbH = cfg.fbH;

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
