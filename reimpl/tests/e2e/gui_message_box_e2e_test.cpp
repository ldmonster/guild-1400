// Real-asset e2e for the modal message-box family
// (gilde.exe 0x4ad6f0/0x4acbd0/0x569a30/0x4ad9dc/0x4adc60 — VIBE_Dialog_ShowMessageBox*).
//
// Mounts the shipped `Resources/forms.BIN`, extracts the actual `Misc\Messagebox.form` /
// `Misc\MessageboxGreen.form` dialog screens, and parses each with the reconstructed FRM2
// parser into the LIVE retained-mode tables (g_windows / g_widgets).  The shipped message
// boxes are backdrops only — the OK/Cancel BUTTONS are added at runtime — so this test
// reproduces that runtime step with the genuine Object_AddToWindow leaf, then drives the
// real MessageBox_* modal loop over the real parsed window: building a real radio group
// from the real buttons and resolving OK/Cancel to (selection + 1) / 0.
//
// GUARDED: if the asset folder isn't present the test passes trivially.
#include "tests/framework/test.h"

#include "gui/message_box.h"
#include "gui/form_parse.h"
#include "gui/form.h"
#include "gui/window.h"
#include "gui/object.h"
#include "gui/radiogroup.h"
#include "gui/input.h"

#include "io/zip_archive.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::gui;

using FrameInput = MessageBoxHost::FrameInput;

// Renderer/property/text edges the parser leans on (same neutral stubs the form-real
// e2e uses) so the data-model parse of real members builds widgets without a gfx db.
namespace guild::gui {
i16  GfxMetricWord(int, int)      { return 8; }
i32  GfxMetricDword(int, int)     { return 8 << 16; }
i16  SliderTrackExtent(int, int)  { return 8; }
void* SceneStateFor(int)          { return nullptr; }
int  GlyphAdvance(void*, int)     { return 4; }
i16  Property_Get(const char*, int) { return 16; }
int  Property_Validate(const char*) { return 0; }
int  Form_PropertyValidate(const char* n) { return (n && n[0]) ? 1 : -1; }
int  Form_FindTextArrayIndex(const char*) { return -1; }
} // namespace guild::gui

static const char* kRoot =
    "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";

static bool assetsPresent() {
    guild::shim::DiskFileSystem fs(kRoot);
    return fs.exists("Resources/forms.BIN");
}

namespace {

// A host that points the modal loop at an already-parsed live window and replays clicks.
struct RealFormHost : MessageBoxHost {
    int   windowSlot = -1;
    int   formId     = -1;
    std::vector<FrameInput> frames;
    size_t i = 0;

    int LoadForm(const char*, bool) override {
        g_currentWindowId = windowSlot;             // the box reads the current window
        g_currentWindow   = &g_windows[windowSlot];
        return formId;
    }
    bool RunFrame(int, guild::i32, unsigned, FrameInput& in) override {
        if (i >= frames.size()) return false;
        in = frames[i++];
        return true;
    }
};

// Parse a named member from forms.BIN into the live tables.  Exact-match on `wantPath`.
bool ParseMember(io::ZipArchive& z, const char* wantPath, FormFile& out) {
    char nm[260];
    io::ZipFileInfo fi;
    for (int r = z.GoToFirstFile(); r == io::kZipOk; r = z.GoToNextFile()) {
        if (z.GetCurrentFileInfo(&fi, nm, sizeof nm) != io::kZipOk)
            continue;
        if (std::strcmp(nm, wantPath) != 0)
            continue;
        std::vector<u8> bytes;
        if (!z.ExtractCurrentFile(bytes))
            return false;
        ResetGuiState();
        out = Form_ParseResourceFile(bytes.data(), bytes.size(), nm);
        return out.ok;
    }
    return false;
}

// Add `n` runtime OK/Cancel-style button widgets to a parsed window (the game does this
// after loading the backdrop form).  Returns the window slot, or -1 if the form has no
// root window.  Uses the genuine Object_AddToWindow leaf.
int AddRuntimeButtons(const FormFile& f, int n) {
    if (f.windows.empty() || f.windows[0].windowSlot < 0)
        return -1;
    int slot = f.windows[0].windowSlot;
    for (int k = 0; k < n; ++k) {
        int w = Object_AddToWindow(slot, 24, 16 + 40 * k, 0);
        if (w < 0) return -1;
        g_widgets[w].type() = kTypeLabel; // a non-'@' child the group builder accepts
        g_widgets[w].btnFlagA() = 1;      // clickable button
    }
    return slot;
}

} // namespace

// Drive the genuine modal box over the REAL `Misc\Messagebox.form` screen with runtime
// buttons: OK on its window resolves to (group selection + 1) and the box tears down.
TEST(GuiMessageBoxRealE2E, ShowOverRealMessageboxForm) {
    if (!assetsPresent()) { CHECK(true); return; }

    guild::shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/forms.BIN"));

    FormFile f;
    CHECK(ParseMember(z, "Misc/Messagebox.form", f));
    CHECK(f.ok);
    CHECK(f.frm2);

    int slot = AddRuntimeButtons(f, 3);
    CHECK(slot >= 0);

    RealFormHost host;
    host.windowSlot = slot;
    host.formId = f.formId;
    FrameInput ok{}; ok.clickedId = kIdOk; ok.clickedWindow = slot;
    host.frames.push_back(ok);

    int r = MessageBox_Show(host, 0, 0, /*textId=*/1);
    CHECK_EQ(r, 1);                              // selection 0 + 1
    CHECK_EQ(g_msgGuardNormal, (guild::u8)0);    // guard cleared

    // A real radio group with 3 buttons was built from the runtime children.
    bool group3 = false;
    for (auto& g : g_radioGroups) if (g.count == 3) group3 = true;
    CHECK(group3);

    std::printf("[GuiMessageBoxRealE2E] real form '%s' (frm2=%d) + 3 runtime buttons -> "
                "OK result=%d\n", f.name.c_str(), f.frm2, r);
}

// Cancel on the real form's window resolves to 0.
TEST(GuiMessageBoxRealE2E, CancelOverRealForm) {
    if (!assetsPresent()) { CHECK(true); return; }

    guild::shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/forms.BIN"));

    FormFile f;
    CHECK(ParseMember(z, "Misc/Messagebox.form", f));
    int slot = AddRuntimeButtons(f, 2);
    CHECK(slot >= 0);

    RealFormHost host;
    host.windowSlot = slot;
    host.formId = f.formId;
    FrameInput cancel{}; cancel.clickedId = kIdCancel; cancel.clickedWindow = slot;
    host.frames.push_back(cancel);

    CHECK_EQ(MessageBox_Show(host, 0, 0, 1), 0);
}

// Green variant over the REAL `Misc\MessageboxGreen.form`: header + body render, the
// fixed green form loads, and OK (no window gate in Green) resolves to selection + 1.
TEST(GuiMessageBoxRealE2E, GreenOverRealGreenForm) {
    if (!assetsPresent()) { CHECK(true); return; }

    guild::shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/forms.BIN"));

    FormFile f;
    CHECK(ParseMember(z, "Misc/MessageboxGreen.form", f));
    CHECK(f.ok);
    int slot = AddRuntimeButtons(f, 2);
    CHECK(slot >= 0);

    struct GreenHost : RealFormHost {
        int header = -1, body = -1;
        void RenderText(int, int t) override { header = t; }
        void RenderBodyText(int, int t) override { body = t; }
    } host;
    host.windowSlot = slot;
    host.formId = f.formId;
    FrameInput ok{}; ok.clickedId = kIdOk; ok.clickedWindow = slot + 7; // wrong window: Green ignores
    host.frames.push_back(ok);

    int r = MessageBox_ShowGreen(host, /*body=*/4900, /*flags=*/0, /*group=*/0, /*header=*/4901);
    CHECK_EQ(r, 1);
    CHECK_EQ(host.header, 4901);
    CHECK_EQ(host.body, 4900);
    CHECK_EQ(g_msgGuardGreen, (guild::u8)0);
}
