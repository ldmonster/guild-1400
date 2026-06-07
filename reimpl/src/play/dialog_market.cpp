// Wave 28 PLAY — REAL DIALOG RENDER for the MARKET / TRADE slice.
// See dialog_market.h for the full grounding. Summary of the REAL siblings reused
// here (called, never redefined — ODR):
//   gui::Form_ParseResourceFile          (form_parse.cpp, VIBE_Form_LoadFromResource
//                                         @0x41beb8) — the real .form parse+build
//   gui::TradePanel_InitSlotTables /     (trade_panel.cpp, @0x50854c / @0x50a794 /
//        PopulateInventorySlots /         @0x50b600) — the real trade-panel layout
//        BuildSliderRow + kSliderOff/...
//   gui::ResetGuiState                   (form.cpp) — clean the retained-mode tables
//   render::SurfaceCreate / ColorFill /  (surface.cpp, VIBE_Surface_* gfx ops)
//        DrawRectOutline / DrawHLine / SetPixelRgb / GetPixelRgb / Destroy
//   sim::Building_ComputeMarketPrice     (building_production.cpp, @0x58f3d0) — price
//   play::ClassifyMarketInteraction /    (slice_market.cpp) — the real opcode-17
//        RunMarketSlice                   command emit + apply + game-day
#include "play/dialog_market.h"

#include <cstring>

#include "gui/form.h"           // g_forms
#include "gui/object.h"         // ResetGuiState
#include "gui/trade_panel.h"    // BuildSliderRow + the real per-widget offsets
#include "gui/window.h"         // g_windows, Window record
#include "render/colorformat.h"
#include "sim/building_production.h"  // Building_ComputeMarketPrice

namespace guild::play {

namespace {

// trunc-to-zero (matches slice_market's price truncation / x86 cvttsd2si).
i32 TruncToInt(double v) { return static_cast<i32>(v); }

// The trade-panel slider widget geometry from BuildSliderRow @0x50b600 (the slider
// is added with w=48 h=100 via VIBE_Widget_AddSliderToWindow). Buttons/icons/labels
// have no explicit size in the on-disk form (sized from gfx metrics at runtime),
// so the dialog gives them a faithful default cell so they draw + hit-test. These
// are RENDER cells, not changes to the recovered layout offsets (those come from
// gui::kSliderOff/kIconOff/kButtonOff/kBuyLblOff/kSellLblOff verbatim).
constexpr int kSliderRectW = gui::kSliderW;   // 48 (real)
constexpr int kSliderRectH = gui::kSliderH;   // 100 (real)
constexpr int kButtonRectW = 22;              // gfx 1720/1721 button footprint
constexpr int kButtonRectH = 16;
constexpr int kIconRectW   = 20;
constexpr int kIconRectH   = 20;
constexpr int kLabelRectW  = 45;              // BuildSliderRow sets label width +20 = 45
constexpr int kLabelRectH  = 8;
constexpr int kSlotRectW   = gui::kSlotCellPitch - 4;  // 60 (the 64-px cell, inset)
constexpr int kSlotRectH   = gui::kSlotCellPitch - 4;

// The panel-parchment clear colour (the trade form draws onto a parchment surface).
constexpr u8 kClearR = 200, kClearG = 180, kClearB = 120;

// Add a widget rect if it has area (clip happens at draw time).
void PushRect(MarketDialog& d, WidgetRole role, int x, int y, int w, int h,
              int rowIndex, i16 ware, int widgetId) {
    WidgetRect wr;
    wr.role = role; wr.x = x; wr.y = y; wr.w = w; wr.h = h;
    wr.rowIndex = rowIndex; wr.ware = ware; wr.widgetId = widgetId;
    d.widgets.push_back(wr);
}

} // namespace

int MarketDialog::buttonCount() const {
    int n = 0;
    for (const auto& w : widgets)
        if (w.role == WidgetRole::kBuyButton || w.role == WidgetRole::kSellButton) ++n;
    return n;
}
int MarketDialog::labelCount() const {
    int n = 0;
    for (const auto& w : widgets)
        if (w.role == WidgetRole::kBuyLabel || w.role == WidgetRole::kSellLabel) ++n;
    return n;
}

SliderRowGeom MarketDialogRowGeom() {
    return SliderRowGeom{
        gui::kSliderOff.dx, gui::kSliderOff.dy,
        gui::kIconOff.dx,   gui::kIconOff.dy,
        gui::kButtonOff.dx, gui::kButtonOff.dy,
        gui::kBuyLblOff.dx, gui::kBuyLblOff.dy,
        gui::kSellLblOff.dx,gui::kSellLblOff.dy,
    };
}

// ---------------------------------------------------------------------------
// MakeSyntheticTradeForm — a minimal valid FRM2 `.form` the real parser accepts.
// One root window at (x,y,w,h); the parser reads the FRM2 header + window record
// exactly as for a shipped member (stride 4124, header 8). Window-record offsets
// from form_parse.h: +0 x, +2 y, +4 h, +6 w, +8 flags, +204 objCount(=0), +3792
// parentIndex(=0 -> root).
// ---------------------------------------------------------------------------
std::vector<u8> MakeSyntheticTradeForm(int x, int y, int w, int h) {
    const int stride = gui::kFrm2RecordStride;  // 4124
    const int header = gui::kFrm2HeaderBytes;   // 8
    std::vector<u8> buf(static_cast<std::size_t>(header) + stride, 0);
    // "FRM2" magic (byte[3]=='2' -> fmt 2).
    buf[0] = 'F'; buf[1] = 'R'; buf[2] = 'M'; buf[3] = '2';
    auto put16 = [&](int off, u16 v) { buf[off] = u8(v & 0xFF); buf[off + 1] = u8(v >> 8); };
    auto put32 = [&](int off, u32 v) {
        buf[off] = u8(v & 0xFF); buf[off + 1] = u8((v >> 8) & 0xFF);
        buf[off + 2] = u8((v >> 16) & 0xFF); buf[off + 3] = u8((v >> 24) & 0xFF);
    };
    put32(4, 1);                       // windowCount = 1
    int rec = header;
    put16(rec + 0, static_cast<u16>(x));   // word[0] x
    put16(rec + 2, static_cast<u16>(y));   // word[1] y
    put16(rec + 4, static_cast<u16>(h));   // h (HIWORD dword@+2)
    put16(rec + 6, static_cast<u16>(w));   // w (HIWORD dword@+4)
    put32(rec + 8, 0x10);                   // flags (textbuffer flag, like the real shells)
    put16(rec + 204, 0);                    // objectCount = 0 (controls built at runtime)
    put32(rec + 3792, 0);                   // parentIndex = 0 (root)
    return buf;
}

// ---------------------------------------------------------------------------
// Lay out the trade rows over a parsed form. Reuses the REAL TradePanel slot tables
// + BuildSliderRow geometry. The row origin walks down the panel like the original
// item column (the production-window column layout: rows stacked by the slot pitch).
// ---------------------------------------------------------------------------
namespace {

void LayoutRows(MarketDialog& d, const std::vector<TradeRow>& rows, u8 mode) {
    // Initialise the REAL trade-panel slot grids (the buy/sell item cells). This is
    // VIBE_TradePanel_InitSlotTables — it sets every slot's grid (x,y).
    gui::TradePanel_InitSlotTables();

    const int panelX = d.panelX, panelY = d.panelY;

    for (std::size_t i = 0; i < rows.size(); ++i) {
        TradeRow tr = rows[i];

        // Compute the REAL price the panel quotes (the same oracle the BuildSliderRow
        // price label reads: VIBE_Building_LookupCachedMarketPrice ~ ComputeMarketPrice).
        tr.unitPrice = TruncToInt(sim::Building_ComputeMarketPrice(tr.ware, 100));

        // The row sits in the buy grid cell i (its real grid coordinate) — the panel
        // stacks item rows down the column. Use the real slot grid (x,y) as the row
        // origin (InitSlotTables placed slot i at ((i%4)<<6)+16 / ((i/4)<<6)+16).
        if (i < static_cast<std::size_t>(gui::kBuySlotCount)) {
            tr.rowX = gui::g_buySlots[i].x;
            tr.rowY = gui::g_buySlots[i].y;
        } else {
            tr.rowX = gui::kSlotInset;
            tr.rowY = gui::kSlotInset + static_cast<int>(i) * gui::kSlotCellPitch;
        }

        // Run the REAL BuildSliderRow layout to get the slider's absolute position +
        // exercise the real widget-id lifecycle (the production window builds these).
        gui::SliderRow row{};
        row.itemId = tr.ware ? tr.ware : 1;  // non-zero -> the build (not destroy) path
        row.rowX = tr.rowX; row.rowY = tr.rowY;
        row.min = 0; row.max = tr.capacity > 0 ? tr.capacity : 100; row.value = 0;
        gui::TradePanel_BuildSliderRow(row, mode, panelX, panelY);

        d.rows.push_back(tr);
        int ri = static_cast<int>(d.rows.size()) - 1;

        // The slot cell (the item grid square) — real InitSlotTables coordinate.
        PushRect(d, WidgetRole::kSlot, panelX + tr.rowX, panelY + tr.rowY,
                 kSlotRectW, kSlotRectH, ri, tr.ware, -1);

        // The slider widget — BuildSliderRow gave us its absolute (x,y).
        PushRect(d, WidgetRole::kSlider, row.sliderAbsX, row.sliderAbsY,
                 kSliderRectW, kSliderRectH, ri, tr.ware, row.sliderId);

        // The item icon (+15,+6 from the row origin, absolute = panel + row + off).
        PushRect(d, WidgetRole::kIcon,
                 panelX + tr.rowX + gui::kIconOff.dx, panelY + tr.rowY + gui::kIconOff.dy,
                 kIconRectW, kIconRectH, ri, tr.ware, row.iconId);

        // The BUY / SELL button (+15,+58) — built when its mode bit is set.
        if (mode & gui::kSliderRowBuy)
            PushRect(d, WidgetRole::kBuyButton,
                     panelX + tr.rowX + gui::kButtonOff.dx,
                     panelY + tr.rowY + gui::kButtonOff.dy,
                     kButtonRectW, kButtonRectH, ri, tr.ware, row.btnId);
        if (mode & gui::kSliderRowSell)
            PushRect(d, WidgetRole::kSellButton,
                     panelX + tr.rowX + gui::kButtonOff.dx,
                     panelY + tr.rowY + gui::kButtonOff.dy + kButtonRectH + 1,
                     kButtonRectW, kButtonRectH, ri, tr.ware, row.btnId);

        // The price labels (+16,+64 buy / +16,+73 sell).
        if (mode & gui::kSliderRowBuy)
            PushRect(d, WidgetRole::kBuyLabel,
                     panelX + tr.rowX + gui::kBuyLblOff.dx,
                     panelY + tr.rowY + gui::kBuyLblOff.dy,
                     kLabelRectW, kLabelRectH, ri, tr.ware, row.buyLbl);
        if (mode & gui::kSliderRowSell)
            PushRect(d, WidgetRole::kSellLabel,
                     panelX + tr.rowX + gui::kSellLblOff.dx,
                     panelY + tr.rowY + gui::kSellLblOff.dy,
                     kLabelRectW, kLabelRectH, ri, tr.ware, row.sellLbl);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// BuildMarketDialog — parse the real form + lay out the trade rows.
// ---------------------------------------------------------------------------
MarketDialog BuildMarketDialog(const u8* formBytes, std::size_t len,
                               const std::vector<TradeRow>& rows,
                               const char* formName, u8 mode) {
    MarketDialog d;

    // Reset the retained-mode GUI tables, then run the REAL parser (it builds the
    // live Window/Widget tables exactly as VIBE_Form_LoadFromResource does).
    gui::ResetGuiState();
    gui::FormFile ff = gui::Form_ParseResourceFile(formBytes, len, formName);
    d.formParsed  = ff.ok;
    d.frm2        = ff.frm2;
    d.formId      = ff.formId;
    d.windowCount = static_cast<int>(ff.windows.size());

    // Find the root (parent-less) window — the panel container the controls draw in.
    for (const auto& w : ff.windows) {
        if (w.parentIndex == 0 && w.windowSlot >= 0) {
            d.rootWindow = w.windowSlot;
            d.panelX = w.x; d.panelY = w.y; d.panelW = w.w; d.panelH = w.h;
            break;
        }
    }
    if (d.rootWindow < 0 && !ff.windows.empty()) {
        const auto& w = ff.windows.front();
        d.rootWindow = w.windowSlot;
        d.panelX = w.x; d.panelY = w.y; d.panelW = w.w; d.panelH = w.h;
    }

    // Push a window rect per parsed form window (the container shells the form ships).
    for (const auto& w : ff.windows) {
        if (w.windowSlot < 0) continue;
        PushRect(d, WidgetRole::kWindow, w.x, w.y, w.w, w.h, -1, 0, w.windowSlot);
    }

    // Lay out the trade rows (the programmatic trade-panel controls).
    LayoutRows(d, rows, mode);
    return d;
}

MarketDialog BuildSyntheticMarketDialog(const std::vector<TradeRow>& rows,
                                        int panelX, int panelY, int panelW, int panelH,
                                        u8 mode) {
    std::vector<u8> form = MakeSyntheticTradeForm(panelX, panelY, panelW, panelH);
    return BuildMarketDialog(form.data(), form.size(), rows, "synthetic.form", mode);
}

// ---------------------------------------------------------------------------
// RENDER — paint the widget rects through the REAL render::Surface 2D ops.
// ---------------------------------------------------------------------------
int SurfaceNonClearPixels(const render::Surface* surf, u8 cr, u8 cg, u8 cb) {
    if (!surf || !surf->pixels) return 0;
    int n = 0;
    for (int y = 0; y < surf->height; ++y) {
        for (int x = 0; x < surf->width; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(surf, x, y, px);
            if (px[0] != cr || px[1] != cg || px[2] != cb) ++n;
        }
    }
    return n;
}

namespace {

// Fill a rect with a solid colour via the real DrawHLine op (clipped by the surface).
void FillRect(render::Surface* s, int x, int y, int w, int h, u8 r, u8 g, u8 b) {
    for (int yy = 0; yy < h; ++yy)
        render::SurfaceDrawHLine(s, x, y + yy, w, r, g, b);
}

} // namespace

void RenderMarketDialogInto(const MarketDialog& dlg, render::Surface* surf,
                            bool clear, DialogRenderStats& stats) {
    stats = DialogRenderStats{};
    if (!surf || !surf->pixels) return;

    if (clear)
        render::SurfaceColorFill(surf, kClearR, kClearG, kClearB);

    for (const auto& w : dlg.widgets) {
        if (w.w <= 0 || w.h <= 0) continue;
        switch (w.role) {
            case WidgetRole::kWindow:
                // The panel container: outline (a window frame).
                render::SurfaceDrawRectOutline(surf, w.x, w.y, w.w, w.h, 90, 70, 40);
                break;
            case WidgetRole::kSlot:
                render::SurfaceDrawRectOutline(surf, w.x, w.y, w.w, w.h, 120, 100, 60);
                break;
            case WidgetRole::kSlider:
                render::SurfaceDrawRectOutline(surf, w.x, w.y, w.w, w.h, 60, 60, 160);
                break;
            case WidgetRole::kIcon:
                FillRect(surf, w.x, w.y, w.w, w.h, 180, 160, 80);
                break;
            case WidgetRole::kBuyButton:
                FillRect(surf, w.x, w.y, w.w, w.h, 40, 180, 40);   // green BUY
                ++stats.buttonsDrawn;
                break;
            case WidgetRole::kSellButton:
                FillRect(surf, w.x, w.y, w.w, w.h, 200, 50, 50);   // red SELL
                ++stats.buttonsDrawn;
                break;
            case WidgetRole::kBuyLabel:
            case WidgetRole::kSellLabel:
                render::SurfaceDrawRectOutline(surf, w.x, w.y, w.w, w.h, 30, 30, 30);
                break;
        }
        ++stats.widgetsDrawn;
    }

    stats.nonClearPixels = SurfaceNonClearPixels(surf, kClearR, kClearG, kClearB);
}

render::Surface* RenderMarketDialog(const MarketDialog& dlg, int fbW, int fbH,
                                    DialogRenderStats& stats) {
    stats = DialogRenderStats{};
    render::Surface* surf = render::SurfaceCreate(fbW, fbH, 16, render::Format565());
    if (!surf) return nullptr;
    RenderMarketDialogInto(dlg, surf, /*clear=*/true, stats);
    return surf;
}

// ---------------------------------------------------------------------------
// CLICK — hit-test a button rect -> MarketInteraction.
// ---------------------------------------------------------------------------
DialogClick ClickMarketDialog(const MarketDialog& dlg, int clickX, int clickY,
                              i32 buildingId, i32 qty, u8 player) {
    DialogClick c;
    for (const auto& w : dlg.widgets) {
        bool isButton = (w.role == WidgetRole::kBuyButton ||
                         w.role == WidgetRole::kSellButton);
        if (!isButton) continue;
        if (clickX >= w.x && clickX < w.x + w.w &&
            clickY >= w.y && clickY < w.y + w.h) {
            c.hitButton = true;
            c.role      = w.role;
            c.rowIndex  = w.rowIndex;
            c.interaction.side = (w.role == WidgetRole::kBuyButton)
                                     ? MarketSide::kBuy : MarketSide::kSell;
            c.interaction.ware       = w.ware;
            c.interaction.qty        = qty;
            c.interaction.buildingId = buildingId;
            c.interaction.buildingKind = 2;   // a kind-2 contor (the trade gate)
            c.interaction.player     = player;
            return c;  // first button hit wins (front-to-back z-order)
        }
    }
    return c;  // {side=kNone}
}

// ---------------------------------------------------------------------------
// RunMarketDialogSlice — the full load+render+click->command flow.
// ---------------------------------------------------------------------------
DialogSliceResult RunMarketDialogSlice(const u8* formBytes, std::size_t len,
                                       const std::vector<TradeRow>& rows,
                                       int clickRow, MarketSide side,
                                       i32 buildingId, i32 qty, u8 player,
                                       std::uint32_t econSeed, sim::CommandQueue& q,
                                       int fbW, int fbH, const char* formName) {
    DialogSliceResult r;

    // Mode: only the requested side's button (so the click targets it).
    u8 mode = (side == MarketSide::kSell) ? gui::kSliderRowSell : gui::kSliderRowBuy;

    // 1. parse the real form + lay out the rows.
    r.dialog = BuildMarketDialog(formBytes, len, rows, formName, mode);

    // 2. render to a headless surface.
    render::Surface* surf = RenderMarketDialog(r.dialog, fbW, fbH, r.render);

    // 3. click the requested row's button (compute the click point from its rect).
    WidgetRole want = (side == MarketSide::kSell) ? WidgetRole::kSellButton
                                                  : WidgetRole::kBuyButton;
    for (const auto& w : r.dialog.widgets) {
        if (w.role == want && w.rowIndex == clickRow) {
            r.click = ClickMarketDialog(r.dialog, w.x + w.w / 2, w.y + w.h / 2,
                                        buildingId, qty, player);
            break;
        }
    }

    // 4. route the click's interaction through the REAL market slice.
    if (r.click.hitButton && r.click.interaction.side != MarketSide::kNone) {
        r.slice = RunMarketSlice(r.click.interaction, econSeed, q);
        r.commandIssued = r.slice.command.issued;
    }

    if (surf) render::SurfaceDestroy(surf);
    return r;
}

} // namespace guild::play
