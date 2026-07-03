// guild::play — native load-game screen (VIBE_Menu_RunLoadGame @0x56a270).
// Drives the 1:1 reconstruction gui::Menu_RunLoadGame; the slot table is built
// from the save directory through the REAL reconstructed browser leaves
// (enumerate @0x569530 / header @0x5a7af0 / slot placement @0x569c50). See header.
#include "play/sdl_loadgame_screen.h"

#include "shim/IGraphicsDevice.h"
#include "shim/IPlatform.h"
#include "shim/IFileSystem.h"
#include "shim_impl/disk_filesystem.h"

#include "gui/loadgame_run.h"      // gui::Menu_RunLoadGame @0x56a270 (1:1 flow)
#include "gui/menu_render.h"       // MenuRenderTarget, MenuFillRect, MenuPalette
#include "gui/text_load.h"         // gui::text::BuildTextArray
#include "gui/text/textdb.h"
#include "io/archive_mount.h"
#include "io/save.h"               // SaveLoadHeaderAndThumbnail @0x5a7af0, SaveHeader
#include "io/save_browser.h"       // SaveBrowserEnumerateSaveFiles @0x569530,
                                   // SaveBrowserFindSaveSlot @0x569c50, kSaveExt
#include "io/vfs.h"
#include "io/vfs_tree.h"
#include "play/menu_assets.h"
#include "play/session_save.h"     // kSaveBrowseDir ("gamedata/saves" @0x624f30)
#include "render/text_raster.h"
#include "render/font.h"
#include "render/types.h"
#include "render/bmp.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace guild::play {
namespace {

constexpr int kVkEsc   = 0x1B;
constexpr int kVkEnter = 0x0D;
constexpr int kVkUp    = 0x26;
constexpr int kVkDown  = 0x28;

// ---- shared raster helpers (file-local copies, as in the sibling screens) ----
const std::uint8_t* GlyphMap() {
    static std::uint8_t table[256];
    static bool init = false;
    if (!init) { render::FontInitGlyphTable(table); init = true; }
    return table;
}
void DrawLabel(render::Surface& s, int x, int y, const std::string& text,
               std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (text.empty()) return;
    render::PresentGlobals pg;
    pg.mode = render::PresentBackend::DDrawLockBlt;
    pg.ppvBits = reinterpret_cast<std::uintptr_t>(s.pixels);
    pg.dibPitch = s.pitch; pg.dibStride = s.widthPx; pg.screenHeight = s.height;
    pg.pitchExtra = 4; pg.lockBitDepth = 32; pg.primary = nullptr;
    render::DrawText(x, y, reinterpret_cast<const std::uint8_t*>(text.c_str()),
                     r, g, b, GlyphMap(), pg, s.fmt);
}
void Outline(std::uint32_t* px, int W, int H, int x, int y, int w, int h,
             std::uint32_t c) {
    auto put = [&](int X, int Y) { if (X >= 0 && X < W && Y >= 0 && Y < H) px[Y * W + X] = c; };
    for (int X = x; X < x + w; ++X) { put(X, y); put(X, y + h - 1); }
    for (int Y = y; Y < y + h; ++Y) { put(x, Y); put(x + w - 1, Y); }
}
void DrawBackground(std::uint32_t* dst, int W, int H, const std::uint32_t* bg,
                    int bw, int bh) {
    if (!bg || bw <= 0 || bh <= 0) return;
    for (int y = 0; y < H; ++y) {
        const int sy = (int)((long long)y * bh / H);
        const std::uint32_t* srow = bg + (std::size_t)sy * bw;
        std::uint32_t* drow = dst + (std::size_t)y * W;
        for (int x = 0; x < W; ++x) {
            const std::uint32_t px = srow[(int)((long long)x * bw / W)];
            if (px & 0xFF000000u) drow[x] = px;
        }
    }
}
void BlitToDevice(const std::uint32_t* src, int w, int h, shim::IGraphicsDevice& dev) {
    shim::Surface* bb = dev.backbuffer();
    if (!bb || !bb->pixels) return;
    const int W = bb->width < w ? bb->width : w;
    const int H = bb->height < h ? bb->height : h;
    auto* base = static_cast<std::uint8_t*>(bb->pixels);
    if (bb->bpp == 32) {
        for (int y = 0; y < H; ++y)
            std::memcpy(base + (std::size_t)y * bb->pitch, src + (std::size_t)y * w,
                        (std::size_t)W * 4);
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

// Light markup strip for the `_OPTIONEN_*` rich strings: drop `$[`/`$]` and the
// `$<digits><letter>` control tokens, keep `%i` (the empty-slot index slot of the
// dword_8C9A04 sprintf), drop other `%<letter>` codes.
std::string StripMarkup(const std::string& s) {
    std::string r;
    std::size_t i = 0;
    while (i < s.size()) {
        const char ch = s[i];
        if (ch == '$') {
            ++i;
            if (i < s.size() && (s[i] == '[' || s[i] == ']')) { ++i; continue; }
            while (i < s.size() && std::isdigit((unsigned char)s[i])) ++i;
            if (i < s.size()) ++i;
            continue;
        }
        if (ch == '%') {
            if (i + 1 < s.size() && s[i + 1] == 'i') { r += "%i"; i += 2; continue; }
            ++i;
            if (i < s.size()) ++i;
            continue;
        }
        r += ch;
        ++i;
    }
    // trim surrounding whitespace
    std::size_t b = r.find_first_not_of(" \t\r\n");
    std::size_t e = r.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    return r.substr(b, e - b + 1);
}

std::string SubstSlotIndex(const std::string& fmt, int idx) {
    std::string r = fmt;
    const std::size_t p = r.find("%i");
    if (p != std::string::npos) r.replace(p, 2, std::to_string(idx));
    return r;
}

// Full-screen background blit at native scale, centered: _OPTIONEN_PIC is 800x600
// so it lands 1:1 at (ox,oy); larger res variants fill the frame. We keep the
// existing DrawBackground (stretches the decoded bg over W x H) for the resolution
// variant the asset loader picked — the variant already matches fbW.
// Draw real `_FONT` glyphs (CP1251) at native scale (1) into the 32bpp scratch;
// returns the measured pixel width. Falls back to the built-in 6px ASCII raster
// when the real font is absent (headless).
int DrawRealText(render::Surface& s, const MenuFont* font, int x, int y,
                 const std::string& text, std::uint8_t r, std::uint8_t g,
                 std::uint8_t b) {
    if (text.empty()) return 0;
    if (font && font->loaded()) {
        font->DrawText(reinterpret_cast<std::uint32_t*>(s.pixels), s.widthPx, s.height,
                       x, y, text.c_str(), 1, r, g, b);
        return font->MeasureWidth(text.c_str());
    }
    DrawLabel(s, x, y, text, r, g, b);
    return (int)text.size() * 6;
}
int MeasureRealText(const MenuFont* font, const std::string& text) {
    if (text.empty()) return 0;
    if (font && font->loaded()) return font->MeasureWidth(text.c_str());
    return (int)text.size() * 6;
}

// Blit a raw 160x120x3 RGB save thumbnail (io::kThumbWidth/Height) into the 32bpp
// scratch at (dx,dy), clipped to the [clipY0,clipY1) row band. Mirrors the
// engine's word_13CED76 raw-bitmap widget (Widget_AddRawBitmapToWindow @0x412618).
// The on-disk thumbnail is row-major RGB (one byte per channel); empty/short
// buffers draw nothing.
void BlitThumbnail(std::uint32_t* dst, int W, int H, int dx, int dy,
                   const std::vector<std::uint8_t>& rgb, int clipY0, int clipY1) {
    const int tw = io::kThumbWidth, th = io::kThumbHeight;
    if ((int)rgb.size() < tw * th * 3) return;
    for (int y = 0; y < th; ++y) {
        const int Y = dy + y;
        if (Y < clipY0 || Y >= clipY1 || Y < 0 || Y >= H) continue;
        const std::uint8_t* srow = rgb.data() + (std::size_t)y * tw * 3;
        std::uint32_t* drow = dst + (std::size_t)Y * W;
        for (int x = 0; x < tw; ++x) {
            const int X = dx + x;
            if (X < 0 || X >= W) continue;
            const std::uint8_t r = srow[x * 3 + 0];
            const std::uint8_t g = srow[x * 3 + 1];
            const std::uint8_t b = srow[x * 3 + 2];
            drow[X] = 0xFF000000u | ((std::uint32_t)r << 16) |
                      ((std::uint32_t)g << 8) | (std::uint32_t)b;
        }
    }
}

// Fill a clipped rectangle directly in the scratch (row band aware).
void FillClip(std::uint32_t* dst, int W, int H, int x, int y, int w, int h,
              int clipY0, int clipY1, std::uint32_t c) {
    for (int Y = y; Y < y + h; ++Y) {
        if (Y < clipY0 || Y >= clipY1 || Y < 0 || Y >= H) continue;
        std::uint32_t* drow = dst + (std::size_t)Y * W;
        for (int X = x; X < x + w; ++X)
            if (X >= 0 && X < W) drow[X] = c;
    }
}

} // namespace

// ===========================================================================
// Layout — NATIVE 800x600, CENTER-TRANSLATE only (ox,oy). The form
// `menu\loadgame_new` (forms.BIN, FRM2): WIN0 (128,136,449,575) flags0x11 (no
// frame-corner gfx); body WIN1 (168,184,390,493) parentIdx1 — the slot list
// area (0 form objects; the 16 rows are built at runtime, @0x569e88). Title rich
// string 0x1864 (`_OPTIONEN_MENUE_LOAD`) is rendered into form window 2
// (@0x56a2e3). Each slot row is a 16px-tall child window
// (AddChildWindow(...,16,...) @0x569e88/@0x56a166) with a text label at x+168
// inside the body window; rows stack from the body top. NEVER scaled — widget
// sizes are native; only the whole 800x600 canvas is centered in W x H.
// ===========================================================================
void LoadGameScreenLayout::RowRect(int i, int scroll, int& rx, int& ry,
                                   int& rw, int& rh) const {
    rx = rowX;
    ry = py + i * rowH - scroll;     // 130px stride (AddChildWindow 130*slot @0x569e92)
    rw = rowW;
    rh = rowH - 2;
    if (rh < 1) rh = 1;
}
int LoadGameScreenLayout::HitRow(int mx, int my, int scroll) const {
    // Only rows whose body is inside the viewport [py, py+viewH) are clickable.
    if (my >= py && my < py + viewH) {
        for (int i = 0; i < kLoadGameSlotCount; ++i) {
            int rx, ry, rw, rh;
            RowRect(i, scroll, rx, ry, rw, rh);
            if (ry + rh <= py || ry >= py + viewH) continue;   // clipped out
            if (mx >= rx && mx < rx + rw && my >= ry && my < ry + rh) return i;
        }
    }
    if (mx >= backX && mx < backX + backW && my >= backY && my < backY + backH)
        return kLoadGameSlotCount;
    return -1;
}
int LoadGameScreenLayout::HitScrollBtn(int mx, int my) const {
    if (mx < btnX || mx >= btnX + btnW) return -1;
    if (my >= upBtnY   && my < upBtnY   + btnH) return 0;   // up
    if (my >= downBtnY && my < downBtnY + btnH) return 1;   // down
    return -1;
}
LoadGameScreenLayout LoadGameComputeLayout(int W, int H) {
    LoadGameScreenLayout L;
    L.W = W; L.H = H;
    L.ox = (W - 800) / 2;            // center-translate (PositionAtCoord mode-2 @0x41d964)
    L.oy = (H - 600) / 2;
    // Body window WIN1 (168,184) 390x493 — the slot list area. The form rect runs
    // below the 600px screen (it is parent-clipped), so the visible list height is
    // the body top down to a bottom margin inside the screen.
    L.px = 168 + L.ox; L.py = 184 + L.oy;
    L.pw = 390;        L.ph = 493;
    L.viewH = (H - 24) - L.py;        // clip to the screen, small bottom margin
    if (L.viewH > L.ph) L.viewH = L.ph;
    if (L.viewH < 130)  L.viewH = 130;
    L.cx = 128 + 449 / 2 + L.ox;     // WIN0 (128,136,449,575) centre x (title window)
    L.titleY = 150 + L.oy;           // title baseline just below WIN0 top
    // Slot rows: 130px stride (each row = 160x120 thumbnail + name/info @x+168).
    L.rowH   = 130;                  // AddChildWindow 130*slot @0x569e92
    L.rowX   = L.px;
    L.rowW   = L.pw;
    L.thumbW = io::kThumbWidth;      // 160
    L.thumbH = io::kThumbHeight;     // 120
    L.nameDX = 168;                  // Object_AddTextLabel(168,..) @0x569ebd
    L.scrollX = 532 + L.ox;          // (legacy; scrollbar removed)
    L.scrollW = 16;
    // Scroll up/down buttons + count, bottom-right of the green frame interior.
    // The frame (_TOOL_TIP_GREEN_BIGGER, 574x450) is centered at px0=(W-574)/2 with
    // its top at win0Y=136; the arrows sit just inside the lower-right border.
    const int fpx0 = (W - 574) / 2;
    const int fpy0 = 136 + L.oy;
    L.btnW = 12; L.btnH = 24;         // _AUSWAHL[1]/[2] blue gem arrows (12x24)
    L.btnX     = fpx0 + 528;          // ~screen x641 — inside the right gold border
    L.upBtnY   = fpy0 + 358;          // ~screen y494
    L.countY   = fpy0 + 388;          // ~screen y524 (count label)
    L.downBtnY = fpy0 + 408;          // ~screen y544
    // Synthetic back hit box (the real screen has no back button — ESC closes).
    L.backX = L.px;  L.backY = L.py + L.viewH + 4;
    L.backW = L.pw;  L.backH = 20;
    return L;
}

// ===========================================================================
// Texts — real localized `_OPTIONEN_*` entries by NAME from textbin.
// ===========================================================================
bool LoadLoadGameTexts(const std::string& gameDir, LoadGameTexts& out) {
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
    const int t = db.FindIndex("_OPTIONEN_MENUE_LOAD+0");          // rich id 0x1864
    if (t < 0 || !db.Text(t)) return false;
    out.title = StripMarkup(db.Text(t));
    // dword_8C9A04 is the empty-slot sprintf FORMAT (it carries the %i row-index
    // hole, 0x56a0fb); the `_OPTIONEN_LOAD_GAME_SLOT_INFO_LEER+0/+1` family holds
    // the empty-row info captions — pick the member with the %i hole.
    {
        std::string s0, s1, r0, r1;
        const int e0 = db.FindIndex("_OPTIONEN_LOAD_GAME_SLOT_INFO_LEER+0");
        const int e1 = db.FindIndex("_OPTIONEN_LOAD_GAME_SLOT_INFO_LEER+1");
        if (e0 >= 0 && db.Text(e0)) { r0 = db.Text(e0); s0 = StripMarkup(r0); }
        if (e1 >= 0 && db.Text(e1)) { r1 = db.Text(e1); s1 = StripMarkup(r1); }
        // The member with the %i hole is the slot NAME row; the OTHER is the field-
        // label block (Город/Дата/Имя игрока/Профессия), kept RAW for its $A breaks.
        if (s1.find("%i") != std::string::npos)      { out.emptyFmt = s1; out.emptyInfo = r0; }
        else if (s0.find("%i") != std::string::npos) { out.emptyFmt = s0; out.emptyInfo = r1; }
        else                                         { out.emptyFmt = !s1.empty() ? s1 : s0; out.emptyInfo = r0; }
    }
    // Occupied-row info captions (`_OPTIONEN_LOAD_GAME_SLOT_INFO+0/+1`): the rich
    // strings 0x18D2/0x18D3 the original renders to the right of the save name.
    {
        const int i0 = db.FindIndex("_OPTIONEN_LOAD_GAME_SLOT_INFO+0");
        const int i1 = db.FindIndex("_OPTIONEN_LOAD_GAME_SLOT_INFO+1");
        if (i0 >= 0 && db.Text(i0)) out.slotInfo0 = StripMarkup(db.Text(i0));
        if (i1 >= 0 && db.Text(i1)) out.slotInfo1 = StripMarkup(db.Text(i1));
    }
    const int c = db.FindIndex("_OPTIONEN_LOAD_GAME_SICHERHEITS_ABFRAGE+0"); // box 257
    if (c >= 0 && db.Text(c)) out.confirm = StripMarkup(db.Text(c));
    return !out.title.empty();
}

// ===========================================================================
// Slot build — the load-bearing part of VIBE_SaveBrowser_LoadSlotMetadata
// @0x569d00 over `fs`, through the REAL reconstructed leaves.
// ===========================================================================
int LoadGameBuildSlotViews(shim::IFileSystem& fs, const LoadGameScreenConfig& cfg,
                           LoadGameSlotView* out) {
    for (int i = 0; i < kLoadGameSlotCount; ++i) out[i] = LoadGameSlotView();

    // The browser scan over the VFS tree (case-insensitive like the original).
    if (!io::VfsTreeInit(&fs, cfg.treeRoot.c_str(), /*caseInsensitive=*/true))
        return 0;
    static io::SaveBrowserRecord recs[256];
    std::memset(recs, 0, sizeof recs);
    int n = io::SaveBrowserEnumerateSaveFiles(kSaveBrowseDir, io::VfsRoot(),
                                              io::kSaveExt, recs, 256);
    io::VfsTreeShutdown();
    if (n < 0) n = 0;
    if (n > kLoadGameSlotCount) n = kLoadGameSlotCount;   // 0x569d64: if (v9 > 16) v9 = 16

    // The original's 544-byte x 16 slot table, ids initialised to -1 (0x569d3d:
    // the +4 / +8 dwords of every row).
    static guild::u8 table[kLoadGameSlotCount * 544];
    std::memset(table, 0, sizeof table);
    for (int i = 0; i < kLoadGameSlotCount; ++i) {
        std::memset(table + i * 544 + 4, 0xFF, 4);
        std::memset(table + i * 544 + 8, 0xFF, 4);
    }

    io::VfsInit(&fs, false);
    const guild::u32 savedVersion = io::SaveVersionGet();
    int occupied = 0;
    for (int i = 0; i < n; ++i) {
        // The original opens record+265 (the enumerator's full "basePath/name" with
        // extension), NOT the +9 name field — @0x569d7f v78=v77+265; @0x569d9c
        // VIBE_Vfs_OpenFile(v78,"rb"). EnumerateSaveFiles truncates +9 at its '.'
        // (@0x5695f6), so the loose file name (with extension) must come from +265.
        // Map the VFS full path to the real saves dir via its basename.
        const char* fp = recs[i].fullPath;
        const char* slash = std::strrchr(fp, '/');
        const std::string fileLeaf = slash ? slash + 1 : fp;   // "Quicksave.SAV"
        const std::string openPath = cfg.openDirReal + "/" + fileLeaf;
        io::VfsHandle* h = io::VfsOpenFile(openPath.c_str(), "rb");   // 0x569d9c "rb"
        if (!h) continue;
        io::SaveHeader hdr;
        std::memset(&hdr, 0, sizeof hdr);
        // Request the 0xE100 (160x120x3) save thumbnail — the per-row preview the
        // original blits via Widget_AddRawBitmapToWindow(word_13CED76) @0x569d00.
        std::vector<guild::u8> thumb(io::kThumbnailBytes, 0);
        const bool ok = io::SaveLoadHeaderAndThumbnail(h, hdr, thumb.data()); // 0x5a7af0
        io::VfsCloseStream(h);
        if (!ok) continue;                                 // header fail -> skip (0x569e1a)
        const bool partial = (hdr.flagByte & 2) != 0;      // (v58[4] & 2)
        if (partial && !cfg.includePartialSaves) continue; // the 1:1 drop @0x569e1a
        // The header metadata record FindSaveSlot consumes: in-game save name at +9.
        io::SaveBrowserRecord hrec;
        std::memset(&hrec, 0, sizeof hrec);
        char nameZ[33];
        std::memcpy(nameZ, hdr.name, 32);
        nameZ[32] = 0;
        std::strncpy(hrec.name, nameZ, sizeof hrec.name - 1);
        const int slot = io::SaveBrowserFindSaveSlot(table, &hrec, hdr.byte649D50); // 0x569c50
        if (slot < 0 || slot >= kLoadGameSlotCount) continue;
        // 0x569e92 / 0x569ecb: the caller stamps the row's window/label ids right
        // after placement (this is what marks the row occupied for later files).
        const std::uint32_t wid = (std::uint32_t)(kLoadGameSlotWidgetBase + slot);
        std::memcpy(table + slot * 544 + 8, &wid, 4);
        std::memcpy(table + slot * 544 + 4, &wid, 4);
        table[slot * 544 + 12] = 1;                        // *(row+12) = 1 (0x569f01)
        LoadGameSlotView& v = out[slot];
        v.occupied = true;
        v.saveName = nameZ;                                // row+25 (record copy +9)
        v.fileName = fileLeaf;            // full loose name + ext (from +265 basename)
        v.openPath = openPath;
        v.version  = hdr.magic;
        v.slotTag  = hdr.byte649D50;
        v.partial  = partial;
        v.thumbnail = std::move(thumb);                    // 160x120x3 RGB (or zeroed)
        v.wealth   = hdr.wealth;                           // +0x54 display wealth
        { char p[97]; std::memcpy(p, hdr.name96, 96); p[96] = 0; v.playerName = p; } // +0x88
        ++occupied;
    }
    io::SaveVersionSet(savedVersion);
    return occupied;
}

// ===========================================================================
// The native host hooks for gui::Menu_RunLoadGame.
// ===========================================================================
namespace {

struct NativeLoadGameHooks : gui::LoadGameRunHooks {
    shim::IGraphicsDevice* dev = nullptr;
    shim::IPlatform* plat = nullptr;
    shim::IFileSystem* fs = nullptr;
    const LoadGameScreenConfig* cfg = nullptr;
    gui::LoadGameRunState* st = nullptr;
    LoadGameScreenResult* res = nullptr;
    LoadGameTexts texts;
    LoadGameScreenLayout layout{};
    MenuAssets* assets = nullptr;
    std::vector<std::uint32_t> scratch;
    LoadGameSlotView views[kLoadGameSlotCount];

    // per-frame input snapshot
    shim::MouseState mouse{};
    bool windowOpen = true;
    bool prevLeft = false, prevEsc = false, prevEnter = false;
    bool prevUp = false, prevDown = false;
    bool clickEdgeNow = false, escEdgeNow = false, enterEdgeNow = false;
    int  hoveredRow = -1;     // 0..15 slot, 16 back, -1 none
    int  selection = -1;      // keyboard selection over occupied rows
    int  scroll = 0;          // list scroll offset (px), 0..layout.maxScroll()
    bool overlayConfirm = false;
    // Scroll-button hold/repeat state (click = one row; hold = continuous).
    int  scrollHoldDir = -1;  // 0 up, 1 down, -1 none
    int  scrollHoldFrames = 0;
    static constexpr int kScrollHoldDelay = 16;   // frames before auto-repeat starts
    static constexpr int kScrollHoldRepeat = 4;   // repeat every N frames while held
    void ScrollByRows(int dir) {
        selection = -1;                            // manual scroll: drop the keyboard pick
        scroll += dir * layout.rowH;
        if (scroll < 0) scroll = 0;
        const int maxS = layout.maxScroll();
        if (scroll > maxS) scroll = maxS;
    }

    // Keep the selected (or first) row inside the viewport (the scrollbar follows
    // the selection, like the engine's slider panel).
    void EnsureVisible() {
        const int maxS = layout.maxScroll();
        if (selection >= 0) {
            const int top = selection * layout.rowH;
            const int bot = top + layout.rowH;
            if (top - scroll < 0)            scroll = top;
            else if (bot - scroll > layout.viewH) scroll = bot - layout.viewH;
        }
        if (scroll < 0) scroll = 0;
        if (scroll > maxS) scroll = maxS;
    }

    void SeedSelection() {
        selection = -1;
        for (int i = 0; i < kLoadGameSlotCount; ++i)
            if (views[i].occupied) { selection = i; break; }
    }
    void MoveSelection(int dir) {
        int i = selection;
        for (int step = 0; step < kLoadGameSlotCount; ++step) {
            i += dir;
            if (i < 0 || i >= kLoadGameSlotCount) break;
            if (views[i].occupied) { selection = i; return; }
        }
    }

    // ---- build: the form leaves are records only (chrome is deferred) ----
    int  FormLoad(const char*) override { return 1; }
    void RenderTitle(int) override {}
    int  GetWindowId(int) override { return 1; }

    void LoadSlotMetadata(int /*form*/, const char* /*saveDir*/,
                          std::vector<gui::LoadGameSlot>& outSlots) override {
        int count = 0;
        if (fs) count = LoadGameBuildSlotViews(*fs, *cfg, views);
        if (res) res->saveCount = count;
        outSlots.clear();
        outSlots.resize(kLoadGameSlotCount);
        for (int i = 0; i < kLoadGameSlotCount; ++i) {
            gui::LoadGameSlot& s = outSlots[i];
            s.present  = views[i].occupied;                       // row[12]
            s.objId    = views[i].occupied ? (kLoadGameSlotWidgetBase + i) : -1; // row+8
            s.widgetId = views[i].occupied ? (kLoadGameSlotWidgetBase + i) : -1; // row+4
            s.name     = views[i].saveName;                       // row+25
        }
        SeedSelection();
    }

    // ---- per-frame: pump + input snapshot + render + present ----
    void ReadInput() {
        if (plat) {
            if (!plat->pumpMessages()) windowOpen = false;
            plat->getMouse(mouse);
        }
        const bool esc   = plat && plat->keyDown(kVkEsc);
        const bool enter = plat && plat->keyDown(kVkEnter);
        const bool up    = plat && plat->keyDown(kVkUp);
        const bool down  = plat && plat->keyDown(kVkDown);
        escEdgeNow   = esc && !prevEsc;
        enterEdgeNow = enter && !prevEnter;
        clickEdgeNow = mouse.left && !prevLeft;
        if (up && !prevUp)     MoveSelection(-1);
        if (down && !prevDown) MoveSelection(+1);
        prevEsc = esc; prevEnter = enter; prevUp = up; prevDown = down;
        prevLeft = mouse.left;
        EnsureVisible();
        hoveredRow = layout.HitRow(mouse.x, mouse.y, scroll);
        if (res) res->hoveredRow = hoveredRow;

        // Scroll buttons: press = scroll one row; hold = continuous after a delay.
        const int sb = layout.HitScrollBtn(mouse.x, mouse.y);
        if (mouse.left && sb >= 0) {
            const int dir = (sb == 0) ? -1 : +1;
            if (clickEdgeNow) {                       // initial press
                ScrollByRows(dir);
                scrollHoldDir = sb; scrollHoldFrames = 0;
            } else if (sb == scrollHoldDir) {         // still holding the same button
                ++scrollHoldFrames;
                if (scrollHoldFrames >= kScrollHoldDelay &&
                    (scrollHoldFrames - kScrollHoldDelay) % kScrollHoldRepeat == 0)
                    ScrollByRows(dir);
            }
        } else {
            scrollHoldDir = -1; scrollHoldFrames = 0;
        }
    }

    void RenderAndPresent() {
        const int W = layout.W, H = layout.H;
        std::fill(scratch.begin(), scratch.end(), 0u);
        gui::MenuRenderTarget tgt = gui::MenuRenderTarget::Wrap(scratch.data(), W, H, W * 4);
        const bool art = assets && assets->loaded();
        const MenuFont* font = art ? &assets->font() : nullptr;
        if (!font || !font->loaded()) font = nullptr;
        // Small engine font (_FONT+1) for the per-slot info field labels (the
        // original renders Город/Дата/... noticeably smaller than the save name).
        const MenuFont* sfont = (art && assets->smallFont().loaded())
                                    ? &assets->smallFont() : font;

        // --- background: real _OPTIONEN_PIC (res variant chosen at load) ---
        if (art)
            DrawBackground(scratch.data(), W, H, assets->background().data(),
                           assets->backgroundWidth(), assets->backgroundHeight());
        else {
            const gui::MenuPalette pal;
            gui::MenuFillRect(&tgt.surf, 0, 0, W, H, pal.bgR, pal.bgG, pal.bgB);
        }

        // Slot-list viewport (used by the row loop + scrollbar below).
        const int pX = layout.px, pY = layout.py, pW = layout.pw, pVH = layout.viewH;
        (void)pX; (void)pW;

        // --- panel: the REAL _TOOL_TIP_GREEN_BIGGER jeweled green frame (the same
        // chrome as the main-menu options sub-screens), centered horizontally, top
        // at win0Y=136. The enclosed transparent centre is flood-filled dark (so the
        // slot list is legible over the city backdrop), the title sits in the green
        // marble bar, and the slot rows render inside the dark interior below. ---
        const render::DecodedShape* frame =
            art ? assets->SpriteByName("_TOOL_TIP_GREEN_BIGGER", 0) : nullptr;
        if (frame && frame->width > 0 && frame->height > 0) {
            const int fw = frame->width, fh = frame->height;
            const int px0 = (W - fw) / 2;
            const int py0 = 136 + layout.oy;
            const std::uint32_t* fa = frame->argb.data();
            static const render::DecodedShape* s_maskFor = nullptr;
            static std::vector<unsigned char> s_interior;
            if (s_maskFor != frame) {
                s_maskFor = frame;
                s_interior.assign((std::size_t)fw * fh, 0);
                std::vector<int> stk;
                const int seed = (fh / 2) * fw + (fw / 2);
                if (!(fa[seed] & 0xFF000000u)) { s_interior[seed] = 1; stk.push_back(seed); }
                while (!stk.empty()) {
                    const int p = stk.back(); stk.pop_back();
                    const int x = p % fw, y = p / fw;
                    const int nb[4] = { x + 1 < fw ? p + 1 : -1, x > 0 ? p - 1 : -1,
                                        y + 1 < fh ? p + fw : -1, y > 0 ? p - fw : -1 };
                    for (int k = 0; k < 4; ++k) {
                        const int q = nb[k];
                        if (q < 0 || s_interior[q] || (fa[q] & 0xFF000000u)) continue;
                        s_interior[q] = 1; stk.push_back(q);
                    }
                }
            }
            for (int ry = 0; ry < fh; ++ry) {
                const int dy = py0 + ry; if (dy < 0 || dy >= H) continue;
                std::uint32_t* drow = scratch.data() + (std::size_t)dy * W;
                for (int rx = 0; rx < fw; ++rx) {
                    const int dx = px0 + rx; if (dx < 0 || dx >= W) continue;
                    const std::size_t fi = (std::size_t)ry * fw + rx;
                    const std::uint32_t ap = fa[fi];
                    if (ap & 0xFF000000u) drow[dx] = ap;
                    else if (s_interior[fi]) {
                        const std::uint32_t c = drow[dx];
                        const int r = ((((c >> 16) & 0xFF) * 82) + 22 * 174) >> 8;
                        const int g = ((((c >> 8) & 0xFF) * 82) + 26 * 174) >> 8;
                        const int b = (((c & 0xFF) * 82) + 16 * 174) >> 8;
                        drow[dx] = 0xFF000000u | ((std::uint32_t)r << 16) | ((std::uint32_t)g << 8) | (std::uint32_t)b;
                    }
                }
            }
            // Title centered on the green marble bar (frame-y 11..41, centre 26).
            if (font && !texts.title.empty()) {
                const int tw = MeasureRealText(font, texts.title);
                const int th = font->lineHeight() > 0 ? font->lineHeight() : 17;
                DrawRealText(tgt.surf, font, px0 + (fw - tw) / 2, py0 + 11 + (31 - th) / 2,
                             texts.title, 255, 255, 214);
            }
        } else {
            // Asset-less fallback: dark plate + plain centered title.
            gui::MenuFillRect(&tgt.surf, pX - 8, pY - 8, pW + layout.scrollW + 24, pVH + 16, 28, 22, 14);
            Outline(scratch.data(), W, H, pX - 8, pY - 8, pW + layout.scrollW + 24, pVH + 16, 0xFFB08C46u);
            const int tw = MeasureRealText(font, texts.title);
            DrawRealText(tgt.surf, font, layout.cx - tw / 2, layout.titleY, texts.title, 255, 226, 150);
        }

        // --- slot rows: 130px-tall rows, each = 160x120 thumbnail (left) + save
        // name (gold) and date/wealth info at x+168, clipped to the viewport and
        // scrolled. (VIBE_SaveBrowser_LoadSlotMetadata @0x569d00.) ---
        const int clipY0 = pY, clipY1 = pY + pVH;
        // Empty-slot placeholder is the rolling gold "?" (_FRAGEZEICHEN, ~25 frames
        // that rotate). Pick the current animation frame from the present counter.
        const render::DecodedShape* qmark = nullptr;
        if (art) {
            static int s_qcount = -1;
            if (s_qcount < 0) {
                s_qcount = 0;
                while (s_qcount < 64 && assets->SpriteByName("_FRAGEZEICHEN", s_qcount)) ++s_qcount;
            }
            if (s_qcount > 0) {
                const int qf = (res ? res->framesPresented / 2 : 0) % s_qcount;
                qmark = assets->SpriteByName("_FRAGEZEICHEN", qf);
            }
        }
        for (int i = 0; i < kLoadGameSlotCount; ++i) {
            int rx, ry, rw, rh;
            layout.RowRect(i, scroll, rx, ry, rw, rh);
            if (ry + rh <= clipY0 || ry >= clipY1) continue;     // off-viewport
            const bool occ = views[i].occupied;
            const bool hov = (i == hoveredRow);
            const bool sel = (i == selection) && occ;
            // Hover / selection band (clipped to the viewport).
            if (sel)             FillClip(scratch.data(), W, H, rx, ry, rw, rh, clipY0, clipY1, 0xFF5A3C1Cu);
            else if (hov && occ) FillClip(scratch.data(), W, H, rx, ry, rw, rh, clipY0, clipY1, 0xFF40321Eu);

            // Thumbnail box (160x120). Occupied: the real save preview; empty: a
            // dark placeholder (the engine's blank word_13CED76 raw bitmap).
            const int tX = rx, tY = ry + (rh - layout.thumbH) / 2;
            FillClip(scratch.data(), W, H, tX, tY, layout.thumbW, layout.thumbH,
                     clipY0, clipY1, 0xFF000000u);
            if (occ && !views[i].thumbnail.empty())
                BlitThumbnail(scratch.data(), W, H, tX, tY, views[i].thumbnail, clipY0, clipY1);
            // Thumbnail border.
            for (int yy = tY; yy < tY + layout.thumbH; ++yy) {
                if (yy < clipY0 || yy >= clipY1 || yy < 0 || yy >= H) continue;
                if (tX >= 0 && tX < W) scratch[(std::size_t)yy * W + tX] = 0xFF8A6E36u;
                const int xr = tX + layout.thumbW - 1;
                if (xr >= 0 && xr < W) scratch[(std::size_t)yy * W + xr] = 0xFF8A6E36u;
            }
            // Empty-slot placeholder: the rolling gold "?" (_FRAGEZEICHEN) centered
            // in the dark thumbnail (the original's animated no-preview indicator).
            if (!occ && qmark && qmark->width > 0) {
                const int qx = tX + (layout.thumbW - qmark->width) / 2;
                const int qy = tY + (layout.thumbH - qmark->height) / 2;
                for (int yy = 0; yy < qmark->height; ++yy) {
                    const int Y = qy + yy;
                    if (Y < clipY0 || Y >= clipY1 || Y < 0 || Y >= H) continue;
                    const std::uint32_t* srow = qmark->argb.data() + (std::size_t)yy * qmark->width;
                    std::uint32_t* drow = scratch.data() + (std::size_t)Y * W;
                    for (int xx = 0; xx < qmark->width; ++xx) {
                        const std::uint32_t p = srow[xx];
                        if (!(p & 0xFF000000u)) continue;
                        const int X = qx + xx; if (X < 0 || X >= W) continue;
                        drow[X] = p;
                    }
                }
            }

            // Save name (gold; Object_SetColor 66) / empty placeholder, at x+168.
            const int nx = rx + layout.nameDX;
            const std::string label = occ ? views[i].saveName
                                          : SubstSlotIndex(texts.emptyFmt, i);
            const int ny = ry + 6;
            if (ny >= clipY0 && ny < clipY1) {
                const int tr = occ ? ((hov || sel) ? 255 : 230) : 150;
                const int tg = occ ? ((hov || sel) ? 240 : 205) : 130;
                const int tb = occ ? ((hov || sel) ? 150 : 90)  : 110;
                DrawRealText(tgt.surf, font, nx, ny, label,
                             (std::uint8_t)tr, (std::uint8_t)tg, (std::uint8_t)tb);
            }
            // Occupied date/wealth info line (rich 0x18D3): player name + wealth.
            if (occ) {
                std::string info = views[i].playerName;
                if (!info.empty()) info += "   ";
                info += std::to_string(views[i].wealth);
                const int iy = ry + 6 + (font ? font->lineHeight() + 4 : 18);
                if (iy >= clipY0 && iy < clipY1)
                    DrawRealText(tgt.surf, font, nx, iy, info, 210, 196, 150);
            } else {
                // Empty slot: the field-label block (Город:/Дата:/Имя игрока:/
                // Профессия:) from _OPTIONEN_LOAD_GAME_SLOT_INFO_LEER, one line per
                // $A break (the original renders these greyed under the slot name).
                const int lh = sfont ? sfont->lineHeight() + 3 : 13;
                int ly = ny + (font ? font->lineHeight() + 6 : 20);
                const std::string& ei = texts.emptyInfo;
                std::size_t p = 0;
                while (p <= ei.size() && ly + lh <= clipY1) {
                    const std::size_t nl = ei.find("$A", p);
                    const std::string seg = ei.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
                    const std::string line = StripMarkup(seg);
                    if (!line.empty() && ly >= clipY0)
                        DrawRealText(tgt.surf, sfont, nx, ly, line, 190, 178, 140);
                    ly += lh;
                    if (nl == std::string::npos) break;
                    p = nl + 2;
                }
            }
        }

        // --- scroll up/down buttons + count (Hud_BuildSliderPanel @0x4bd388): a
        // blue gem triangle pointing up and one down, with the slot count between
        // them, at the bottom-right of the panel. No scrollbar. The engine draws
        // these arrows as primitives, so we render them the same way. ---
        {
            const int hovBtn = layout.HitScrollBtn(mouse.x, mouse.y);
            const bool atTop = scroll <= 0;
            const bool atBot = scroll >= layout.maxScroll();
            // The real blue gem arrows: _AUSWAHL shape 1 (up) / 2 (down). Brighten
            // on hover, dim when the list can't scroll that way.
            const render::DecodedShape* upA   = art ? assets->SpriteByName("_AUSWAHL", 1) : nullptr;
            const render::DecodedShape* downA = art ? assets->SpriteByName("_AUSWAHL", 2) : nullptr;
            auto blitArrow = [&](const render::DecodedShape* sh, int bx, int by, bool hovered, bool disabled) {
                if (!sh || sh->width <= 0) return;
                for (int yy = 0; yy < sh->height; ++yy) {
                    const int Y = by + yy; if (Y < 0 || Y >= H) continue;
                    const std::uint32_t* srow = sh->argb.data() + (std::size_t)yy * sh->width;
                    std::uint32_t* drow = scratch.data() + (std::size_t)Y * W;
                    for (int xx = 0; xx < sh->width; ++xx) {
                        std::uint32_t p = srow[xx];
                        if (!(p & 0xFF000000u)) continue;
                        const int X = bx + xx; if (X < 0 || X >= W) continue;
                        int r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
                        if (disabled) { r = r * 5 / 8; g = g * 5 / 8; b = b * 5 / 8; }     // darker
                        else if (hovered) {                                                // lighter
                            r += (255 - r) / 3; g += (255 - g) / 3; b += (255 - b) / 3;
                        }
                        drow[X] = 0xFF000000u | ((std::uint32_t)r << 16) | ((std::uint32_t)g << 8) | (std::uint32_t)b;
                    }
                }
            };
            blitArrow(upA,   layout.btnX, layout.upBtnY,   hovBtn == 0, atTop);
            blitArrow(downA, layout.btnX, layout.downBtnY, hovBtn == 1, atBot);
            // Count label (total slots) centered between the arrows.
            if (sfont) {
                char num[8]; std::snprintf(num, sizeof num, "%d", kLoadGameSlotCount);
                const int tw = sfont->MeasureWidth(num);
                DrawRealText(tgt.surf, sfont, layout.btnX + layout.btnW / 2 - tw / 2,
                             layout.countY, num, 235, 215, 150);
            }
        }

        // Confirm overlay (byte_63CC40 gate -> RunMessageBox 257,
        // _OPTIONEN_LOAD_GAME_SICHERHEITS_ABFRAGE).
        if (overlayConfirm) {
            const int ow = 360, oh = 120;
            const int ox = layout.cx - ow / 2;
            const int oy = layout.py + (layout.ph - oh) / 2;
            gui::MenuFillRect(&tgt.surf, ox, oy, ow, oh, 22, 18, 12);
            Outline(scratch.data(), W, H, ox, oy, ow, oh, 0xFFFFD060u);
            const int cw = MeasureRealText(font, texts.confirm);
            DrawRealText(tgt.surf, font, layout.cx - cw / 2, oy + oh / 3, texts.confirm,
                         255, 226, 150);
        }
        // One-shot framebuffer dump for visual verification (GUILD_LOADGAME_DUMP).
        if (const char* mp = std::getenv("GUILD_LOADGAME_DUMP")) {
            static bool dumped = false;
            if (!dumped) {
                dumped = true;
                std::vector<std::uint8_t> rgb((std::size_t)W * H * 3);
                for (std::size_t i = 0, n = (std::size_t)W * H; i < n; ++i) {
                    const std::uint32_t c = scratch[i];
                    rgb[i*3+0] = (std::uint8_t)((c >> 16) & 0xFF);
                    rgb[i*3+1] = (std::uint8_t)((c >> 8) & 0xFF);
                    rgb[i*3+2] = (std::uint8_t)(c & 0xFF);
                }
                std::vector<std::uint8_t> bmp = render::BmpSave24Bit(W, H, rgb.data());
                if (FILE* f = std::fopen(mp, "wb")) {
                    std::fwrite(bmp.data(), 1, bmp.size(), f); std::fclose(f);
                }
            }
        }
        BlitToDevice(scratch.data(), W, H, *dev);
        dev->present();
        if (res) ++res->framesPresented;
        if (cfg->frameCapMs > 0 && plat) plat->sleepMs((std::uint32_t)cfg->frameCapMs);
    }

    int RunFrameLoop() override {
        ReadInput();
        RenderAndPresent();
        if (!windowOpen) {
            if (res) res->quitByWindow = true;
            return 0;
        }
        return (st && st->close) ? 0 : 1;   // dword_631614 armed -> loop ends
    }

    // dword_672230 — close/escape this frame (ESC edge or back-row click).
    bool CloseRequested(int) override {
        if (escEdgeNow) return true;
        if (clickEdgeNow && hoveredRow == kLoadGameSlotCount) return true;
        return false;
    }
    // dword_672228 — a click edge (mouse, or Enter as a click on the selection).
    bool ClickEdge(int) override {
        if (clickEdgeNow && hoveredRow >= 0 && hoveredRow < kLoadGameSlotCount) return true;
        if (enterEdgeNow && selection >= 0) return true;
        return false;
    }
    // dword_62D22C — the hovered widget id the slot scan matches against.
    int HoverId(int) override {
        if (enterEdgeNow && selection >= 0) return kLoadGameSlotWidgetBase + selection;
        if (hoveredRow >= 0 && hoveredRow < kLoadGameSlotCount)
            return kLoadGameSlotWidgetBase + hoveredRow;
        return -1;
    }
    // RunMessageBox(dword_8C9A08, 257) — the confirm gate (byte_63CC40).
    bool ConfirmLoad() override {
        overlayConfirm = true;
        bool ok = false;
        for (;;) {
            if (cfg->maxFrames >= 0 && res && res->framesPresented >= cfg->maxFrames)
                break;
            ReadInput();
            RenderAndPresent();
            if (!windowOpen) { if (res) res->quitByWindow = true; break; }
            if (enterEdgeNow) { ok = true; break; }
            if (escEdgeNow) break;
        }
        overlayConfirm = false;
        return ok;
    }
};

} // namespace

// ===========================================================================
// RunLoadGameScreen — gui::Menu_RunLoadGame on the native window.
// ===========================================================================
LoadGameScreenResult RunLoadGameScreen(shim::IGraphicsDevice& device,
                                       shim::IPlatform& plat,
                                       const LoadGameScreenConfig& cfg) {
    LoadGameScreenResult res;

    NativeLoadGameHooks h;
    h.dev = &device;
    h.plat = &plat;
    h.cfg = &cfg;
    h.res = &res;
    h.layout = LoadGameComputeLayout(cfg.fbW, cfg.fbH);
    h.scratch.assign((std::size_t)cfg.fbW * cfg.fbH, 0u);

    // Save enumeration source: the caller's fs, else a disk fs over gameDir.
    shim::DiskFileSystem diskFs(cfg.gameDir);
    h.fs = cfg.fs ? cfg.fs : (cfg.gameDir.empty() ? nullptr : &diskFs);

    res.usedRealText = LoadLoadGameTexts(cfg.gameDir, h.texts);
    if (!res.usedRealText) {
        h.texts.title    = "Load game";
        h.texts.emptyFmt = "- empty slot %i -";
        h.texts.confirm  = "Load this game?";
    }
    if (h.texts.emptyFmt.empty()) h.texts.emptyFmt = "- empty slot %i -";
    if (h.texts.confirm.empty())  h.texts.confirm  = "Load this game?";

    // Load-Game is a MAIN-MENU sub-screen, so it sits over the live title-screen
    // CITY backdrop (_MENUE_BACKGROUND), exactly like the main-menu options pages
    // — NOT the in-game _OPTIONEN_PIC bookshelf (verified against gilde.exe).
    // Resolution variants _1024/_1152 by framebuffer width.
    const char* bgName = cfg.fbW >= 1152 ? "_MENUE_BACKGROUND_1152"
                       : cfg.fbW >= 1024 ? "_MENUE_BACKGROUND_1024"
                                         : "_MENUE_BACKGROUND";
    shim::DiskFileSystem assetFs(cfg.gameDir);
    MenuAssets assets;
    bool haveAssets =
        !cfg.gameDir.empty() && assets.Load(assetFs, "gfx/gilde.gfx", bgName);
    // Fall back to the base 800x600 _MENUE_BACKGROUND if the res variant is absent.
    if (!haveAssets && !cfg.gameDir.empty() && cfg.fbW >= 1024)
        haveAssets = assets.Load(assetFs, "gfx/gilde.gfx", "_MENUE_BACKGROUND");
    if (haveAssets) h.assets = &assets;

    gui::LoadGameRunState st;
    st.confirmGate = cfg.confirmGate ? 1 : 0;   // byte_63CC40
    gui::LoadGameRunRecord rec;
    h.st = &st;

    gui::LoadGameRunHooks* prev = gui::LoadGame_SetRunHooks(&h);
    const int picked = gui::Menu_RunLoadGame(st, &rec, cfg.maxFrames);  // @0x56a270
    gui::LoadGame_SetRunHooks(prev);

    res.confirmed    = (picked == 1);
    res.sessionFlags = st.sessionFlags;          // word_63C740 (10 on pick)
    res.loadPath     = st.loadPath;              // byte_122F530
    res.slot         = rec.matchedSlot;
    if (res.confirmed && res.slot >= 0 && res.slot < kLoadGameSlotCount) {
        res.savePath = h.views[res.slot].openPath;
        res.saveName = h.views[res.slot].saveName;
    }
    if (!res.confirmed && !res.quitByWindow) res.back = true;
    return res;
}

} // namespace guild::play
