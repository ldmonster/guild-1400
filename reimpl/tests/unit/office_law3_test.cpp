// Unit tests for the office_law3 slice (VIBE_Office_* / VIBE_Gesetz_* wave-14).
// Golden vectors computed by hand against the recovered tables/constants and the
// Hex-Rays pseudocode of each function.
#include <cstring>

#include "test.h"
#include "world/office_law3.h"

using namespace guild;
using namespace guild::world;

// ---------------------------------------------------------------------------
// OfficeInitHolderTable (0x47de78)
// ---------------------------------------------------------------------------
TEST(OfficeLaw3_Init, ClearsAndSeedsTable) {
    OfficeInitRecord t[kOfficeInitRecordCount];
    // Pre-poison to confirm the clear+seed actually writes every field.
    std::memset(t, 0xAB, sizeof(t));

    int ret = OfficeInitHolderTable(t);
    CHECK_EQ(ret, 7709);

    for (int r = 0; r < kOfficeInitRecordCount; ++r) {
        CHECK_EQ(static_cast<int>(t[r].holder), r);     // holder = index
        CHECK_EQ(t[r].city, -1);                        // city = -1
        CHECK_EQ(t[r].secondary, -1);                   // secondary = -1
        CHECK_EQ(static_cast<int>(t[r].type),
                 static_cast<int>(kOfficeTypeSeed[r]));  // type seed
    }
    // Spot-check the recovered seed sequence (record 0,1 -> 1; 4,5 -> 4; 8,9 -> 7).
    CHECK_EQ(static_cast<int>(t[0].type), 1);
    CHECK_EQ(static_cast<int>(t[1].type), 1);
    CHECK_EQ(static_cast<int>(t[4].type), 4);
    CHECK_EQ(static_cast<int>(t[5].type), 4);
    CHECK_EQ(static_cast<int>(t[8].type), 7);
    CHECK_EQ(static_cast<int>(t[9].type), 7);
    CHECK_EQ(static_cast<int>(t[36].type), 34);
}

// ---------------------------------------------------------------------------
// OfficeFindRoleTemplate (0x57c184)
// ---------------------------------------------------------------------------
TEST(OfficeLaw3_Role, OutOfRangeAndBadTable) {
    CHECK_EQ(OfficeFindRoleTemplate(0, 0), kRoleNotFound);    // roleId == 0
    CHECK_EQ(OfficeFindRoleTemplate(0, 76), kRoleNotFound);   // roleId >= 76
    CHECK_EQ(OfficeFindRoleTemplate(0, 200), kRoleNotFound);
    CHECK_EQ(OfficeFindRoleTemplate(2, 33), kRoleNotFound);   // which not in {0,1}
    CHECK_EQ(OfficeFindRoleTemplate(99, 33), kRoleNotFound);
}

TEST(OfficeLaw3_Role, TableAExactMatches) {
    // kRoleTableA[0]=33, [1]=32, [2]=31, [27]=13, [45]=1, [74]=12.
    CHECK_EQ(OfficeFindRoleTemplate(0, 33), 0);
    CHECK_EQ(OfficeFindRoleTemplate(0, 32), 1);
    CHECK_EQ(OfficeFindRoleTemplate(0, 31), 2);
    CHECK_EQ(OfficeFindRoleTemplate(0, 13), 27);
    CHECK_EQ(OfficeFindRoleTemplate(0, 1), 45);
    CHECK_EQ(OfficeFindRoleTemplate(0, 12), 74);
    // Verify against the table itself for every live id (1..75 each appear once).
    for (u8 id = 1; id < 76; ++id) {
        int idx = OfficeFindRoleTemplate(0, id);
        CHECK(idx >= 0 && idx < kRoleTemplateSlots);
        CHECK_EQ(static_cast<int>(kRoleTableA[idx]), static_cast<int>(id));
    }
}

TEST(OfficeLaw3_Role, TableBMatchesAndTerminatorStop) {
    // kRoleTableB[27]=34 (differs from A which has 13 there).
    CHECK_EQ(OfficeFindRoleTemplate(1, 34), 27);
    CHECK_EQ(OfficeFindRoleTemplate(1, 33), 0);
    // Table B has 69 live entries then zeros. id 12 lives at index 68 (last live).
    CHECK_EQ(OfficeFindRoleTemplate(1, 12), 68);
    // Ids 13..18 are ABSENT from table B (A had them at 27..32, B has 34..39 there).
    // The original returns the end-of-scan slot (&v4[10*v3]) on no match: the first
    // zero terminator, at index 69.
    for (u8 absent = 13; absent <= 18; ++absent)
        CHECK_EQ(OfficeFindRoleTemplate(1, absent), 69);
    // Every other id 1..75 (except 13..18) is present and resolves to an exact match.
    for (u8 id = 1; id < 76; ++id) {
        if (id >= 13 && id <= 18) continue;
        int idx = OfficeFindRoleTemplate(1, id);
        CHECK(idx >= 0 && idx < kRoleTemplateSlots);
        CHECK_EQ(static_cast<int>(kRoleTableB[idx]), static_cast<int>(id));
    }
}

// ---------------------------------------------------------------------------
// OfficeAwaitPromoteResult (0x562c9c)
// ---------------------------------------------------------------------------
namespace {
struct FlowModel {
    i32 promoteRet = -1;
    u8  type = 0;
    int statusSeq[8] = {0};
    int statusIdx = 0;
    int refreshCalls = 0;
};
FlowModel g_fm;
i32 FmPromote(i32, i32, i32, void*) { return g_fm.promoteRet; }
u8  FmType(i32, void*)              { return g_fm.type; }
void FmRefresh(void*)              { ++g_fm.refreshCalls; }
int FmStatus(i32, void*) {
    int v = g_fm.statusSeq[g_fm.statusIdx];
    if (g_fm.statusIdx < 7) ++g_fm.statusIdx;
    return v;
}
void InstallFlow() {
    OfficeFlowHooks h{&FmPromote, &FmType, &FmRefresh, &FmStatus, nullptr};
    OfficeSetFlowHooks(h);
}
} // namespace

TEST(OfficeLaw3_Await, DefaultPromoteFails) {
    OfficeFlowHooksReset();
    CHECK_EQ(OfficeAwaitPromoteResult(1, 2, 3), false); // default tryPromote == -1
}

TEST(OfficeLaw3_Await, PromoteFailsReturnsFalse) {
    g_fm = FlowModel{};
    g_fm.promoteRet = -1;
    InstallFlow();
    CHECK_EQ(OfficeAwaitPromoteResult(5, 0, 1), false);
}

TEST(OfficeLaw3_Await, NonGuildHeadShortCircuitsTrue) {
    g_fm = FlowModel{};
    g_fm.promoteRet = 42;
    g_fm.type = 3;               // type != 6 -> return true immediately
    InstallFlow();
    CHECK_EQ(OfficeAwaitPromoteResult(5, 0, 1), true);
    CHECK_EQ(g_fm.refreshCalls, 0); // no spin
}

TEST(OfficeLaw3_Await, GuildHeadSpinsThenSucceeds) {
    g_fm = FlowModel{};
    g_fm.promoteRet = 7;
    g_fm.type = 6;               // guild head -> must poll status
    // status: 0,0,1 then re-read 1 (final) -> 1 != 2 -> true
    g_fm.statusSeq[0] = 0;
    g_fm.statusSeq[1] = 0;
    g_fm.statusSeq[2] = 1;       // loop exits here
    g_fm.statusSeq[3] = 1;       // final re-read
    InstallFlow();
    CHECK_EQ(OfficeAwaitPromoteResult(5, 0, 1), true);
    CHECK_EQ(g_fm.refreshCalls, 2); // refreshed on the two pending reads
}

TEST(OfficeLaw3_Await, GuildHeadFailedCommandReturnsFalse) {
    g_fm = FlowModel{};
    g_fm.promoteRet = 7;
    g_fm.type = 6;
    g_fm.statusSeq[0] = 2;       // already final == 2 -> no spin
    g_fm.statusSeq[1] = 2;       // final re-read == 2 -> false
    InstallFlow();
    CHECK_EQ(OfficeAwaitPromoteResult(5, 0, 1), false);
    CHECK_EQ(g_fm.refreshCalls, 0);
}

// ---------------------------------------------------------------------------
// Gesetz description text-id selectors (0x4c2e74 / 2f58 / 3020 / 3278)
// ---------------------------------------------------------------------------
namespace {
struct DescModel {
    int lastTextId = -999;
    int renderCalls = 0;
    int portrait = 7;
};
DescModel g_dm;
int DmPortrait(i32, void*) { return g_dm.portrait; }
void DmRender(char*, int id, void*) { g_dm.lastTextId = id; ++g_dm.renderCalls; }
void InstallDesc() { GesetzSetDescHooks(&DmPortrait, &DmRender, nullptr); }
} // namespace

TEST(OfficeLaw3_Desc, LowOp2SingularPluralAndValueBranch) {
    g_dm = DescModel{}; InstallDesc();
    // lowFlag <= 1 -> flag 1 -> base 4159 ; value < 117 path
    GesetzDescResult r = GesetzFormatDescriptionLow(nullptr, 2, 100, 1, 500, 0);
    CHECK(r.valid);
    CHECK_EQ(r.textId, 4159);          // 4158 + 1
    CHECK_EQ(g_dm.lastTextId, 4159);
    // lowFlag > 1 -> flag 0 -> base 4158 ; value >= 117 path (no id change for base)
    r = GesetzFormatDescriptionLow(nullptr, 2, 200, 1, 500, 5);
    CHECK_EQ(r.textId, 4158);
}

TEST(OfficeLaw3_Desc, LowOp3AndOp4) {
    g_dm = DescModel{}; InstallDesc();
    GesetzDescResult r3 = GesetzFormatDescriptionLow(nullptr, 3, 0, 1, 0, 0);
    CHECK_EQ(r3.textId, 4164);         // 4163 + 1
    GesetzDescResult r4 = GesetzFormatDescriptionLow(nullptr, 4, 0, 1, 0, 9);
    CHECK_EQ(r4.textId, 4168);         // 4168 + 0
}

TEST(OfficeLaw3_Desc, LowFallback) {
    g_dm = DescModel{}; InstallDesc();
    char buf[64] = {0};
    GesetzDescResult r = GesetzFormatDescriptionLow(buf, 5, 0, 1, 0, 0);
    CHECK(!r.valid);
    CHECK(r.usedFallback);
    CHECK(std::strcmp(buf, "Hm, das ist doch nicht verboten") == 0);
    CHECK_EQ(g_dm.renderCalls, 0);     // fallback never renders
}

TEST(OfficeLaw3_Desc, MidOps) {
    g_dm = DescModel{}; InstallDesc();
    CHECK_EQ(GesetzFormatDescriptionMid(nullptr, 13, 0, 1, 0).textId, 4213);
    CHECK_EQ(GesetzFormatDescriptionMid(nullptr, 15, 0, 1, 9).textId, 4223);
    char buf[64] = {0};
    GesetzDescResult r = GesetzFormatDescriptionMid(buf, 14, 0, 1, 0);
    CHECK(!r.valid && r.usedFallback);
}

TEST(OfficeLaw3_Desc, HighOpTable) {
    g_dm = DescModel{}; InstallDesc();
    const int bases[] = {4228, 4233, 4238, 4243, 4248,
                         4253, 4258, 4263, 4268, 4273};
    for (int i = 0; i < 10; ++i) {
        u8 op = static_cast<u8>(16 + i);
        // flag 1 (lowFlag 0 <= 1) -> +1
        GesetzDescResult r = GesetzFormatDescriptionHigh(nullptr, op, 0, 1, 0);
        CHECK(r.valid);
        CHECK_EQ(r.textId, bases[i] + 1);
    }
    // flag 0 path
    CHECK_EQ(GesetzFormatDescriptionHigh(nullptr, 16, 0, 1, 7).textId, 4228);
    // out of switch -> invalid
    CHECK(!GesetzFormatDescriptionHigh(nullptr, 26, 0, 1, 0).valid);
}

TEST(OfficeLaw3_Desc, DispatcherRouting) {
    g_dm = DescModel{}; InstallDesc();
    // op < 8 -> Low (op 3 -> 4163+flag)
    CHECK_EQ(GesetzFormatDescription(nullptr, 3, 0, 1, 0, 0).textId, 4164);
    // op < 16 -> Mid (op 13 -> 4213)
    CHECK_EQ(GesetzFormatDescription(nullptr, 13, 0, 1, 0, 0).textId, 4213);
    // op in [16,26) -> High (op 20 -> 4248+flag)
    CHECK_EQ(GesetzFormatDescription(nullptr, 20, 0, 1, 0, 0).textId, 4249);
    // op >= 26 -> invalid, no render
    GesetzDescResult r = GesetzFormatDescription(nullptr, 26, 0, 1, 0, 0);
    CHECK(!r.valid);
    r = GesetzFormatDescription(nullptr, 200, 0, 1, 0, 0);
    CHECK(!r.valid);
}

// ---------------------------------------------------------------------------
// Law-book modal leaves (0x55a9bc / 0x558bbc / 0x558b30)
// ---------------------------------------------------------------------------
namespace {
struct UiModel {
    int mapState = 0;
    i32 selectionRet = 555;
    const char* lastResource = nullptr;
    int lastPrimary = -1, lastSecondary = -1;
    int modalRet = 99;
    i32 selSubject = 0; i32 selArg = 0; const char* selCaption = nullptr;
};
UiModel g_um;
int UmMap(u8, void*) { return g_um.mapState; }
i32 UmSel(i32 s, const char* c, i32 a, void*) {
    g_um.selSubject = s; g_um.selCaption = c; g_um.selArg = a;
    return g_um.selectionRet;
}
int UmModal(const char* res, int p, int s, void*) {
    g_um.lastResource = res; g_um.lastPrimary = p; g_um.lastSecondary = s;
    return g_um.modalRet;
}
void InstallUi() {
    GesetzUiHooks h{&UmMap, &UmSel, &UmModal, nullptr};
    GesetzSetUiHooks(h);
}
} // namespace

TEST(OfficeLaw3_Ui, OpenSelectionNullSubject) {
    g_um = UiModel{}; InstallUi();
    CHECK_EQ(GesetzOpenPersonSelectionIfValid(0, 4, "cap", 9), 0);
}

TEST(OfficeLaw3_Ui, OpenSelectionBuildingBusyAborts) {
    g_um = UiModel{}; g_um.mapState = 1; InstallUi();
    CHECK_EQ(GesetzOpenPersonSelectionIfValid(123, 4, "cap", 9), 0);
}

TEST(OfficeLaw3_Ui, OpenSelectionProceeds) {
    g_um = UiModel{}; g_um.mapState = 0; g_um.selectionRet = 777; InstallUi();
    CHECK_EQ(GesetzOpenPersonSelectionIfValid(123, 4, "cap", 9), 777);
    CHECK_EQ(g_um.selSubject, 123);
    CHECK_EQ(g_um.selArg, 9);
    CHECK(std::strcmp(g_um.selCaption, "cap") == 0);
}

TEST(OfficeLaw3_Ui, ShowErrorDialogIds) {
    g_um = UiModel{}; g_um.modalRet = 5; InstallUi();
    CHECK_EQ(GesetzShowApplicationErrorDialog(1234), 5);
    CHECK(std::strcmp(g_um.lastResource, "Gesetze\\Antrag_Fehler") == 0);
    CHECK_EQ(g_um.lastPrimary, 1234);
    // errorTextId 0 -> default
    GesetzShowApplicationErrorDialog(0);
    CHECK_EQ(g_um.lastPrimary, kDefaultErrorTextId);
}

TEST(OfficeLaw3_Ui, ShowLawBookInfoIds) {
    g_um = UiModel{}; g_um.modalRet = 8; InstallUi();
    CHECK_EQ(GesetzShowLawBookInfoDialog(100), 8);
    CHECK(std::strcmp(g_um.lastResource, "Gesetze\\Gesetzbuch_Info") == 0);
    CHECK_EQ(g_um.lastPrimary, 101);     // base + 1
    CHECK_EQ(g_um.lastSecondary, 102);   // base + 2
}
