// Wave 30 PLAY B2 — REAL DIALOG RENDER for the COUNCIL / OFFICE slice.
// See dialog_council.h for the full grounding. Summary of the REAL siblings reused
// here (called, never redefined — ODR):
//   gui::Form_ParseResourceFile          (form_parse.cpp, VIBE_Form_LoadFromResource
//                                         @0x41beb8) — the real .form parse+build
//   gui::ResetGuiState                   (form.cpp) — clean the retained-mode tables
//   render::SurfaceCreate / ColorFill /  (surface.cpp, VIBE_Surface_* gfx ops)
//        DrawRectOutline / DrawHLine / GetPixelRgb / Destroy
//   play::BuildCouncilPacket /           (slice_council.cpp) — the real opcode-68
//        ApplyCouncilPacket                candidacy command + apply
#include "play/dialog_council.h"

#include <cstring>

#include "gui/form.h"           // g_forms
#include "gui/object.h"         // ResetGuiState
#include "gui/window.h"         // g_windows, Window record
#include "render/colorformat.h"

namespace guild::play {

namespace {

// The council apply panel draws onto a parchment-style surface (the Rathaus skin).
constexpr u8 kClearR = 196, kClearG = 178, kClearB = 128;

// Faithful render cells for the runtime-built office buttons. The on-disk form
// carries NO button coordinates (the buttons are built by the apply dialog at
// runtime via Text_RenderRichString / Form_GetChildObjectId), so these are a
// documented render-layout choice inside the real win1 list rect, NOT a recovered
// offset (see header INERT note).
constexpr int kBtnInsetX = 12;   // left inset inside the list window
constexpr int kBtnTopY   = 28;   // below the title line
constexpr int kBtnH      = 22;   // one office button height
constexpr int kBtnGap    = 6;    // vertical gap between buttons
constexpr int kTitleH    = 16;   // the title text line height
constexpr int kTitleInset= 8;

void PushRect(CouncilDialog& d, CouncilWidgetRole role, int x, int y, int w, int h,
              int officeIndex, u8 officeType, int widgetId) {
    CouncilWidgetRect wr;
    wr.role = role; wr.x = x; wr.y = y; wr.w = w; wr.h = h;
    wr.officeIndex = officeIndex; wr.officeType = officeType; wr.widgetId = widgetId;
    d.widgets.push_back(wr);
}

void put16(std::vector<u8>& b, int off, u16 v) {
    b[off] = u8(v & 0xFF); b[off + 1] = u8(v >> 8);
}
void put32(std::vector<u8>& b, int off, u32 v) {
    b[off] = u8(v & 0xFF); b[off + 1] = u8((v >> 8) & 0xFF);
    b[off + 2] = u8((v >> 16) & 0xFF); b[off + 3] = u8((v >> 24) & 0xFF);
}

} // namespace

int CouncilDialog::buttonCount() const {
    int n = 0;
    for (const auto& w : widgets)
        if (w.role == CouncilWidgetRole::kOfficeButton) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// MakeSyntheticCouncilForm — a minimal valid FRM2 `.form` the real parser accepts:
// win0 root at (x,y,w,h) + win1 child list window inset inside it (parentIndex=1,
// 1-based -> win0). Mirrors the real amtsbewerbung.form structure (root + inner
// list window). Window-record offsets from form_parse.h.
// ---------------------------------------------------------------------------
std::vector<u8> MakeSyntheticCouncilForm(int x, int y, int w, int h) {
    const int stride = gui::kFrm2RecordStride;  // 4124
    const int header = gui::kFrm2HeaderBytes;   // 8
    std::vector<u8> buf(static_cast<std::size_t>(header) + 2 * stride, 0);
    buf[0] = 'F'; buf[1] = 'R'; buf[2] = 'M'; buf[3] = '2';
    put32(buf, 4, 2);                       // windowCount = 2

    // win0 — root container.
    int rec = header;
    put16(buf, rec + 0, static_cast<u16>(x));
    put16(buf, rec + 2, static_cast<u16>(y));
    put16(buf, rec + 4, static_cast<u16>(h));
    put16(buf, rec + 6, static_cast<u16>(w));
    put32(buf, rec + 8, 0x10);
    put16(buf, rec + 204, 0);
    put32(buf, rec + 3792, 0);              // parentIndex = 0 (root)

    // win1 — inner list window (inset inside win0), parent = win0 (1-based slot 1).
    rec = header + stride;
    int lx = x + 32, ly = y + 32, lw = (w > 80 ? w - 64 : w), lh = (h > 80 ? h - 64 : h);
    put16(buf, rec + 0, static_cast<u16>(lx));
    put16(buf, rec + 2, static_cast<u16>(ly));
    put16(buf, rec + 4, static_cast<u16>(lh));
    put16(buf, rec + 6, static_cast<u16>(lw));
    put32(buf, rec + 8, 0x10);
    put16(buf, rec + 204, 0);
    put32(buf, rec + 3792, 1);              // parentIndex = 1 -> win0
    return buf;
}

// ---------------------------------------------------------------------------
// Lay out the office buttons stacked down the real list window. Mirrors the apply
// dialog's loop (one button per Office_BuildPromotionList entry).
// ---------------------------------------------------------------------------
namespace {

void LayoutOffices(CouncilDialog& d, const std::vector<OfficeChoice>& offices) {
    const int lx = (d.listWindow >= 0 || d.listW > 0) ? d.listX : d.panelX;
    const int ly = (d.listWindow >= 0 || d.listW > 0) ? d.listY : d.panelY;
    const int lw = (d.listW > 0) ? d.listW : d.panelW;

    // The title text line (Text_RenderRichString 0x1610).
    PushRect(d, CouncilWidgetRole::kTitle,
             lx + kTitleInset, ly + kTitleInset,
             (lw > 2 * kTitleInset) ? lw - 2 * kTitleInset : lw, kTitleH, -1, 0, -1);

    int btnW = (lw > 2 * kBtnInsetX) ? lw - 2 * kBtnInsetX : lw;
    for (std::size_t i = 0; i < offices.size(); ++i) {
        int by = ly + kBtnTopY + static_cast<int>(i) * (kBtnH + kBtnGap);
        int bx = lx + kBtnInsetX;
        d.offices.push_back(offices[i]);
        int oi = static_cast<int>(d.offices.size()) - 1;
        // Synthetic widget id mirrors Form_GetChildObjectId's per-button slot id (the
        // dispatch matches the clicked object id to this; here it indexes the row).
        PushRect(d, CouncilWidgetRole::kOfficeButton, bx, by, btnW, kBtnH,
                 oi, offices[i].officeType, 1000 + oi);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// BuildCouncilDialog — parse the real form + lay out the office buttons.
// ---------------------------------------------------------------------------
CouncilDialog BuildCouncilDialog(const u8* formBytes, std::size_t len,
                                 const std::vector<OfficeChoice>& offices,
                                 i32 applicantId, const char* formName) {
    CouncilDialog d;
    d.applicantId = applicantId;

    gui::ResetGuiState();
    gui::FormFile ff = gui::Form_ParseResourceFile(formBytes, len, formName);
    d.formParsed  = ff.ok;
    d.frm2        = ff.frm2;
    d.formId      = ff.formId;
    d.windowCount = static_cast<int>(ff.windows.size());

    // Root window (parentIndex == 0) = the panel container.
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
    // Inner list window (parentIndex == 1 -> win0) = where Form_SelectWindow(form,1)
    // builds the buttons.
    for (const auto& w : ff.windows) {
        if (w.parentIndex == 1 && w.windowSlot >= 0) {
            d.listWindow = w.windowSlot;
            d.listX = w.x; d.listY = w.y; d.listW = w.w; d.listH = w.h;
            break;
        }
    }
    if (d.listWindow < 0) {  // single-window form -> draw buttons in the root
        d.listX = d.panelX; d.listY = d.panelY; d.listW = d.panelW; d.listH = d.panelH;
    }

    // Push a window rect per parsed form window (the container shells the form ships).
    for (const auto& w : ff.windows) {
        if (w.windowSlot < 0) continue;
        PushRect(d, CouncilWidgetRole::kWindow, w.x, w.y, w.w, w.h, -1, 0, w.windowSlot);
    }

    LayoutOffices(d, offices);
    return d;
}

CouncilDialog BuildSyntheticCouncilDialog(const std::vector<OfficeChoice>& offices,
                                          i32 applicantId,
                                          int panelX, int panelY, int panelW, int panelH) {
    std::vector<u8> form = MakeSyntheticCouncilForm(panelX, panelY, panelW, panelH);
    return BuildCouncilDialog(form.data(), form.size(), offices, applicantId,
                              "synthetic_council.form");
}

// ---------------------------------------------------------------------------
// RENDER — paint the widget rects through the REAL render::Surface 2D ops.
// ---------------------------------------------------------------------------
int CouncilSurfaceNonClearPixels(const render::Surface* surf, u8 cr, u8 cg, u8 cb) {
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

void RenderCouncilDialogInto(const CouncilDialog& dlg, render::Surface* surf,
                             bool clear, CouncilRenderStats& stats) {
    stats = CouncilRenderStats{};
    if (!surf || !surf->pixels) return;
    if (clear) render::SurfaceColorFill(surf, kClearR, kClearG, kClearB);

    for (const auto& w : dlg.widgets) {
        if (w.w <= 0 || w.h <= 0) continue;
        switch (w.role) {
            case CouncilWidgetRole::kWindow:
                render::SurfaceDrawRectOutline(surf, w.x, w.y, w.w, w.h, 90, 70, 40);
                break;
            case CouncilWidgetRole::kTitle:
                render::SurfaceDrawRectOutline(surf, w.x, w.y, w.w, w.h, 40, 30, 20);
                break;
            case CouncilWidgetRole::kOfficeButton:
                FillRect(surf, w.x, w.y, w.w, w.h, 70, 110, 170);   // a parchment button
                render::SurfaceDrawRectOutline(surf, w.x, w.y, w.w, w.h, 30, 30, 30);
                ++stats.buttonsDrawn;
                break;
        }
        ++stats.widgetsDrawn;
    }
    stats.nonClearPixels = CouncilSurfaceNonClearPixels(surf, kClearR, kClearG, kClearB);
}

render::Surface* RenderCouncilDialog(const CouncilDialog& dlg, int fbW, int fbH,
                                     CouncilRenderStats& stats) {
    stats = CouncilRenderStats{};
    render::Surface* surf = render::SurfaceCreate(fbW, fbH, 16, render::Format565());
    if (!surf) return nullptr;
    RenderCouncilDialogInto(dlg, surf, /*clear=*/true, stats);
    return surf;
}

// ---------------------------------------------------------------------------
// CLICK — hit-test an office button rect -> CouncilInteraction (kApplyCandidacy).
// ---------------------------------------------------------------------------
CouncilDialogClick ClickCouncilDialog(const CouncilDialog& dlg, int clickX, int clickY) {
    CouncilDialogClick c;
    c.interaction.action = CouncilAction::kNone;   // no hit -> not a candidacy
    for (const auto& w : dlg.widgets) {
        if (w.role != CouncilWidgetRole::kOfficeButton) continue;
        if (clickX >= w.x && clickX < w.x + w.w &&
            clickY >= w.y && clickY < w.y + w.h) {
            c.hitButton  = true;
            c.officeIndex = w.officeIndex;
            c.interaction.action      = CouncilAction::kApplyCandidacy;
            c.interaction.applicantId = dlg.applicantId;
            c.interaction.officeType  = w.officeType;
            // The holder key the panel targets for this office row.
            if (w.officeIndex >= 0 &&
                w.officeIndex < static_cast<int>(dlg.offices.size()))
                c.interaction.holderKey = dlg.offices[w.officeIndex].holderKey;
            return c;  // first button hit wins (front-to-back z-order)
        }
    }
    return c;  // {action=kNone}
}

// ---------------------------------------------------------------------------
// RunCouncilDialog — the full load+render+click->command flow.
// ---------------------------------------------------------------------------
CouncilDialogResult RunCouncilDialog(const u8* formBytes, std::size_t len,
                                     const std::vector<OfficeChoice>& offices,
                                     i32 applicantId, int clickOffice,
                                     const char* formName) {
    CouncilDialogResult r;

    // 1. parse the real form + lay out the office buttons.
    r.dialog = BuildCouncilDialog(formBytes, len, offices, applicantId, formName);

    // 2. render to a headless surface (size from the real panel geometry).
    int fbW = r.dialog.panelX + r.dialog.panelW + 16;
    int fbH = r.dialog.panelY + r.dialog.panelH + 16;
    if (fbW < 64) fbW = 64;
    if (fbH < 64) fbH = 64;
    render::Surface* surf = RenderCouncilDialog(r.dialog, fbW, fbH, r.render);

    // 3. click the requested office button (its rect centre).
    for (const auto& w : r.dialog.widgets) {
        if (w.role == CouncilWidgetRole::kOfficeButton && w.officeIndex == clickOffice) {
            r.click = ClickCouncilDialog(r.dialog, w.x + w.w / 2, w.y + w.h / 2);
            break;
        }
    }

    // 4. route the click's interaction through the REAL council command path.
    if (r.click.hitButton && r.click.interaction.action != CouncilAction::kNone) {
        r.command        = BuildCouncilPacket(r.click.interaction);
        r.commandBuilt   = (r.command.opcode == 68);
        r.commandOpcode  = r.command.opcode;
        r.commandOffice  = r.command.officeType;
        r.applyResult    = ApplyCouncilPacket(r.command);
        r.commandApplied = (r.applyResult == 1);
    }

    if (surf) render::SurfaceDestroy(surf);
    return r;
}

} // namespace guild::play
