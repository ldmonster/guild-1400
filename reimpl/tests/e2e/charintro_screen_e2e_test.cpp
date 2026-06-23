// Real-asset e2e for the difficulty screen (VIBE_Menu_ChooseCharacterIntroVariant
// @0x52e4e0). Loads the shipped `_M0_DIFFICULTY` markup from textbin_deutsch.BIN and
// renders one frame, asserting the real content (5 difficulty rows + back) and a
// non-blank panel. GUARDED: skips cleanly when the game dir is absent.
#include "tests/framework/test.h"
#include "play/sdl_charintro_screen.h"
#include "play/menu_assets.h"
#include "shim_impl/disk_filesystem.h"
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
using namespace guild;
static std::string GameDir(){ if(const char*e=std::getenv("GUILD_GAME_DIR"))return e; return "europe_guild_1400_original";}

TEST(CharIntroScreen, RealDifficultyContentRenders) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/textbin_deutsch.BIN")) {
        std::printf("  [skip] CharIntroScreen: game dir absent\n"); CHECK(true); return; }

    play::CharIntroContent c;
    CHECK(play::LoadCharIntroContent(GameDir(), c));   // the real _M0_DIFFICULTY markup
    CHECK(!c.heading.empty());
    CHECK_EQ((int)c.options.size(), 6);                // 5 difficulty + back
    CHECK_EQ((int)c.selectable.size(), 6);
    for (int i = 0; i < 5; ++i) CHECK(c.selectable[i]);
    CHECK(!c.selectable[5]);                            // %in back

    // Layout is well-formed and hit-tests the rows.
    const int W = 800, H = 600;
    play::CharIntroLayout L = play::CharIntroComputeLayout(W, H, (int)c.options.size());
    int rx, ry, rw, rh; L.RowRect(2, rx, ry, rw, rh);
    CHECK_EQ(L.HitRow(rx + rw / 2, ry + rh / 2), 2);    // centre of row 2 hits row 2
    CHECK_EQ(L.HitRow(0, 0), -1);                       // corner hits nothing

    // Render one frame and confirm the panel area is not blank.
    play::MenuAssets assets; bool ha = assets.Load(fs);
    std::vector<std::uint32_t> px((std::size_t)W * H, 0u);
    play::RenderCharIntroFrame(px.data(), W, H, c, /*hover=*/2, /*seed=*/1, ha ? &assets : nullptr);
    long nonzero = 0;
    for (int y = L.py; y < L.py + L.ph; ++y)
        for (int x = L.px; x < L.px + L.pw; ++x)
            if ((px[(std::size_t)y * W + x] & 0x00FFFFFFu) != 0) ++nonzero;
    CHECK(nonzero > 1000);   // the panel + text drew real pixels
}
