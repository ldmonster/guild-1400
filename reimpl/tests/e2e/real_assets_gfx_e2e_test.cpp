// Real-asset e2e: drive the reconstructed gfx-catalogue loader
// (VIBE_Gui_LoadGfxFile / Form_LoadFromBuffer @0x41b888) against the REAL shipped
// `gfx/gilde.gfx` (objectCount = 1806, 84-byte records). Verifies the full table
// populates and spot-checks record names/fields.
//
// GUARDED: if the shipped asset folder isn't present the test passes trivially so
// the suite stays green everywhere (CI without the game files).
#include "tests/framework/test.h"

#include "gui/form_loader.h"
#include "gui/form.h"
#include "gui/window.h"
#include "gui/object.h"

#include "shim_impl/disk_filesystem.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::gui;

// Renderer/property edge stubs the gui/form_loader TU references (RegisterGfxState
// is weak in form_loader.cpp; we count its real calls here to validate the walk).
namespace {
int g_realGfxStateCount = 0;
}
namespace guild::gui {
void RegisterGfxState(int /*gfxIndex*/) { ++g_realGfxStateCount; }
}

static const char* kRoot =
    "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";

static bool assetsPresent() {
    guild::shim::DiskFileSystem fs(kRoot);
    return fs.exists("Resources/forms.BIN");
}

// Slurp a loose file through the shim filesystem.
static bool SlurpFile(guild::shim::DiskFileSystem& fs, const char* path,
                      std::vector<u8>& out, std::size_t cap = 0) {
    guild::shim::IFile* f = fs.open(path, "rb");
    if (!f) return false;
    std::int64_t sz = f->size();
    std::size_t want = (cap && (std::size_t)sz > cap) ? cap : (std::size_t)sz;
    out.resize(want);
    std::size_t got = f->read(out.data(), want);
    out.resize(got);
    fs.close(f);
    return true;
}

TEST(RealAssetsGfx, LoadGildeGfxFullTable) {
    if (!assetsPresent()) { CHECK(true); return; } // skipped: no assets

    guild::shim::DiskFileSystem fs(kRoot);

    // gilde.gfx is ~59 MB on disk (table + raw gfx data). The catalogue table is
    // only the first 4 + 1806*84 = 151708 bytes; Form_LoadFromBuffer reads exactly
    // that many. Slurp a generous prefix (the trailing data is ignored by the parse,
    // which only consumes the declared record count).
    const std::size_t kTableBytes = 4u + 1806u * 84u; // 151708
    std::vector<u8> buf;
    CHECK(SlurpFile(fs, "gfx/gilde.gfx", buf, kTableBytes));
    CHECK(buf.size() >= kTableBytes);

    // The recovered header: u32 objectCount @+0, then the first record name @+4.
    u32 count = 0; std::memcpy(&count, buf.data(), 4);
    CHECK_EQ(count, 1806u);
    CHECK(std::strncmp((const char*)buf.data() + 4, "_WIN_BORDER", 11) == 0);

    // ---- Drive the reconstructed loader on the REAL bytes. ----
    ResetGuiState();
    ResetGfxObjects();
    g_realGfxStateCount = 0;

    bool ok = Form_LoadFromBuffer(buf.data(), buf.size(), /*initTables=*/true);
    CHECK(ok);
    CHECK_EQ(g_gfxObjectCount, 1806);

    // Table baseline init stamped each Form/Window/Widget slot with its own index.
    CHECK_EQ(g_forms[0].dw[0], 0);
    CHECK_EQ(g_forms[47].dw[0], 47);
    CHECK_EQ(g_windows[0].at<i32>(0), 0);
    CHECK_EQ(g_windows[95].at<i32>(0), 95);
    CHECK_EQ(g_widgets[0].marker(), 0);
    CHECK_EQ(g_widgets[511].marker(), 511);

    // ---- Spot-check record names (name lives at record +0). ----
    // The records are copied verbatim into g_gfxObjects[i].raw; the leading bytes of
    // each record are the NUL-terminated object name.
    auto recName = [](int i) { return (const char*)g_gfxObjects[i].raw; };
    CHECK(std::strcmp(recName(0), "_WIN_BORDER") == 0);
    CHECK(std::strcmp(recName(1), "_WIN_BORDER+1") == 0);
    CHECK(std::strcmp(recName(2), "_WIN_BORDER+2") == 0);
    CHECK(std::strcmp(recName(100), "_SLIDER_WAAGERECHT_BLACK_SMALL") == 0);
    CHECK(std::strcmp(recName(1805), "_COMBAT_TAKTIK+6") == 0);

    // ---- Spot-check fields the GUI cluster reads. ----
    // Record 0 (_WIN_BORDER): flag bit 0x1 set + non-zero scene handle (5425).
    CHECK_EQ(g_gfxObjects[0].flags() & kGfxFlagHasState, kGfxFlagHasState);
    CHECK_EQ(g_gfxObjects[0].sceneHandle(), 5425);
    // width() is the 16.16 metric at +78; record 0 = 8<<16 = 8 px.
    CHECK_EQ(g_gfxObjects[0].width() >> 16, 8);
    // Record 1 is a passive frame: no flag, no scene.
    CHECK_EQ(g_gfxObjects[1].flags() & kGfxFlagHasState, 0);
    CHECK_EQ(g_gfxObjects[1].sceneHandle(), 0);

    // ---- The post-read walk registered a scene-state for every drawable source. ----
    // 35 records have (flags & 0x1) && sceneHandle != 0 in the shipped file.
    CHECK_EQ(g_realGfxStateCount, 35);
}
