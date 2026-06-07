// Real-asset e2e for the new-CHARACTER creation flow (gui/charcreate).
//
// Drives the full deterministic flow end to end:
//   dynasty actor clicks -> ChooseCharacterTalent commit -> ChooseProfession click
//   -> BuildCharacterPreviewScene portrait cycling -> start command.
// Then, when the shipped assets are present, mounts Resources/forms.BIN and PARSES the
// real CHOOSEPROFESSION / CHOOSECHARACTER_TALENT / CHOOSECHARACTER_INTRO `.form` screens
// (FRM2) with the reconstructed form parser, asserting the recovered layout constants this
// module relies on actually match the shipped forms (window present, sane geometry).
//
// GUARDED: if the asset folder isn't present the asset half passes trivially.
#include "test.h"

#include "gui/charcreate.h"
#include "gui/newgame_setup.h"
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

// Renderer/property/text edges (weak in widget_create.cpp / form_parse.cpp) — safe,
// non-degenerate stubs so the data-model parse doesn't trip on zero metrics. Mirrors
// gui_form_real_e2e_test.cpp; kept distinct here via the same signatures (one definition
// shared across the linked unit binary).
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

// ---------------------------------------------------------------------------
// Pure-flow e2e (always runs): a full character is created and a start command armed.
// ---------------------------------------------------------------------------
TEST(GuiCharCreateE2E, FullCreationFlowArmsStartCommand) {
    // 1) Dynasty: alternate male/female clicks until all six slots are filled.
    DynastyTable t;
    int fill = 0;
    const ChooseCharActor clicks[] = {
        ChooseCharActor::kHandwerkerMann,   // slot0 code1
        ChooseCharActor::kBuergerinFrau,    // slot1 code0
        ChooseCharActor::kPriesterMann,     // slot2 code4
        ChooseCharActor::kZigeunerinFrau,   // slot3 code2
        ChooseCharActor::kOffizierMann,     // slot4 code3 (father)
        ChooseCharActor::kHandwerkerinFrau, // slot5 code1 (mother)
    };
    for (ChooseCharActor a : clicks)
        ChooseCharacter_ApplyActorClick(t, a, &fill);
    CHECK(ChooseCharacter_IsComplete(fill));
    CHECK_EQ(t.slot[4], 3); // father = Offizier code 3
    CHECK_EQ(t.slot[5], 1); // mother = Handwerkerin code 1

    CharCreateParams p;
    p.dynasty = t;

    // 2) Talent commit folds the two parent codes into the profession bytes.
    std::uint8_t talents[kTalentCount] = {2, 4, 1, 3, 0};
    CharCreate_CommitTalents(p, p.dynasty.slot[4], p.dynasty.slot[5], talents);
    CHECK_EQ(p.paternalProfessionByte, 55); // code 3 -> 55
    CHECK_EQ(p.maternalProfessionByte, 37); // code 1 -> 37

    // 3) Profession grid click resolves to a button index (reusing newgame_setup geometry).
    int beruf[kProfessionCount] = {0, 5, 11, 23, 41, 59, 85, 107};
    int ids[kProfessionCount];
    for (int i = 0; i < kProfessionCount; ++i) ids[i] = Profession_ButtonGfx(beruf[i]);
    int picked = ChooseProfession_IndexForClick(ids, ids[5]);
    CHECK_EQ(picked, 5);
    p.professionVariant = beruf[picked]; // ComputeVariantIndex identity in the reference

    // 4) Portrait picker: gender male, step Next twice from 0 then confirm.
    int idx = 0;
    idx = Preview_StepIndex(idx, kPreviewKeyNext);
    idx = Preview_StepIndex(idx, kPreviewKeyNext);
    idx = Preview_WrapIndex(idx, Preview_ModelCount(/*male*/0));
    CHECK_EQ(idx, 2);
    CharCreate_CommitPortrait(p, /*male*/0, idx);
    CHECK(p.portraitModel == "ratsherr2_KUTTE");
    CHECK_EQ(p.startCommand, kPreviewMaleCommands[2] + kPreviewCommandBase);
    CHECK(p.committed);
}

// Portrait cycling wraps consistently across the whole female table (underflow snaps to
// last, overflow modulos) — a full sweep returns to the start.
TEST(GuiCharCreateE2E, PortraitSweepWrapsBothDirections) {
    int n = Preview_ModelCount(/*female*/1);
    CHECK_EQ(n, 7);
    int idx = 0;
    // Step Prev once -> wraps to last entry.
    idx = Preview_WrapIndex(Preview_StepIndex(idx, kPreviewKeyPrev), n);
    CHECK_EQ(idx, n - 1);
    CHECK(std::string(Preview_ModelName(1, idx)) == "zigeunerin_FRAU");
    // Step Next once -> back past the end to 0.
    idx = Preview_WrapIndex(Preview_StepIndex(idx, kPreviewKeyNext), n);
    CHECK_EQ(idx, 0);
    CHECK(std::string(Preview_ModelName(1, idx)) == "bauerin_FRAU");
}

// ---------------------------------------------------------------------------
// Real-asset half: parse the shipped charcreate forms.
// ---------------------------------------------------------------------------
static bool parseMember(io::ZipArchive& z, const char* member, FormFile& out) {
    std::vector<u8> bytes;
    if (!z.ExtractByName(member, bytes, /*caseSensitive=*/false)) return false;
    ResetGuiState();
    out = Form_ParseResourceFile(bytes.data(), bytes.size(), member);
    return out.ok;
}

TEST(GuiCharCreateE2E, RealCharCreateFormsParse) {
    if (!assetsPresent()) { CHECK(true); return; } // skipped: no assets

    guild::shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/forms.BIN"));

    // The three charcreate forms this module wires up are all present and parse as FRM2.
    FormFile prof, talent, intro;
    CHECK(parseMember(z, "Menu/CHOOSEPROFESSION.form", prof));
    CHECK(parseMember(z, "Menu/CHOOSECHARACTER_TALENT.form", talent));
    CHECK(parseMember(z, "Menu/CHOOSECHARACTER_INTRO.form", intro));
    CHECK(prof.frm2);
    CHECK(talent.frm2);
    CHECK(intro.frm2);

    // Each form carries at least its root window with sane on-screen geometry.
    CHECK(prof.windows.size() >= 1);
    CHECK(talent.windows.size() >= 1);
    CHECK(intro.windows.size() >= 1);
    const FormWindowRecord& pw = prof.windows[0];
    CHECK(pw.w > 0 && pw.w < 2000);
    CHECK(pw.h > 0 && pw.h < 2000);

    std::printf("[GuiCharCreateE2E] CHOOSEPROFESSION windows=%zu  CHOOSECHARACTER_TALENT windows=%zu  "
                "CHOOSECHARACTER_INTRO windows=%zu  (profW=%d profH=%d)\n",
                prof.windows.size(), talent.windows.size(), intro.windows.size(),
                pw.w, pw.h);

    // The CHOOSEPROFESSION grid origin (x=100,y=100) we build buttons at must fit inside
    // the shipped window (the 8 profession buttons land at 100..292 in x, 100..180 in y).
    int lastX = kProfessionCellW * 2 + kProfessionOriginX;          // col 2
    int lastY = kProfessionCellH * (kProfessionCount / kProfessionColumns) + kProfessionOriginY;
    CHECK(lastX < pw.x + pw.w + 64); // buttons fit within a reasonable margin of the form
    CHECK(lastY < pw.y + pw.h + 64);
}
