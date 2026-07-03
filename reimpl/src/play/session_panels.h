#pragma once
// =============================================================================
// guild::play — IN-GAME SESSION PANELS: the tooltip lifecycle + the selected-
// entity info panel, composited over the session's 16bpp RGB565 framebuffer
// with the SAME reconstructed draw leaves play::SessionHud uses.
//
// What runs each frame (all REAL reconstructions, with provenance):
//
//   TOOLTIP
//   * gui::Tooltip_Dispatch        — VIBE_Tooltip_DispatchByType @0x4f7424, the
//     per-frame show/hide/rebuild lifecycle over the hover state (the same
//     dword_633908/63390C/75BF3C model gui/tooltip_dispatch reconstructs).
//   * gui::Tooltip_ClassifySubject — the @0x4f7424 classification core, driven
//     over this session's shadow scene tables (the pick boundary materialises
//     the object/building/person table entries the live scene-graph would
//     supply — the same boundary play::SessionSelect documents for @0x4b950c).
//   * the content builders (gui/tooltip_content, NEW this wave):
//       VIBE_Tooltip_BuildObject   @0x4f7a10  (incl. the @0x6496A9 producer
//                                              table walk + weapon-user rows)
//       VIBE_Tooltip_BuildUpgrade  @0x4f8154
//       VIBE_Tooltip_BuildBuilding @0x4f78e4
//       VIBE_Tooltip_BuildPerson   @0x4f84ac
//       VIBE_Tooltip_BuildContact  @0x4f83e8  (via Tooltip_ResolveContact)
//     plus the screen-edge clamp block (Tooltip_ClampToScreen) that repositions
//     an overflowing tooltip at screenW-16-w — applied 1:1 to the composited box.
//
//   INFO PANEL
//   * gui::InfoPanel_Update        — VIBE_InfoPanel_Update @0x4b84c0: subject
//     classification + change detection (rebuild only when the selection
//     changed; the dword_631E24.. snapshot).
//   * gui::InfoPanel_Build*        — the @0x4b64b0/0x4b6930/0x4b6c80/0x4b6db8/
//     0x4b7104 content builders (gui/infopanel_build), run through a recording
//     InfoPanelHost so every widget/text/slider op lands with its original
//     coordinates and ids.
//
//   COMPOSITION (the same 16bpp-native leaves as SessionHud):
//     box fills     gui::MenuFillRect @0x423c70 -> render::SurfaceDrawHLine
//     box frames    render::SurfaceDrawRectOutline @0x4242d4
//     text lines    render::DrawText/DrawGlyph @0x434E18/0x434D0C
//     icons         play::HudRenderHooks::drawSprite -> the REAL
//                   render::ShapeShowFromBank @0x5d861c chain
//     slider/bar    gui::MenuFillRect track + play::HudBarFillPixels fill
//
// NAMED GAPS (documented, not papered over):
//   * VIBE_Text_RenderRichString @0x59d6e8 (the rich-text renderer) is NOT
//     reconstructed.  The composited line text is a plain-glyph projection of
//     the recovered template + its original arguments: text-ids resolve through
//     gui::text::TextDb when supplied, "$x" control tokens are stripped, and
//     %i/%s/%a/%N substitutions use the original argument order.  Every
//     template/argument pair itself is byte-exact (tooltip_content).
//   * The ToolTip\*.form / panel\infopanel_*.form window geometry is asset
//     data parsed by the form loader; the box anchors here are caller-tunable
//     (the SessionHud::Layout precedent), while the per-widget coordinates
//     INSIDE the box are the builders' recovered values.
//   * VIBE_Tooltip_BuildPersonDetailed @0x4f882c is out of the dispatch call
//     tree (only VIBE_MapView_PanelDispatcher @0x5441d0 calls it) — deferred.
// =============================================================================
#include "guild/common/types.h"
#include "gui/infopanel.h"          // InfoSelection / InfoPanel_Update @0x4b84c0
#include "gui/infopanel_build.h"    // InfoPanel_Build* @0x4b64b0.. + records
#include "gui/tooltip_content.h"    // the @0x4f7a10.. content builders (NEW)
#include "gui/tooltip_dispatch.h"   // Tooltip_Dispatch @0x4f7424
#include "render/types.h"           // render::Surface

#include <string>
#include <vector>

namespace guild::gui::text { class TextDb; }

namespace guild::play {

// ---------------------------------------------------------------------------
// Per-frame hover/selection inputs (the session's substitute for the live
// globals each reconstruction reads; every field cites its dword).
// ---------------------------------------------------------------------------
struct PanelObjectHover {                 // object/upgrade tooltip content
    gui::TooltipObjectView view;          // the 65-byte record fields
    gui::TooltipObjectEnv  env;           // economy-leaf results
    // Upgrade-kind extras (the @0x4f8154 owner scan + scaled price).
    gui::TooltipUpgradeOwnerView upgradeOwner;
    gui::TooltipUpgradeEnv       upgradeEnv;
};
struct PanelBuildingHover {               // building tooltip content
    u8  colorSelector = 0;                // record[583]
    i32 extraField    = 0;                // *(i32*)(record+579)
    i32 salePrice     = 0;                // VIBE_Building_ComputeSalePrice @0x591480
};
struct PanelPersonHover {                 // person tooltip content
    gui::TooltipPersonView view;
    gui::TooltipPersonEnv  env;
};

struct SessionPanelsInputs {
    // ---- hover state (one frame) ----
    int hoveredTooltipId = -1;            // dword_75BF3C (-1 = nothing hovered)
    int scenePickActive  = 0;             // dword_672238 (3D pick this frame)
    int cursorX = 0, cursorY = 0;         // unk_67220E / dword_672210 (>>16)
    // Which scene table the hovered widget's +736 reference would point into.
    gui::TooltipKind hoverKind = gui::TooltipKind::kNone;
    int hoverObjectCode   = 0;            // object id (object/upgrade hover)
    u8  hoverObjectClass  = 0;            // objectBase[65*code] (builder select)
    int hoverBuildingCode = 0;            // building code (building hover)
    u16 hoverPersonId     = 0;            // person record word +0 (person hover)
    // Pending contact-tooltip request (dword_631724 / byte name).
    int         contactReqId = 0;
    const char* contactName  = nullptr;
    // Content feeds for the live builder (null = zeroed defaults).
    const PanelObjectHover*   object   = nullptr;
    const PanelBuildingHover* building = nullptr;
    const PanelPersonHover*   person   = nullptr;

    // ---- selection state (drives InfoPanel_Update @0x4b84c0) ----
    gui::InfoSelection selection{};       // the dword_631744.. live handles
    const gui::InfoObjectRecord*   selObject   = nullptr;
    const gui::InfoBuildingRecord* selBuilding = nullptr;
    const gui::InfoPersonRecord*   selPerson   = nullptr;
    int  selSelectionFlags = 0;           // ComputeSelectionFlags value (& 0x10)
    int  selCategory       = 0;           // building category (second sprite)
    bool selItemIsOwn      = false;       // item == player test
    bool selOwnerMatches   = false;       // BuildObject class-2/6 owner gate
    bool selSlotsAfter     = false;       // CollectSlotsAfterObject verdict

    // Optional localized strings (text ids resolve to real strings when set).
    const gui::text::TextDb* textDb = nullptr;
    // The selected building's scene TYPE NAME (e.g. "SCHMUGGLERLOCH" from the
    // owner-matched node's gb_<type> model). When set, the building panel name
    // resolves through the localized keyed record "_GEB_<TYPE>_NAME+0" (the
    // textbin key namespace) — the engine's own name text for the type.
    const char* selBuildingTypeName = nullptr;
};

// Observable per-frame results.
struct SessionPanelsResult {
    // tooltip
    gui::TooltipAction tooltipAction = gui::TooltipAction::kNone;
    bool tooltipVisible = false;          // dword_633908 != -1 after dispatch
    int  tooltipBuilds  = 0;              // builder runs this frame (0/1)
    int  tooltipTextOps = 0;              // text lines composited
    int  tooltipIconOps = 0;              // icon ops (sprite hook attempts)
    int  tooltipIconBlits = 0;            // icon ops the real blitter drew
    int  tooltipX = 0, tooltipY = 0, tooltipW = 0, tooltipH = 0;
    bool tooltipClamped = false;          // the 0x4f7aa5 clamp fired
    char tooltipForm[40] = {0};           // loaded ToolTip\*.form name
    // info panel
    gui::InfoBuilder panelBuilder = gui::InfoBuilder::kNone; // builder run (rebuilds only)
    bool panelRebuilt  = false;           // InfoPanel_Update fired a builder
    bool panelVisible  = false;           // a built panel is on screen
    int  panelTextOps  = 0;
    int  panelIconOps  = 0;
    int  panelIconBlits = 0;
    int  panelSliderFillPx = -1;          // slider fill width (-1 = no slider)
    char panelForm[40] = {0};             // loaded panel\*.form name
};

class SessionPanels {
public:
    // Caller-tunable anchors (the SessionHud::Layout precedent; the .form
    // window geometry is asset data — see the header named gaps).
    struct Layout {
        int tooltipW       = 160;  // composited tooltip box width
        int tooltipOffsetX = 12;   // box offset from the cursor
        int tooltipOffsetY = 12;
        int lineH          = 9;    // DrawText 5x7 line pitch (SessionHud's +9)
        int pad            = 3;    // box inner padding
        int panelX = 4, panelY = 200;        // info-panel box top-left
        int panelW = 200, panelH = 120;      // info-panel box size
        // Draw the grey fill + outline behind the panel content. With the real
        // sidebar chrome the panel sits in the sidebar CARD slot (the stone art
        // IS the background), so the box is skipped (content only).
        bool drawBox = true;
    };
    Layout layout;

    // Optional card-text renderer (the sidebar-card small gold face). When set
    // and drawBox is off, panel text lines draw through this hook centred at
    // (cx, y) instead of the built-in 5x7 debug face. Returns the drawn width
    // (0 = declined -> fall back to the 5x7 face).
    struct CardTextHook {
        // Draw `text` centred at (cx, y), word-wrapped to `maxW` px. Returns
        // the number of lines drawn (0 = declined -> 5x7 fallback).
        int (*draw)(render::Surface& s, int cx, int y, int maxW,
                    const char* text, void* user) = nullptr;
        void* user = nullptr;
    };
    CardTextHook cardText;

    SessionPanels();
    ~SessionPanels();   // out-of-line (vector<Op> with nested Op)

    // One frame: run the REAL tooltip dispatch (@0x4f7424) + the REAL info-
    // panel update (@0x4b84c0) over `in`, then composite both onto `s`
    // (a 16bpp RGB565 render::Surface).  Safe no-op on a degenerate surface.
    void Frame(render::Surface& s, const SessionPanelsInputs& in);

    const SessionPanelsResult& lastResult() const { return last_; }

    // The live dispatch state (the dword_633908.. block) — exposed for tests,
    // like gui::TooltipDispatchState itself.
    const gui::TooltipDispatchState& tooltipState() const { return tip_; }

    // Reset the lifecycle (new session): tears down the tooltip form state and
    // invalidates the info-panel snapshot.
    void Reset();

private:
    struct Op;                       // one recorded builder emission
    class TipHost;                   // recording gui::TooltipContentHost
    class PanelHost;                 // recording gui::InfoPanelHost
    class Sink;                      // gui::TooltipDispatchSink -> builders

    void RunTooltip(render::Surface& s, const SessionPanelsInputs& in);
    void RunPanel(render::Surface& s, const SessionPanelsInputs& in);
    void DrawTooltipBox(render::Surface& s, const SessionPanelsInputs& in);
    void DrawPanelBox(render::Surface& s, const SessionPanelsInputs& in);
    std::string SynthesizePanelLine(const char* fmt,
                                    const SessionPanelsInputs& in);

    gui::TooltipDispatchState tip_;       // dword_633908/63390C/633914/633918..
    gui::InfoSnapshot         snap_;      // dword_631E24.. selection snapshot
    bool snapValid_ = false;

    // Shadow scene tables for the REAL classification arithmetic (the pick
    // boundary documented in play/session_select.h): a 65-byte-stride object
    // span, a building span, one person record.
    std::vector<u8> objectShadow_;        // kObjectSpan bytes (class byte live)
    std::vector<u8> buildingShadow_;      // kBuildingSpan bytes (code byte live)
    u8  personShadow_[268] = {0};         // one word_12CE910-stride record
    u16 contactShadowWord_ = 0;           // *(u16*)dword_63172C model

    std::vector<Op> tipOps_;              // last built tooltip content
    std::vector<Op> panelOps_;            // last built panel content
    std::string     tipFormName_;
    std::string     panelFormName_;
    int  formSeq_ = 0;                    // monotonic recording form handles
    int  tipBoxX_ = 0, tipBoxY_ = 0;      // box anchor latched at build time
    bool tipClamped_ = false;             // the 0x4f7aa5 clamp fired at build

    SessionPanelsResult last_;
};

} // namespace guild::play
