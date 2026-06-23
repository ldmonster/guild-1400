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

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace guild::play {

namespace {

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

    last_.captionGlyphs += DrawLine16(s, layout.captionX, layout.captionY,
                                      money.c_str(),
                                      pal.textR, pal.textG, pal.textB);
    last_.captionGlyphs += DrawLine16(s, layout.captionX, layout.captionY + 9,
                                      date.c_str(),
                                      pal.textR, pal.textG, pal.textB);

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
