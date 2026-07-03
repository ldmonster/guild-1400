// Real-asset e2e for the perspective screen (VIBE_Menu_RunChooseHistory @0x52d684).
// Loads the shipped `_M0_HISTORIE` markup + the three `_M0_HISTORIE_MODUS` mode names from
// textbin_deutsch.BIN, asserts the assembled content (3 mode rows with the real names
// substituted + a back row), the row->History-flag mapping, and a non-blank render.
#include "tests/framework/test.h"
#include "play/sdl_choosehistory_screen.h"
#include "play/menu_assets.h"
#include "shim_impl/disk_filesystem.h"
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
using namespace guild;
static std::string GameDir(){ if(const char*e=std::getenv("GUILD_GAME_DIR"))return e; return "europe_guild_1400_original";}

TEST(ChooseHistoryScreen, RealPerspectiveContent) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/textbin_deutsch.BIN")) {
        std::printf("  [skip] ChooseHistoryScreen: game dir absent\n"); CHECK(true); return; }

    play::CharIntroContent c;
    CHECK(play::LoadChooseHistoryContent(GameDir(), c));
    CHECK(!c.heading.empty());
    CHECK_EQ((int)c.options.size(), 4);                 // 3 modes + back
    CHECK_EQ((int)c.selectable.size(), 4);
    CHECK(c.selectable[0]); CHECK(c.selectable[1]); CHECK(c.selectable[2]);
    CHECK(!c.selectable[3]);                            // back (%in)
    // The `%ia[%s]` slots were filled with the real mode names — no placeholder remains.
    for (int i = 0; i < 3; ++i) {
        CHECK(c.options[i] != "%s");
        CHECK(!c.options[i].empty());
    }

    // Row -> History flag mapping (id0->1, id1->2, id2->0).
    CHECK_EQ(play::ChooseHistory_RowToFlag(0), 1);
    CHECK_EQ(play::ChooseHistory_RowToFlag(1), 2);
    CHECK_EQ(play::ChooseHistory_RowToFlag(2), 0);

    // Render one frame; the panel draws real pixels.
    const int W = 800, H = 600;
    play::CharIntroLayout L = play::CharIntroComputeLayout(W, H, (int)c.options.size());
    play::MenuAssets assets; bool ha = assets.Load(fs);
    std::vector<std::uint32_t> px((std::size_t)W * H, 0u);
    play::RenderCharIntroFrame(px.data(), W, H, c, /*hover=*/1, /*seed=*/0, ha ? &assets : nullptr);
    long nonzero = 0;
    for (int y = L.py; y < L.py + L.ph; ++y)
        for (int x = L.px; x < L.px + L.pw; ++x)
            if ((px[(std::size_t)y * W + x] & 0x00FFFFFFu) != 0) ++nonzero;
    CHECK(nonzero > 1000);
}

// Tasks screen (VIBE "Ваши задания" / _M0_AUFTRAEGE) — shown after factual history.
TEST(ChooseTasksScreen, RealTaskContent) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/textbin_deutsch.BIN")) {
        std::printf("  [skip] ChooseTasksScreen: game dir absent\n"); CHECK(true); return; }

    play::CharIntroContent c;
    CHECK(play::LoadChooseTasksContent(GameDir(), c));
    CHECK(!c.heading.empty());
    CHECK_EQ((int)c.options.size(), 7);                 // 6 task modes + back
    CHECK_EQ((int)c.selectable.size(), 7);
    for (int i = 0; i < 6; ++i) {                       // the %ia[%s] slots got real names
        CHECK(c.selectable[i]);
        CHECK(c.options[i] != "%s");
        CHECK(!c.options[i].empty());
    }
    CHECK(!c.selectable[6]);                            // appended back row

    // The 7-button column: equal width (widest label), centred at x=W/2, top y=246, pitch 40.
    const int W = 800, H = 600;
    play::MenuAssets assets; bool ha = assets.Load(fs);
    std::vector<play::NewGameButtonRect> rects =
        play::ChooseHistoryButtonRects(W, H, c, ha ? &assets : nullptr, play::kChooseTasksBtnTop0);
    CHECK_EQ((int)rects.size(), 7);
    CHECK_EQ(rects[0].y, 246);                          // button 0 top (design == native here)
    CHECK_EQ(rects[1].y - rects[0].y, 40);              // 40px pitch
    for (const auto& r : rects) {
        CHECK_EQ(r.x + r.w / 2, W / 2);                 // centred on screen
        CHECK_EQ(r.w, rects[0].w);                      // uniform width
    }

    // Render one frame; real pixels drew.
    std::vector<std::uint32_t> px((std::size_t)W * H, 0u);
    play::RenderChooseHistoryFrame(px.data(), W, H, c, /*hover=*/0, /*seed=*/-1,
                                   ha ? &assets : nullptr, /*backdrop=*/nullptr,
                                   GameDir(), play::kChooseTasksBtnTop0);
    long nonzero = 0;
    for (std::size_t i = 0; i < (std::size_t)W * H; ++i)
        if ((px[i] & 0x00FFFFFFu) != 0) ++nonzero;
    CHECK(nonzero > 1000);
}
