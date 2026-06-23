// guild::play — native perspective screen (VIBE_Menu_RunChooseHistory @0x52d684).
// Real `_M0_HISTORIE` markup + the three `_M0_HISTORIE_MODUS` mode names, rendered with the
// difficulty screen's primitives; returns the picked History flag (1/2/0).
#include "play/sdl_choosehistory_screen.h"

#include "shim/IGraphicsDevice.h"
#include "shim/IPlatform.h"
#include "shim_impl/disk_filesystem.h"
#include "io/archive_mount.h"
#include "gui/text_load.h"
#include "gui/text/textdb.h"
#include "gui/choosehistory_run.h"     // ChooseHistory_SeedIndex
#include "play/menu_assets.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace guild::play {

namespace {
constexpr int kVkEsc = 0x1B;

void BlitToDevice(const std::uint32_t* src, int w, int h, shim::IGraphicsDevice& dev) {
    shim::Surface* bb = dev.backbuffer();
    if (!bb || !bb->pixels) return;
    const int W = bb->width < w ? bb->width : w;
    const int H = bb->height < h ? bb->height : h;
    auto* base = static_cast<std::uint8_t*>(bb->pixels);
    if (bb->bpp == 32) {
        for (int y = 0; y < H; ++y)
            std::memcpy(base + (std::size_t)y * bb->pitch, src + (std::size_t)y * w, (std::size_t)W * 4);
    } else if (bb->bpp == 16) {
        for (int y = 0; y < H; ++y) {
            auto* row = reinterpret_cast<std::uint16_t*>(base + (std::size_t)y * bb->pitch);
            const std::uint32_t* s = src + (std::size_t)y * w;
            for (int x = 0; x < W; ++x) {
                const std::uint32_t c = s[x];
                row[x] = (std::uint16_t)((((c >> 16 & 0xFF) >> 3) << 11) |
                                         (((c >> 8 & 0xFF) >> 2) << 5) | ((c & 0xFF) >> 3));
            }
        }
    }
}

// Replace the first `%s` in `s` with `rep`; returns true if one was substituted.
bool SubstOne(std::string& s, const std::string& rep) {
    const std::size_t p = s.find("%s");
    if (p == std::string::npos) return false;
    s.replace(p, 2, rep);
    return true;
}
} // namespace

bool LoadChooseHistoryContent(const std::string& gameDir, CharIntroContent& out) {
    if (gameDir.empty()) return false;
    shim::DiskFileSystem fs(gameDir);
    const char* arch = "Resources/textbin_deutsch.BIN";
    if (!fs.exists(arch)) return false;
    io::ArchiveMount mount;
    if (!mount.Mount(&fs, arch, /*caseInsensitive=*/true)) return false;
    gui::text::TextDb db;
    for (const io::ArchiveMember& m : mount.members()) {
        const std::string& n = m.name;
        if (n.size() < 4) continue;
        std::string ext = n.substr(n.size() - 4);
        for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
        if (ext != ".res") continue;
        std::vector<u8> bytes;
        if (!mount.OpenMember(n.c_str(), bytes) || bytes.empty()) continue;
        gui::text::BuildTextArray(bytes.data(), bytes.size(), db);
    }
    const int idx = db.FindIndex("_M0_HISTORIE+0");
    if (idx < 0) return false;
    const char* markup = db.Text(idx);
    if (!markup || !*markup) return false;

    // The three perspective mode names that fill the `%ia[%s]` slots (and the body `%s`).
    std::string modes[3];
    for (int k = 0; k < 3; ++k) {
        const std::string key = "_M0_HISTORIE_MODUS+" + std::to_string(k);
        const int mi = db.FindIndex(key.c_str());
        modes[k] = (mi >= 0 && db.Text(mi)) ? db.Text(mi) : "";
    }

    out = ParseDifficultyMarkup(markup);   // heading + prompt + options (labels "%s" / "Назад")
    // Substitute the option `%s` labels with the mode names, in order.
    int mi = 0;
    for (std::size_t i = 0; i < out.options.size(); ++i) {
        if (out.options[i] == "%s" && mi < 3) out.options[i] = modes[mi++];
    }
    // Substitute the prompt `%s` placeholders (the body references the three modes).
    for (int k = 0; k < 3; ++k) if (!SubstOne(out.prompt, modes[k])) break;
    return !out.options.empty();
}

ChooseHistoryScreenResult RunChooseHistoryScreen(shim::IGraphicsDevice& device,
                                                 shim::IPlatform& plat,
                                                 const ChooseHistoryConfig& cfg) {
    ChooseHistoryScreenResult res;

    const int W = cfg.fbW, H = cfg.fbH;
    std::vector<std::uint32_t> scratch((std::size_t)W * H, 0u);

    CharIntroContent content;
    res.usedRealText = LoadChooseHistoryContent(cfg.gameDir, content);
    if (!res.usedRealText) {
        content.heading = "Historical perspective";
        content.prompt  = "Choose how history unfolds.";
        content.options = {"Factual historical account", "Your own personal history",
                           "No historical events", "back"};
        content.selectable = {true, true, true, false};
    }
    const int rowCount = (int)content.options.size();
    const CharIntroLayout L = CharIntroComputeLayout(W, H, rowCount);
    const int seedRow = gui::ChooseHistory_SeedIndex(cfg.seedMode);   // 0->2,1->0,2->1

    shim::DiskFileSystem assetFs(cfg.gameDir);
    MenuAssets assets;
    const bool haveAssets = !cfg.gameDir.empty() && assets.Load(assetFs);

    int frame = 0;
    bool prevLeft = false;
    for (;;) {
        if (cfg.maxFrames >= 0 && frame >= cfg.maxFrames) break;

        shim::MouseState ms{};
        plat.getMouse(ms);
        res.hoveredRow = L.HitRow(ms.x, ms.y);

        RenderCharIntroFrame(scratch.data(), W, H, content, res.hoveredRow, seedRow,
                             haveAssets ? &assets : nullptr);
        BlitToDevice(scratch.data(), W, H, device);
        device.present();
        ++res.framesPresented;

        if (!plat.pumpMessages()) { res.quitByWindow = true; res.back = true; break; }
        plat.getMouse(ms);
        const bool leftEdge = ms.left && !prevLeft;
        prevLeft = ms.left;

        if (plat.keyDown(kVkEsc)) { res.back = true; break; }
        if (leftEdge) {
            const int hov = L.HitRow(ms.x, ms.y);
            if (hov >= 0) {
                const bool selectable = content.selectable.size() > (std::size_t)hov
                                        && content.selectable[hov];
                if (selectable) { res.confirmed = true; res.historyFlag = ChooseHistory_RowToFlag(hov); break; }
                else            { res.back = true; break; }   // back row
            }
        }

        if (cfg.frameCapMs > 0) plat.sleepMs((std::uint32_t)cfg.frameCapMs);
        ++frame;
    }
    return res;
}

} // namespace guild::play
