#pragma once
// Wave 28 PLAY — REAL DIALOG RENDER for the MARKET / TRADE slice.
//
// The P5 market slice (Wave 27, src/play/slice_market.{h,cpp}) already emits the
// REAL opcode-17 trade command (VIBE_Command_QueueRequest17 @0x49465c) and applies
// the treasury/stock/price effect. But its "dialog" was abstract: it never built or
// rendered the actual in-game trade window. This module closes that gap — it
// LOADS the REAL shipped trade form, LAYS OUT the REAL trade-panel widgets,
// RENDERS them to a headless software surface (widgets visible as non-clear pixels
// at their real rects), and ROUTES a BUY/SELL button press back into slice_market's
// real command emit.
//
// ===========================================================================
// GROUNDING (the real subsystem this drives)
// ===========================================================================
//
//   * THE FORM SHELL: Resources/forms.BIN member `Handel/Handel_Produktion.form`
//     ("Handel" = trade) — a FRM2 retained-mode form, 5 windows (1 root container
//     window + nested-window backings). Parsed BYTE-FOR-BYTE by the real parser
//     gui::Form_ParseResourceFile (VIBE_Form_LoadFromResource @0x41beb8), which
//     builds the live Window/Widget tables. The shipped .form stores only the
//     window CONTAINER shells (type-64 backing objects); the trade controls are
//     built PROGRAMMATICALLY at runtime by the trade-panel builder.
//
//   * THE TRADE-PANEL WIDGETS: VIBE_TradePanel_BuildProductionWindow @0x5087bc
//     opens the form and fills it via:
//       - VIBE_TradePanel_InitSlotTables @0x50854c  : the 4x4 buy + 4x2 sell item
//         grids (56-byte slot records, grid coords ((k%4)<<6)+16 / ((k/4)<<6)+16|208)
//       - VIBE_TradePanel_PopulateInventorySlots @0x50a794 : stock/capacity/fill
//       - VIBE_TradePanel_BuildSliderRow @0x50b600  : per-item slider ROW. Decoded
//         layout (relative to the row origin rowX,rowY, confirmed @0x50b600):
//             slider     @ x+6 , y+0    (VIBE_Widget_AddSliderToWindow, w48 h100)
//             icon       @ x+15, y+6
//             BUY/SELL button @ x+15, y+58   (VIBE_Object_AddToWindow; gfx 1720 buy
//                                             [a5&2], 1721 sell [a5&1])
//             buy  price label @ x+16, y+64  (mode&2; price = LookupCachedMarketPrice)
//             sell price label @ x+16, y+73  (mode&1)
//         The price labels read VIBE_Building_LookupCachedMarketPrice(*a4, player) —
//         the SAME oracle slice_market::ClassifyMarketInteraction reads via
//         sim::Building_ComputeMarketPrice (@0x58f3d0).
//
//   * THE BUTTON->COMMAND ROUTE: a click on the BUY/SELL button (the +15,+58
//     widget) fires, through the GUI frame loop's command-handler dispatch
//     (VIBE_He_FindFirstHandlerByFilter @0x4c63f8 + VIBE_GameLogic_RunFrameLoop),
//     the opcode-17 trade request that VIBE_Trade_RequestSellObjekt @0x46bff0
//     builds. This module reproduces the hit-test + side classification from the
//     REAL widget rects, then routes to slice_market::RunMarketSlice.
//
// ===========================================================================
// WIRED REAL siblings vs INERT gaps (declared honestly)
// ===========================================================================
//   WIRED: gui::Form_ParseResourceFile (the real .form parse + table build),
//          gui::TradePanel_InitSlotTables / PopulateInventorySlots / BuildSliderRow
//          (the real layout geometry), render::SurfaceCreate / SurfaceColorFill /
//          SurfaceDrawRectOutline / SurfaceDrawHLine / SurfaceSetPixelRgb (the real
//          2D software-surface ops), sim::Building_ComputeMarketPrice (the real
//          price oracle), and the WHOLE slice_market command->apply->day pipeline
//          (play::RunMarketSlice over the real sim::CommandQueue codec).
//   INERT (true gap, hooked + SAID SO): the live GUI frame-loop click dispatch
//          (VIBE_GameLogic_RunFrameLoop -> VIBE_He_* command-handler table) that, in
//          the running game, turns a mouse-up on the button widget into the
//          opcode-17 emit is not reconstructed as a single function. This module
//          performs the FAITHFUL hit-test against the real widget rects and the
//          side classification (which is what the dispatch decides) and routes to
//          the real command builder; the deep handler-table walk itself is the inert
//          piece. The trade-panel widget allocation leaves (Object_AddToWindow /
//          AddSliderToWindow) assign synthetic widget ids inside trade_panel.cpp
//          (its existing AllocWidget) — that is the renderer-binding gap that module
//          already declares, not new to here.
//
// Additive: no edits to slice_market.cpp / trade_panel.cpp / form_parse.cpp / any
// gui or render .cpp. All inert defaults defined in dialog_market.cpp.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "guild/common/types.h"
#include "gui/form_parse.h"
#include "play/slice_market.h"
#include "render/surface.h"
#include "sim/command.h"

namespace guild::gui { struct SliderRow; }

namespace guild::play {

// ===========================================================================
// A laid-out widget rect (one drawable control of the trade dialog). Each carries
// its REAL screen rect + a role so the renderer paints it and the hit-test routes a
// click. Rects come from the real form-window geometry (FormWindowRecord) and the
// real trade-panel layout offsets (BuildSliderRow @0x50b600).
// ===========================================================================
enum class WidgetRole : std::uint8_t {
    kWindow    = 0,  // a form window/container rect (FRM2 window record)
    kSlot      = 1,  // an item slot cell (InitSlotTables grid)
    kSlider    = 2,  // a slider-row slider widget (+6,+0; w48 h100)
    kIcon      = 3,  // a slider-row item icon (+15,+6)
    kBuyButton = 4,  // a slider-row BUY button (+15,+58, gfx 1720, mode&2)
    kSellButton= 5,  // a slider-row SELL button (+15,+58, gfx 1721, mode&1)
    kBuyLabel  = 6,  // a slider-row buy-price label (+16,+64)
    kSellLabel = 7,  // a slider-row sell-price label (+16,+73)
};

struct WidgetRect {
    WidgetRole role = WidgetRole::kWindow;
    int x = 0, y = 0, w = 0, h = 0;   // absolute screen rect
    int rowIndex = -1;                // which slider row (>=0) this belongs to (-1 form window)
    i16 ware     = 0;                 // the good prototype this row trades (slot/row widgets)
    int widgetId = -1;                // the id BuildSliderRow assigned (or window slot)
};

// ===========================================================================
// The trade-row model the dialog lays out: one item the player can buy/sell, with
// its real catalog ware id + the real unit price the oracle quotes.
// ===========================================================================
struct TradeRow {
    i16 ware       = 0;    // good prototype (the priced scene-type id)
    i32 stock      = 0;    // building stock in this row (PopulateInventorySlots)
    i32 capacity   = 0;
    i32 unitPrice  = 0;    // trunc(Building_ComputeMarketPrice(ware,100))
    int rowX = 0, rowY = 0;// the row origin in the panel window
};

// ===========================================================================
// The parsed + laid-out dialog: the real form structure + the laid-out widget rects.
// ===========================================================================
struct MarketDialog {
    bool formParsed   = false;     // Form_ParseResourceFile accepted the bytes
    bool frm2         = false;     // FRM2 layout (the shipped trade forms)
    int  formId       = -1;        // allocated form slot
    int  windowCount  = 0;         // windows the parser built
    int  rootWindow   = -1;        // the first root window slot (the panel container)
    int  panelX = 0, panelY = 0;   // the panel (root window) origin
    int  panelW = 0, panelH = 0;   // the panel size (real form geometry)

    std::vector<TradeRow>   rows;      // the trade rows (one per priceable ware)
    std::vector<WidgetRect> widgets;   // every laid-out widget rect (drawable+clickable)

    int buttonCount() const;           // BUY+SELL button rects
    int labelCount()  const;           // price label rects
};

// ===========================================================================
// Step 1 — LOAD + LAY OUT the dialog over a parsed form buffer.
//
// Parses `formBytes` with the REAL parser (Form_ParseResourceFile), then lays out
// the trade-panel widgets for `rows` using the REAL trade-panel offsets
// (TradePanel_BuildSliderRow + InitSlotTables). Each row gets a slider, icon, BUY +
// SELL button and a buy/sell price label, positioned at the real per-widget offsets
// relative to the row origin inside the panel window. Returns the laid-out dialog.
//
// `mode` selects which buttons/labels each row builds (gui::kSliderRowBuy |
// gui::kSliderRowSell); default = both (a full trade row).
MarketDialog BuildMarketDialog(const u8* formBytes, std::size_t len,
                               const std::vector<TradeRow>& rows,
                               const char* formName = "Handel/Handel_Produktion.form",
                               u8 mode = 0x03 /* buy|sell */);

// Convenience: build the dialog with a SYNTHETIC minimal FRM2 form (one root window)
// so unit tests can exercise the layout without forms.BIN. Same layout math.
MarketDialog BuildSyntheticMarketDialog(const std::vector<TradeRow>& rows,
                                        int panelX, int panelY, int panelW, int panelH,
                                        u8 mode = 0x03);

// Produce a minimal valid FRM2 `.form` byte buffer with one root window at the given
// geometry (so the REAL parser builds a real window record). Exposed for tests.
std::vector<u8> MakeSyntheticTradeForm(int x, int y, int w, int h);

// ===========================================================================
// Step 2 — RENDER the dialog to a headless software surface.
//
// Clears the surface (panel-parchment colour), then paints every widget rect with
// the REAL render::Surface 2D ops: window/slot rects as outlines, buttons as filled
// blocks (buy = green, sell = red), labels/sliders/icons as outlined blocks. The
// result is a surface whose non-clear pixels land exactly on the real widget rects.
// Caller owns the returned Surface (render::SurfaceDestroy).
// ===========================================================================
struct DialogRenderStats {
    int nonClearPixels = 0;   // pixels differing from the clear colour
    int widgetsDrawn   = 0;   // widget rects painted (clipped-in)
    int buttonsDrawn   = 0;
};

// Render into a freshly created surface (fbW x fbH, 16bpp). Returns the surface (or
// null on alloc failure) and fills `stats`.
render::Surface* RenderMarketDialog(const MarketDialog& dlg, int fbW, int fbH,
                                    DialogRenderStats& stats);

// Render into an EXISTING surface (does not clear unless `clear`). Fills `stats`.
void RenderMarketDialogInto(const MarketDialog& dlg, render::Surface* surf,
                            bool clear, DialogRenderStats& stats);

// Count surface pixels that differ from `clear` colour (a non-blank measure).
int SurfaceNonClearPixels(const render::Surface* surf, u8 r, u8 g, u8 b);

// ===========================================================================
// Step 3 — ROUTE a click on a widget rect -> a MarketInteraction.
//
// Hit-tests the dialog's widget rects against (clickX,clickY); the FIRST BUY/SELL
// button hit yields a MarketInteraction for that row's ware + side (buy/sell). A
// click that lands on no button yields {side=kNone}. `buildingId`,`qty`,`player`
// fill the parts the dialog does not carry (the panel is opened FOR a building).
//
// This is the faithful reproduction of what the GUI frame-loop click dispatch
// decides (which button -> which trade side -> which ware); the deep handler-table
// walk is the inert gap (see header preamble).
struct DialogClick {
    bool             hitButton = false;   // a BUY/SELL button was clicked
    WidgetRole       role      = WidgetRole::kWindow;
    int              rowIndex  = -1;
    MarketInteraction interaction{};      // {side=kNone} when no button hit
};
DialogClick ClickMarketDialog(const MarketDialog& dlg, int clickX, int clickY,
                              i32 buildingId, i32 qty, u8 player);

// ===========================================================================
// Step 4 — the END-TO-END dialog interaction: load+render+click->command.
//
// Ties the whole flow: parse the real form, lay out the rows, render to a surface,
// click the requested row's BUY (or SELL) button, then route the resulting
// interaction through the REAL slice_market::RunMarketSlice so the actual opcode-17
// command applies + a game-day advances. Returns both the dialog render result and
// the market slice result.
//
// The caller must have populated the live sim arrays (synthetic seed or LoadWorld)
// and seeded g_sceneTypes for the traded wares (so the oracle prices them). `q` is a
// standalone CommandQueue (Init()'d).
struct DialogSliceResult {
    MarketDialog       dialog{};
    DialogRenderStats  render{};
    DialogClick        click{};
    MarketSliceResult  slice{};       // the real command->apply->day result
    bool               commandIssued = false;
};

// `clickRow` selects which laid-out row to click; `side` which button. The caller
// supplies the building the panel was opened for. Renders into `fbW x fbH`.
DialogSliceResult RunMarketDialogSlice(const u8* formBytes, std::size_t len,
                                       const std::vector<TradeRow>& rows,
                                       int clickRow, MarketSide side,
                                       i32 buildingId, i32 qty, u8 player,
                                       std::uint32_t econSeed, sim::CommandQueue& q,
                                       int fbW, int fbH,
                                       const char* formName = "Handel/Handel_Produktion.form");

// Read back the per-build layout offsets this module used (the real BuildSliderRow
// offsets) so tests can assert the geometry is the reconstructed one.
struct SliderRowGeom { int sliderDx, sliderDy, iconDx, iconDy, buttonDx, buttonDy,
                           buyLblDx, buyLblDy, sellLblDx, sellLblDy; };
SliderRowGeom MarketDialogRowGeom();

} // namespace guild::play
