// =============================================================================
// guild::play — IN-GAME SESSION HUD OVERLAY implementation. See session_hud.h
// (the header carries the full gilde.gfx investigation + the named gap:
// VIBE_Shape_ConvertRgbTo16 @0x5d7c0c is not reconstructed, so the sprite blit
// uses the documented DefaultHudSpriteBank fallback of play/wire_hud_bridge).
//
// Every pixel written here goes through a reconstructed leaf:
//   gui::MenuFillRect -> render::SurfaceDrawHLine        (0x423c70 -> 0x423ffc)
//   render::SurfaceDrawRectOutline                       (0x4242d4)
//   render::DrawText -> DrawGlyph                        (0x434E18 -> 0x434D0C)
//   HudRenderHooks::drawSprite -> render::ShapeShowFromBank -> ShapeBlitColored16
//                                                        (0x5d861c -> 0x5d7164)
// over the session's native 16bpp RGB565 framebuffer (the format the leaves
// natively support: DrawGlyph's 16bpp gate, SurfaceDrawHLine's 16bpp pack, and
// ShapeBlitColored16's u16 destination).
// =============================================================================
#include "play/session_hud.h"

#include "play/wire_hud_bridge.h"    // InstallRealHudBridge (real ShapeShowFromBank)
#include "gui/form_loader.h"         // Form_LoadFromBuffer (0x41b888 parse half)
#include "gui/hud.h"                 // StatusText_Register (0x4bcc80)
#include "gui/menu_render.h"         // MenuFillRect (0x423c70 software fill)
#include "render/surface.h"          // SurfaceDrawRectOutline (0x4242d4)
#include "render/text_raster.h"      // DrawText / DrawGlyph (0x434E18 / 0x434D0C)
#include "render/font.h"             // FontInitGlyphTable (0x42E350)
#include "render/surface_present.h"  // PresentGlobals / PresentBackend
#include "shim/IFileSystem.h"
#include "io/archive_mount.h"        // textbin_deutsch.BIN mount (chrome strings)
#include "gui/text_load.h"           // BuildTextArray
#include "gui/text/textdb.h"         // TextDb (localized chrome strings)
#include "world/money_format.h"      // MoneyFormatWithSeparators (0x11 currency glyph)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace guild::play {

namespace {

int DrawGold565(render::Surface& s, const MenuFont& fnt, int x, int y,
                const char* text, bool centerX);   // defined below (2nd block)

inline u16 RdU16(const u8* p) { return (u16)(p[0] | (p[1] << 8)); }
inline u32 RdU32(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

// Lazily-initialised 256-entry glyph map (render::FontInitGlyphTable, the
// runtime byte_75FB50 build — same wiring play/hud_render uses).
const u8* GlyphMap() {
    static u8 table[256];
    static bool init = false;
    if (!init) { render::FontInitGlyphTable(table); init = true; }
    return table;
}

// PresentGlobals that drives DrawText/DrawGlyph straight into the 16bpp session
// framebuffer (the Lock-copy path; DrawGlyph's native 16bpp gate stamps RGB565
// pixels). Mirrors hud_render's SurfacePresent but with the 16bpp lock fields.
render::PresentGlobals SurfacePresent16(render::Surface& s) {
    render::PresentGlobals g;
    g.mode         = render::PresentBackend::DDrawLockBlt;
    g.ppvBits      = reinterpret_cast<std::uintptr_t>(s.pixels);
    g.dibPitch     = s.pitch;
    g.dibStride    = s.width;     // visible width (DrawGlyph clip: x+5 <= width)
    g.screenHeight = s.height;
    g.pitchExtra   = 2;           // bytes per pixel (16bpp)
    g.lockBitDepth = 16;
    g.primary      = nullptr;
    return g;
}

// Draw one caption line via the REAL render::DrawText leaf. Returns the chars
// submitted (DrawText advances 6px per char; space advances without drawing).
int DrawLine16(render::Surface& s, int x, int y, const char* text,
               u8 r, u8 g, u8 b) {
    if (!text || !text[0]) return 0;
    render::PresentGlobals pg = SurfacePresent16(s);
    render::DrawText(x, y, reinterpret_cast<const u8*>(text), r, g, b,
                     GlyphMap(), pg, s.fmt);
    return static_cast<int>(std::strlen(text));
}

} // namespace

// ---------------------------------------------------------------------------
const char* SessionHud::missingDecode() {
    return "VIBE_Shape_ConvertRgbTo16 @0x5d7c0c (24bpp RLE shape -> 16bpp RLE; "
           "with VIBE_Shape_Convert8To16 @0x5d7924), the per-shape converter "
           "VIBE_ShapeBank_ConvertNew @0x5d80a8 runs from VIBE_State_Helper "
           "@0x40e014 — not reconstructed (render_leaves9 inert hook), so the "
           "depth-2 gilde.gfx banks cannot be converted to the depth-1 banks "
           "render::ShapeShowFromBank @0x5d861c rasterizes";
}

// ---------------------------------------------------------------------------
bool SessionHud::Init(shim::IFileSystem* fs) {
    gfxLoaded_      = false;
    gfxObjectCount_ = 0;
    bankCount_      = 0;
    fmt2Banks_      = 0;
    depth1Banks_    = 0;
    fs_             = fs;   // stashed for DecodePanelChrome (font + strings)

    // Always wire the sprite hook to the REAL 2D blit leaf chain
    // (ShapeShowFromBank @0x5d861c -> ShapeBlitColored16 @0x5d7164) over the
    // documented DefaultHudSpriteBank — the fallback the named gap mandates.
    InstallRealHudBridge();

    if (!fs) return false;
    shim::IFile* f = fs->open("gfx/gilde.gfx", "rb");
    if (!f) return false;
    const std::int64_t sz = f->size();
    if (sz <= 0) { fs->close(f); return false; }
    std::vector<u8> bytes(static_cast<std::size_t>(sz));
    f->seek(0, 0 /*SEEK_SET*/);
    const std::size_t got = f->read(bytes.data(), bytes.size());
    fs->close(f);
    if (got != bytes.size()) return false;

    // REAL table parse — the file half of VIBE_Gui_LoadGfxFile @0x41b888:
    // u32 objectCount (cap 2048) + count x 84-byte records into g_gfxObjects,
    // plus the Form/Window/Widget baseline init.
    if (!gui::Form_LoadFromBuffer(bytes.data(), bytes.size()))
        return false;
    gfxObjectCount_ = gui::g_gfxObjectCount;

    // Investigation: walk the records' data blobs and classify each SHAPBANK's
    // pixel format byte (@bank+52 — the byte VIBE_State_Helper @0x40e014 tests
    // against the screen format before ShapeBank_ConvertNew @0x5d80a8).
    const std::size_t len = bytes.size();
    for (int i = 0; i < gfxObjectCount_; ++i) {
        const std::size_t base = 4 + static_cast<std::size_t>(i) * 84;
        if (base + 84 > len) break;
        const u32 off  = RdU32(bytes.data() + base + 48);
        const u32 size = RdU32(bytes.data() + base + 56);
        if (!off || !size || static_cast<std::size_t>(off) + 53 > len) continue;
        if (std::memcmp(bytes.data() + off, "SHAPBANK", 8) != 0) continue;
        ++bankCount_;
        const u8 fmt = bytes[off + 52];
        if (fmt == 2) ++fmt2Banks_;
        if (fmt == 1) ++depth1Banks_;   // a bank ShapeShowFromBank could blit
    }

    // Directory + reconstructed depth-2 pixel decode (DecodeShapeBlob, 1:1 of
    // the VIBE_FrameTable_Index @0x5fbb24 row-table RLE walk) over the same
    // bytes, for callers that want the decoded REAL artwork pixels.
    if (!archive_.LoadFromMemory(std::move(bytes)))
        return false;

    gfxLoaded_ = true;
    return true;
}

// ---------------------------------------------------------------------------
bool SessionHud::DecodePanelChrome(const char* panelName, const char* crestName) {
    panelChrome_ = render::DecodedShape{};
    cityCrest_   = render::DecodedShape{};
    if (!gfxLoaded_ || !panelName)
        return false;
    if (!archive_.DecodeShapeByName(panelName, 0, panelChrome_))
        panelChrome_ = render::DecodedShape{};
    if (crestName && *crestName &&
        !archive_.DecodeShapeByName(crestName, 0, cityCrest_))
        cityCrest_ = render::DecodedShape{};

    // Chrome CONTENT: the gold gothic banner font (_FONT, the main-menu button
    // face — the banner/date/money face in the original), the red button
    // 3-slice (_BUTTON_RED shapes 0/1/2), and the localized strings.
    if (fs_) {
        bannerFont_.Load(*fs_, "gfx/gilde.gfx", "_FONT");
        smallFont_.Load(*fs_, "gfx/gilde.gfx", "_FONT+1");
    }
    if (!archive_.DecodeShapeByName("_BUTTON_RED", 0, btnCapL_)) btnCapL_ = render::DecodedShape{};
    if (!archive_.DecodeShapeByName("_BUTTON_RED", 1, btnCapR_)) btnCapR_ = render::DecodedShape{};
    if (!archive_.DecodeShapeByName("_BUTTON_RED", 2, btnMid_))  btnMid_  = render::DecodedShape{};

    if (fs_ && fs_->exists("Resources/textbin_deutsch.BIN")) {
        io::ArchiveMount mount;
        if (mount.Mount(fs_, "Resources/textbin_deutsch.BIN", /*caseInsensitive=*/true)) {
            gui::text::TextDb db;
            for (const io::ArchiveMember& mem : mount.members()) {
                const std::string& n = mem.name;
                if (n.size() < 4) continue;
                std::string ext = n.substr(n.size() - 4);
                for (char& c : ext) c = (char)std::tolower((unsigned char)c);
                if (ext != ".res") continue;
                std::vector<u8> b;
                if (mount.OpenMember(n.c_str(), b) && !b.empty())
                    gui::text::BuildTextArray(b.data(), b.size(), db);
            }
            textDb_ = std::move(db);           // persist for the panel layer
            textDbLoaded_ = true;
            gui::text::TextDb& tdb = textDb_;
            auto get = [&](const char* key) -> std::string {
                const int i = tdb.FindIndex(key);
                return (i >= 0 && tdb.Text(i)) ? tdb.Text(i) : std::string();
            };
            title_ = get("_TITEL_MAENNLICH+1");           // "Господин" (rank-1 male title)
            for (int k = 0; k < 4; ++k)                    // Весна/Лето/Осень/Зима
                seasons_[k] = get(("_JAHRESZEITEN+" + std::to_string(k)).c_str());
            optLabel_ = get("_INFOPANEL_OPTIONEN+0");      // "Опции"
            buildLabel_    = get("_INFOPANEL_BAUEN+0");    // "Стройка"
            overviewLabel_ = get("_INFOPANEL_OPTIONEN+4"); // "Обзор"
            infoLabel_     = get("_INFOPANEL_OPTIONEN+2"); // "Информация"
            // "Транспорт": the $[..$] heading of the transport-window text (the
            // localized DB has no standalone button entry; the heading is the
            // same word the button shows, trailing space trimmed).
            std::string t = get("_NEV_TRANSPORT_STADTLOCATION+0");
            const std::size_t o = t.find("$[");
            const std::size_t e = (o == std::string::npos) ? o : t.find("$]", o + 2);
            if (e != std::string::npos) {
                t = t.substr(o + 2, e - (o + 2));
                while (!t.empty() && t.back() == ' ') t.pop_back();
                transLabel_ = t;
            }
        }
    }
    // Card-text renderer for the sidebar info card: the small gold face,
    // centred (the same DrawGold565 leaf the buttons/money use).
    panels_.cardText.user = this;
    panels_.cardText.draw = [](render::Surface& s, int cx, int y, int maxW,
                               const char* text, void* user) -> int {
        auto* self = static_cast<SessionHud*>(user);
        const MenuFont& f = self->smallFont_;
        if (!f.loaded() || !text || !*text) return 0;
        // Word-wrap on spaces to maxW (the card's width), centred per line.
        const int lh = (f.lineHeight() > 0 ? f.lineHeight() : 12) + 1;
        std::string word, lineStr;
        std::vector<std::string> lines;
        for (const char* p2 = text;; ++p2) {
            if (*p2 && *p2 != ' ') { word += *p2; continue; }
            if (!word.empty()) {
                std::string cand = lineStr.empty() ? word : lineStr + " " + word;
                if (!lineStr.empty() && f.MeasureWidth(cand.c_str()) > maxW) {
                    lines.push_back(lineStr);
                    lineStr = word;
                } else {
                    lineStr = cand;
                }
                word.clear();
            }
            if (!*p2) break;
        }
        if (!lineStr.empty()) lines.push_back(lineStr);
        int n = 0;
        for (const std::string& L : lines) {
            if (DrawGold565(s, f, cx, y + n * lh, L.c_str(), /*centerX=*/true))
                ++n;
        }
        return n > 0 ? n : (int)lines.size();
    };

    return panelChrome_.width > 0;
}

namespace {
// Draw a gold-gothic MenuFont string onto the 565 surface: render into a
// transparent ARGB scratch, then alpha-keyed blit 1:1. `centerX` centres the
// run on x when true. Returns the drawn pixel width.
int DrawGold565(render::Surface& s, const MenuFont& fnt, int x, int y,
                const char* text, bool centerX) {
    if (!fnt.loaded() || !text || !*text) return 0;
    const int tw = fnt.MeasureWidth(text);
    const int th = fnt.lineHeight() > 0 ? fnt.lineHeight() : 17;
    if (tw <= 0) return 0;
    std::vector<u32> scratch((std::size_t)tw * th, 0u);
    fnt.DrawText(scratch.data(), tw, th, 0, 0, text, 1, 0, 0, 0, false);
    const int x0 = centerX ? x - tw / 2 : x;
    for (int yy = 0; yy < th; ++yy) {
        const int Y = y + yy;
        if (Y < 0 || Y >= s.height) continue;
        u16* drow = reinterpret_cast<u16*>(s.pixels + (std::size_t)Y * s.pitch);
        const u32* srow = scratch.data() + (std::size_t)yy * tw;
        for (int xx = 0; xx < tw; ++xx) {
            const int X = x0 + xx;
            if (X < 0 || X >= s.width) continue;
            const u32 p = srow[xx];
            if (!(p & 0xFF000000u)) continue;
            const u8 r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
            drow[X] = (u16)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }
    return tw;
}

// Nearest-neighbour ARGB -> RGB565 alpha-keyed scaled blit onto the session
// surface (A==0 texels skipped — the panel's transparent 3D-view center).
void BlitArgbTo565(render::Surface& s, const render::DecodedShape& sh,
                   int dx, int dy, int dw, int dh) {
    if (sh.width <= 0 || sh.height <= 0 || dw <= 0 || dh <= 0)
        return;
    for (int y = 0; y < dh; ++y) {
        const int Y = dy + y;
        if (Y < 0 || Y >= s.height) continue;
        const int sy = y * sh.height / dh;
        const u32* srow = sh.argb.data() + (std::size_t)sy * sh.width;
        u16* drow = reinterpret_cast<u16*>(s.pixels + (std::size_t)Y * s.pitch);
        for (int x = 0; x < dw; ++x) {
            const int X = dx + x;
            if (X < 0 || X >= s.width) continue;
            const u32 p = srow[x * sh.width / dw];
            if (!(p & 0xFF000000u)) continue;
            const u8 r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
            drow[X] = (u16)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }
}
} // namespace

// ---------------------------------------------------------------------------
void SessionHud::Render(void* fb16, int w, int h, int pitchBytes,
                        const Inputs& in) {
    last_ = SessionHudResult{};
    if (!fb16 || w <= 0 || h <= 0 || pitchBytes < 2 * w || (pitchBytes & 1))
        return;

    // Wrap the session framebuffer as a 16bpp RGB565 render::Surface (the
    // record VIBE_Surface_Create @0x42311c fills; pixels at +0x1C, widthPx
    // stride at +0x10, clip rect [0,w)x[0,h)).
    render::Surface s{};
    s.width   = w;
    s.height  = h;
    s.pitch   = pitchBytes;
    s.widthPx = pitchBytes / 2;
    s.bpp     = 16;
    s.pixels  = static_cast<u8*>(fb16);
    s.clipX0  = 0;
    s.clipY0  = 0;
    s.clipX1  = w;
    s.clipY1  = h;
    s.fmt     = render::Format565();

    const HudPalette pal;                       // reconstructed HUD palette
    const HudRenderHooks& hooks = GetHudRenderHooks();
    const int mapX = layout.mapX >= 0 ? layout.mapX : (w > 140 ? w - 140 : 0);
    const int mapY = layout.mapY;

    // -----------------------------------------------------------------------
    // 0. Panel chrome — the real _PANEL_* screen furniture (gold top banner +
    //    right sidebar, transparent centre) scaled over the whole frame, then
    //    the city crest into the sidebar crest slot (design 800x600: crest
    //    centre ~(744,145)). Drawn FIRST so every HUD element lands on top.
    // -----------------------------------------------------------------------
    const bool chrome = panelChrome_.width > 0;
    if (chrome) {
        BlitArgbTo565(s, panelChrome_, 0, 0, w, h);
        if (cityCrest_.width > 0) {
            const int cw = cityCrest_.width  * w / 800;
            const int ch = cityCrest_.height * h / 600;
            BlitArgbTo565(s, cityCrest_, 744 * w / 800 - cw / 2,
                          145 * h / 600 - ch / 2, cw, ch);
        }
        if (bannerFont_.loaded()) {
            // Top banner: "<title> <player>" left, "<season> A.D.<year>"
            // centre-right — the original's gold gothic face. Season/year from
            // the day counter: season = day % 4 (GetSeasonFromDay @0x58339c),
            // year = 1400 + day / 4 (4 season-days per year).
            std::string name = in.playerName ? in.playerName : "";
            std::string banner = title_;
            if (!name.empty()) banner += (banner.empty() ? "" : " ") + name;
            if (!banner.empty())
                DrawGold565(s, bannerFont_, 66 * w / 800, 17 * h / 600,
                            banner.c_str(), /*centerX=*/false);
            const int day = in.clock ? in.clock->day : 0;
            if (!seasons_[day % 4].empty()) {
                char date[64];
                std::snprintf(date, sizeof(date), "%s A.D.%d",
                              seasons_[day % 4].c_str(), 1400 + day / 4);
                DrawGold565(s, bannerFont_, 480 * w / 800, 17 * h / 600,
                            date, /*centerX=*/true);
            }
            // Sidebar money slot: the ENGINE money string (trailing 0x11
            // currency glyph — the font's gold-coin ligature), small face.
            const MenuFont& sf = smallFont_.loaded() ? smallFont_ : bannerFont_;
            {
                i64 mm = in.money;
                if (mm > std::numeric_limits<i32>::max()) mm = std::numeric_limits<i32>::max();
                if (mm < std::numeric_limits<i32>::min()) mm = std::numeric_limits<i32>::min();
                const std::string ms =
                    world::MoneyFormatWithSeparators((i32)mm, in.moneyRate);
                DrawGold565(s, sf, 745 * w / 800, 480 * h / 600,
                            ms.c_str(), /*centerX=*/true);
            }
            // Sidebar red buttons (Опции / Транспорт): _BUTTON_RED 3-slice at
            // the sidebar slots, SMALL gold label centred (the original's
            // sidebar face).
            auto redButton = [&](int bx, int by, int bw, int bh, const std::string& label) {
                if (btnCapL_.width <= 0 || btnMid_.width <= 0 || btnCapR_.width <= 0)
                    return;
                const int capW = btnCapL_.width * w / 800;
                int midW = bw - 2 * capW;
                if (midW < 0) midW = 0;
                BlitArgbTo565(s, btnCapL_, bx, by, capW, bh);
                BlitArgbTo565(s, btnMid_, bx + capW, by, midW, bh);
                BlitArgbTo565(s, btnCapR_, bx + capW + midW, by, capW, bh);
                if (!label.empty()) {
                    const int th2 = sf.lineHeight() > 0 ? sf.lineHeight() : 12;
                    DrawGold565(s, sf, bx + bw / 2, by + (bh - th2) / 2,
                                label.c_str(), /*centerX=*/true);
                }
            };
            // Selection-state pair (live captures): idle = Стройка/Обзор,
            // building selected = Информация/Транспорт. Falls back to the
            // generic Опции pair when a label is missing from the text db.
            const bool haveSel = in.selectedId != 0;
            const std::string& top =
                haveSel ? (!infoLabel_.empty() ? infoLabel_ : optLabel_)
                        : (!buildLabel_.empty() ? buildLabel_ : optLabel_);
            const std::string& bottom =
                haveSel ? transLabel_
                        : (!overviewLabel_.empty() ? overviewLabel_ : transLabel_);
            redButton(700 * w / 800, 404 * h / 600, 92 * w / 800, 22 * h / 600, top);
            redButton(700 * w / 800, 432 * h / 600, 92 * w / 800, 22 * h / 600, bottom);

            // Floating selection label (under the HAUSPFEIL marker): the
            // building name, small gold face, centred, wrapped to two lines.
            if (in.labelText && in.labelText[0] && sf.loaded()) {
                const std::string full = in.labelText;
                std::string l1 = full, l2;
                if (sf.MeasureWidth(full.c_str()) > 170) {
                    // break at the space nearest the middle
                    std::size_t best = std::string::npos;
                    for (std::size_t sp = full.find(' '); sp != std::string::npos;
                         sp = full.find(' ', sp + 1)) {
                        if (best == std::string::npos ||
                            std::llabs((long long)sp - (long long)full.size() / 2) <
                                std::llabs((long long)best - (long long)full.size() / 2))
                            best = sp;
                    }
                    if (best != std::string::npos) {
                        l1 = full.substr(0, best);
                        l2 = full.substr(best + 1);
                    }
                }
                const int lh = sf.lineHeight() > 0 ? sf.lineHeight() : 12;
                DrawGold565(s, sf, in.labelX, in.labelY, l1.c_str(), /*centerX=*/true);
                if (!l2.empty())
                    DrawGold565(s, sf, in.labelX, in.labelY + lh + 1, l2.c_str(), true);
            }
        }
    }

    // -----------------------------------------------------------------------
    // 1. Bottom player bar — the VIBE_PlayerBar_BuildContent @0x4b11e4 model:
    //    real slot assignment (PlayerBar_AssignSlot de-dup/free-scan), real
    //    78px-pitch layout (PlayerBar_SlotLayout), icon sprite through the
    //    REAL ShapeShowFromBank hook, then track + production fill + frame.
    // -----------------------------------------------------------------------
    gui::ResetPlayerBar();
    for (int i = 0; in.barObjects && i < in.barObjectCount; ++i) {
        const HudBarObject& obj = in.barObjects[i];
        const int slot = gui::PlayerBar_AssignSlot(obj.objId);
        if (slot < 0) continue;                  // bar full (>32)
        const gui::PlayerBarLayout L = gui::PlayerBar_SlotLayout(slot);

        const int subX = layout.barX + L.subWinX;
        const int subY = layout.barY + L.subWinY;

        if (hooks.drawSprite &&
            hooks.drawSprite(&s, layout.barX + L.spriteX,
                             layout.barY + L.spriteY, /*gfx*/ 1403,
                             hooks.userData))
            ++last_.spriteBlits;

        gui::MenuFillRect(&s, subX, subY, L.subWinW, L.subWinH,
                          pal.barTrackR, pal.barTrackG, pal.barTrackB);

        const int fillPx = HudBarFillPixels(obj.ratio, L.subWinW);
        if (fillPx > 0)
            last_.barFillRows += gui::MenuFillRect(
                &s, subX, subY, fillPx, L.subWinH,
                pal.barFillR, pal.barFillG, pal.barFillB);

        render::SurfaceDrawRectOutline(&s, subX, subY, L.subWinW, L.subWinH,
                                       pal.barFrameR, pal.barFrameG,
                                       pal.barFrameB);
        ++last_.barSlotsDrawn;
    }

    // -----------------------------------------------------------------------
    // 2. Money + game date/time caption. Money through the REAL
    //    world::MoneyFormatWithSeparators @0x58f798 (HudMoneyString); the
    //    time-of-day through the REAL gui::Clock_ComputeTimeOfDay @0x527778
    //    over the tick accumulator (HudDateString); the calendar day from the
    //    sim::GameTime record. The engine's money is 32-bit — clamp the i64.
    // -----------------------------------------------------------------------
    i64 m = in.money;
    if (m > std::numeric_limits<i32>::max()) m = std::numeric_limits<i32>::max();
    if (m < std::numeric_limits<i32>::min()) m = std::numeric_limits<i32>::min();
    const std::string money = HudMoneyString(static_cast<i32>(m), in.moneyRate);
    const int day = in.clock ? in.clock->day : 0;
    const std::string date = HudDateString(day, in.clockTick);

    if (!(chrome && bannerFont_.loaded())) {
        // Asset-less caption (the 5x7 debug face). With the chrome + gold font
        // active the banner/date/money draw in the real face above instead.
        const int moneyX = layout.moneyX >= 0 ? layout.moneyX : layout.captionX;
        const int moneyY = layout.moneyY >= 0 ? layout.moneyY : layout.captionY;
        last_.captionGlyphs += DrawLine16(s, moneyX, moneyY, money.c_str(),
                                          pal.textR, pal.textG, pal.textB);
        last_.captionGlyphs += DrawLine16(s, layout.captionX, layout.captionY + 9,
                                          date.c_str(),
                                          pal.textR, pal.textG, pal.textB);
    }

    // -----------------------------------------------------------------------
    // 3. Selected-entity status line. Register the selection in the REAL
    //    50-dword-stride status-text table (StatusText_Register @0x4bcc80 —
    //    de-dup by key, first-free-slot alloc), then stamp the line through
    //    the same DrawText leaf.
    // -----------------------------------------------------------------------
    if (in.selectedId != 0) {
        last_.statusSlot = gui::StatusText_Register(in.selectedId, /*tag*/ 0);
        char line[96];
        if (in.selectedName && in.selectedName[0])
            std::snprintf(line, sizeof(line), "%s", in.selectedName);
        else
            std::snprintf(line, sizeof(line), "OBJEKT %d", in.selectedId);
        last_.statusGlyphs += DrawLine16(s, layout.statusX, layout.statusY,
                                         line, pal.textR, pal.textG, pal.textB);
    }

    // -----------------------------------------------------------------------
    // 4. Map markers — project each world position through the REAL
    //    gui::MapView_ComputeMarkerScreenPos @0x5440b4 (HudMarkerScreenXY),
    //    optional marker artwork through the sprite hook, then the filled
    //    dot + outline at (mapOrigin + projected).
    // -----------------------------------------------------------------------
    const int kMarkerSize = 4;
    for (int i = 0; in.markers && i < in.markerCount; ++i) {
        const MarkerXY xy = HudMarkerScreenXY(in.markers[i], in.markerPanX,
                                              in.markerPanY,
                                              in.markerCameraOrigX);
        const int mx = mapX + xy.x;
        const int my = mapY + xy.y;

        if (hooks.drawSprite &&
            hooks.drawSprite(&s, mx, my, /*gfx*/ 1404, hooks.userData))
            ++last_.spriteBlits;

        gui::MenuFillRect(&s, mx, my, kMarkerSize, kMarkerSize,
                          pal.markerR, pal.markerG, pal.markerB);
        render::SurfaceDrawRectOutline(&s, mx, my, kMarkerSize, kMarkerSize,
                                       pal.markerEdgeR, pal.markerEdgeG,
                                       pal.markerEdgeB);
        ++last_.markersDrawn;
    }

    // -----------------------------------------------------------------------
    // 5. Wave-2 panel layer (ADDITIVE): the REAL tooltip lifecycle
    //    (VIBE_Tooltip_DispatchByType @0x4f7424 + the content builders) and
    //    the REAL selected-entity info panel (VIBE_InfoPanel_Update @0x4b84c0
    //    + the @0x4b64b0.. builders), composited through the same 16bpp
    //    leaves.  Inputs.panels == null keeps the legacy frame byte-identical.
    // -----------------------------------------------------------------------
    if (in.panels) {
        panels_.Frame(s, *in.panels);
        const SessionPanelsResult& pr = panels_.lastResult();
        last_.tooltipVisible = pr.tooltipVisible;
        last_.tooltipTextOps = pr.tooltipTextOps;
        last_.tooltipIconOps = pr.tooltipIconOps;
        last_.panelVisible   = pr.panelVisible;
        last_.panelTextOps   = pr.panelTextOps;
        last_.panelIconOps   = pr.panelIconOps;
        last_.spriteBlits   += pr.tooltipIconBlits + pr.panelIconBlits;
    }
}

} // namespace guild::play
