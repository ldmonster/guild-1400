// Real-asset e2e for the recovered `.form` parser (gilde.exe 0x41beb8 ->
// guild::gui::Form_ParseResourceFile). Mounts the shipped `Resources/forms.BIN`
// (a PKZIP archive of ~477 members, 323 of them `.form` screens) with the
// reconstructed ZipArchive over a DiskFileSystem, extracts every `.form` member, and
// drives the recovered FRM2/old-layout parser. Verifies that real members parse
// cleanly, build windows + widgets with sane ids/positions, and reports the counts.
//
// This SUPERSEDES the robustness-only contract documented in
// real_assets_forms_e2e_test.cpp (which only proved Form_LoadFromBuffer rejects FRM2
// without crashing) — here we actually PARSE the FRM2 layout into records.
//
// GUARDED: if the asset folder isn't present the test passes trivially.
#include "tests/framework/test.h"

#include "gui/form_parse.h"
#include "gui/form.h"
#include "gui/window.h"
#include "gui/object.h"

#include "io/zip_archive.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::gui;

// Override the renderer/property/text edges (weak in widget_create.cpp / form_parse.cpp)
// with safe non-zero stubs. The real game seeds these from the loaded gfx catalogue;
// for the data-model parse we only need them non-degenerate so the slider tile math
// (range / tile) and label/property resolution don't trip on zero metrics.
namespace guild::gui {
i16  GfxMetricWord(int, int)      { return 8; }
i32  GfxMetricDword(int, int)     { return 8 << 16; } // >>16 == 8 (non-zero tile)
i16  SliderTrackExtent(int, int)  { return 8; }
void* SceneStateFor(int)          { return nullptr; }
int  GlyphAdvance(void*, int)     { return 4; }
i16  Property_Get(const char*, int) { return 16; }
int  Property_Validate(const char*) { return 0; }
int  Form_PropertyValidate(const char* n) { return (n && n[0]) ? 1 : -1; }
int  Form_FindTextArrayIndex(const char*) { return -1; } // no real text db mounted
} // namespace guild::gui

static const char* kRoot =
    "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";

static bool assetsPresent() {
    guild::shim::DiskFileSystem fs(kRoot);
    return fs.exists("Resources/forms.BIN");
}

static bool endsWithForm(const char* name) {
    std::size_t n = std::strlen(name);
    if (n < 5) return false;
    const char* e = name + n - 5;
    return (e[0] == '.' &&
            (e[1] == 'f' || e[1] == 'F') &&
            (e[2] == 'o' || e[2] == 'O') &&
            (e[3] == 'r' || e[3] == 'R') &&
            (e[4] == 'm' || e[4] == 'M'));
}

TEST(GuiFormRealE2E, ParseEveryFormMemberFromFormsBin) {
    if (!assetsPresent()) { CHECK(true); return; } // skipped: no assets

    guild::shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/forms.BIN"));

    int formMembers = 0;
    int parsedOk    = 0;   // Form_ParseResourceFile returned .ok
    int failed      = 0;
    int frm2Count   = 0;
    int oldCount    = 0;
    long totalWindows = 0;
    long totalWidgets = 0;

    // Sample a couple of well-known members for detailed assertions.
    int gebWindows = -1, gebObjects = -1;
    std::string firstFailed;

    char nm[260];
    io::ZipFileInfo fi;
    for (int r = z.GoToFirstFile(); r == io::kZipOk; r = z.GoToNextFile()) {
        if (z.GetCurrentFileInfo(&fi, nm, sizeof nm) != io::kZipOk)
            continue;
        if (!endsWithForm(nm))
            continue;
        ++formMembers;

        std::vector<u8> bytes;
        if (!z.ExtractCurrentFile(bytes)) { ++failed; if (firstFailed.empty()) firstFailed = nm; continue; }

        // Fresh tables per member so one form can't leak into the next.
        ResetGuiState();
        FormFile f = Form_ParseResourceFile(bytes.data(), bytes.size(), nm);
        if (!f.ok) { ++failed; if (firstFailed.empty()) firstFailed = nm; continue; }
        ++parsedOk;
        if (f.frm2) ++frm2Count; else ++oldCount;

        // Count built windows + objects across this form.
        int objs = 0;
        for (const auto& w : f.windows) objs += static_cast<int>(w.objects.size());
        totalWindows += static_cast<long>(f.windows.size());
        totalWidgets += objs;

        if (std::strstr(nm, "Geb_Bauen.form")) {
            gebWindows = static_cast<int>(f.windows.size());
            gebObjects = objs;
        }
    }

    std::printf("[GuiFormRealE2E] .form members=%d parsedOk=%d failed=%d "
                "(FRM2=%d old=%d) totalWindows=%ld totalObjects=%ld\n",
                formMembers, parsedOk, failed, frm2Count, oldCount,
                totalWindows, totalWidgets);
    if (!firstFailed.empty())
        std::printf("[GuiFormRealE2E] first failed member: %s\n", firstFailed.c_str());

    // Every shipped .form member must parse (0 failures target).
    CHECK_EQ(formMembers, 323);
    CHECK_EQ(failed, 0);
    CHECK_EQ(parsedOk, 323);
    CHECK_EQ(frm2Count, 321);
    CHECK_EQ(oldCount, 2);

    // The sampled Geb_Bauen.form has 4 windows; window 0 carries 9 objects, so the
    // form has >= 9 objects total.
    CHECK_EQ(gebWindows, 4);
    CHECK(gebObjects >= 9);

    // Sanity floor on the aggregate (real screens carry many controls).
    CHECK(totalWindows > 300);
    CHECK(totalWidgets > 100);
}

// Detailed single-member check: parse Geb_Bauen.form and verify the recovered window
// geometry + object table match the ground-truth bytes inspected via xxd.
TEST(GuiFormRealE2E, GebBauenWindowsAndObjects) {
    if (!assetsPresent()) { CHECK(true); return; }

    guild::shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/forms.BIN"));

    std::vector<u8> bytes;
    CHECK(z.ExtractByName("Bauen/Geb_Bauen.form", bytes, /*caseSensitive=*/false));
    CHECK_EQ(bytes.size(), static_cast<std::size_t>(16504)); // 8 + 4*4124

    ResetGuiState();
    FormFile f = Form_ParseResourceFile(bytes.data(), bytes.size(), "Geb_Bauen");
    CHECK(f.ok);
    CHECK(f.frm2);
    CHECK_EQ(f.windows.size(), static_cast<std::size_t>(4));

    // Window 0 (root) geometry from the bytes: x=57 y=69 w=480 h=550 flags=0x11.
    const FormWindowRecord& w0 = f.windows[0];
    CHECK_EQ(w0.x, 57);
    CHECK_EQ(w0.y, 69);
    CHECK_EQ(w0.w, 480);
    CHECK_EQ(w0.h, 550);
    CHECK_EQ(w0.flags, 0x11u);
    CHECK_EQ(w0.parentIndex, 0);           // root
    CHECK_EQ(w0.objects.size(), static_cast<std::size_t>(9));

    // Windows 1..3 are children of window 0 (parentIndex == 1).
    for (int i = 1; i < 4; ++i) {
        CHECK_EQ(f.windows[i].parentIndex, 1);
        CHECK_EQ(f.windows[i].parentWindowSlot, w0.windowSlot);
        CHECK(f.windows[i].windowSlot >= 0);
    }

    // First three objects are the named "_RAHMEN_HAUSBAU+N" sprites at sane positions.
    CHECK_EQ(w0.objects[0].name, std::string("_RAHMEN_HAUSBAU+1"));
    CHECK_EQ(w0.objects[1].name, std::string("_RAHMEN_HAUSBAU+2"));
    CHECK_EQ(w0.objects[2].name, std::string("_RAHMEN_HAUSBAU+3"));
    for (const auto& o : w0.objects) {
        // Positions are within a generous screen bound (no garbage).
        CHECK(o.x >= 0 && o.x < 2000);
        CHECK(o.y >= 0 && o.y < 2000);
    }
}
