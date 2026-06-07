#pragma once
// guild::app — REAL-asset GUI-forms driver (INTEGRATION glue, not a translation).
//
// Wires the already-reconstructed pieces into one end-to-end flow over the REAL
// shipped bytes:
//
//   MountRealGameAssets(fs, gameDir)              [app/real_boot]   ->
//   the mounted Resources/forms.BIN member index  [io/ArchiveMount] ->
//   for every `.form` member: OpenMember -> bytes [io/ArchiveMount] ->
//   gui::Form_ParseResourceFile(bytes)            [gui/form_parse]  ->
//   the live retained-mode Form/Window/Widget tables                .
//
// The `.form` members in Resources/forms.BIN use the retained-mode "FRM2" layout
// (321/323 are FRM2; the gilde.gfx gfx-object catalogue parser Form_LoadFromBuffer
// REJECTS them — see tests/e2e/real_assets_forms_e2e_test.cpp). This driver runs
// the CORRECT parser (Form_ParseResourceFile @0x41beb8), which BUILDS the windows
// and widgets, so we can assert structural results: forms parsed, total windows /
// widgets created, child-window links, label/input/slider/sprite markup, and the
// per-form window-id table.
//
// The retained-mode tables are small (48 form slots, 96 windows, 511 widgets), so
// to drive ALL ~323 shipped forms we ResetGuiState() before each parse (exactly as
// the gfx-catalogue e2e does) and tally the per-form structure into the result.
//
// The only OS boundary is shim::IFileSystem. The renderer/text-cluster leaves the
// FRM2 parser reaches (Form_PropertyValidate / Form_FindTextArrayIndex) are weak
// edges; this .cpp installs INERT, counting hooks for them so the data-model build
// is exercised without pulling in the renderer.
#include "guild/common/types.h"
#include "shim/IFileSystem.h"

#include <cstddef>
#include <string>
#include <vector>

namespace guild::app {

// Per-form structural summary (what Form_ParseResourceFile built for one member).
struct DrivenForm {
    std::string name;            // member path, e.g. "Bauen/Geb_Bauen.form"
    bool        parsed = false;  // FormFile.ok
    bool        frm2   = false;  // FRM2 layout vs the 2 old-layout members
    int         formId = -1;     // allocated form slot
    int         windowCount = 0; // windows the parser produced
    int         rootWindows = 0; // windows with parentIndex == 0
    int         childWindows = 0;// windows linked under a parent (parentIndex != 0)
    int         widgetCount = 0;  // total objects that produced a widget (idx >= 0)
    int         labelCount = 0;   // type 'C' labels built
    int         inputCount = 0;   // type 'A' input fields built
    int         sliderCount = 0;  // type 'E' sliders built
    int         spriteCount = 0;  // type 5 / 64 sprite/window-backing objects built
    int         objectRecords = 0;// total object records seen (built or inert)
    std::size_t bytes = 0;        // inflated member size
};

// Aggregate result of driving the whole forms.BIN over the FRM2 parser.
struct RealFormsResult {
    bool assetsPresent = false;  // the game dir / forms.BIN was found
    bool mounted       = false;  // forms.BIN mounted through the VFS
    std::size_t archiveMembers = 0; // total members indexed in forms.BIN

    int formMembers = 0;   // `.form` members seen
    int parsedForms = 0;   // members Form_ParseResourceFile accepted (.ok)
    int frm2Forms   = 0;   // of those, FRM2 layout
    int oldForms    = 0;   // of those, old layout
    int failedForms = 0;   // members that failed to extract or parse

    // Cross-form structural totals (sum over every parsed form).
    long totalWindows  = 0;
    long totalChildWindows = 0;
    long totalWidgets  = 0;
    long totalLabels   = 0;
    long totalInputs   = 0;
    long totalSliders  = 0;
    long totalSprites  = 0;
    long totalObjectRecords = 0;

    // Edge-hook fire counts (inert; prove the parser reached the renderer/text edges).
    long propertyValidateCalls = 0;  // Form_PropertyValidate hits
    long findTextIndexCalls     = 0;  // Form_FindTextArrayIndex hits

    int  maxWindowsInOneForm = 0;    // richest single form (window count)
    int  maxWidgetsInOneForm = 0;    // richest single form (widget count)
    std::string richestFormName;     // name of the max-widgets form

    std::vector<DrivenForm> forms;   // per-form detail (in archive order)
};

// Drive a single already-extracted `.form` byte buffer through the FRM2 parser and
// summarize what it built. Resets the GUI tables first so each member starts clean.
// `name` is recorded into the summary (and the form record). Used by the unit and
// integration tests directly; the e2e path calls it per member.
DrivenForm DriveFormBuffer(const guild::u8* data, std::size_t len, const char* name);

// Full real-asset run: mount `<gameDir>/Resources/forms.BIN` through the VFS bound
// to `fs`, then parse EVERY `.form` member through Form_ParseResourceFile, building
// the widget trees and tallying structure. `fs` MUST be rooted at the real game
// dir. If forms.BIN is absent, returns with assetsPresent=false (caller skips).
//
// Binds the process-global VFS (io::VfsInit) for the run and tears it down
// (io::VfsShutdown) before returning, so it is self-contained.
RealFormsResult DriveRealForms(guild::shim::IFileSystem* fs, const std::string& gameDir);

// Test seam: install the inert counting edge-hooks this module uses for
// Form_PropertyValidate / Form_FindTextArrayIndex and zero their counters. Called
// automatically by DriveRealForms / DriveFormBuffer; exposed so a test can read the
// counters back (RealFormsHookCounts).
void InstallInertFormHooks();

struct FormHookCounts { long propertyValidate = 0; long findTextIndex = 0; };
FormHookCounts RealFormsHookCounts();

} // namespace guild::app
