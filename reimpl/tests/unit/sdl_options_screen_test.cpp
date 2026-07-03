// Unit tests for play::RunOptionsScreen — the native Game/Gfx/Sfx options pages.
// Backend-agnostic: MemoryGraphicsDevice + a sequenced test platform.
//
// The row tables are asserted against the ORIGINAL forms (gilde.exe):
//   VIBE_Menu_RunOptionsGfx  @0x56c21c   VIBE_Menu_RunOptionsSfx @0x56c808
//   VIBE_Menu_RunOptionsGame @0x56cc44
#include "test.h"
#include "play/sdl_options_screen.h"
#include "play/settings_io.h"
#include "shim_impl/memory_graphics.h"
#include "shim/IPlatform.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>

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

// ---- Geometry mirror: tracks sdl_options_screen.cpp's real-FRM2 layout. ----
// The renderer derives every hit rect from the form windows (center-translated
// only). These mirror BuildLayout/RowTrackRect/HitOk/HitCancel for the test
// framebuffer (fbW x fbH). Pages: Gfx/Sfx/Game (page index 1/2/0 -> see below).
constexpr int kDesignW = 800, kDesignH = 600;
struct Geom { int win0X, win0Y, win0W, win0H, win1X, win1Y, sliderX, range; const int* rowY; int rowCount; };
const int kGfxRowY[9]   = { 16, 48, 96, 160, 128, 192, 240, 272, 304 };
const int kSfxRowY[5]   = { 32, 120, 160, 200, 280 };
const int kGameRowY[11] = { 8, 72, 96, 120, 144, 208, 232, 168, 256, 280, 304 };
Geom GeomFor(play::OptionsPage p) {
    switch (p) {
        case play::OptionsPage::kGfx:  return { 112,120,452,574, 146,171, 208, 160, kGfxRowY,  9 };
        case play::OptionsPage::kSfx:  return { 112,120,449,575, 144,168, 208, 140, kSfxRowY,  5 };
        case play::OptionsPage::kGame: default: return { 104,120,449,575, 136,168, 208, 140, kGameRowY, 11 };
    }
}
constexpr int kSliderTrackW = 100, kSliderTrackH = 18, kBtnW = 96, kBtnH = 33;
constexpr int kCapL = 68, kCapR = 67;   // slider +/- end-cap widths (asset-less)

// Active page + framebuffer for the geometry mirror (tests set these via BaseCfg).
play::OptionsPage gPage = play::OptionsPage::kSfx;
int gFbW = 640, gFbH = 480;

// Click row i: part 0 = MINUS (left cap), 1 = track centre, 2 = PLUS (right cap).
// (Bool rows toggle on any in-band click.)
SeqPlatform::Step OnPart(int i, int part, bool left) {
    const Geom g = GeomFor(gPage);
    const int ox = (gFbW - kDesignW) / 2, oy = (gFbH - kDesignH) / 2;
    const int sx = ox + g.win1X + g.sliderX;
    int px = (part == 0) ? sx + kCapL / 2
           : (part == 2) ? sx + kCapL + g.range + kCapR / 2
                         : sx + kCapL + g.range / 2;
    SeqPlatform::Step s; s.x = px; s.y = oy + g.win1Y + g.rowY[i]; s.left = left; return s;
}
SeqPlatform::Step OnRow(int i, bool left)   { return OnPart(i, 2, left); }  // default: plus
SeqPlatform::Step OnPlus(int i, bool left)  { return OnPart(i, 2, left); }
SeqPlatform::Step OnMinus(int i, bool left) { return OnPart(i, 0, left); }
// Mirrors BuildLayout's OK/Cancel rects: GeomFor stores win0W/win0H swapped, so
// panelW=win0H, panelH=win0W; the panel is screen-centered; gap=78, btnY uses -21.
SeqPlatform::Step OnBack(int /*rowCount*/, bool left) {
    const Geom g = GeomFor(gPage);
    const int oy = (gFbH - kDesignH) / 2;
    const int panelW = g.win0H, panelH = g.win0W;
    const int win0ScrX = (gFbW - panelW) / 2, win0ScrY = oy + g.win0Y;
    const int btnY = win0ScrY + panelH - kBtnH - 21;
    const int gap = 78, totalW = kBtnW * 2 + gap;
    const int firstX = win0ScrX + (panelW - totalW) / 2;
    SeqPlatform::Step s; s.x = firstX + kBtnW / 2; s.y = btnY + kBtnH / 2; s.left = left; return s;
}
SeqPlatform::Step OnCancel(bool left) {
    const Geom g = GeomFor(gPage);
    const int oy = (gFbH - kDesignH) / 2;
    const int panelW = g.win0H, panelH = g.win0W;
    const int win0ScrX = (gFbW - panelW) / 2, win0ScrY = oy + g.win0Y;
    const int btnY = win0ScrY + panelH - kBtnH - 21;
    const int gap = 78, totalW = kBtnW * 2 + gap;
    const int firstX = win0ScrX + (panelW - totalW) / 2;
    SeqPlatform::Step s; s.x = firstX + kBtnW + gap + kBtnW / 2; s.y = btnY + kBtnH / 2; s.left = left; return s;
}

play::OptionsConfig BaseCfg(play::OptionsPage p) {
    play::OptionsConfig c; c.page = p;
    c.fbW = 640; c.fbH = 480; c.frameCapMs = 0; c.maxFrames = 60;
    gPage = p; gFbW = c.fbW; gFbH = c.fbH;
    return c;
}
} // namespace

TEST(OptionsUnit, RowsMatchOriginalBuildOrderAndRanges) {
    config::GfxSettings g; config::SoundSettings s; config::GameSettings m;
    auto gfx = play::OptionsRowsFor(play::OptionsPage::kGfx, g, s, m);
    auto sfx = play::OptionsRowsFor(play::OptionsPage::kSfx, g, s, m);
    auto game = play::OptionsRowsFor(play::OptionsPage::kGame, g, s, m);

    // Gfx @0x56c21c: children 0..8 in build order.
    CHECK(gfx.size() == 9);
    const char* gfxKeys[9] = { "cur_res", "details", "texture_scale", "floor_lod",
                               "floor_mipmapping", "lod_handling", "shadow_detail",
                               "fog_plane", "camera_limits" };
    for (int i = 0; i < 9; ++i) CHECK_EQ(std::string(gfx[i].key), std::string(gfxKeys[i]));
    CHECK(gfx[0].kind == play::OptionKind::kCycle);
    CHECK(gfx[0].maxV == 2);              // caps default: byte_62D59A + byte_62D59B set
    CHECK(!gfx[0].hidden);                // byte_63CC40 clear -> visible
    CHECK(gfx[1].maxV == 2);              // details   0..2
    CHECK(gfx[5].maxV == 1);              // lod_handling 0..1 (SetValueOrText max 1!)
    CHECK(gfx[7].minV == 50 && gfx[7].maxV == 100);  // gamma slider (50, 100)
    CHECK(gfx[7].value == 100);           // seed = 100 - fog_plane (fog_plane 0)
    CHECK(gfx[8].maxV == 2);              // camera_limits 0..2 (a 3-option cycle)

    // Sfx @0x56c808: children 0..4.
    CHECK(sfx.size() == 5);
    CHECK(std::string(sfx[0].key) == "master_vol");
    CHECK(sfx[0].kind == play::OptionKind::kStep);
    CHECK(sfx[0].maxV == 127);
    CHECK(std::string(sfx[4].key) == "msx_freq");
    CHECK(sfx[4].maxV == 4);

    // Game @0x56cc44: children 0,1,2,3,4,5,6,9,10,11,12.
    CHECK(game.size() == 11);
    const char* gameKeys[11] = { "speed", "mouse_speed", "scroll_speed", "camera_speed",
                                 "invert_mouse", "show_cursor_txt", "show_geb_info",
                                 "panel_mode", "help_events", "hints", "panel_help" };
    for (int i = 0; i < 11; ++i) CHECK_EQ(std::string(game[i].key), std::string(gameKeys[i]));
    CHECK(game[0].maxV == 160);
    CHECK(game[1].maxV == 500);
    CHECK(game[4].hidden);                // invert_mouse: built then force-hidden
    CHECK(game[7].maxV == 4);             // panel_mode 0..4 (5-line dropdown)
}

TEST(OptionsUnit, GfxRowCapsControlResolutionRangeAndVisibility) {
    config::GfxSettings g; config::SoundSettings s; config::GameSettings m;
    play::OptionsRowCaps caps;
    caps.resCap1024 = false; caps.resCap1280 = false;
    auto r0 = play::OptionsRowsFor(play::OptionsPage::kGfx, g, s, m, caps);
    CHECK(r0[0].maxV == 0);               // neither cap byte set -> v21 = 0
    caps.resCap1024 = true;
    auto r1 = play::OptionsRowsFor(play::OptionsPage::kGfx, g, s, m, caps);
    CHECK(r1[0].maxV == 1);               // byte_62D59A -> v21 = 1
    caps.inGame = true;
    auto r2 = play::OptionsRowsFor(play::OptionsPage::kGfx, g, s, m, caps);
    CHECK(r2[0].hidden);                  // byte_63CC40 -> SetVisibleRecursive(0)
}

TEST(OptionsUnit, SfxClickStepsMasterVolumeAndApplies) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kSfx);
    cfg.sound.masterVol = 0;  // start at min
    // Click master_vol once (row 0 -> +16), then Back to apply.
    plat.steps = { OnRow(0, false), OnRow(0, true), OnRow(0, false),
                   OnBack(5, false), OnBack(5, true) };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(r.back);
    CHECK(!r.cancelled);
    CHECK(r.changed);
    CHECK(r.sound.masterVol == 16);   // 0 -> +16 step
    CHECK(r.lastToggledRow == 0);
}

TEST(OptionsUnit, SfxApplyReachesVolumeSink) {
    // Task: the changed SoundSettings must reach audio at the API level — the
    // native analogue of 0x56cc1f WriteGfxSettings -> Audio_ApplyVolumeSettings.
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kSfx);
    cfg.sound.masterVol = 0;
    int applied = -1;
    play::OptionsApplyHooks().applyVolumeSettings =
        [&](const config::SoundSettings& s) { applied = s.masterVol; };
    plat.steps = { OnRow(0, false), OnRow(0, true), OnRow(0, false),
                   OnBack(5, false), OnBack(5, true) };
    play::RunOptionsScreen(dev, plat, cfg);
    play::OptionsApplyHooks().applyVolumeSettings = nullptr;
    CHECK(applied == 16);             // the post-apply SoundSettings reached the sink
}

TEST(OptionsUnit, GameToggleFlipsShowCursorText) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kGame);
    cfg.game.showCursorTxt = 1;    // row 5 = show_cursor_txt toggle (child 5)
    plat.steps = { OnRow(5, false), OnRow(5, true), OnRow(5, false),
                   OnBack(11, false), OnBack(11, true) };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(!r.cancelled);
    CHECK(r.changed);
    CHECK(r.game.showCursorTxt == 0);   // toggled off
}

TEST(OptionsUnit, GameInvertMouseRowHiddenAndForcedZero) {
    // 0x56cf04: the invert_mouse widget is built then SetVisibleRecursive(0);
    // 0x56d1d2: byte_1233568 = 0 on OK regardless of any value.
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kGame);
    cfg.game.invertMouse = 1;   // even a (hand-edited) nonzero seed...
    // Click squarely on row 4 — the hidden row must NOT actuate.
    plat.steps = { OnRow(4, false), OnRow(4, true), OnRow(4, false),
                   OnBack(11, false), OnBack(11, true) };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(!r.cancelled);
    CHECK(r.lastToggledRow == -1);      // hidden row never hit
    CHECK(r.game.invertMouse == 0);     // ...is forced back to 0 on apply
}

TEST(OptionsUnit, GfxResolutionCycleUpdatesDerivedSize) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kGfx);
    cfg.gfx.curRes = 0;  // 800x600; the cur_res row is BUILD-ORDER row 0 now
    plat.steps = { OnRow(0, false), OnRow(0, true), OnRow(0, false),
                   OnBack(9, false), OnBack(9, true) };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(!r.cancelled);
    CHECK(r.resChanged);
    CHECK(r.gfx.curRes == 1);          // cycled to idx 1
    // Derived size mirrors the real (interleaved height,width) res table @0x63D70C:
    int ew = 0, eh = 0; config::ResolutionForIndex(1, &ew, &eh);
    CHECK(r.gfx.resWidth == ew);
    CHECK(r.gfx.resHeight == eh);
    CHECK(ew == 768 && eh == 1024);
}

TEST(OptionsUnit, GfxGammaStepInvertsIntoFogPlane) {
    // The gamma slider stores fog_plane INVERTED: live in [50,100], saved
    // fog_plane = 50 - (live - 50)  (0x56c79d).
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kGfx);
    cfg.gfx.fogPlane = 0;              // seed -> slider 100 (the max)
    // MINUS one step on the gamma row (row 7): slider 100 -> 90 -> fog_plane
    // = 50 - (90 - 50) = 10.
    plat.steps = { OnMinus(7, false), OnMinus(7, true), OnMinus(7, false),
                   OnBack(9, false), OnBack(9, true) };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(!r.cancelled);
    CHECK(r.changed);
    CHECK((int)r.gfx.fogPlane == 10);  // 50 - (90 - 50) = 10
}

TEST(OptionsUnit, EscCancelsAndDiscardsChanges) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kSfx);
    cfg.sound.masterVol = 0;
    SeqPlatform::Step esc; esc.key = 0x1B;
    // Toggle then ESC: the edit is made but NOT applied to res.sound.
    plat.steps = { OnRow(0, false), OnRow(0, true), OnRow(0, false), esc };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(r.cancelled);
    CHECK(r.quitByEsc);
    CHECK(r.sound.masterVol == 0);     // discarded — config unchanged
    CHECK(!r.persisted);
}

TEST(OptionsUnit, CancelButtonDiscardsLikeEsc) {
    // The _BUTTON_RED Cancel (_OPTIONEN_BUTTONS+1) discards the edit, same as ESC.
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kSfx);
    cfg.sound.masterVol = 0;
    plat.steps = { OnRow(0, false), OnRow(0, true), OnRow(0, false),
                   OnCancel(false), OnCancel(true) };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(r.cancelled);
    CHECK(r.sound.masterVol == 0);     // discarded
    CHECK(!r.persisted);
}

TEST(OptionsUnit, BackWithoutEditsReturnsUnchanged) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kGame);
    plat.steps = { OnBack(11, false), OnBack(11, true) };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(!r.cancelled);
    CHECK(!r.changed);
}

TEST(OptionsUnit, RendersFramesAndIsDeterministic) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat; plat.steps = { OnRow(0, false) };  // hover, never click
    auto cfg = BaseCfg(play::OptionsPage::kGfx);
    play::OptionsResult a = play::RunOptionsScreen(dev, plat, cfg);
    shim::MemoryGraphicsDevice dev2; CHECK(dev2.init(640, 480, 32, false));
    SeqPlatform plat2; plat2.steps = { OnRow(0, false) };
    play::OptionsResult b = play::RunOptionsScreen(dev2, plat2, cfg);
    CHECK(a.framesPresented > 0);
    CHECK(a.framesPresented == b.framesPresented);
    CHECK(a.changed == b.changed);
}

TEST(OptionsUnit, IniBindingLoadsSeedAndPersistsOnApply) {
    // Screen-scripted persistence: a click mutates the INI-bound setting and
    // Back/OK persists it through the real serializer into the bound file —
    // preserving every line the serializer does not own.
    namespace fsx = std::filesystem;
    const fsx::path dir = fsx::temp_directory_path() / "guild_opts_unit_ini";
    std::error_code ec; fsx::create_directories(dir, ec);
    const fsx::path ini = dir / "Gilde.INI";
    {
        std::ofstream f(ini, std::ios::binary);
        f << "[General]\r\nBildmodus=FULLSCREEN\r\n\r\n"
             "[Sound]\r\n; the master volume\r\nmaster_vol=32\r\nsfx_vol=20\r\n\r\n"
             "[Network]\r\nPort=7531\r\n";
    }

    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kSfx);
    cfg.iniDir = dir.string();                 // bindIni defaults true
    // Loaded seed (32) is used, NOT cfg.sound (left default 0): click row 0
    // (32 -> 48), then apply.
    plat.steps = { OnRow(0, false), OnRow(0, true), OnRow(0, false),
                   OnBack(5, false), OnBack(5, true) };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(!r.cancelled);
    CHECK(r.persisted);
    CHECK((int)r.sound.masterVol == 48);       // seed 32 + step 16

    // Reload through the real reader: the mutation persisted.
    std::ifstream f(ini, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(text.find("master_vol=48") != std::string::npos);
    // Untouched lines preserved byte-for-byte.
    CHECK(text.find("; the master volume") != std::string::npos);
    CHECK(text.find("Bildmodus=FULLSCREEN") != std::string::npos);
    CHECK(text.find("Port=7531") != std::string::npos);

    fsx::remove_all(dir, ec);
}

TEST(OptionsUnit, IniBindingEscDoesNotWriteFile) {
    namespace fsx = std::filesystem;
    const fsx::path dir = fsx::temp_directory_path() / "guild_opts_unit_ini2";
    std::error_code ec; fsx::create_directories(dir, ec);
    const fsx::path ini = dir / "Gilde.INI";
    const std::string before = "[Sound]\r\nmaster_vol=32\r\n";
    { std::ofstream f(ini, std::ios::binary); f << before; }

    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kSfx);
    cfg.iniDir = dir.string();
    SeqPlatform::Step esc; esc.key = 0x1B;
    plat.steps = { OnRow(0, false), OnRow(0, true), OnRow(0, false), esc };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(r.cancelled);
    CHECK(!r.persisted);
    std::ifstream f(ini, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(text == before);                     // cancel never touches the file

    fsx::remove_all(dir, ec);
}

// ---- HARDENING (wave-12): value extremes + out-of-range seeds ----

// The PLUS button at max CLAMPS (the engine's +/- buttons do not wrap); every
// result stays within [minV,maxV] — no overshoot, no OOB on the row table.
TEST(OptionsUnit, StepClampsAtMaxStaysInRange) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kSfx);
    cfg.sound.masterVol = 112;   // 112 + 16 = 128 > 127 -> clamps to 127 (max)
    plat.steps = { OnPlus(0, false), OnPlus(0, true), OnPlus(0, false),
                   OnBack(5, false), OnBack(5, true) };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(!r.cancelled);
    CHECK(r.sound.masterVol == 127);
    CHECK(r.sound.masterVol >= 0 && r.sound.masterVol <= 127);
}

// Out-of-range seed values (a corrupt-looking config) must render and actuate
// without faulting; the value text is snprintf'd into a small fixed buffer.
TEST(OptionsUnit, OutOfRangeSeedValuesRenderSafely) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kSfx);
    cfg.bindIni = false;
    cfg.sound.masterVol = 32000;     // wildly out of [0,127]
    cfg.sound.sfxVol = -5000;
    cfg.sound.msxFreq = 99;
    SeqPlatform::Step esc; esc.key = 0x1B;
    plat.steps = { OnRow(0, false), OnRow(0, true), OnRow(0, false), esc };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(r.cancelled);              // ESC -> cancel; nothing committed
    CHECK(r.framesPresented > 0);
}

// A clicked out-of-range value clamps into [minV,maxV] on actuate (the trailing
// clamp in ActuateValue), so the persisted value is always in-range.
TEST(OptionsUnit, ClickActuatesOutOfRangeSeedIntoRange) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kGame);
    cfg.bindIni = false;
    cfg.game.speed = 100000;        // far above the 160 max
    plat.steps = { OnRow(0, false), OnRow(0, true), OnRow(0, false),
                   OnBack(11, false), OnBack(11, true) };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(!r.cancelled);
    CHECK(r.game.speed >= 0 && r.game.speed <= 160);
}

// Tiny framebuffer: the panel + every row label must clip cleanly (DrawGlyph
// right/bottom clip). ASAN guards any scratch overrun.
TEST(OptionsUnit, TinyBufferNoOob) {
    for (play::OptionsPage p : { play::OptionsPage::kGame, play::OptionsPage::kGfx,
                                 play::OptionsPage::kSfx }) {
        shim::MemoryGraphicsDevice dev; CHECK(dev.init(48, 40, 32, false));
        SeqPlatform plat;
        SeqPlatform::Step esc; esc.key = 0x1B;
        plat.steps = { esc };
        auto cfg = BaseCfg(p);
        cfg.bindIni = false;
        cfg.fbW = 48; cfg.fbH = 40; cfg.maxFrames = 4;
        play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
        CHECK(r.cancelled);
        CHECK(r.framesPresented > 0);
    }
}
