// Wave 30 PLAY B2 — REAL DIALOG RENDER for the BANK / LOAN (Kredit) slice.
// See dialog_bank.h for the full grounding. Summary of the REAL siblings reused
// here (called, never redefined — ODR):
//   gui::Form_ParseResourceFile          (form_parse.cpp, VIBE_Form_LoadFromResource
//                                         @0x41beb8) — the real .form parse+build
//   gui::ResetGuiState                   (form.cpp) — clean the retained-mode tables
//   render::SurfaceCreate / ColorFill /  (surface.cpp, VIBE_Surface_* gfx ops)
//        DrawRectOutline / DrawHLine / GetPixelRgb / Destroy
//   play::ClassifyBankInteraction /      (slice_bank.cpp) — the real opcode-15 loan
//        RunBankSlice                      command classify + emit + apply + day
#include "play/dialog_bank.h"

#include <cstring>

#include "gui/form.h"           // g_forms
#include "gui/object.h"         // ResetGuiState
#include "gui/window.h"         // g_windows, Window record
#include "render/colorformat.h"

namespace guild::play {

namespace {

// The take-loan panel draws onto a parchment-style surface (the Geldleihe skin).
constexpr u8 kClearR = 188, kClearG = 170, kClearB = 124;

// Faithful render cells for the runtime-built rows/buttons. The on-disk form carries
// NO row/button coordinates (the rows are built by the take-loan dialog at runtime
// via Window_AddChildWindow and the buttons via Hud_BuildButtonRow), so these are a
// documented render-layout choice inside the real win2/win3 rects, NOT a recovered
// offset (see header INERT note). The lender-row height 16 IS the real value (the
// child window is created with height 16: Window_AddChildWindow(..,16,..)).
constexpr int kRowH      = 16;   // real lender-row child-window height
constexpr int kRowGap    = 8;    // *(int*)v52 = sliderH + 8 row pitch gap
constexpr int kRowInsetX = 8;
constexpr int kRowTopY   = 8;
constexpr int kBtnInsetX = 4;
constexpr int kBtnTopY   = 8;
constexpr int kBtnH      = 24;
constexpr int kBtnGap    = 8;

void PushRect(BankDialog& d, BankWidgetRole role, int x, int y, int w, int h,
              int lenderIndex, i32 lenderId, int widgetId) {
    BankWidgetRect wr;
    wr.role = role; wr.x = x; wr.y = y; wr.w = w; wr.h = h;
    wr.lenderIndex = lenderIndex; wr.lenderId = lenderId; wr.widgetId = widgetId;
    d.widgets.push_back(wr);
}

void put16(std::vector<u8>& b, int off, u16 v) {
    b[off] = u8(v & 0xFF); b[off + 1] = u8(v >> 8);
}
void put32(std::vector<u8>& b, int off, u32 v) {
    b[off] = u8(v & 0xFF); b[off + 1] = u8((v >> 8) & 0xFF);
    b[off + 2] = u8((v >> 16) & 0xFF); b[off + 3] = u8((v >> 24) & 0xFF);
}

// Build one FRM2 window record into buf at byte offset `rec`.
void PutWindow(std::vector<u8>& buf, int rec, int x, int y, int w, int h,
               u32 flags, u32 parentIndex) {
    put16(buf, rec + 0, static_cast<u16>(x));
    put16(buf, rec + 2, static_cast<u16>(y));
    put16(buf, rec + 4, static_cast<u16>(h));
    put16(buf, rec + 6, static_cast<u16>(w));
    put32(buf, rec + 8, flags);
    put16(buf, rec + 204, 0);
    put32(buf, rec + 3792, parentIndex);
}

} // namespace

int BankDialog::rowCount() const {
    int n = 0;
    for (const auto& w : widgets)
        if (w.role == BankWidgetRole::kLenderRow) ++n;
    return n;
}
int BankDialog::buttonCount() const {
    int n = 0;
    for (const auto& w : widgets)
        if (w.role == BankWidgetRole::kConfirmBtn || w.role == BankWidgetRole::kCancelBtn)
            ++n;
    return n;
}

// ---------------------------------------------------------------------------
// MakeSyntheticBankForm — a minimal valid FRM2 `.form` the real parser accepts,
// mirroring the real 5-window shape (root + slider + row-list + button + slider).
// ---------------------------------------------------------------------------
std::vector<u8> MakeSyntheticBankForm(int x, int y, int w, int h) {
    const int stride = gui::kFrm2RecordStride;  // 4124
    const int header = gui::kFrm2HeaderBytes;   // 8
    std::vector<u8> buf(static_cast<std::size_t>(header) + 5 * stride, 0);
    buf[0] = 'F'; buf[1] = 'R'; buf[2] = 'M'; buf[3] = '2';
    put32(buf, 4, 5);                       // windowCount = 5

    // win0 — root container.
    PutWindow(buf, header + 0 * stride, x, y, w, h, 0x10, 0);
    // win1 — slider track (flags 0x110).
    PutWindow(buf, header + 1 * stride, x + 105, y + 20, 42, (h > 80 ? h - 80 : h),
              0x110, 1);
    // win2 — the lender-row list window.
    PutWindow(buf, header + 2 * stride, x + 53, y + 109,
              (w > 160 ? w - 160 : w), (h > 100 ? h - 100 : h), 0x10, 1);
    // win3 — the button window.
    PutWindow(buf, header + 3 * stride, x + 58, y + (h > 40 ? h - 40 : 0),
              (w > 40 ? w - 40 : w), 34, 0x10, 1);
    // win4 — slider track (flags 0x110).
    PutWindow(buf, header + 4 * stride, x + 53, y + 64, 41, (h > 100 ? h - 100 : h),
              0x110, 1);
    return buf;
}

// ---------------------------------------------------------------------------
// Lay out the slider + lender rows + buttons over the real form windows.
// ---------------------------------------------------------------------------
namespace {

void LayoutLoanPanel(BankDialog& d, const std::vector<LenderChoice>& lenders) {
    // The amount slider track (the real win1, flags 0x110).
    if (d.sliderWindow >= 0) {
        // find the slider window geometry from the pushed window rects
        for (const auto& w : d.widgets) {
            if (w.role == BankWidgetRole::kWindow && w.widgetId == d.sliderWindow) {
                PushRect(d, BankWidgetRole::kAmountSlider, w.x, w.y, w.w, w.h, -1, -1,
                         w.widgetId);
                break;
            }
        }
    }

    // The lender rows, stacked down the real win2 list window.
    int rx, ry, rw;
    if (d.rowWindow >= 0 && d.rowW > 0) {
        rx = d.rowX; ry = d.rowY; rw = d.rowW;
    } else {
        rx = d.panelX; ry = d.panelY; rw = d.panelW;
    }
    for (std::size_t i = 0; i < lenders.size(); ++i) {
        int ly = ry + kRowTopY + static_cast<int>(i) * (kRowH + kRowGap);
        int lx = rx + kRowInsetX;
        int lw = (rw > 2 * kRowInsetX) ? rw - 2 * kRowInsetX : rw;
        d.lenders.push_back(lenders[i]);
        int li = static_cast<int>(d.lenders.size()) - 1;
        PushRect(d, BankWidgetRole::kLenderRow, lx, ly, lw, kRowH, li,
                 lenders[i].lenderId, 2000 + li);
    }

    // The cancel + confirm buttons in the real win3 button window. Hud_BuildButtonRow
    // built two buttons: buf[0] cancel, buf[1] confirm.
    int bx, by, bw;
    if (d.buttonWindow >= 0 && d.btnW > 0) {
        bx = d.btnX; by = d.btnY; bw = d.btnW;
    } else {
        bx = d.panelX; by = d.panelY + (d.panelH > 40 ? d.panelH - 40 : 0);
        bw = d.panelW;
    }
    int btnW = (bw > 2 * kBtnInsetX) ? bw - 2 * kBtnInsetX : bw;
    // a vertical column (the win3 button window is a tall narrow strip): cancel above
    // confirm.
    PushRect(d, BankWidgetRole::kCancelBtn, bx + kBtnInsetX, by + kBtnTopY,
             btnW, kBtnH, -1, -1, 3000);
    PushRect(d, BankWidgetRole::kConfirmBtn, bx + kBtnInsetX,
             by + kBtnTopY + kBtnH + kBtnGap, btnW, kBtnH, -1, -1, 3001);
}

} // namespace

// ---------------------------------------------------------------------------
// BuildBankDialog — parse the real form + lay out the loan panel.
// ---------------------------------------------------------------------------
BankDialog BuildBankDialog(const u8* formBytes, std::size_t len,
                           const std::vector<LenderChoice>& lenders,
                           i32 borrowerId, i32 amount, u8 player,
                           const char* formName) {
    BankDialog d;
    d.borrowerId = borrowerId;
    d.amount     = amount;
    d.player     = player;

    gui::ResetGuiState();
    gui::FormFile ff = gui::Form_ParseResourceFile(formBytes, len, formName);
    d.formParsed  = ff.ok;
    d.frm2        = ff.frm2;
    d.formId      = ff.formId;
    d.windowCount = static_cast<int>(ff.windows.size());

    // Root window (parentIndex == 0).
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

    // Push a window rect per parsed form window (the container shells the form ships)
    // BEFORE classifying child windows (LayoutLoanPanel reads these for slider geom).
    for (const auto& w : ff.windows) {
        if (w.windowSlot < 0) continue;
        PushRect(d, BankWidgetRole::kWindow, w.x, w.y, w.w, w.h, -1, -1, w.windowSlot);
    }

    // Classify the child windows by their real geometry order. The real form's child
    // windows (parentIndex == 1) are, in file order: win1 slider (flags&0x100), win2
    // row list (widest), win3 button (narrow tall strip), win4 slider (flags&0x100).
    int widest = -1, widestW = 0;
    for (const auto& w : ff.windows) {
        if (w.parentIndex != 1 || w.windowSlot < 0) continue;
        bool isSlider = (w.flags & 0x100) != 0;
        if (isSlider) {
            if (d.sliderWindow < 0) {
                d.sliderWindow = w.windowSlot;
                d.sliderX = w.x; d.sliderY = w.y;
            }
            continue;
        }
        if (w.w > widestW) { widestW = w.w; widest = w.windowSlot;
                             d.rowX = w.x; d.rowY = w.y; d.rowW = w.w; }
    }
    if (widest >= 0) d.rowWindow = widest;
    // The button window: the non-slider child window that is NOT the widest row list
    // (a narrow tall strip).
    for (const auto& w : ff.windows) {
        if (w.parentIndex != 1 || w.windowSlot < 0) continue;
        if ((w.flags & 0x100) != 0) continue;
        if (w.windowSlot == d.rowWindow) continue;
        d.buttonWindow = w.windowSlot;
        d.btnX = w.x; d.btnY = w.y; d.btnW = w.w;
        break;
    }

    LayoutLoanPanel(d, lenders);
    return d;
}

BankDialog BuildSyntheticBankDialog(const std::vector<LenderChoice>& lenders,
                                    i32 borrowerId, i32 amount, u8 player,
                                    int panelX, int panelY, int panelW, int panelH) {
    std::vector<u8> form = MakeSyntheticBankForm(panelX, panelY, panelW, panelH);
    return BuildBankDialog(form.data(), form.size(), lenders, borrowerId, amount,
                           player, "synthetic_bank.form");
}

// ---------------------------------------------------------------------------
// RENDER — paint the widget rects through the REAL render::Surface 2D ops.
// ---------------------------------------------------------------------------
int BankSurfaceNonClearPixels(const render::Surface* surf, u8 cr, u8 cg, u8 cb) {
    if (!surf || !surf->pixels) return 0;
    int n = 0;
    for (int y = 0; y < surf->height; ++y)
        for (int x = 0; x < surf->width; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(surf, x, y, px);
            if (px[0] != cr || px[1] != cg || px[2] != cb) ++n;
        }
    return n;
}

namespace {
void FillRect(render::Surface* s, int x, int y, int w, int h, u8 r, u8 g, u8 b) {
    for (int yy = 0; yy < h; ++yy)
        render::SurfaceDrawHLine(s, x, y + yy, w, r, g, b);
}
} // namespace

void RenderBankDialogInto(const BankDialog& dlg, render::Surface* surf,
                          bool clear, BankRenderStats& stats) {
    stats = BankRenderStats{};
    if (!surf || !surf->pixels) return;
    if (clear) render::SurfaceColorFill(surf, kClearR, kClearG, kClearB);

    for (const auto& w : dlg.widgets) {
        if (w.w <= 0 || w.h <= 0) continue;
        switch (w.role) {
            case BankWidgetRole::kWindow:
                render::SurfaceDrawRectOutline(surf, w.x, w.y, w.w, w.h, 90, 70, 40);
                break;
            case BankWidgetRole::kAmountSlider:
                render::SurfaceDrawRectOutline(surf, w.x, w.y, w.w, w.h, 60, 60, 160);
                break;
            case BankWidgetRole::kLenderRow:
                FillRect(surf, w.x, w.y, w.w, w.h, 170, 150, 90);
                render::SurfaceDrawRectOutline(surf, w.x, w.y, w.w, w.h, 60, 50, 30);
                ++stats.rowsDrawn;
                break;
            case BankWidgetRole::kConfirmBtn:
                FillRect(surf, w.x, w.y, w.w, w.h, 40, 180, 40);   // green confirm
                render::SurfaceDrawRectOutline(surf, w.x, w.y, w.w, w.h, 30, 30, 30);
                ++stats.buttonsDrawn;
                break;
            case BankWidgetRole::kCancelBtn:
                FillRect(surf, w.x, w.y, w.w, w.h, 200, 50, 50);   // red cancel
                render::SurfaceDrawRectOutline(surf, w.x, w.y, w.w, w.h, 30, 30, 30);
                ++stats.buttonsDrawn;
                break;
        }
        ++stats.widgetsDrawn;
    }
    stats.nonClearPixels = BankSurfaceNonClearPixels(surf, kClearR, kClearG, kClearB);
}

render::Surface* RenderBankDialog(const BankDialog& dlg, int fbW, int fbH,
                                  BankRenderStats& stats) {
    stats = BankRenderStats{};
    render::Surface* surf = render::SurfaceCreate(fbW, fbH, 16, render::Format565());
    if (!surf) return nullptr;
    RenderBankDialogInto(dlg, surf, /*clear=*/true, stats);
    return surf;
}

// ---------------------------------------------------------------------------
// CLICK — hit-test the confirm button (with the radio-selected lender) -> a
// BankInteraction (kTake).
// ---------------------------------------------------------------------------
BankDialogClick ClickBankDialog(const BankDialog& dlg, int clickX, int clickY,
                                int selectedRow) {
    BankDialogClick c;
    for (const auto& w : dlg.widgets) {
        if (w.role != BankWidgetRole::kConfirmBtn) continue;
        if (clickX >= w.x && clickX < w.x + w.w &&
            clickY >= w.y && clickY < w.y + w.h) {
            c.hitConfirm = true;
            c.lenderIndex = selectedRow;
            c.interaction.side       = LoanSide::kTake;
            c.interaction.borrowerId = dlg.borrowerId;
            c.interaction.amount     = dlg.amount;
            c.interaction.player     = dlg.player;
            c.interaction.lenderId   = -1;  // default bank sink
            if (selectedRow >= 0 && selectedRow < static_cast<int>(dlg.lenders.size()))
                c.interaction.lenderId = dlg.lenders[selectedRow].lenderId;
            return c;
        }
    }
    return c;  // {side=kNone}
}

// ---------------------------------------------------------------------------
// RunBankDialog — the full load+render+click->command flow.
// ---------------------------------------------------------------------------
BankDialogResult RunBankDialog(const u8* formBytes, std::size_t len,
                               const std::vector<LenderChoice>& lenders,
                               i32 borrowerId, i32 amount, u8 player, int selectedRow,
                               std::uint32_t econSeed, sim::CommandQueue& q,
                               int fbW, int fbH, const char* formName) {
    BankDialogResult r;

    // 1. parse the real form + lay out the loan panel.
    r.dialog = BuildBankDialog(formBytes, len, lenders, borrowerId, amount, player,
                               formName);

    // 2. render to a headless surface.
    render::Surface* surf = RenderBankDialog(r.dialog, fbW, fbH, r.render);

    // 3. click the confirm button (its rect centre) with the selected lender row.
    for (const auto& w : r.dialog.widgets) {
        if (w.role == BankWidgetRole::kConfirmBtn) {
            r.click = ClickBankDialog(r.dialog, w.x + w.w / 2, w.y + w.h / 2,
                                      selectedRow);
            break;
        }
    }

    // 4. route the click's interaction through the REAL bank slice.
    if (r.click.hitConfirm && r.click.interaction.side != LoanSide::kNone) {
        r.slice = RunBankSlice(r.click.interaction, econSeed, q);
        r.commandIssued = r.slice.command.issued;
    }

    if (surf) render::SurfaceDestroy(surf);
    return r;
}

} // namespace guild::play
