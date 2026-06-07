#pragma once
// Wave 30 PLAY B2 — REAL DIALOG RENDER for the BANK / LOAN (Kredit) slice.
//
// The P5 bank slice (Wave 28, src/play/slice_bank.{h,cpp}) already emits the REAL
// opcode-15 loan command (VIBE_Command_EnqueueCmd15 @0x494604) over the real
// sim::CommandQueue codec and applies the borrower cash+debt effect. But its
// "dialog" was abstract: it never built or rendered the actual in-game take-loan
// window. This module closes that gap exactly the way Wave 29's dialog_market did
// for the trade window: it LOADS the REAL shipped take-loan form, LAYS OUT the REAL
// loan-panel widgets (an amount slider + one lender row per lender + a confirm
// button), RENDERS them to a headless software surface (widgets visible as non-clear
// pixels at their real rects), and ROUTES the confirm button press back into
// slice_bank's real opcode-15 loan command.
//
// ===========================================================================
// GROUNDING (the real subsystem this drives — decompiled this wave)
// ===========================================================================
//
//   * THE FORM SHELL: Resources/forms.BIN member
//     `Locations/Geldleihe/Geldleihe_Kreditaufnehmen.form` ("Kreditaufnehmen" = take
//     a loan). A FRM2 retained-mode form, 5 windows: win0 root container (x=63 y=72
//     w=477 h=545, 10 shell objects: _RAHMEN_GELDLEIHE sprites + type-64 nested-
//     window backings _RAHMEN_THEMEN), win1 (x=168 y=92 w=42 h=337, flags 0x110 =
//     slider track), win2 (x=116 y=181 w=319 h=441 — the lender-row list window),
//     win3 (x=121 y=507 w=34 h=435 — the button row), win4 (x=116 y=136 w=41 h=440,
//     flags 0x110 — slider track). Parsed BYTE-FOR-BYTE by the real parser
//     gui::Form_ParseResourceFile (VIBE_Form_LoadFromResource @0x41beb8). The shipped
//     .form stores only the container/track shells; the lender rows + buttons are
//     built PROGRAMMATICALLY at runtime by the take-loan dialog builder.
//
//   * THE LOAN-PANEL WIDGETS + BUTTON ROUTING:
//     VIBE_Credit_ShowTakeLoanDialog @0x51a244 — the live take-loan loop:
//       form = Form_LoadFromResource(0,0,"locations\\geldleihe\\geldleihe_kreditaufnehmen");
//       Form_CenterChildWindows(form); Form_SelectWindow(form,0/1);
//       n = Building_CollectByCityHandle(city, borrower, buf);   // the lenders
//       for (k=0..n) {                                            // one lender ROW:
//           Form_SelectWindow(form, 2);
//           Object_AddToWindow(...); win = Window_AddChildWindow(..,16,..);
//           word_67EDFC[476*win] = 69;                            // widget type 69 = slider
//           Text_RenderRichString(0x14E5, lenderName, amount, ..);
//       }
//       RadioGroup_Create(n, rows[0]);                            // pick a lender row
//       Form_SelectWindow(form, 3);                               // the button window
//       Hud_BuildButtonRow(.., buf, 2, ..); cancel=buf[0]; confirm=buf[1];
//       while (RunFrameLoop(...)) {                               // on click:
//         if (dword_75BF38 == 1210) {                             // a click event
//           if (confirm == dword_62D22C)                          // confirm clicked:
//               Credit_ConfirmLoanRequest(borrower, rows[selectedRadio]);
//           else if (cancel == dword_62D22C)
//               Credit_ShowNewLoanDialog(borrower);
//         }
//       }
//     Credit_ConfirmLoanRequest @0x51a100 -> Command_EnqueueCmd15 (opcode 15) — the
//     SAME command slice_bank::ClassifyBankInteraction classifies. So a click on the
//     confirm button takes a loan from the selected lender row for the slider amount.
//
// ===========================================================================
// WIRED REAL siblings vs INERT gaps (declared honestly)
// ===========================================================================
//   WIRED: gui::Form_ParseResourceFile (the real .form parse + table build),
//          render::SurfaceCreate / SurfaceColorFill / SurfaceDrawRectOutline /
//          SurfaceDrawHLine / SurfaceGetPixelRgb (the real 2D software-surface ops),
//          and the WHOLE slice_bank command pipeline (ClassifyBankInteraction +
//          InstallBankCommandHandler + RunBankSlice over the real opcode-15 codec).
//   INERT (true gap, hooked + SAID SO): the live GUI frame-loop click dispatch
//          (VIBE_GameLogic_RunFrameLoop @0x4c09a0 -> dword_75BF38==1210 click event ->
//          confirm==dword_62D22C -> the Credit_ConfirmLoanRequest call) that, in the
//          running game, turns a mouse-up on the confirm button into the opcode-15
//          emit is NOT reconstructed as a single function. This module performs the
//          FAITHFUL hit-test against the real confirm-button rect + reads the selected
//          lender row + slider amount (which is what the dispatch + radio group
//          decide) and routes to the real command path; the deep frame-loop walk
//          itself is the inert piece. The lender-row + button GEOMETRY (the original
//          places each row via Window_AddChildWindow and the buttons via
//          Hud_BuildButtonRow at runtime) is laid out here as a faithful column inside
//          the real win2/win3 rects — the on-disk form carries NO row/button
//          coordinates (they are runtime), so this is a documented render-layout
//          choice, not a recovered offset.
//
// Additive: no edits to slice_bank.cpp / any gui or render .cpp. All inert defaults
// defined in dialog_bank.cpp.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "guild/common/types.h"
#include "gui/form_parse.h"
#include "play/slice_bank.h"
#include "render/surface.h"
#include "sim/command.h"

namespace guild::play {

// ===========================================================================
// A laid-out widget rect (one drawable control of the take-loan dialog).
// ===========================================================================
enum class BankWidgetRole : std::uint8_t {
    kWindow      = 0,  // a form window/container rect (FRM2 window record)
    kAmountSlider= 1,  // the loan-amount slider track (win1/win4, flags 0x110)
    kLenderRow   = 2,  // one lender row (Window_AddChildWindow, widget type 69)
    kConfirmBtn  = 3,  // the confirm / take-loan button (Hud_BuildButtonRow buf[1])
    kCancelBtn   = 4,  // the cancel / new-loan button (buf[0])
};

struct BankWidgetRect {
    BankWidgetRole role = BankWidgetRole::kWindow;
    int x = 0, y = 0, w = 0, h = 0;   // absolute screen rect
    int lenderIndex = -1;             // which lender row (>=0)
    i32 lenderId    = -1;             // the lender id this row offers
    int widgetId    = -1;
};

// ===========================================================================
// One lender the take-loan panel lists — what Building_CollectByCityHandle returns:
// a lender (bank) the borrower may take a loan from.
// ===========================================================================
struct LenderChoice {
    i32 lenderId = -1;   // the lender/bank person id (op15 a1)
    i32 maxLoan  = 0;    // the offered loan ceiling (the slider max for this row)
};

// ===========================================================================
// The parsed + laid-out dialog: the real form structure + the laid-out widgets.
// ===========================================================================
struct BankDialog {
    bool formParsed = false;
    bool frm2       = false;
    int  formId     = -1;
    int  windowCount = 0;
    int  rootWindow  = -1;     // win0 root container slot
    int  rowWindow   = -1;     // win2 lender-row list window slot
    int  buttonWindow= -1;     // win3 button window slot
    int  sliderWindow= -1;     // win1 slider track window slot
    int  panelX = 0, panelY = 0, panelW = 0, panelH = 0;
    // child-window geometry (resolved from the real form records)
    int  rowX = 0, rowY = 0, rowW = 0;       // win2 lender-row list rect
    int  btnX = 0, btnY = 0, btnW = 0;       // win3 button window rect
    int  sliderX = 0, sliderY = 0;           // win1 slider track origin

    i32  borrowerId = 0;       // the borrowing person id (op15 a2)
    i32  amount     = 0;       // the slider loan amount (op15 a3)
    u8   player     = 0;

    std::vector<LenderChoice>    lenders;   // the lenders listed (one row each)
    std::vector<BankWidgetRect>  widgets;   // every laid-out widget rect

    int rowCount() const;
    int buttonCount() const;
};

// ===========================================================================
// Step 1 — LOAD + LAY OUT the dialog over a parsed form buffer.
//
// Parses `formBytes` with the REAL parser, finds the root/row/button/slider windows,
// lays out the amount slider, one lender row per `lenders` entry stacked down the
// real win2 rect, and confirm/cancel buttons in the real win3 rect. `borrowerId`,
// `amount`, `player` fill the loan command the confirm click emits.
BankDialog BuildBankDialog(const u8* formBytes, std::size_t len,
                           const std::vector<LenderChoice>& lenders,
                           i32 borrowerId, i32 amount, u8 player,
                           const char* formName = "Locations/Geldleihe/Geldleihe_Kreditaufnehmen.form");

// Convenience: build with a SYNTHETIC minimal FRM2 form (root + row/button/slider
// child windows) so unit tests exercise the layout without forms.BIN.
BankDialog BuildSyntheticBankDialog(const std::vector<LenderChoice>& lenders,
                                    i32 borrowerId, i32 amount, u8 player,
                                    int panelX, int panelY, int panelW, int panelH);

// Produce a minimal valid FRM2 `.form` byte buffer mirroring the real 5-window shape
// (root + slider + row-list + button + slider).
std::vector<u8> MakeSyntheticBankForm(int x, int y, int w, int h);

// ===========================================================================
// Step 2 — RENDER the dialog to a headless software surface.
// ===========================================================================
struct BankRenderStats {
    int nonClearPixels = 0;
    int widgetsDrawn   = 0;
    int buttonsDrawn   = 0;
    int rowsDrawn      = 0;
};

render::Surface* RenderBankDialog(const BankDialog& dlg, int fbW, int fbH,
                                  BankRenderStats& stats);
void RenderBankDialogInto(const BankDialog& dlg, render::Surface* surf,
                          bool clear, BankRenderStats& stats);
int  BankSurfaceNonClearPixels(const render::Surface* surf, u8 r, u8 g, u8 b);

// ===========================================================================
// Step 3 — ROUTE a click on the confirm button (with a selected lender row) ->
// a BankInteraction (kTake). A click on the cancel button or no button yields
// {side=kNone}. `selectedRow` is the radio-group-selected lender row.
// ===========================================================================
struct BankDialogClick {
    bool            hitConfirm = false;
    int             lenderIndex = -1;
    BankInteraction interaction{};   // {side=kNone} when not a confirm
};
BankDialogClick ClickBankDialog(const BankDialog& dlg, int clickX, int clickY,
                                int selectedRow);

// ===========================================================================
// Step 4 — the END-TO-END dialog interaction: load+render+click->command.
//
// Parse the real form, lay out the lenders, render to a surface, select lender row
// `selectedRow`, click the confirm button, then route the resulting interaction
// through the REAL slice_bank::RunBankSlice so the actual opcode-15 command applies
// (the borrower's cash + debt) + a game-day advances. Returns the render result + the
// bank slice result.
//
// The caller has populated the live sim arrays (synthetic seed or LoadWorld) with a
// live borrower Person whose id == borrowerId. `q` is a standalone CommandQueue.
struct BankDialogResult {
    BankDialog      dialog{};
    BankRenderStats render{};
    BankDialogClick click{};
    BankSliceResult slice{};
    bool            commandIssued = false;
};

BankDialogResult RunBankDialog(const u8* formBytes, std::size_t len,
                               const std::vector<LenderChoice>& lenders,
                               i32 borrowerId, i32 amount, u8 player, int selectedRow,
                               std::uint32_t econSeed, sim::CommandQueue& q,
                               int fbW, int fbH,
                               const char* formName = "Locations/Geldleihe/Geldleihe_Kreditaufnehmen.form");

} // namespace guild::play
