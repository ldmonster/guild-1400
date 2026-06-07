#include "app/real_forms_driver.h"
#include "app/real_boot.h"

#include "gui/form_parse.h"
#include "gui/form.h"
#include "gui/window.h"
#include "gui/object.h"

#include "io/vfs.h"
#include "io/archive_mount.h"

#include <cstring>

namespace guild::app {

// ---------------------------------------------------------------------------
// Inert, counting edge-hooks for the FRM2 parser's renderer/text leaves.
//
// gui/form_parse.cpp forward-declares Form_PropertyValidate (0x40dfd4) and
// Form_FindTextArrayIndex (0x44e0d8) as WEAK with neutral (-1) defaults. The real
// implementations are renderer/text-cluster edges not reconstructed here. We supply
// STRONG definitions in this library .cpp (so they win the link in every executable
// that pulls this module, and no test ever has to define a symbol src/ references).
//
// They are INERT: no renderer state, just deterministic, counted answers that let
// the data-model BUILD proceed:
//   * PropertyValidate returns a stable non-negative sentinel id (so sprite/slider
//     graphic-id resolution is non-null and the create leaves run their full path),
//   * FindTextArrayIndex returns a non-negative id (so type-'C' labels actually get
//     built — the parser guards label creation on index != -1).
// Both bump a counter so a test can prove the parser reached the edge.
namespace {
long g_propValidateCalls = 0;
long g_findTextCalls     = 0;

// A stable, harmless graphic/text id. Non-negative so the build proceeds; small so
// it never trips the gfx-object table bounds in the create leaves (they treat it as
// an index into the metric table whose neutral defaults return 0).
constexpr int kInertGfxId  = 1;
constexpr int kInertTextId = 1;
} // namespace

void InstallInertFormHooks() {
    g_propValidateCalls = 0;
    g_findTextCalls     = 0;
}

FormHookCounts RealFormsHookCounts() {
    return FormHookCounts{g_propValidateCalls, g_findTextCalls};
}

} // namespace guild::app

// Strong definitions of the gui FRM2 parser's weak edges (override the -1 defaults).
namespace guild::gui {
int Form_PropertyValidate(const char* /*name*/) {
    ++guild::app::g_propValidateCalls;
    return guild::app::kInertGfxId;
}
int Form_FindTextArrayIndex(const char* /*name*/) {
    ++guild::app::g_findTextCalls;
    return guild::app::kInertTextId;
}

// Strong definitions of the widget-CREATE leaves' weak renderer/metric edges. These
// are graphics-metric / scene-state reads the slider/sprite SIZE math performs; with
// no renderer cluster wired they are INERT. The metric reads must return a NON-ZERO
// packed tile size, though: Widget_CreateSlider computes `range / (metric>>16)`, so a
// 0 metric would divide-by-zero on the real slider objects. We return a small fixed
// packed metric (16 px in the high word) so the size math is well-defined and the
// data-model build proceeds; the visual surface/blit is still deferred.
constexpr i32 kInertMetricPacked = 16 << 16; // 16 px in the 16.16 high word
i16 GfxMetricWord(int /*gfxId*/, int /*byteOff*/)   { return 16; }
i32 GfxMetricDword(int /*gfxId*/, int /*byteOff*/)  { return kInertMetricPacked; }
i16 SliderTrackExtent(int /*gfxBase*/, int /*flags*/) { return 16; }
void* SceneStateFor(int /*gfxId*/)                  { return nullptr; }
int GlyphAdvance(void* /*state*/, int /*which*/)    { return 8; }
int ButtonBankFrameCount(int /*rec*/)               { return 1; }
// Property_Get measures text pixel width; inert non-negative answer keeps label
// widths well-defined. Property_Validate (the widget-create variant, distinct from
// the FRM2-parser Form_PropertyValidate above) resolves a font/property id.
i16 Property_Get(const char* /*text*/, int /*font*/) { return 8; }
int Property_Validate(const char* /*name*/)          { return guild::app::kInertGfxId; }
} // namespace guild::gui

namespace guild::app {

// ---------------------------------------------------------------------------
// Drive one extracted `.form` buffer through the FRM2 parser, summarize structure.
DrivenForm DriveFormBuffer(const guild::u8* data, std::size_t len, const char* name) {
    DrivenForm df;
    df.name  = name ? name : "";
    df.bytes = len;

    // Start every form from a clean retained-mode table (the live tables only hold
    // 48 forms / 96 windows / 511 widgets; driving 323 forms back-to-back would
    // exhaust them — exactly why the gfx-catalogue e2e resets per member too).
    gui::ResetGuiState();

    gui::FormFile ff = gui::Form_ParseResourceFile(data, len, df.name.c_str());
    df.parsed = ff.ok;
    df.frm2   = ff.frm2;
    df.formId = ff.formId;
    if (!ff.ok)
        return df;

    df.windowCount = static_cast<int>(ff.windows.size());
    for (const auto& wr : ff.windows) {
        if (wr.parentIndex != 0)
            ++df.childWindows;
        else
            ++df.rootWindows;

        for (const auto& orec : wr.objects) {
            ++df.objectRecords;
            if (orec.widgetIdx < 0)
                continue;
            ++df.widgetCount;
            switch (orec.type) {
                case gui::kFormObjSprite:
                case gui::kFormObjWindow: ++df.spriteCount; break;
                case gui::kFormObjLabel:  ++df.labelCount;  break;
                case gui::kFormObjInput:  ++df.inputCount;  break;
                case gui::kFormObjSlider: ++df.sliderCount; break;
                default: break;
            }
        }
    }
    return df;
}

namespace {
bool EndsWithForm(const std::string& name) {
    if (name.size() < 5) return false;
    const char* e = name.c_str() + name.size() - 5;
    return e[0] == '.' &&
           (e[1] == 'f' || e[1] == 'F') &&
           (e[2] == 'o' || e[2] == 'O') &&
           (e[3] == 'r' || e[3] == 'R') &&
           (e[4] == 'm' || e[4] == 'M');
}

// Find the mounted forms.BIN in a RealGameAssets bundle.
const io::ArchiveMount* FindFormsMount(const RealGameAssets& assets) {
    for (const auto& a : assets.archives) {
        if (!a.mounted || !a.mount) continue;
        // The default set names it "Resources/forms.BIN".
        if (a.name.find("forms.BIN") != std::string::npos ||
            a.name.find("FORMS.BIN") != std::string::npos)
            return a.mount.get();
    }
    return nullptr;
}
} // namespace

// ---------------------------------------------------------------------------
RealFormsResult DriveRealForms(guild::shim::IFileSystem* fs, const std::string& gameDir) {
    RealFormsResult r;
    InstallInertFormHooks();

    if (!fs || !fs->exists("Resources/forms.BIN"))
        return r; // assetsPresent stays false -> caller skips
    r.assetsPresent = true;

    // Mount the real assets (binds the process-global VFS + mounts Resources/*.BIN).
    // We only need forms.BIN, but reuse the real boot helper so the wiring is the
    // genuine path; mount just forms.BIN to keep the run lean.
    RealGameAssets assets =
        MountRealGameAssets(fs, gameDir, "Gilde.INI",
                            {"Resources/forms.BIN"}, /*caseInsensitive=*/true);

    const io::ArchiveMount* mount = FindFormsMount(assets);
    if (mount && mount->isMounted()) {
        r.mounted = true;
        r.archiveMembers = mount->memberCount();

        // Walk the indexed members; for every `.form`, extract bytes through the
        // mount and parse them with the FRM2 parser, building the widget trees.
        for (const auto& m : mount->members()) {
            if (!EndsWithForm(m.name))
                continue;
            ++r.formMembers;

            std::vector<guild::u8> bytes;
            // const_cast: OpenMember is non-const (it advances the zip cursor); the
            // index it reads is logically const for our purposes.
            io::ArchiveMount* mm = const_cast<io::ArchiveMount*>(mount);
            if (!mm->OpenMember(m.name.c_str(), bytes) || bytes.empty()) {
                ++r.failedForms;
                continue;
            }

            DrivenForm df = DriveFormBuffer(bytes.data(), bytes.size(), m.name.c_str());
            if (!df.parsed) {
                ++r.failedForms;
                r.forms.push_back(std::move(df));
                continue;
            }

            ++r.parsedForms;
            if (df.frm2) ++r.frm2Forms; else ++r.oldForms;
            r.totalWindows       += df.windowCount;
            r.totalChildWindows  += df.childWindows;
            r.totalWidgets       += df.widgetCount;
            r.totalLabels        += df.labelCount;
            r.totalInputs        += df.inputCount;
            r.totalSliders       += df.sliderCount;
            r.totalSprites       += df.spriteCount;
            r.totalObjectRecords += df.objectRecords;

            if (df.windowCount > r.maxWindowsInOneForm)
                r.maxWindowsInOneForm = df.windowCount;
            if (df.widgetCount > r.maxWidgetsInOneForm) {
                r.maxWidgetsInOneForm = df.widgetCount;
                r.richestFormName     = df.name;
            }
            r.forms.push_back(std::move(df));
        }
    }

    FormHookCounts hc = RealFormsHookCounts();
    r.propertyValidateCalls = hc.propertyValidate;
    r.findTextIndexCalls    = hc.findTextIndex;

    // Tear down the process-global VFS we bound (assets' ArchiveMounts are owned by
    // the local `assets` and freed on return; the VFS is global, so close it).
    io::VfsShutdown();
    return r;
}

} // namespace guild::app
