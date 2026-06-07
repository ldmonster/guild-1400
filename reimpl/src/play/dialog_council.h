#pragma once
// Wave 30 PLAY B2 — REAL DIALOG RENDER for the COUNCIL / OFFICE (politics) slice.
//
// The P5 council slice (Wave 27, src/play/slice_council.{h,cpp}) already emits the
// REAL opcode-68 candidacy command (VIBE_Command_RequestBuildOp68 @0x495454) and
// applies the holder-rank effect (VIBE_Office_AssignToCandidate @0x47e4e0). But its
// "dialog" was abstract: it never built or rendered the actual in-game
// apply-for-office window. This module closes that gap exactly the way Wave 29's
// dialog_market did for the trade window: it LOADS the REAL shipped office form,
// LAYS OUT the REAL apply-panel widgets (one button per eligible office), RENDERS
// them to a headless software surface (widgets visible as non-clear pixels at their
// real rects), and ROUTES a confirm-button press back into slice_council's real
// opcode-68 command emit.
//
// ===========================================================================
// GROUNDING (the real subsystem this drives — decompiled this wave)
// ===========================================================================
//
//   * THE FORM SHELL: Resources/forms.BIN member
//     `Locations/Rathaus/amtsbewerbung.form` ("amtsbewerbung" = apply for office).
//     A FRM2 retained-mode form, 2 windows: win0 root container (x=8 y=56 w=352
//     h=448, 5 shell objects: _ORNAMENTS_BLACK sprites + a type-64 nested-window
//     backing), win1 the inner list window (x=40 y=88 w=288 h=384, parent=1). Parsed
//     BYTE-FOR-BYTE by the real parser gui::Form_ParseResourceFile
//     (VIBE_Form_LoadFromResource @0x41beb8). The shipped .form stores only the
//     container shells; the per-office BUTTONS are built PROGRAMMATICALLY at runtime
//     by the office-apply dialog builder.
//
//   * THE APPLY-PANEL WIDGETS + BUTTON ROUTING:
//     VIBE_Office_ShowApplicationDialog @0x520114 — the live apply-for-office loop:
//       form = Form_LoadFromResource(0,0,"locations\\rathaus\\amtsbewerbung");
//       Form_CenterChildWindows(form); ...
//       n = Office_BuildPromotionList(person, 6, buf);  // category 6 = elective
//       Form_SelectWindow(form, 1); Window_RemoveChildren(...);
//       Text_RenderRichString(0x1610);                  // the title text
//       for (k=0..n) {                                   // ONE button per office
//           btn = Text_RenderRichString(0x1613, office[k]+525);
//           buttonId[k] = Form_GetChildObjectId(form, .., btn);
//       }
//       while (RunFrameLoop(...)) {                       // on click:
//         if (dword_75BF38 != -1) {                       // a click event
//           find k where buttonId[k] == dword_62D22C;     // the clicked object id
//           Office_ShowCandidacyDialog(person, office[k], .., form);  // -> apply
//         }
//       }
//     Office_ShowCandidacyDialog @0x51fc50 -> Office_ConfirmCandidacyDialog @0x47fcf0
//     -> Office_ApplyForCandidacy @0x47e1b8 -> RequestBuildOp68 (opcode 68) — the
//     SAME command slice_council::BuildCouncilPacket builds. So a click on office
//     button k selects officeType office[k] and applies for that candidacy.
//
// ===========================================================================
// WIRED REAL siblings vs INERT gaps (declared honestly)
// ===========================================================================
//   WIRED: gui::Form_ParseResourceFile (the real .form parse + table build),
//          render::SurfaceCreate / SurfaceColorFill / SurfaceDrawRectOutline /
//          SurfaceDrawHLine / SurfaceGetPixelRgb (the real 2D software-surface ops),
//          and the WHOLE slice_council command->apply pipeline (BuildCouncilPacket /
//          ApplyCouncilPacket over the real opcode-68 / OfficeAssignToCandidate).
//   INERT (true gap, hooked + SAID SO): the live GUI frame-loop click dispatch
//          (VIBE_GameLogic_RunFrameLoop @0x4c09a0 -> dword_75BF38/dword_62D22C click
//          event -> the Office_ShowCandidacyDialog call) that, in the running game,
//          turns a mouse-up on an office button into the opcode-68 emit is NOT
//          reconstructed as a single function. This module performs the FAITHFUL
//          hit-test against the real button rects + selects the office that button
//          builds for (which is exactly what the dispatch decides) and routes to the
//          real command builder; the deep frame-loop walk itself is the inert piece.
//          The per-office button GEOMETRY (the original lets Form_GetChildObjectId /
//          the rich-string layout place each button inside win1) is laid out here as
//          a faithful stacked column inside the real win1 rect — the on-disk form
//          carries NO button coordinates (they are runtime), so this is a documented
//          render-layout choice, not a recovered offset.
//
// Additive: no edits to slice_council.cpp / any gui or render .cpp. All inert
// defaults defined in dialog_council.cpp.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "guild/common/types.h"
#include "gui/form_parse.h"
#include "play/slice_council.h"
#include "render/surface.h"

namespace guild::play {

// ===========================================================================
// A laid-out widget rect (one drawable control of the council dialog).
// ===========================================================================
enum class CouncilWidgetRole : std::uint8_t {
    kWindow      = 0,  // a form window/container rect (FRM2 window record)
    kTitle       = 1,  // the title text line (Text_RenderRichString 0x1610)
    kOfficeButton= 2,  // one eligible-office button (Text_RenderRichString 0x1613)
};

struct CouncilWidgetRect {
    CouncilWidgetRole role = CouncilWidgetRole::kWindow;
    int x = 0, y = 0, w = 0, h = 0;   // absolute screen rect
    int officeIndex = -1;             // which office row (>=0) this button is for
    u8  officeType  = 0;              // the office type this button applies for
    int widgetId    = -1;            // window slot / synthetic button id
};

// ===========================================================================
// One eligible office the apply panel lists — what Office_BuildPromotionList(person,
// 6, buf) returns: a category-6 (elective) office the applicant can stand for.
// ===========================================================================
struct OfficeChoice {
    u8 officeType = 0;   // the office type id (op68 officeType field)
    u8 holderKey  = 0;   // the targeted holder-slot character-id key (op68 holderA)
};

// ===========================================================================
// The parsed + laid-out dialog: the real form structure + the laid-out widgets.
// ===========================================================================
struct CouncilDialog {
    bool formParsed = false;
    bool frm2       = false;
    int  formId     = -1;
    int  windowCount = 0;
    int  rootWindow  = -1;     // win0 root container slot
    int  listWindow  = -1;     // win1 inner list window slot (where buttons go)
    int  panelX = 0, panelY = 0, panelW = 0, panelH = 0;   // root window geometry
    int  listX = 0, listY = 0, listW = 0, listH = 0;       // list window geometry

    i32  applicantId = 0;      // the clicking character (op68 applicant)

    std::vector<OfficeChoice>     offices;   // the eligible offices listed
    std::vector<CouncilWidgetRect> widgets;  // every laid-out widget rect

    int buttonCount() const;
};

// ===========================================================================
// Step 1 — LOAD + LAY OUT the dialog over a parsed form buffer.
//
// Parses `formBytes` with the REAL parser (Form_ParseResourceFile), finds the root
// + inner list windows, and lays out one office button per `offices` entry stacked
// down the real win1 list rect (the apply panel column). Returns the laid-out dialog.
// `applicantId` is the clicking character packed into the op68 command on a click.
CouncilDialog BuildCouncilDialog(const u8* formBytes, std::size_t len,
                                 const std::vector<OfficeChoice>& offices,
                                 i32 applicantId,
                                 const char* formName = "Locations/Rathaus/amtsbewerbung.form");

// Convenience: build with a SYNTHETIC minimal FRM2 form (root + child list window)
// so unit tests exercise the layout without forms.BIN. Same layout math.
CouncilDialog BuildSyntheticCouncilDialog(const std::vector<OfficeChoice>& offices,
                                          i32 applicantId,
                                          int panelX, int panelY, int panelW, int panelH);

// Produce a minimal valid FRM2 `.form` byte buffer: win0 root at the given geometry
// + win1 child list window inset inside it (so the REAL parser builds real records).
std::vector<u8> MakeSyntheticCouncilForm(int x, int y, int w, int h);

// ===========================================================================
// Step 2 — RENDER the dialog to a headless software surface.
// ===========================================================================
struct CouncilRenderStats {
    int nonClearPixels = 0;
    int widgetsDrawn   = 0;
    int buttonsDrawn   = 0;
};

render::Surface* RenderCouncilDialog(const CouncilDialog& dlg, int fbW, int fbH,
                                     CouncilRenderStats& stats);
void RenderCouncilDialogInto(const CouncilDialog& dlg, render::Surface* surf,
                             bool clear, CouncilRenderStats& stats);
int  CouncilSurfaceNonClearPixels(const render::Surface* surf, u8 r, u8 g, u8 b);

// ===========================================================================
// Step 3 — ROUTE a click on an office button -> a CouncilInteraction.
//
// Hit-tests the dialog's office-button rects against (clickX,clickY); the FIRST
// button hit yields a CouncilInteraction (kApplyCandidacy) for that office's type +
// holder key + the dialog's applicant. A click on no button yields {action=kNone}.
// ===========================================================================
struct CouncilDialogClick {
    bool               hitButton = false;
    int                officeIndex = -1;
    CouncilInteraction interaction{};   // {action=kNone} when no button hit
};
CouncilDialogClick ClickCouncilDialog(const CouncilDialog& dlg, int clickX, int clickY);

// ===========================================================================
// Step 4 — the END-TO-END dialog interaction: load+render+click->command.
//
// Parse the real form, lay out the offices, render to a surface, click the requested
// office button, then route the resulting interaction through the REAL council
// command path (BuildCouncilPacket -> ApplyCouncilPacket). Returns the dialog render
// result + the built/applied command outcome.
//
// The caller has prepared the live politics tables (seated holder slots) so the apply
// can seat the candidacy (see slice_council::RunCouncilStepsSynthetic / RunCouncilSlice
// for the seeding rig). `clickOffice` selects which laid-out office button to click.
struct CouncilDialogResult {
    CouncilDialog       dialog{};
    CouncilRenderStats  render{};
    CouncilDialogClick  click{};

    CouncilPacket       command{};      // BuildCouncilPacket output (opcode 68)
    bool                commandBuilt   = false;
    int                 commandOpcode  = 0;
    u8                  commandOffice  = 0;
    int                 applyResult    = 0;   // ApplyCouncilPacket (1 == seated)
    bool                commandApplied = false;
};

CouncilDialogResult RunCouncilDialog(const u8* formBytes, std::size_t len,
                                     const std::vector<OfficeChoice>& offices,
                                     i32 applicantId, int clickOffice,
                                     const char* formName = "Locations/Rathaus/amtsbewerbung.form");

} // namespace guild::play
