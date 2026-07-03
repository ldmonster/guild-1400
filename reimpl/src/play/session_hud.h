#pragma once
// =============================================================================
// guild::play — IN-GAME SESSION HUD OVERLAY (the native city session's HUD).
//
// Composites the live in-game HUD (money + game date/time caption, the bottom
// player bar, the selected-entity status text, map markers) directly onto the
// session's 16bpp RGB565 framebuffer — the format play::RealCityRenderer
// renders into (its Render() contract: a device init()'d to fbW x fbH x 16bpp;
// see play/real_city_render.h) — using ONLY the reconstructed draw leaves:
//
//   * caption text   render::DrawText / DrawGlyph  (0x434E18 / 0x434D0C — the
//                    5x7 GUI font raster; natively gated on 16bpp AND 32bpp
//                    lock targets, so it stamps RGB565 pixels directly)
//   * bar/marker     gui::MenuFillRect -> render::SurfaceDrawHLine (0x423c70 /
//     fills          0x423ffc) and render::SurfaceDrawRectOutline (0x4242d4),
//                    all bpp-honouring (16bpp packs through ColorFormat)
//   * money string   world::MoneyFormatWithSeparators (0x58f798) via
//                    play::HudMoneyString (play/hud_render)
//   * clock          gui::Clock_ComputeTimeOfDay (0x527778) via
//                    play::HudDateString (play/hud_render)
//   * bar layout     gui::PlayerBar_AssignSlot / _SlotLayout / the ratio math
//                    (VIBE_PlayerBar_BuildContent @0x4b11e4 cluster)
//   * markers        gui::MapView_ComputeMarkerScreenPos (0x5440b4) via
//                    play::HudMarkerScreenXY
//   * status text    gui::StatusText_Register (0x4bcc80 — the 50-dword-stride
//                    status-text table) + the same DrawText leaf for the line
//   * sprite blit    play::HudRenderHooks::drawSprite -> the REAL
//                    render::ShapeShowFromBank (0x5d861c) ->
//                    ShapeBlitColored16 (0x5d7164) installed by
//                    play::InstallRealHudBridge (wire_hud_bridge). NOTE: the
//                    bridge's hook wraps the target as a 16bpp
//                    ColorBlitTarget16 — on THIS session surface (genuinely
//                    16bpp) that wrap is exactly the engine's surface model.
//
// REAL gilde.gfx INVESTIGATION (Init loads the REAL artwork file)
// -----------------------------------------------------------------------------
// Init() reads gfx/gilde.gfx through the shim filesystem and parses it with the
// REAL reconstructed loaders:
//   * gui::Form_LoadFromBuffer (the file-parse half of VIBE_Gui_LoadGfxFile
//     @0x41b888): u32 objectCount (=1806 in the shipped file, cap 2048) + 1806
//     84-byte records into gui::g_gfxObjects — the "d2:fileobj" table.
//   * render::GfxArchive (gfx_archive.h): the record directory (+48 dataOffset,
//     +56 dataSize) over the on-disk SHAPBANK containers, plus the reconstructed
//     depth-2 pixel decode (DecodeShapeBlob — 1:1 of the VIBE_FrameTable_Index
//     @0x5fbb24 row-table RLE walk).
//
// FINDING — why the REAL shapes are NOT fed to render::ShapeShowFromBank:
//   Every SHAPBANK that gilde.gfx ships is pixel-format 2 (24bpp; bank format
//   byte @bank+52 == 2, every shape depth byte @shape+12 == 2 — verified over
//   all 172 data-carrying records of the 1806-record file). The unscaled bank
//   blitter VIBE_Shape_ShowFromBank @0x5d861c only RASTERIZES depth-1 (16bpp
//   RLE) shapes via VIBE_Shape_BlitColored16 @0x5d7164; a depth-2 shape is a
//   NO-OP success (returns 1, draws nothing). At runtime the original converts
//   each loaded bank to depth 1 on the lazy-load path:
//       VIBE_State_Helper        @0x40e014  (d2_LoadObj: raw bank read, then)
//    -> VIBE_ShapeBank_ConvertNew@0x5d80a8  (fmt-2/fmt-0 bank -> fmt-1 bank)
//    -> VIBE_Shape_ConvertToNew  @0x5d8080  (per-shape depth dispatch)
//    -> VIBE_Shape_ConvertRgbTo16@0x5d7c0c  (24bpp RLE shape -> 16bpp RLE)  and
//       VIBE_Shape_Convert8To16  @0x5d7924  (8bpp variant; unused by gilde.gfx)
//   THE NAMED GAP: VIBE_Shape_ConvertRgbTo16 @0x5d7c0c (and @0x5d7924) are NOT
//   reconstructed — render/render_leaves9.h routes them through inert hooks
//   (RenderLeaves9Hooks.ConvertRgbTo16/Convert8To16) and no real implementation
//   exists anywhere in src/** (world/wire_worldnet.cpp documents the same gap).
//   Without that converter no depth-1 bank can be produced from the real file,
//   so per the no-cheap-analogue rule the sprite blit falls back to the
//   DOCUMENTED DefaultHudSpriteBank path that play/wire_hud_bridge ships (a
//   real-format in-memory depth-1 bank fed to the REAL ShapeShowFromBank leaf).
//   Init() still loads + parses the real file (the real table load is real),
//   and exposes the investigation results so tests can pin the finding.
// =============================================================================
#include "guild/common/types.h"
#include "play/hud_render.h"        // HudBarObject / HudMarker / hooks / strings
#include "gui/text/textdb.h"        // gui::text::TextDb (panel line resolve)
#include "play/menu_assets.h"       // MenuFont (the gold gothic banner font)
#include "play/session_panels.h"    // tooltip dispatch + info panel composite
#include "render/gfx_archive.h"     // render::GfxArchive (real gilde.gfx directory)
#include "sim/types.h"              // sim::GameTime (14-byte packed clock record)

#include <string>
#include <vector>

namespace guild::shim { class IFileSystem; }

namespace guild::play {

// Per-frame draw counts of one SessionHud::Render pass (observable output).
struct SessionHudResult {
    int barSlotsDrawn  = 0;   // player-bar slots laid out + filled
    int barFillRows    = 0;   // rows MenuFillRect filled across all slot sub-bars
    int captionGlyphs  = 0;   // chars submitted to DrawText (money + date lines)
    int statusGlyphs   = 0;   // chars submitted for the selected-entity status line
    int statusSlot     = -1;  // gui::StatusText_Register slot (-1 = none)
    int markersDrawn   = 0;   // markers projected + drawn
    int spriteBlits    = 0;   // drawSprite hook calls that reported a blit
    // ---- wave-2 panel extension (additive; 0/false when Inputs.panels==null)
    bool tooltipVisible = false; // a tooltip form is up (dword_633908 != -1)
    int  tooltipTextOps = 0;     // tooltip text lines composited
    int  tooltipIconOps = 0;     // tooltip icon ops (sprite hook attempts)
    bool panelVisible   = false; // a built info panel is on screen
    int  panelTextOps   = 0;     // panel text lines composited
    int  panelIconOps   = 0;     // panel icon ops (sprite hook attempts)
};

class SessionHud {
public:
    // ------------------------------------------------------------------
    // Live inputs for one HUD frame.
    // ------------------------------------------------------------------
    struct Inputs {
        // Player money. The engine's money is a 32-bit register value
        // (VIBE_Money_FormatWithSeparators takes eax); a session i64 outside
        // the i32 range is clamped at this boundary (documented adaptation).
        i64 money = 0;
        i32 moneyRate = 1;             // per-currency display rate (1 = identity)

        // The sim clock record (calendar day) + the HUD tick accumulator
        // (gilde.exe dword_1233558) the REAL Clock_ComputeTimeOfDay consumes.
        const sim::GameTime* clock = nullptr;
        int clockTick = 0;

        // Selected entity (status line + StatusText_Register key).
        int selectedId = 0;            // 0 = nothing selected
        const char* selectedName = nullptr;

        // Bottom player bar: owned objects + production fill ratios.
        const HudBarObject* barObjects = nullptr;
        int barObjectCount = 0;

        // Map markers (world positions projected by the real MapView leaf).
        const HudMarker* markers = nullptr;
        int markerCount = 0;
        int markerPanX = 0, markerPanY = 0, markerCameraOrigX = 0;

        // ---- wave-2 panel extension (ADDITIVE; null = HUD renders exactly as
        // before).  When set, SessionHud runs the in-game panel pass after the
        // markers: the REAL tooltip lifecycle (VIBE_Tooltip_DispatchByType
        // @0x4f7424 + the @0x4f7a10/0x4f8154/0x4f78e4/0x4f84ac/0x4f83e8
        // content builders) and the REAL selected-entity info panel
        // (VIBE_InfoPanel_Update @0x4b84c0 + the @0x4b64b0.. builders),
        // composited through the same 16bpp leaves (see play/session_panels.h).
        const SessionPanelsInputs* panels = nullptr;

        // Player display name for the top banner ("<title> <name>", e.g.
        // "Господин Patri Müller"); null/empty = banner shows the title only.
        const char* playerName = nullptr;

        // Floating selection label (the original's building name under the
        // HAUSPFEIL marker): screen-centred at (labelX, labelY), wrapped to two
        // lines in the small gold face. Null = no label.
        const char* labelText = nullptr;
        int labelX = 0, labelY = 0;
    };

    // HUD anchor layout within the frame (caller-tunable, like RenderHud's
    // caller-supplied anchors). mapX < 0 selects the computed right-edge
    // default (w - 140) at render time.
    struct Layout {
        int captionX = 8,  captionY = 8;   // money line; date line is +9px below
        int statusX  = 8,  statusY  = 30;  // selected-entity status line
        int barX     = 4,  barY     = 48;  // player-bar strip top-left
        int mapX     = -1, mapY     = 8;   // marker/map region top-left
        // Split money anchor (the sidebar money slot); <0 = follow captionX/Y.
        int moneyX   = -1, moneyY  = -1;
    };
    Layout layout;

    // ------------------------------------------------------------------
    // PANEL CHROME (the in-city screen furniture): decode the real
    // `_PANEL_*` 800x600 overlay (gold top banner + right sidebar with the
    // transparent 3D-view center) and the city crest `_STADTWAPPEN_<CITY>`
    // from the loaded gilde.gfx; Render() composites them (alpha-keyed,
    // scaled) BEFORE the HUD elements. Real pixels through the reconstructed
    // depth-2 decode; the engine-leaf 24->16 bank conversion
    // (VIBE_Shape_ConvertRgbTo16 @0x5d7c0c) remains the named gap — this
    // feeds the SAME artwork through the surface path instead. Returns true
    // if the panel decoded (needs a successful Init()).
    // ------------------------------------------------------------------
    bool DecodePanelChrome(const char* panelName, const char* crestName);
    bool panelChromeReady() const { return panelChrome_.width > 0; }

    // ------------------------------------------------------------------
    // Load the REAL HUD artwork. Reads gfx/gilde.gfx via `fs`, runs the real
    // gui::Form_LoadFromBuffer table parse (g_gfxObjects, 1806 records) and the
    // render::GfxArchive directory scan, then installs the sprite bridge
    // (play::InstallRealHudBridge -> real render::ShapeShowFromBank over the
    // DefaultHudSpriteBank — see the header FINDING for why the real depth-2
    // banks cannot be fed to the blitter: VIBE_Shape_ConvertRgbTo16 @0x5d7c0c
    // is not reconstructed). Returns true when the real file was loaded and
    // parsed; on a null fs / missing file the bridge is still installed and
    // the HUD remains renderable (returns false).
    // ------------------------------------------------------------------
    bool Init(shim::IFileSystem* fs);

    // ------------------------------------------------------------------
    // Composite the HUD over the session's 16bpp RGB565 framebuffer.
    // `fb16`/`w`/`h`/`pitchBytes` describe the frame RealCityRenderer rendered
    // (pitchBytes must be >= 2*w and even). The world pixels are NOT cleared —
    // the HUD overlays them, exactly the original's DrawUniverseAndStats
    // (@0x5b3bbc) "world frame, then stats on top" model. Draw counts land in
    // lastResult(). Null/degenerate targets are a safe no-op.
    // ------------------------------------------------------------------
    void Render(void* fb16, int w, int h, int pitchBytes, const Inputs& in);

    const SessionHudResult& lastResult() const { return last_; }

    // The panel layer (tooltip + info panel) — exposed for layout tuning and
    // for the per-frame results (SessionPanels::lastResult()).
    SessionPanels&       panels()       { return panels_; }
    const SessionPanels& panels() const { return panels_; }
    // The localized text array loaded with the chrome (null before/without it) —
    // feed to SessionPanelsInputs.textDb so panel lines resolve real strings.
    const gui::text::TextDb* textDb() const {
        return textDbLoaded_ ? &textDb_ : nullptr;
    }

    // ---- investigation results (filled by Init) ----------------------
    bool gfxLoaded()      const { return gfxLoaded_; }
    int  gfxObjectCount() const { return gfxObjectCount_; }  // 84-byte records parsed
    int  bankCount()      const { return bankCount_; }       // SHAPBANK blobs found
    int  fmt2BankCount()  const { return fmt2Banks_; }       // 24bpp banks (all of them)
    int  depth1BankCount()const { return depth1Banks_; }     // blittable banks (0 shipped)
    // True only if the real file carried at least one depth-1 bank the blitter
    // could rasterize directly (the shipped gilde.gfx carries none).
    bool realShapesUsable() const { return depth1Banks_ > 0; }
    // The precisely named missing decode that blocks the real-shape feed.
    static const char* missingDecode();

    const render::GfxArchive& archive() const { return archive_; }

private:
    render::GfxArchive archive_;
    SessionPanels panels_;
    SessionHudResult last_;
    render::DecodedShape panelChrome_;   // _PANEL_* 800x600 ARGB overlay (alpha-keyed)
    render::DecodedShape cityCrest_;     // _STADTWAPPEN_<CITY> ARGB
    // Chrome CONTENT (loaded by DecodePanelChrome): the gold gothic banner font,
    // the red button 3-slice, and the localized strings (textbin_deutsch.BIN):
    // title _TITEL_MAENNLICH+1, seasons _JAHRESZEITEN+0..3, button labels.
    MenuFont bannerFont_;                // _FONT   (banner/date — big gold)
    MenuFont smallFont_;                 // _FONT+1 (sidebar buttons/money — small gold)
    gui::text::TextDb textDb_;           // the full localized text array (chrome load)
    bool textDbLoaded_ = false;
    render::DecodedShape btnCapL_, btnCapR_, btnMid_;   // _BUTTON_RED 0/1/2
    std::string title_, seasons_[4], optLabel_, transLabel_;
    // Selection-state sidebar buttons (the live captures): with NOTHING
    // selected the pair reads Стройка/Обзор (_INFOPANEL_BAUEN+0 /
    // _INFOPANEL_OPTIONEN+4); with a building selected Информация/Транспорт
    // (_INFOPANEL_OPTIONEN+2 / the transport heading).
    std::string buildLabel_, overviewLabel_, infoLabel_;
    shim::IFileSystem* fs_ = nullptr;    // stashed by Init for DecodePanelChrome
    bool gfxLoaded_      = false;
    int  gfxObjectCount_ = 0;
    int  bankCount_      = 0;
    int  fmt2Banks_      = 0;
    int  depth1Banks_    = 0;
};

} // namespace guild::play
