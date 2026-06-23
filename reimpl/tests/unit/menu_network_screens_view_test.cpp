// Layout / label pin tests for the NETWORK sub-screen 1:1 VIEW:
//   * Menu\CHOOSENETWORK     hub    (gilde.exe 0x529a64)
//   * Menu\CHOOSENETWORK_IP  join   (gilde.exe 0x529074)
//   * Menu\SEARCH_NETWORK    search (gilde.exe 0x529248)
//
// Two halves:
//   1) HEADLESS (no game dir): the renderers must produce a stable, center-
//      translated layout and not crash. Labels are empty (no textbin), the flat
//      fallback art is used; rects are pinned to the form geometry from
//      MENU-SUBSCREENS-GROUNDTRUTH.md.
//   2) ASSET-GUARDED (GUILD_GAME_DIR present): the real `_OPTIONEN_NETZWERK_*`
//      captions resolve (non-empty, rich-text stripped) and the real gfx is used.
//
// The view is a pure framebuffer paint, so we render into a scratch buffer and
// inspect the returned layout + a couple of pixel probes.
#include "tests/framework/test.h"
#include "play/menu_recon_network_screens.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string gameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "europe_guild_1400_original";
}
bool assetsPresent() {
    shim::DiskFileSystem fs(gameDir());
    return fs.exists("gfx/gilde.gfx") && fs.exists("Resources/textbin.BIN");
}

// Native form geometry, center-translated into an 800x600 framebuffer (ox=oy=0).
constexpr int kHubWinX = 144, kHubWinY = 160, kHubWinW = 297;
constexpr int kIpWinX = 152, kIpWinY = 192, kIpWinW = 224;
constexpr int kSrchWinX = 120, kSrchWinY = 120, kSrchWinW = 451, kSrchWinH = 575;

} // namespace

// ===========================================================================
// StripNetRichMarkup — the rich-text code stripper.
// ===========================================================================
TEST(NetView, StripMarkupKeepsBracketedInner) {
    // $Z $[ ... $] $3A $B  -> keep only the bracketed run
    CHECK(play::StripNetRichMarkup("$Z$[Hallo$]$3A$B") == std::string("Hallo"));
    // %ia[ ... ]$A inline button -> keep the label
    CHECK(play::StripNetRichMarkup("%ia[Join game]$A") == std::string("Join game"));
    // $3A prefix line-feed code stripped, trailing kept
    CHECK(play::StripNetRichMarkup("$3ASearching:") == std::string("Searching:"));
    // plain text untouched
    CHECK(play::StripNetRichMarkup("Enter IP:") == std::string("Enter IP:"));
    // $C clear + $A line-feed around text
    CHECK(play::StripNetRichMarkup("$CWaiting$A") == std::string("Waiting"));
    // null safe
    CHECK(play::StripNetRichMarkup(nullptr).empty());
}

// ===========================================================================
// HEADLESS layout pins (no game dir): center-translated form geometry.
// ===========================================================================
TEST(NetView, HubLayoutHeadless) {
    std::vector<u32> fb((std::size_t)800 * 600, 0u);
    play::NetViewContext ctx; ctx.gameDir.clear(); ctx.fbW = 800; ctx.fbH = 600; ctx.hover = -1;
    play::NetHubLayout L = play::RenderNetworkHubView(fb.data(), ctx);

    CHECK(!L.usedArt);                 // no assets -> flat fallback
    CHECK_EQ(L.buttonCount, 3);        // host / search / profile (decompile 0x529a64)
    // window rect == native form (800x600 => ox=oy=0)
    CHECK_EQ(L.window.x, kHubWinX);
    CHECK_EQ(L.window.y, kHubWinY);
    CHECK_EQ(L.window.w, kHubWinW);
    // buttons stacked, equal width, inside the window, increasing y
    for (int i = 0; i < L.buttonCount; ++i) {
        CHECK(L.buttons[i].w > 0);
        CHECK_EQ(L.buttons[i].h, 33);  // _BUTTON_RED native height
        CHECK(L.buttons[i].x >= kHubWinX);
        CHECK(L.buttons[i].x + L.buttons[i].w <= kHubWinX + kHubWinW);
        if (i > 0) CHECK(L.buttons[i].y > L.buttons[i - 1].y);
    }
    // all buttons share one uniform width (main-menu equalization)
    CHECK_EQ(L.buttons[0].w, L.buttons[1].w);
    CHECK_EQ(L.buttons[1].w, L.buttons[2].w);
}

TEST(NetView, IpLayoutHeadless) {
    std::vector<u32> fb((std::size_t)800 * 600, 0u);
    play::NetViewContext ctx; ctx.gameDir.clear(); ctx.fbW = 800; ctx.fbH = 600;
    play::NetIpLayout L = play::RenderNetworkIpView(fb.data(), ctx, "127.0.0.1");

    CHECK(!L.usedArt);
    CHECK_EQ(L.window.x, kIpWinX);
    CHECK_EQ(L.window.y, kIpWinY);
    CHECK_EQ(L.window.w, kIpWinW);
    // prompt at form (16,56), input at (16,88) -> translated (ox=oy=0)
    CHECK_EQ(L.prompt.x, kIpWinX + 16);
    CHECK_EQ(L.prompt.y, kIpWinY + 56);
    CHECK_EQ(L.input.x, kIpWinX + 16);
    CHECK_EQ(L.input.y, kIpWinY + 88);
    // OK button below the input, within the window
    CHECK(L.okButton.y > L.input.y);
    CHECK(L.okButton.x >= kIpWinX);
    CHECK(L.okButton.x + L.okButton.w <= kIpWinX + kIpWinW);
    CHECK_EQ(L.okButton.h, 33);
}

TEST(NetView, SearchLayoutHeadless) {
    std::vector<u32> fb((std::size_t)800 * 600, 0u);
    play::NetViewContext ctx; ctx.gameDir.clear(); ctx.fbW = 800; ctx.fbH = 600;
    std::vector<std::string> servers = { "AUGSBURG", "BERLIN", "KOELN" };
    play::NetSearchLayout L = play::RenderNetworkSearchView(fb.data(), ctx, servers);

    CHECK(!L.usedArt);
    CHECK_EQ(L.window.x, kSrchWinX);
    CHECK_EQ(L.window.y, kSrchWinY);
    CHECK_EQ(L.window.w, kSrchWinW);
    CHECK_EQ(L.rowCount, 3);
    // rows stacked top->bottom, inside the window
    for (int i = 0; i < L.rowCount; ++i) {
        CHECK(L.rows[i].x >= kSrchWinX);
        if (i > 0) CHECK(L.rows[i].y > L.rows[i - 1].y);
    }
    // four-button row near the bottom, left->right
    CHECK(L.connectButton.x < L.refreshButton.x);
    CHECK(L.refreshButton.x < L.directButton.x);
    CHECK(L.directButton.x < L.cancelButton.x);
    CHECK(L.connectButton.y >= kSrchWinY + kSrchWinH - 60);
    CHECK_EQ(L.connectButton.h, 33);
    CHECK_EQ(L.connectButton.w, L.cancelButton.w);  // uniform width
}

// Center-translate: a 1024x768 framebuffer offsets every rect by ((W-800)/2,(H-600)/2)
// and never scales the widget sizes (native_main_menu model).
TEST(NetView, HubCenterTranslateNotScaled) {
    std::vector<u32> fb800((std::size_t)800 * 600, 0u);
    std::vector<u32> fb1024((std::size_t)1024 * 768, 0u);
    play::NetViewContext c8; c8.gameDir.clear(); c8.fbW = 800; c8.fbH = 600;
    play::NetViewContext c10; c10.gameDir.clear(); c10.fbW = 1024; c10.fbH = 768;
    play::NetHubLayout a = play::RenderNetworkHubView(fb800.data(), c8);
    play::NetHubLayout b = play::RenderNetworkHubView(fb1024.data(), c10);

    const int ox = (1024 - 800) / 2, oy = (768 - 600) / 2;
    CHECK_EQ(b.window.x, a.window.x + ox);
    CHECK_EQ(b.window.y, a.window.y + oy);
    CHECK_EQ(b.window.w, a.window.w);                 // size NOT scaled
    CHECK_EQ(b.buttons[0].w, a.buttons[0].w);         // button width NOT scaled
    CHECK_EQ(b.buttons[0].x, a.buttons[0].x + ox);
    CHECK_EQ(b.buttons[0].y, a.buttons[0].y + oy);
}

// Headless render must paint *something* into the framebuffer (not all zero).
TEST(NetView, HeadlessPaintsPixels) {
    std::vector<u32> fb((std::size_t)800 * 600, 0u);
    play::NetViewContext ctx; ctx.gameDir.clear(); ctx.fbW = 800; ctx.fbH = 600;
    play::RenderNetworkHubView(fb.data(), ctx);
    bool any = false;
    for (u32 p : fb) if (p != 0u) { any = true; break; }
    CHECK(any);
}

// ===========================================================================
// ASSET-GUARDED: real localized captions + real gfx.
// ===========================================================================
TEST(NetView, HubLabelsResolveWithAssets) {
    if (!assetsPresent()) return;   // skip cleanly when the install is absent
    std::vector<u32> fb((std::size_t)800 * 600, 0u);
    play::NetViewContext ctx; ctx.gameDir = gameDir(); ctx.fbW = 800; ctx.fbH = 600;
    play::NetHubLayout L = play::RenderNetworkHubView(fb.data(), ctx);

    CHECK(L.usedArt);                      // real _OPTIONEN_PIC decoded
    CHECK(!L.titleLabel.empty());          // _OPTIONEN_NETZWERK_MENUE+0
    for (int i = 0; i < L.buttonCount; ++i)
        CHECK(!L.labels[i].empty());       // MENUE+1..+3
    // captions are rich-text stripped (no leftover markup introducers)
    for (int i = 0; i < L.buttonCount; ++i) {
        CHECK(L.labels[i].find('$') == std::string::npos);
        CHECK(L.labels[i].find('[') == std::string::npos);
    }
}

TEST(NetView, IpLabelsResolveWithAssets) {
    if (!assetsPresent()) return;
    std::vector<u32> fb((std::size_t)800 * 600, 0u);
    play::NetViewContext ctx; ctx.gameDir = gameDir(); ctx.fbW = 800; ctx.fbH = 600;
    play::NetIpLayout L = play::RenderNetworkIpView(fb.data(), ctx, "127.0.0.1");

    CHECK(L.usedArt);
    CHECK(!L.titleLabel.empty());   // JOINEN+0
    CHECK(!L.promptLabel.empty());  // JOINEN+1 ("Enter IP:")
    CHECK(!L.okLabel.empty());      // JOINEN+2
    CHECK(L.promptLabel.find('$') == std::string::npos);
}

TEST(NetView, SearchLabelsResolveWithAssets) {
    if (!assetsPresent()) return;
    std::vector<u32> fb((std::size_t)800 * 600, 0u);
    play::NetViewContext ctx; ctx.gameDir = gameDir(); ctx.fbW = 800; ctx.fbH = 600;
    std::vector<std::string> servers = { "TESTSERVER" };
    play::NetSearchLayout L = play::RenderNetworkSearchView(fb.data(), ctx, servers);

    CHECK(L.usedArt);
    CHECK(!L.titleLabel.empty());    // SUCHE_SERVER+0
    CHECK(!L.connectLabel.empty());  // BUTTON_CONNECT+0
    CHECK(!L.refreshLabel.empty());  // BUTTON_REFRESH+0
    CHECK(!L.directLabel.empty());   // BUTTON_DIRECT+0
    CHECK(!L.cancelLabel.empty());   // BUTTON_CANCEL+0
}

// ===========================================================================
// Driver: RunNetworkScreen (VIBE_Menu_ChooseNetworkMode @0x529a64) — the live
// "Сетевая игра" hub on a headless device. ESC backs out to the menu; a click on
// the JOIN row enters the search sub-view. (Networking itself is rule-6 gated.)
// ===========================================================================
#include "shim_impl/memory_graphics.h"
#include "shim/IPlatform.h"

namespace {
struct NetSeqPlatform : shim::IPlatform {
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
}

TEST(NetView, DriverHubEscBacksOut) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
    NetSeqPlatform plat;
    plat.steps = { {}, {}, { 0, 0, false, 0x1B }, {} };   // a couple frames, then ESC
    plat.maxPumps = 40;
    play::NetworkScreenConfig cfg;
    cfg.gameDir = assetsPresent() ? gameDir() : std::string();
    cfg.fbW = 800; cfg.fbH = 600; cfg.frameCapMs = 0; cfg.maxFrames = 60;
    play::NetworkScreenResult r = play::RunNetworkScreen(dev, plat, cfg);
    CHECK(r.back);                 // ESC -> back to the menu
    CHECK(!r.confirmed);           // no session launched (rule 6)
    CHECK(r.framesPresented > 0);
    CHECK(dev.presentCount() > 0);
}

TEST(NetView, DriverHubClickJoinEntersSearch) {
    if (!assetsPresent()) { std::printf("    (skipped: no game dir)\n"); return; }
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
    // Compute the JOIN (row 1) button centre from a one-shot hub render.
    std::vector<std::uint32_t> fb(800 * 600, 0u);
    play::NetViewContext ctx; ctx.gameDir = gameDir(); ctx.fbW = 800; ctx.fbH = 600;
    play::NetHubLayout hub = play::RenderNetworkHubView(fb.data(), ctx);
    const play::NetViewRect jr = hub.buttons[1];
    NetSeqPlatform plat;
    plat.steps = {
        {}, {},
        { jr.x + jr.w / 2, jr.y + jr.h / 2, false, 0 },   // hover join
        { jr.x + jr.w / 2, jr.y + jr.h / 2, true,  0 },   // click join -> search
        {}, {}, { 0, 0, false, 0x1B },                    // ESC search -> hub
        {}, { 0, 0, false, 0x1B },                        // ESC hub -> back
    };
    plat.maxPumps = 60;
    play::NetworkScreenConfig cfg;
    cfg.gameDir = gameDir(); cfg.fbW = 800; cfg.fbH = 600; cfg.frameCapMs = 0; cfg.maxFrames = 120;
    play::NetworkScreenResult r = play::RunNetworkScreen(dev, plat, cfg);
    CHECK_EQ(r.chosenMode, 1);     // JOIN/SEARCH was activated
    CHECK(r.back);                 // and we eventually backed out
}
