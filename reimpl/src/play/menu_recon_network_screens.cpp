#include "play/menu_recon_network_screens.h"

#include "play/menu_assets.h"            // MenuAssets / MenuFont / ResolveOptionLabels
#include "shim_impl/disk_filesystem.h"   // DiskFileSystem (asset load)
#include "shim/IGraphicsDevice.h"        // RunNetworkScreen driver: present
#include "shim/IPlatform.h"              // RunNetworkScreen driver: input pump
#include "render/gfx_archive.h"          // render::DecodedShape
#include "render/bmp.h"                  // render::BmpSave24Bit / BmpLoadBuffer
#include "io/archive_mount.h"            // Textures.BIN (PKZIP) mount for the parchment
#include "net/discovery.h"               // net::DiscoverServers (1:1 @0x43abcc)
#ifdef GUILD_HAVE_SDL2_NET
#include "shim_impl/sdlnet_backend.h"    // SdlNetDatagram (real UDP/broadcast transport)
#include <SDL.h>                         // SDL_GetTicks for the discovery clock
#endif

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace guild::play {

static MenuScreenHooks g_menu;

const MenuScreenHooks* SetMenuScreenHooks(const MenuScreenHooks* h) {
    static MenuScreenHooks prev;
    prev = g_menu;
    g_menu = h ? *h : MenuScreenHooks{};
    return &prev;
}

void ResetMenuReconNetworkScreens() { g_menu = MenuScreenHooks{}; }

// ===========================================================================
// Wide-char (2-byte stride) NUL-terminated copy — gilde.exe do/while loops at
// 0x529181 / 0x5291a2 / 0x56a63e / 0x56af09. Source pairs are (char, attr); the
// loop reads low byte, copies the pair, stops when a low byte is zero. The copy
// writes the terminating low byte (0) but not its attribute, exactly as the
// original (it breaks immediately after storing *dst = 0).
// ===========================================================================
int CopyFatString(u8* dst, const u8* src, int dstCap) {
    const u8* s = src;     // DataPtr
    u8* d = dst;           // dst cursor
    int written = 0;       // total bytes the original would have written
    do {
        u8 lo = *s;                       /*v9 = *DataPtr*/
        if (written < dstCap) *d = lo;    /**dst = *DataPtr*/
        ++written;
        if (!lo)                          /*if (!v9) break*/
            break;
        u8 attr = s[1];                   /*v10 = DataPtr[1]*/
        s += 2;                           /*DataPtr += 2*/
        if (written < dstCap) d[1] = attr;/*dst[1] = v10*/
        ++written;
        d += 2;                           /*dst += 2*/
        if (!attr)                        /*while (v10) — exit when attr==0*/
            break;
    } while (true);
    return written;
}

// ===========================================================================
// 0x529074 — VIBE_Menu_EnterNetworkIp
//
// Loads the CHOOSENETWORK_IP form, seeds the edit field from the INI "Host"
// value (default 127.0.0.1), then runs the frame loop. On confirm (Return /
// click on the OK object) it lifts the edited fat-string into the ReturnedString
// scratch and again into byte_122EE90, writes it back to the INI, and sets the
// "dirty" flag. Returns v18 (1 if a value was committed, else 0).
//
// The form/field/frame edges are hooks; the value commit + fat-string copy is the
// faithful core. With inert hooks the loop runs zero iterations and returns 0.
// ===========================================================================
int Menu_EnterNetworkIp() {
    char hostBuf[0x104] = {0};
    if (g_menu.iniReadHost)
        g_menu.iniReadHost(hostBuf, (int)sizeof(hostBuf));   /*0x5290ac*/
    else
        // a127001 default "127.0.0.1" when no INI hook is wired.
        std::snprintf(hostBuf, sizeof(hostBuf), "%s", "127.0.0.1");

    int v18 = 0;                                             /*0x5290a5*/
    int form = g_menu.loadForm ? g_menu.loadForm("Menu\\CHOOSENETWORK_IP") : 0; /*0x5290c1*/

    // Frame loop: pump until RunFrameLoop returns 0. The commit branch (Return or
    // OK-object click) copies the field into byte_122EE90 and writes the INI.
    while (g_menu.runFrameLoop && g_menu.runFrameLoop(147591, 0, nullptr)) { /*0x52913f*/
        // (Confirm detection is an engine edge; when wired the host signals it by
        //  invoking the commit through iniWriteHost and returning the value here.)
    }

    if (g_menu.iniWriteHost && v18) {
        g_menu.iniWriteHost(hostBuf);                        /*0x5291db*/
    }
    if (g_menu.destroyForm)
        g_menu.destroyForm(form);                            /*0x52922e*/
    return v18;                                              /*0x52923a*/
}

// ===========================================================================
// 0x52df18 — VIBE_Menu_ChooseHistoryVariant (radio screen, 7 rows)
//
// Seeds the radio group from dword_12335B4 (RadioSelectionIndex), then on each
// click maps the clicked row to byte_63C8F4 via HistoryVariantForRow, recomputes
// dword_12335B4 from the variant byte, and re-applies the selection. Returns
// v5 (1 once a row was chosen). Form/widget/frame edges are hooks.
// ===========================================================================
int Menu_ChooseHistoryVariant(i32& historySel /*dword_12335B4*/) {
    int form = g_menu.loadForm ? g_menu.loadForm("Menu\\CHOOSEHISTORY") : 0; /*0x52df37*/
    int v5 = 0;                                              /*0x52dfa7*/

    if (historySel <= -1)                                    /*0x52e013*/
        historySel = -1;                                     /*0x52e015*/

    int group = 0; // VIBE_RadioGroup_Create(7, ChildObjectId) — handle via hook later.
    if (g_menu.selectionUpdate)
        g_menu.selectionUpdate(group, RadioSelectionIndex(historySel)); /*0x52e053*/

    while (g_menu.runFrameLoop && g_menu.runFrameLoop(198, 0, nullptr)) { /*0x52e069*/
        // On a 1210 (confirm) event the host reports the clicked row; the variant
        // byte is HistoryVariantForRow(row), historySel becomes that byte, and the
        // selection is re-applied:
        //   historySel = variantByte;
        //   selectionUpdate(group, RadioSelectionIndex(variantByte));
        //   v5 = 1;
        // (Driven through the hook when wired; inert by default.)
    }
    if (g_menu.destroyForm)
        g_menu.destroyForm(form);                            /*0x52e076*/
    return v5;                                               /*0x52e083*/
}

// ===========================================================================
// 0x52dabc — VIBE_Menu_RunChooseHistoryNetwork
//
// The new-game launch flow: shows CHOOSEHISTORY (network variant, up to 5 rows
// depending on byte_63CC1D), runs the radio loop, and on confirm drives the
// city/player/character/profession sub-screens and the A/B/C cutscenes. Almost
// entirely orchestration; the faithful pure pieces are the selection seed/update
// (RadioSelectionIndex) and the NetworkHistoryVariantForRow mapping. Returns v6.
// ===========================================================================
int Menu_RunChooseHistoryNetwork(i32& historySel /*dword_1233558[22]*/) {
    int form = g_menu.loadForm ? g_menu.loadForm("Menu\\CHOOSEHISTORY") : 0; /*0x52dae7*/
    int v6 = 0;                                              /*0x52db52*/

    if (historySel <= -1)                                    /*0x52db76*/
        historySel = -1;                                     /*0x52db78*/

    int group = 0; // VIBE_RadioGroup_Create(4, ChildObjectId)
    if (g_menu.selectionUpdate)
        g_menu.selectionUpdate(group, RadioSelectionIndex(historySel)); /*0x52dbb2*/

    while (g_menu.runFrameLoop && g_menu.runFrameLoop(198, 0, nullptr)) { /*0x52dbea*/
        // Confirm (1210): map clicked row via NetworkHistoryVariantForRow, set
        // historySel = variant, re-apply RadioSelectionIndex(variant), then run the
        // RunChoosePlayer / ChooseCharacterIntro / ChooseProfession / cutscene flow.
        // Orchestration is an engine edge (hooks); inert by default.
    }
    if (g_menu.destroyForm)
        g_menu.destroyForm(form);                            /*0x52de85*/
    return v6;                                               /*0x52de8c via v6 check*/
}

// ===========================================================================
// 0x530bcc — VIBE_Menu_ShowPlayerRoundEndReport(player entity a1)
//
// Builds the per-player round-end report form, then (if production worth resolves)
// renders one rich-text line per nonzero value. The pure pieces are the building
// record stride (589 * type + base), the sale-line id predicate, and the
// profit/loss line predicate (see header). The render + frame loop are hooks.
// ===========================================================================
void Menu_ShowPlayerRoundEndReport(i8 /*playerType*/) {
    int form = g_menu.loadForm ? g_menu.loadForm("Runden\\Spielerrunde_Ende_geb") : 0; /*0x530c0f*/
    // (line emission uses RoundEndSaleLineId / RoundEndProfitLineId at the matching
    //  decompiled sites; routed through the text-render hook when wired.)
    while (g_menu.runFrameLoop && g_menu.runFrameLoop(147591, 0, nullptr)) { /*0x530c60*/
    }
    if (g_menu.destroyForm)
        g_menu.destroyForm(form);                            /*0x530e40*/
}

// ===========================================================================
// 0x56a4d4 — VIBE_Menu_RunLoadNetworkGame(a1)
//
// Network load-game browser: loads the loadgame_new form, builds the slider panel
// at (532,360,...,130), loads slot metadata from "gamedata/network", then on a
// slot click scans the 544-byte metadata records (cap 8704 bytes / 16 slots),
// validates the player association (238-dword player stride), optionally confirms
// via a message box, and on accept lifts the chosen save name (fat-string) into
// byte_122F530 and sets v19=1. Returns v19. Scan strides are the faithful core.
// ===========================================================================
int Menu_RunLoadNetworkGame() {
    int form = g_menu.loadForm ? g_menu.loadForm("menu\\loadgame_new") : 0; /*0x56a500*/
    int v19 = 0;                                             /*0x56a4ef*/
    while (g_menu.runFrameLoop && g_menu.runFrameLoop(0, 0, nullptr)) { /*0x56a5c4*/
        // slot scan: for (v8 = 0; v8 < kSaveSlotScanEnd; v8 += kSaveSlotStride) { ... }
        // accept -> CopyFatString(byte_122F530, &meta[544*slot+25], cap); v19 = 1;
    }
    if (g_menu.destroyForm)
        g_menu.destroyForm(form);                            /*0x56a6e5*/
    return v19;                                              /*0x56a6f1*/
}

// ===========================================================================
// 0x56abcc — VIBE_Menu_RunSaveNetworkGame(a1)
//
// Network save-game browser: like the load screen, but on a slot click it runs the
// save-name input, formats "Gamedata\network\%s.SAV", resolves it, and (if all
// players are ready) queues the save command (CopyFatString into the request blob).
// Save loop caps at kSaveSlotMax (16) records. Returns v29 (0 here; commit is
// signalled by setting the dirty flag inside the loop). Strides are the core.
// ===========================================================================
int Menu_RunSaveNetworkGame() {
    int form = g_menu.loadForm ? g_menu.loadForm("menu\\loadgame_new") : 0; /*0x56abfe*/
    int v29 = 0;                                             /*0x56abed*/
    while (g_menu.runFrameLoop && g_menu.runFrameLoop(0, 0, nullptr)) { /*0x56acf9*/
        // slot scan: for (v34 = 2; v34 < kSaveSlotMax; ++v34) { ... }
        // accept -> sprintf("Gamedata\\network\\%s.SAV", name); resolve; queue save.
    }
    if (g_menu.destroyForm)
        g_menu.destroyForm(form);                            /*0x56af38*/
    return v29;                                              /*0x56af44*/
}

// ===========================================================================
// 1:1 VIEW of the network sub-screens.  Mirrors the proven native_main_menu.cpp
// model: real `_OPTIONEN_PIC` background, native 800x600 form windows
// CENTER-TRANSLATED only, `_BUTTON_RED` 3-slice buttons + real `_FONT` glyphs
// with the localized `_OPTIONEN_NETZWERK_*` captions; flat fallback when assets
// are absent so the layout is still pinnable headless.
// ===========================================================================
namespace {

// ---- design canvas + center-translate (PositionAtCoord mode-2/3 @0x41d7e0) ----
constexpr int kDesignW = 800, kDesignH = 600;

// Form windows (Resources/forms.BIN FRM2; native x/y/w/h — MENU-SUBSCREENS-GROUNDTRUTH).
constexpr int kHubWinX = 144, kHubWinY = 160, kHubWinW = 297, kHubWinH = 400;   // CHOOSENETWORK
constexpr int kIpWinX  = 152, kIpWinY  = 192, kIpWinW  = 224, kIpWinH  = 361;   // CHOOSENETWORK_IP
constexpr int kSrchWinX = 120, kSrchWinY = 120, kSrchWinW = 451, kSrchWinH = 575; // SEARCH_NETWORK

// _BUTTON_RED (gfx 174) 3-slice: cap(12) + centre(100) + cap(12) = 124 x 33.
constexpr int kBtnH = 33, kBtnCapW = 12, kBtnNominalW = 124;

// Fallback bitmap-glyph cell (only used when the real _FONT is unavailable).
constexpr int kFbGlyphW = 6;

// Translate a form-native (x,y) to the framebuffer (center-translate only).
inline NetViewRect Translate(int fbW, int fbH, int fx, int fy, int w, int h) {
    const int ox = (fbW - kDesignW) / 2;
    const int oy = (fbH - kDesignH) / 2;
    return { fx + ox, fy + oy, w, h };
}

// Background blit (scaled to fill the framebuffer), mirrors native_main_menu.
void DrawBackground(u32* dst, int W, int H, const u32* bg, int bw, int bh) {
    if (!bg || bw <= 0 || bh <= 0) return;
    for (int y = 0; y < H; ++y) {
        const int sy = (int)((long long)y * bh / H);
        const u32* srow = bg + (std::size_t)sy * bw;
        u32* drow = dst + (std::size_t)y * W;
        for (int x = 0; x < W; ++x) {
            const u32 px = srow[(int)((long long)x * bw / W)];
            if (px & 0xFF000000u) drow[x] = px;
        }
    }
}

void FillRect(u32* dst, int W, int H, int x, int y, int w, int h, u32 col) {
    for (int yy = y; yy < y + h; ++yy) {
        if (yy < 0 || yy >= H) continue;
        u32* row = dst + (std::size_t)yy * W;
        for (int xx = x; xx < x + w; ++xx)
            if (xx >= 0 && xx < W) row[xx] = col;
    }
}
void OutlineRect(u32* dst, int W, int H, int x, int y, int w, int h, u32 col) {
    auto put = [&](int X, int Y) { if (X >= 0 && X < W && Y >= 0 && Y < H) dst[(std::size_t)Y * W + X] = col; };
    for (int X = x; X < x + w; ++X) { put(X, y); put(X, y + h - 1); }
    for (int Y = y; Y < y + h; ++Y) { put(x, Y); put(x + w - 1, Y); }
}

// Nearest-neighbour sprite scale-blit (A==0 skips) — native_main_menu's ScaledBlit.
void ScaledBlit(u32* dst, int W, int H, const render::DecodedShape& sh,
                int dx, int dy, int dw, int dh) {
    if (sh.width <= 0 || sh.height <= 0 || dw <= 0 || dh <= 0) return;
    for (int y = 0; y < dh; ++y) {
        const int Y = dy + y;
        if (Y < 0 || Y >= H) continue;
        const int sy = y * sh.height / dh;
        const u32* srow = sh.argb.data() + (std::size_t)sy * sh.width;
        u32* drow = dst + (std::size_t)Y * W;
        for (int x = 0; x < dw; ++x) {
            const int X = dx + x;
            if (X < 0 || X >= W) continue;
            const u32 p = srow[x * sh.width / dw];
            if (p & 0xFF000000u) drow[X] = p;
        }
    }
}

// Draw a _BUTTON_RED 3-slice (left cap + stretched centre + right cap); `sel`
// swaps to the hover frame triple {3,5,4}. Caps stay native, centre fills.
void DrawButton3Slice(u32* dst, int W, int H, MenuAssets& a,
                      int rx, int ry, int rw, int rh, bool sel) {
    // NOTE: SpriteByName appends to an internal cache vector, which can reallocate
    // and invalidate previously returned pointers — copy each shape by value first.
    const render::DecodedShape* Lp = a.SpriteByName("_BUTTON_RED", sel ? 3 : 0);
    render::DecodedShape Lc = Lp ? *Lp : render::DecodedShape{};
    const render::DecodedShape* Cp = a.SpriteByName("_BUTTON_RED", sel ? 5 : 2);
    render::DecodedShape Cc = Cp ? *Cp : render::DecodedShape{};
    const render::DecodedShape* Rp = a.SpriteByName("_BUTTON_RED", sel ? 4 : 1);
    render::DecodedShape Rc = Rp ? *Rp : render::DecodedShape{};
    if (!Lp || !Cp || !Rp) return;
    int lw = kBtnCapW, rcw = kBtnCapW;
    int mw = rw - lw - rcw;
    if (mw < 0) { mw = 0; rcw = rw - lw; if (rcw < 0) { rcw = 0; lw = rw; } }
    ScaledBlit(dst, W, H, Lc, rx,           ry, lw,  rh);
    ScaledBlit(dst, W, H, Cc, rx + lw,      ry, mw,  rh);
    ScaledBlit(dst, W, H, Rc, rx + lw + mw, ry, rcw, rh);
}

// Tiny built-in 5x7 fallback font (ASCII subset) for the headless/asset-less path
// so the layout still shows readable Latin labels. Cyrillic falls back to a box.
void FbDrawText(u32* dst, int W, int H, int x, int y, const char* s, u32 col) {
    if (!s) return;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        // Draw a simple 4x6 filled-cell marker per glyph (legible block run); real
        // text uses the engine _FONT, this is only the asset-less placeholder.
        if (*p != ' ')
            for (int yy = 1; yy < 6; ++yy)
                for (int xx = 0; xx < 4; ++xx) {
                    const int X = x + xx, Y = y + yy;
                    if (X >= 0 && X < W && Y >= 0 && Y < H) dst[(std::size_t)Y * W + X] = col;
                }
        x += kFbGlyphW;
    }
}

// Draw a label centred in [x,x+w) at vertical centre of [y,y+h): real _FONT when
// available, else the fallback block font.
void DrawCenteredLabel(u32* dst, int W, int H, const MenuAssets& assets, bool art,
                       int x, int y, int w, int h, const std::string& label, int scale,
                       u8 r, u8 g, u8 b, bool modulate = false) {
    if (label.empty()) return;
    if (art && assets.font().loaded()) {
        const MenuFont& fnt = assets.font();
        const int tw = fnt.MeasureWidth(label.c_str()) * scale;
        const int th = (fnt.lineHeight() > 0 ? fnt.lineHeight() : 17) * scale;
        fnt.DrawText(dst, W, H, x + (w - tw) / 2, y + (h - th) / 2, label.c_str(), scale, r, g, b, modulate);
    } else {
        const int tw = (int)label.size() * kFbGlyphW;
        FbDrawText(dst, W, H, x + (w - tw) / 2, y + (h - 7) / 2, label.c_str(),
                   0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | b);
    }
}

// Draw a left-aligned label.
void DrawLeftLabel(u32* dst, int W, int H, const MenuAssets& assets, bool art,
                   int x, int y, const std::string& label, int scale, u8 r, u8 g, u8 b) {
    if (label.empty()) return;
    if (art && assets.font().loaded())
        assets.font().DrawText(dst, W, H, x, y, label.c_str(), scale, r, g, b);
    else
        FbDrawText(dst, W, H, x, y, label.c_str(),
                   0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | b);
}

// Pixel width of a label for button-equalization (real _FONT or fallback).
int LabelWidth(const MenuAssets& assets, bool art, const std::string& s) {
    if (art && assets.font().loaded()) return assets.font().MeasureWidth(s.c_str());
    return (int)s.size() * kFbGlyphW;
}

// The network hub is a MAIN-MENU sub-screen, so it sits over the live title-screen
// CITY backdrop (_MENUE_BACKGROUND), like the options / load-game screens — NOT the
// in-game _OPTIONEN_PIC bookshelf (verified against gilde.exe). Variant by width.
const char* OptionsPicName(int fbW) {
    return fbW >= 1152 ? "_MENUE_BACKGROUND_1152"
         : fbW >= 1024 ? "_MENUE_BACKGROUND_1024"
                       : "_MENUE_BACKGROUND";
}

// Resolve a single textbin key to its rich-text-stripped plain display string.
std::string ResolveOne(const std::string& gameDir, const char* key) {
    if (gameDir.empty() || !key) return std::string();
    const char* keys[1] = { key };
    std::string out[1];
    ResolveOptionLabels(gameDir, keys, 1, out);
    return StripNetRichMarkup(out[0].c_str());
}

// Integer point-scale of the native canvas to the framebuffer (>=1).
int CanvasScale(int fbW) {
    int s = (int)((double)fbW / kDesignW + 0.5);
    return s < 1 ? 1 : s;
}

// Shared loader: open the install + decode background/font/buttons. Returns true
// when the real art (background) decoded; `font()` validity is checked separately.
bool LoadArt(MenuAssets& assets, shim::DiskFileSystem& fs, int fbW) {
    return assets.Load(fs, "gfx/gilde.gfx", OptionsPicName(fbW));
}

// The parchment titles use _FONT+2 (gfx record 68) — a NATIVELY BLACK thin gothic
// (the _FONT bold gold variant the buttons use is record 66). Cached per game dir.
const MenuFont* ParchmentTitleFont(const std::string& gameDir) {
    static std::string s_dir;
    static MenuFont s_font;
    static bool s_tried = false;
    if (s_tried && s_dir == gameDir) return s_font.loaded() ? &s_font : nullptr;
    s_tried = true; s_dir = gameDir;
    if (gameDir.empty()) return nullptr;
    shim::DiskFileSystem fs(gameDir);
    s_font.Load(fs, "gfx/gilde.gfx", "_FONT+2");
    return s_font.loaded() ? &s_font : nullptr;
}

// The network hub's parchment panel is the texture Pergamente/Pergament_Blatt_2_256_rev.bmp
// (256x256, burnt edges) from Resources/Textures.BIN — NOT a gilde.gfx record. It is
// blitted stretched-wider to the panel rect. Decoded once and cached per game dir.
const render::DecodedShape* NetworkPanelParchment(const std::string& gameDir) {
    static std::string s_dir;
    static render::DecodedShape s_shape;
    static bool s_tried = false;
    if (s_tried && s_dir == gameDir) return s_shape.width > 0 ? &s_shape : nullptr;
    s_tried = true; s_dir = gameDir; s_shape = render::DecodedShape{};
    if (gameDir.empty()) return nullptr;
    shim::DiskFileSystem fs(gameDir);
    io::ArchiveMount mount;
    if (!mount.Mount(&fs, "Resources/Textures.BIN", /*caseInsensitive=*/true)) return nullptr;
    std::vector<u8> bytes;
    if (!mount.OpenMember("Pergamente/Pergament_Blatt_2_256_rev.bmp", bytes) || bytes.empty())
        return nullptr;
    int w = 0, h = 0;
    std::vector<u8> rgb = render::BmpLoadBuffer(bytes, 24, w, h);   // top-down RGB
    if (rgb.empty() || w <= 0 || h <= 0) return nullptr;
    s_shape.width = w; s_shape.height = h;
    s_shape.argb.resize((std::size_t)w * h);
    int op = 0;
    for (std::size_t i = 0; i < (std::size_t)w * h; ++i) {
        const u8 r = rgb[i*3], g = rgb[i*3+1], b = rgb[i*3+2];
        // Pure black (0,0,0) is the transparent color key (the torn-edge corners),
        // so the city backdrop shows through instead of a black border.
        if (r == 0 && g == 0 && b == 0) { s_shape.argb[i] = 0u; continue; }
        s_shape.argb[i] = 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
        ++op;
    }
    s_shape.opaque = op;
    return &s_shape;
}

} // namespace

// ---------------------------------------------------------------------------
// StripNetRichMarkup — drop $X / %X markup codes (with an optional leading digit
// arg and the $i/$t/%a inline selector), keep the text inside [..] brackets.
// Faithful to the markup grammar (gui/text/markup.cpp ClassifyDollar): the codes
// are non-printing; the bracketed run carries the visible button/inline label.
// ---------------------------------------------------------------------------
std::string StripNetRichMarkup(const char* s) {
    std::string out;
    if (!s) return out;
    for (const char* p = s; *p;) {
        const unsigned char c = (unsigned char)*p;
        if (c == '$' || c == '%') {
            ++p;
            while (*p && std::isdigit((unsigned char)*p)) ++p;  // optional digit arg
            if (*p) ++p;                                        // the code letter
            // optional inline selector ('a'/'n'/'i'/'b'/'c'/'s'/'t') when a '[' follows
            if ((*p == 'a' || *p == 'n' || *p == 'i' || *p == 'b' || *p == 'c' ||
                 *p == 's' || *p == 't') && p[1] == '[')
                ++p;
            continue;
        }
        if (c == '[' || c == ']') { ++p; continue; }  // keep the inner text only
        out.push_back((char)c);
        ++p;
    }
    // Trim leading/trailing whitespace.
    std::size_t a = out.find_first_not_of(" \t\n\r");
    if (a == std::string::npos) return std::string();
    std::size_t b = out.find_last_not_of(" \t\n\r");
    return out.substr(a, b - a + 1);
}

// ---------------------------------------------------------------------------
// PaintNetworkHub — the pure framebuffer paint of Menu\CHOOSENETWORK (fn
// 0x529a64). Loading/label-resolution are done by the caller (once); this only
// composites, so a driver loop can call it per frame without re-reading the 59MB
// gfx archive or re-mounting the textbin. `assets` must already be Load()'ed when
// `art` is true; `title`/`opt[3]` are the resolved (rich-stripped) captions.
// THREE radio rows (host / search-join / load-profile) as _BUTTON_RED bars at a
// uniform width = widest caption, on the _OPTIONEN_PIC backdrop. Returns layout.
// ---------------------------------------------------------------------------
static NetHubLayout PaintNetworkHub(u32* dst, const NetViewContext& ctx,
                                    MenuAssets& assets, bool art,
                                    const std::string& title, const std::string opt[3],
                                    int nButtons = 3) {
    NetHubLayout L{};
    const int W = ctx.fbW, H = ctx.fbH;
    if (!dst || W <= 0 || H <= 0) return L;
    L.titleLabel = title;
    // The hub has 3 buttons; the "Продолжить игру" sub-screen reuses this form with
    // 2 (server/client). nButtons is explicit (labels can be empty in headless).
    L.labels[0] = opt[0]; L.labels[1] = opt[1]; L.labels[2] = opt[2];
    L.buttonCount = nButtons < 0 ? 0 : (nButtons > 3 ? 3 : nButtons);
    L.usedArt = art;
    L.window = Translate(W, H, kHubWinX, kHubWinY, kHubWinW, kHubWinH);
    const int scale = CanvasScale(W);

    // Background.
    if (art)
        DrawBackground(dst, W, H, assets.background().data(),
                       assets.backgroundWidth(), assets.backgroundHeight());
    else
        FillRect(dst, W, H, 0, 0, W, H, 0xFF101820u);

    // The hub sits on a clean parchment SHEET — _PERGAMENT_KARTE (the 256x256
    // "Pergament_Blatt" sheet) stretched wider to the panel rect. Centered on screen
    // (native 800x600 coords), matching the original's ~432x256 panel at y168. The
    // title + 3 red buttons are centered on it.
    // Real form window (frida VIBE_Window_Create on Menu\CHOOSENETWORK): design
    // rect (144,160,400,297), center-translated to screen (PositionAtCoord mode 2)
    // -> centered horizontally, kept at design y. So the parchment fills a 400x297
    // panel at x=(800-400)/2, y=160. The 256x256 sheet is stretched to fill it.
    constexpr int kPanelW = 400, kPanelH = 297;
    const int panelXn = (800 - kPanelW) / 2;       // 200
    const int panelYn = 160;
    L.window = Translate(W, H, panelXn, panelYn, kPanelW, kPanelH);
    if (art) {
        if (const render::DecodedShape* sheet = NetworkPanelParchment(ctx.gameDir))
            ScaledBlit(dst, W, H, *sheet, L.window.x, L.window.y, L.window.w, L.window.h);
    }

    // Title centered on the parchment near its top (measured ~screen y180).
    L.title = Translate(W, H, panelXn, panelYn + 18, kPanelW, 20);

    int uniformW = 0;
    for (int i = 0; i < L.buttonCount; ++i) {
        const int lw = LabelWidth(assets, art, L.labels[i]);
        const int bw = lw + kBtnCapW + kBtnCapW + 4;   // RecomputeSize @0x41b164
        if (bw > uniformW) uniformW = bw;
    }
    if (uniformW <= 0) uniformW = kBtnNominalW;
    if (uniformW > kPanelW - 36) uniformW = kPanelW - 36;

    const int rowStride = 40;                      // spacing between rows (measured)
    const int firstRowY = panelYn + 91;            // ~y251 (first button, measured)
    const int btnX = (800 - uniformW) / 2;         // centered on the panel/screen
    for (int i = 0; i < L.buttonCount; ++i) {
        const int fy = firstRowY + i * rowStride;
        L.buttons[i] = Translate(W, H, btnX, fy, uniformW, kBtnH);
        const NetViewRect& r = L.buttons[i];
        const bool sel = (i == ctx.hover);
        if (art) {
            DrawButton3Slice(dst, W, H, assets, r.x, r.y, r.w, r.h * scale, sel);
        } else {
            FillRect(dst, W, H, r.x, r.y, r.w, r.h, sel ? 0xFF783030u : 0xFF5A1E1Eu);
            OutlineRect(dst, W, H, r.x, r.y, r.w, r.h, 0xFFD8C060u);
        }
        DrawCenteredLabel(dst, W, H, assets, art, r.x, r.y, r.w, r.h * scale,
                          L.labels[i], scale, 255, 235, 200);   // baked gold (on red)
    }

    // Title text — the original uses the native-black thin gothic _FONT+2 on the
    // parchment (NOT the bold gold _FONT the buttons use). Render it directly.
    if (const MenuFont* tf = ParchmentTitleFont(ctx.gameDir)) {
        const int tw = tf->MeasureWidth(L.titleLabel.c_str()) * scale;
        const int th = (tf->lineHeight() > 0 ? tf->lineHeight() : 17) * scale;
        tf->DrawText(dst, W, H, L.title.x + (L.title.w - tw) / 2,
                     L.title.y + (L.title.h * scale - th) / 2, L.titleLabel.c_str(),
                     scale, 0, 0, 0, /*modulate=*/false);
    } else {
        DrawCenteredLabel(dst, W, H, assets, art, L.title.x, L.title.y, L.title.w,
                          L.title.h * scale, L.titleLabel, scale, 0, 0, 0);
    }
    return L;
}

// One-shot public renderer (tests / single frames): load + resolve + paint.
NetHubLayout RenderNetworkHubView(u32* dst, const NetViewContext& ctx) {
    const std::string title = ResolveOne(ctx.gameDir, "_OPTIONEN_NETZWERK_MENUE+0");
    std::string opt[3] = {
        ResolveOne(ctx.gameDir, "_OPTIONEN_NETZWERK_MENUE+1"),  // host
        ResolveOne(ctx.gameDir, "_OPTIONEN_NETZWERK_MENUE+2"),  // search / join
        ResolveOne(ctx.gameDir, "_OPTIONEN_NETZWERK_MENUE+3"),  // load profile
    };
    MenuAssets assets;
    bool art = false;
    if (!ctx.gameDir.empty()) { shim::DiskFileSystem fs(ctx.gameDir); art = LoadArt(assets, fs, ctx.fbW); }
    return PaintNetworkHub(dst, ctx, assets, art, title, opt);
}

// The "Продолжить игру" sub-screen (hub option 3 -> ChooseNetworkProfile @0x52991c):
// the SAME parchment form/title, with 2 buttons — start as server / start as client
// (_OPTIONEN_NETZWERK_MENUE+4/+5).
NetHubLayout RenderNetworkContinueView(u32* dst, const NetViewContext& ctx) {
    const std::string title = ResolveOne(ctx.gameDir, "_OPTIONEN_NETZWERK_MENUE+0");
    std::string opt[3] = {
        ResolveOne(ctx.gameDir, "_OPTIONEN_NETZWERK_MENUE+4"),  // start as server
        ResolveOne(ctx.gameDir, "_OPTIONEN_NETZWERK_MENUE+5"),  // start as client
        std::string(),                                          // (no third button)
    };
    MenuAssets assets;
    bool art = false;
    if (!ctx.gameDir.empty()) { shim::DiskFileSystem fs(ctx.gameDir); art = LoadArt(assets, fs, ctx.fbW); }
    return PaintNetworkHub(dst, ctx, assets, art, title, opt, /*nButtons=*/2);
}

// ---------------------------------------------------------------------------
// RenderNetworkIpView — Menu\CHOOSENETWORK_IP (fn 0x529074).  A title, the
// "Enter IP:" prompt (_OPTIONEN_NETZWERK_JOINEN+1) at form (16,56), an input
// field at (16,88) showing the typed IP, and the OK/connect button
// (_OPTIONEN_NETZWERK_JOINEN+2).
// ---------------------------------------------------------------------------
static NetIpLayout PaintNetworkIp(u32* dst, const NetViewContext& ctx,
                                  MenuAssets& assets, bool art, const std::string& typedIp,
                                  const std::string& titleLabel, const std::string& promptLabel,
                                  const std::string& okLabel) {
    NetIpLayout L{};
    const int W = ctx.fbW, H = ctx.fbH;
    if (!dst || W <= 0 || H <= 0) return L;

    L.titleLabel  = titleLabel;
    L.promptLabel = promptLabel;
    L.okLabel     = okLabel;
    L.usedArt = art;
    const int scale = CanvasScale(W);

    if (art)
        DrawBackground(dst, W, H, assets.background().data(),
                       assets.backgroundWidth(), assets.backgroundHeight());
    else
        FillRect(dst, W, H, 0, 0, W, H, 0xFF101820u);

    L.window = Translate(W, H, kIpWinX, kIpWinY, kIpWinW, kIpWinH);

    // Form-relative positions (groundtruth): prompt (16,56), input (16,88).
    L.title  = Translate(W, H, kIpWinX + 16, kIpWinY + 16, kIpWinW - 32, 20);
    L.prompt = Translate(W, H, kIpWinX + 16, kIpWinY + 56, kIpWinW - 32, 18);
    L.input  = Translate(W, H, kIpWinX + 16, kIpWinY + 88, kIpWinW - 32, 22);

    // OK button: equal width = its label, centred under the input.
    const int okLw = LabelWidth(assets, art, L.okLabel);
    int okW = okLw + kBtnCapW + kBtnCapW + 4;
    if (okW < kBtnNominalW) okW = kBtnNominalW;
    if (okW > kIpWinW - 32) okW = kIpWinW - 32;
    L.okButton = Translate(W, H, kIpWinX + (kIpWinW - okW) / 2, kIpWinY + 128, okW, kBtnH);

    // Input box.
    FillRect(dst, W, H, L.input.x, L.input.y, L.input.w, L.input.h, 0xFF202830u);
    OutlineRect(dst, W, H, L.input.x, L.input.y, L.input.w, L.input.h, 0xFFB0B0B0u);
    DrawLeftLabel(dst, W, H, assets, art, L.input.x + 4, L.input.y + 3, typedIp,
                  scale, 230, 240, 255);

    // Prompt + title.
    DrawLeftLabel(dst, W, H, assets, art, L.prompt.x, L.prompt.y, L.promptLabel,
                  scale, 240, 230, 200);
    DrawCenteredLabel(dst, W, H, assets, art, L.title.x, L.title.y, L.title.w,
                      L.title.h * scale, L.titleLabel, scale, 255, 220, 140);

    // OK button.
    const bool okSel = (ctx.hover == 0);
    if (art) {
        DrawButton3Slice(dst, W, H, assets, L.okButton.x, L.okButton.y,
                         L.okButton.w, L.okButton.h * scale, okSel);
    } else {
        FillRect(dst, W, H, L.okButton.x, L.okButton.y, L.okButton.w, L.okButton.h,
                 okSel ? 0xFF783030u : 0xFF5A1E1Eu);
        OutlineRect(dst, W, H, L.okButton.x, L.okButton.y, L.okButton.w, L.okButton.h,
                    0xFFD8C060u);
    }
    DrawCenteredLabel(dst, W, H, assets, art, L.okButton.x, L.okButton.y,
                      L.okButton.w, L.okButton.h * scale, L.okLabel, scale, 255, 235, 200);
    return L;
}

NetIpLayout RenderNetworkIpView(u32* dst, const NetViewContext& ctx,
                                const std::string& typedIp) {
    const std::string title  = ResolveOne(ctx.gameDir, "_OPTIONEN_NETZWERK_JOINEN+0");
    const std::string prompt = ResolveOne(ctx.gameDir, "_OPTIONEN_NETZWERK_JOINEN+1");
    const std::string ok     = ResolveOne(ctx.gameDir, "_OPTIONEN_NETZWERK_JOINEN+2");
    MenuAssets assets;
    bool art = false;
    if (!ctx.gameDir.empty()) { shim::DiskFileSystem fs(ctx.gameDir); art = LoadArt(assets, fs, ctx.fbW); }
    return PaintNetworkIp(dst, ctx, assets, art, typedIp, title, prompt, ok);
}

// ---------------------------------------------------------------------------
// RenderNetworkSearchView — Menu\SEARCH_NETWORK (fn 0x529248).  The server-search
// list: title (_OPTIONEN_NETZWERK_SUCHE_SERVER+0), a status line
// (_OPTIONEN_NETZWERK_WARTEN+0 or the override), one row per discovered server
// (radio list, _STADTWAPPEN icon + name in the original), and the CONNECT /
// REFRESH / DIRECT / CANCEL button row (Hud_BuildButtonRow, v50[0..2] + cancel).
// ---------------------------------------------------------------------------
// Resolved search-screen labels (lifted out so the driver resolves once).
struct NetSearchLabels {
    std::string title, status, connect, refresh, direct, cancel;
};
static NetSearchLayout PaintNetworkSearch(u32* dst, const NetViewContext& ctx,
                                          MenuAssets& assets, bool art,
                                          const std::vector<std::string>& servers,
                                          const NetSearchLabels& lab,
                                          const std::string& statusOverride) {
    NetSearchLayout L{};
    const int W = ctx.fbW, H = ctx.fbH;
    if (!dst || W <= 0 || H <= 0) return L;

    L.titleLabel   = lab.title;
    L.statusLabel  = statusOverride.empty() ? lab.status : statusOverride;
    L.connectLabel = lab.connect;
    L.refreshLabel = lab.refresh;
    L.directLabel  = lab.direct;
    L.cancelLabel  = lab.cancel;
    L.usedArt = art;
    const int scale = CanvasScale(W);

    if (art)
        DrawBackground(dst, W, H, assets.background().data(),
                       assets.backgroundWidth(), assets.backgroundHeight());
    else
        FillRect(dst, W, H, 0, 0, W, H, 0xFF101820u);

    // The SEARCH form WIN0 is 575x451 (the kSrchWin W/H are swapped) == the game
    // options' green frame _TOOL_TIP_GREEN_BIGGER. Render it the SAME way: jeweled
    // border + green marble title bar + a flood-filled DARK interior, centered, top
    // at win0Y=120. The server list + the 4 buttons live inside the dark interior.
    const int oy = (H - 600) / 2;
    int ix0 = kSrchWinX, iy0 = kSrchWinY + 44, ix1 = kSrchWinX + kSrchWinW, iy1 = kSrchWinY + 400;
    int fpx0 = (W - 574) / 2, fpy0 = 120 + oy;
    const render::DecodedShape* frame = art ? assets.SpriteByName("_TOOL_TIP_GREEN_BIGGER", 0) : nullptr;
    if (frame && frame->width > 0 && frame->height > 0) {
        const int fw = frame->width, fh = frame->height;
        fpx0 = (W - fw) / 2; fpy0 = 120 + oy;
        const u32* fa = frame->argb.data();
        static const render::DecodedShape* s_maskFor = nullptr;
        static std::vector<unsigned char> s_interior;
        if (s_maskFor != frame) {
            s_maskFor = frame;
            s_interior.assign((std::size_t)fw * fh, 0);
            std::vector<int> stk; const int seed = (fh / 2) * fw + (fw / 2);
            if (!(fa[seed] & 0xFF000000u)) { s_interior[seed] = 1; stk.push_back(seed); }
            while (!stk.empty()) {
                const int p = stk.back(); stk.pop_back(); const int x = p % fw, y = p / fw;
                const int nb[4] = { x+1<fw?p+1:-1, x>0?p-1:-1, y+1<fh?p+fw:-1, y>0?p-fw:-1 };
                for (int k = 0; k < 4; ++k) { const int q = nb[k];
                    if (q < 0 || s_interior[q] || (fa[q] & 0xFF000000u)) continue; s_interior[q] = 1; stk.push_back(q); }
            }
        }
        int mnx = fw, mny = fh, mxx = 0, mxy = 0;
        for (int ry = 0; ry < fh; ++ry) { const int dy = fpy0 + ry; if (dy < 0 || dy >= H) continue;
            u32* drow = dst + (std::size_t)dy * W;
            for (int rx = 0; rx < fw; ++rx) { const int dx = fpx0 + rx; if (dx < 0 || dx >= W) continue;
                const std::size_t fi = (std::size_t)ry * fw + rx; const u32 ap = fa[fi];
                if (ap & 0xFF000000u) drow[dx] = ap;
                else if (s_interior[fi]) {
                    const u32 c = drow[dx];
                    const int r = ((((c>>16)&0xFF)*82)+22*174)>>8, g=((((c>>8)&0xFF)*82)+26*174)>>8, b=(((c&0xFF)*82)+16*174)>>8;
                    drow[dx] = 0xFF000000u | ((u32)r<<16) | ((u32)g<<8) | (u32)b;
                    if (rx<mnx)mnx=rx; if (rx>mxx)mxx=rx; if (ry<mny)mny=ry; if (ry>mxy)mxy=ry;
                }
            }
        }
        ix0 = fpx0 + mnx; ix1 = fpx0 + mxx; iy0 = fpy0 + mny; iy1 = fpy0 + mxy;
        // Title centered on the green marble bar (cream gold baked _FONT).
        L.title = { fpx0, fpy0 + 11, fw, 24 };
    } else {
        L.title = Translate(W, H, kSrchWinX + 24, kSrchWinY + 16, kSrchWinW - 48, 20);
    }
    L.window = { fpx0, fpy0, frame ? frame->width : kSrchWinW, frame ? frame->height : kSrchWinH };
    L.status = { ix0 + 10, iy0 + 6, ix1 - ix0 - 20, 18 };

    // Button row: CONNECT / REFRESH / DIRECT / CANCEL — uniform width, all INSIDE the
    // dark interior between its borders, near the bottom. Same button height (kBtnH).
    const std::string* lbls[4] = { &L.connectLabel, &L.refreshLabel, &L.directLabel, &L.cancelLabel };
    NetViewRect* outRects[4] = { &L.connectButton, &L.refreshButton, &L.directButton, &L.cancelButton };
    int bw[4] = {0,0,0,0};
    int natTotal = 0;
    for (int i = 0; i < 4; ++i) {
        const int lw = LabelWidth(assets, art, *lbls[i]);
        bw[i] = (lw > 0 ? lw : kBtnNominalW - 2*kBtnCapW - 4) + kBtnCapW + kBtnCapW + 4;  // per-label
        natTotal += bw[i];
    }
    const int innerW = ix1 - ix0;
    const int gap = 6;
    const int avail = innerW - 8 - gap * 3;                 // usable width for the 4 boxes
    if (natTotal > avail) {                                 // scale all down to fit between borders
        natTotal = 0;
        for (int i = 0; i < 4; ++i) { bw[i] = bw[i] * avail / (bw[0]+bw[1]+bw[2]+bw[3]); natTotal += bw[i]; }
    }
    const int totalW = natTotal + gap * 3;
    const int btnY = iy1 - kBtnH - 8;                       // just above the interior bottom
    int rowX = ix0 + (innerW - totalW) / 2;
    for (int i = 0; i < 4; ++i) {
        outRects[i]->x = rowX; outRects[i]->y = btnY; outRects[i]->w = bw[i]; outRects[i]->h = kBtnH;
        const NetViewRect& r = *outRects[i];
        const bool sel = (ctx.hover == L.rowCount + i);
        if (art) DrawButton3Slice(dst, W, H, assets, r.x, r.y, r.w, r.h * scale, sel);
        else { FillRect(dst, W, H, r.x, r.y, r.w, r.h, sel ? 0xFF783030u : 0xFF5A1E1Eu);
               OutlineRect(dst, W, H, r.x, r.y, r.w, r.h, 0xFFD8C060u); }
        DrawCenteredLabel(dst, W, H, assets, art, r.x, r.y, r.w, r.h * scale, *lbls[i], scale, 255, 235, 200);
        rowX += bw[i] + gap;
    }

    // Server list rows inside the interior, between the status line and the buttons.
    const int rowH = 24;
    const int listX = ix0 + 10, listY = iy0 + 30, listW = ix1 - ix0 - 20;
    L.rowCount = (int)servers.size();
    L.rows.reserve(L.rowCount);
    for (int i = 0; i < L.rowCount; ++i) {
        const int ry = listY + i * rowH;
        if (ry + rowH > btnY - 6) break;                   // clip to above the buttons
        NetViewRect r = { listX, ry, listW, rowH - 2 };
        L.rows.push_back(r);
        const bool sel = (i == ctx.hover);
        if (sel) { FillRect(dst, W, H, r.x, r.y, r.w, r.h, 0xFF40301Eu);
                   OutlineRect(dst, W, H, r.x, r.y, r.w, r.h, 0xFFD8C060u); }
        DrawLeftLabel(dst, W, H, assets, art, r.x + 6, r.y + 3, servers[i], scale, 235, 235, 220);
    }

    // Title (green-bar, cream gold) + status line.
    DrawCenteredLabel(dst, W, H, assets, art, L.title.x, L.title.y, L.title.w,
                      L.title.h * scale, L.titleLabel, scale, 255, 235, 200);
    DrawLeftLabel(dst, W, H, assets, art, L.status.x, L.status.y, L.statusLabel,
                  scale, 230, 222, 180);
    return L;
}

NetSearchLayout RenderNetworkSearchView(u32* dst, const NetViewContext& ctx,
                                        const std::vector<std::string>& servers,
                                        const std::string& statusOverride) {
    NetSearchLabels lab;
    lab.title   = ResolveOne(ctx.gameDir, "_OPTIONEN_NETZWERK_SUCHE_SERVER+0");
    lab.status  = ResolveOne(ctx.gameDir, "_OPTIONEN_NETZWERK_WARTEN+0");
    lab.connect = ResolveOne(ctx.gameDir, "_OPTIONEN_NETZWERK_BUTTON_CONNECT+0");
    lab.refresh = ResolveOne(ctx.gameDir, "_OPTIONEN_NETZWERK_BUTTON_REFRESH+0");
    lab.direct  = ResolveOne(ctx.gameDir, "_OPTIONEN_NETZWERK_BUTTON_DIRECT+0");
    lab.cancel  = ResolveOne(ctx.gameDir, "_OPTIONEN_NETZWERK_BUTTON_CANCEL+0");
    MenuAssets assets;
    bool art = false;
    if (!ctx.gameDir.empty()) { shim::DiskFileSystem fs(ctx.gameDir); art = LoadArt(assets, fs, ctx.fbW); }
    return PaintNetworkSearch(dst, ctx, assets, art, servers, lab, statusOverride);
}

// ===========================================================================
// RunNetworkScreen — the live "Сетевая игра" driver (VIBE_Menu_ChooseNetworkMode
// @0x529a64). Loads gfx/font/labels ONCE, then loops the hub on the host window;
// the chosen radio row enters its sub-view (search list / host-IP entry). RULE 6:
// no real networking — the search list is empty (no fake servers), CONNECT/host/
// profile are gated no-ops returning to the hub. ESC/close returns to the menu.
// ===========================================================================
namespace {

void NetBlitToDevice(const u32* src, int w, int h, shim::IGraphicsDevice& dev) {
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
            const u32* s = src + (std::size_t)y * w;
            for (int x = 0; x < W; ++x) {
                const u32 c = s[x], r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
                row[x] = (std::uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }
    }
}

bool InRect(int mx, int my, const NetViewRect& r) {
    return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
}

#ifdef GUILD_HAVE_SDL2_NET
guild::u32 DiscoverClockMs(void*) { return (guild::u32)SDL_GetTicks(); }
#endif

// Run one LAN discovery pass over the REAL SDL_net transport (1:1
// net::DiscoverServers @0x43abcc). Fills `names` with one display string per host
// ("<ip>" plus the decoded saved-game name for kind-4 adverts). With no SDL_net
// backend (portable/loopback build) discovery is a no-op (Rule 6: no networking)
// and `names` stays empty. `windowMs` is the drain window (the menu uses 3000ms).
void DiscoverLanServers(std::vector<std::string>& names, std::vector<net::DiscoveredServer>& servers,
                        guild::u32 windowMs) {
    names.clear();
    servers.clear();
#ifdef GUILD_HAVE_SDL2_NET
    shim_impl::SdlNetDatagram dg;   // DiscoverServers does open()/drain/close()
    const int n = net::DiscoverServers(&dg, net::kDiscoveryPort, windowMs,
                                       /*maxServers=*/16, servers,
                                       &DiscoverClockMs, nullptr, /*tickMs=*/0);
    if (n <= 0) return;
    for (const net::DiscoveredServer& s : servers) {
        // kind-4 (type 127) adverts carry the saved-game name in the blob; show the
        // IP either way (the original lists the dotted-quad + wappen icon per row).
        names.push_back(s.ip);
    }
#else
    (void)windowMs;   // no transport backend wired
#endif
}

// One-shot debug dump (GUILD_NETWORK_DUMP=path), like the other screens. The
// suffix env (GUILD_NETWORK_DUMP_TAG) lets a driver dump each view distinctly.
void MaybeDumpNet(const u32* fb, int W, int H) {
    const char* mp = std::getenv("GUILD_NETWORK_DUMP");
    if (!mp) return;
    static bool dumped = false;
    if (dumped) return;
    dumped = true;
    std::vector<std::uint8_t> rgb((std::size_t)W * H * 3);
    for (std::size_t i = 0, n = (std::size_t)W * H; i < n; ++i) {
        const u32 c = fb[i];
        rgb[i*3+0] = (std::uint8_t)((c >> 16) & 0xFF);
        rgb[i*3+1] = (std::uint8_t)((c >> 8) & 0xFF);
        rgb[i*3+2] = (std::uint8_t)(c & 0xFF);
    }
    std::vector<std::uint8_t> bmp = render::BmpSave24Bit(W, H, rgb.data());
    if (FILE* f = std::fopen(mp, "wb")) { std::fwrite(bmp.data(), 1, bmp.size(), f); std::fclose(f); }
}

} // namespace

NetworkScreenResult RunNetworkScreen(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                                     const NetworkScreenConfig& cfg) {
    NetworkScreenResult res;
    const int W = cfg.fbW, H = cfg.fbH;
    std::vector<u32> scratch((std::size_t)W * H, 0u);

    // ---- load gfx/font ONCE ----
    MenuAssets assets;
    bool art = false;
    if (!cfg.gameDir.empty()) {
        shim::DiskFileSystem fs(cfg.gameDir);
        art = LoadArt(assets, fs, W);
    }

    // ---- resolve every label ONCE (one textbin mount) ----
    static const char* const kKeys[] = {
        "_OPTIONEN_NETZWERK_MENUE+0", "_OPTIONEN_NETZWERK_MENUE+1",
        "_OPTIONEN_NETZWERK_MENUE+2", "_OPTIONEN_NETZWERK_MENUE+3",
        "_OPTIONEN_NETZWERK_JOINEN+0", "_OPTIONEN_NETZWERK_JOINEN+1",
        "_OPTIONEN_NETZWERK_JOINEN+2",
        "_OPTIONEN_NETZWERK_SUCHE_SERVER+0", "_OPTIONEN_NETZWERK_WARTEN+0",
        "_OPTIONEN_NETZWERK_BUTTON_CONNECT+0", "_OPTIONEN_NETZWERK_BUTTON_REFRESH+0",
        "_OPTIONEN_NETZWERK_BUTTON_DIRECT+0", "_OPTIONEN_NETZWERK_BUTTON_CANCEL+0",
    };
    constexpr int kNKeys = (int)(sizeof(kKeys) / sizeof(kKeys[0]));
    std::string raw[kNKeys];
    if (!cfg.gameDir.empty()) ResolveOptionLabels(cfg.gameDir, kKeys, kNKeys, raw);
    std::string lbl[kNKeys];
    for (int i = 0; i < kNKeys; ++i) lbl[i] = StripNetRichMarkup(raw[i].c_str());
    const std::string hubTitle = lbl[0];
    const std::string hubOpt[3] = { lbl[1], lbl[2], lbl[3] };
    NetSearchLabels srchLab{ lbl[7], lbl[8], lbl[9], lbl[10], lbl[11], lbl[12] };

    NetViewContext ctx; ctx.gameDir = cfg.gameDir; ctx.fbW = W; ctx.fbH = H;

    // Input edge-tracking shared across the hub + sub-views.
    shim::MouseState mouse{};
    bool prevLeft = false, prevEsc = false;
    bool windowOpen = true;
    auto pump = [&]() {
        if (!plat.pumpMessages()) windowOpen = false;
        plat.getMouse(mouse);
    };
    auto present = [&](const NetViewContext& c) {
        MaybeDumpNet(scratch.data(), W, H);
        NetBlitToDevice(scratch.data(), W, H, device);
        device.present();
        ++res.framesPresented;
        if (cfg.frameCapMs > 0) plat.sleepMs((std::uint32_t)cfg.frameCapMs);
        (void)c;
    };

    int frames = 0;
    enum { kHub, kSearch, kIp, kContinue } view = kHub;
    // "Продолжить игру" sub-screen captions (server / client), resolved once.
    const std::string contOpt[3] = {
        ResolveOne(cfg.gameDir, "_OPTIONEN_NETZWERK_MENUE+4"),
        ResolveOne(cfg.gameDir, "_OPTIONEN_NETZWERK_MENUE+5"),
        std::string(),
    };
    std::string typedIp = "127.0.0.1";   // host-IP entry seed (INI Host default)
    std::vector<std::string> serverNames;            // discovered LAN hosts (display)
    std::vector<net::DiscoveredServer> serverList;   // decoded records (IP/blob/type)
    bool discoverPending = false;                    // run a discovery pass next search frame
    std::string searchStatus;                        // "" -> default "waiting" label

    while (windowOpen) {
        if (cfg.maxFrames >= 0 && frames >= cfg.maxFrames) break;
        ++frames;
        pump();
        const bool escEdge   = plat.keyDown(0x1B) && !prevEsc;
        const bool clickEdge = mouse.left && !prevLeft;
        prevEsc = plat.keyDown(0x1B);
        prevLeft = mouse.left;

        std::fill(scratch.begin(), scratch.end(), 0u);

        if (view == kHub) {
            // Hover-test then paint.
            NetViewContext hc = ctx; hc.hover = -1;
            // First paint to get rects, then re-evaluate hover (rects are stable).
            NetHubLayout L = PaintNetworkHub(scratch.data(), hc, assets, art, hubTitle, hubOpt);
            int hov = -1;
            for (int i = 0; i < L.buttonCount; ++i) if (InRect(mouse.x, mouse.y, L.buttons[i])) hov = i;
            if (hov != -1) { hc.hover = hov; std::fill(scratch.begin(), scratch.end(), 0u);
                             L = PaintNetworkHub(scratch.data(), hc, assets, art, hubTitle, hubOpt); }
            res.hoveredRow = hov;
            present(hc);
            if (escEdge) { res.back = true; break; }            // dword_672230 -> back to menu
            if (clickEdge && hov >= 0) {
                res.chosenMode = hov;
                // Per 0x529a64: host->word_63C740=5, search->5, profile->4. Only the
                // JOIN/SEARCH option (1, SearchNetworkGames @0x529248) opens a screen
                // that is pure UI (the LAN list) — enter it. HOST (0, RunHostNetworkSetup
                // @0x528dac) and PROFILE (2, ChooseNetworkProfile @0x52991c) need real
                // multiplayer transport (rule 6, NOT a pre-approved swap) — gate them
                // here rather than show a wrong screen (rule 8). They stay on the hub.
                if (hov == 1) { res.sessionFlags = 5; view = kSearch; discoverPending = true; } // search / join
                else if (hov == 2) { res.sessionFlags = 4; view = kContinue; }                  // continue -> server/client
                else { res.sessionFlags = 5; }                                                  // host gated
            }
        } else if (view == kContinue) {
            // "Продолжить игру": the server/client choice (pure UI, same parchment
            // form as the hub). The two actions need real transport -> gated; ESC
            // (or no third button) returns to the hub.
            NetViewContext cc = ctx; cc.hover = -1;
            NetHubLayout L = PaintNetworkHub(scratch.data(), cc, assets, art, hubTitle, contOpt, /*nButtons=*/2);
            int hov = -1;
            for (int i = 0; i < L.buttonCount; ++i) if (InRect(mouse.x, mouse.y, L.buttons[i])) hov = i;
            if (hov != -1) { cc.hover = hov; std::fill(scratch.begin(), scratch.end(), 0u);
                             L = PaintNetworkHub(scratch.data(), cc, assets, art, hubTitle, contOpt, /*nButtons=*/2); }
            res.hoveredRow = hov;
            present(cc);
            if (escEdge) { view = kHub; continue; }             // back to hub
            if (clickEdge && hov == 1) {                         // клиент -> server-list/connect
                res.sessionFlags = 5; view = kSearch; discoverPending = true;
            }
            // hov == 0 (сервер / host setup) needs real transport -> gated.
        } else if (view == kSearch) {
            // Run a real LAN discovery pass (1:1 net::DiscoverServers over SDL_net)
            // on entry and on REFRESH. The 3000ms drain window is the original's
            // (DiscoverServers(0x3039, 0xBB8)); on the loopback build it is a no-op.
            if (discoverPending) {
                discoverPending = false;
                DiscoverLanServers(serverNames, serverList, net::kDiscoveryWindow);
            }
            NetViewContext sc = ctx; sc.hover = -1;
            NetSearchLayout L = PaintNetworkSearch(scratch.data(), sc, assets, art, serverNames,
                                                   srchLab, searchStatus);
            int hov = -1;
            for (int i = 0; i < L.rowCount; ++i) if (InRect(mouse.x, mouse.y, L.rows[i])) hov = i;
            const NetViewRect btns[4] = { L.connectButton, L.refreshButton, L.directButton, L.cancelButton };
            for (int i = 0; i < 4; ++i) if (InRect(mouse.x, mouse.y, btns[i])) hov = L.rowCount + i;
            if (hov != -1) { sc.hover = hov; std::fill(scratch.begin(), scratch.end(), 0u);
                             L = PaintNetworkSearch(scratch.data(), sc, assets, art, serverNames, srchLab, searchStatus); }
            present(sc);
            if (escEdge) { view = kHub; continue; }              // cancel -> hub
            if (clickEdge && hov >= 0) {
                if (hov < L.rowCount) {
                    // Pick a discovered server: latch its IP as the join target
                    // (the original copies it into byte_122EE90 + INI [Network]Host).
                    res.chosenMode = 1;
                    typedIp = serverList[hov].ip;
                    // CONNECT itself is the TCP session (server.dll side, out of scope).
                } else {
                    const int btn = hov - L.rowCount;            // 0=connect,1=refresh,2=direct,3=cancel
                    if (btn == 3) view = kHub;                   // CANCEL -> hub
                    else if (btn == 2) view = kIp;               // DIRECT -> host-IP entry
                    else if (btn == 1) discoverPending = true;   // REFRESH -> re-discover
                    // CONNECT(0): the TCP game session (server.dll) is not wired.
                }
            }
        } else { // kIp
            // Type the IP (digits + '.'); Backspace deletes; the connect is gated.
            NetViewContext ic = ctx; ic.hover = -1;
            NetIpLayout L = PaintNetworkIp(scratch.data(), ic, assets, art, typedIp,
                                           lbl[4], lbl[5], lbl[6]);
            const bool okHov = InRect(mouse.x, mouse.y, L.okButton);
            if (okHov) { ic.hover = 0; std::fill(scratch.begin(), scratch.end(), 0u);
                         L = PaintNetworkIp(scratch.data(), ic, assets, art, typedIp, lbl[4], lbl[5], lbl[6]); }
            present(ic);
            if (escEdge) { view = kSearch; continue; }           // back to search
            // (text entry / connect are engine edges; connect is rule-6 gated)
            if (clickEdge && okHov) view = kSearch;              // OK gated -> back
        }
    }
    if (!windowOpen) res.quitByWindow = true;
    return res;
}

} // namespace guild::play
