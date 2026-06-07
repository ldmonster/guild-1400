// E2E flow across the office_law3 slice: initialize the office-holder table, look
// up a staff role template for a freshly-seeded office, run a promotion through the
// await-result flow, then render a law-violation description for the new officeholder
// and show the resulting modal — wiring the hook leaves together as the game would.
#include <cstring>
#include <string>
#include <vector>

#include "test.h"
#include "world/office_law3.h"

using namespace guild;
using namespace guild::world;

namespace {

// A tiny in-memory "engine" the hooks delegate to, modelling the parts office_law3
// does not own (promotion command queue, packet status, person portraits, modals).
struct FakeEngine {
    // promotion command queue
    int nextCmdId = 1000;
    std::vector<int> commandStatus;     // index = cmdId - 1000
    // person model
    u8  promotedType = 6;               // guild head, forces the status spin
    int portraitId = 41;
    // recorded UI
    std::vector<std::string> modals;
    std::vector<int> modalPrimary;
    int refreshTicks = 0;
};
FakeEngine g_eng;

i32 EngPromote(i32 person, i32 from, i32 to, void*) {
    (void)person; (void)from; (void)to;
    int id = g_eng.nextCmdId++;
    // command starts pending (0), becomes success (1) after two refresh ticks.
    g_eng.commandStatus.push_back(0);
    return id;
}
u8 EngType(i32, void*) { return g_eng.promotedType; }
void EngRefresh(void*) {
    ++g_eng.refreshTicks;
    // After two pumps the latest command resolves to success.
    if (g_eng.refreshTicks >= 2 && !g_eng.commandStatus.empty())
        g_eng.commandStatus.back() = 1;
}
int EngStatus(i32 cmdId, void*) {
    int idx = cmdId - 1000;
    if (idx < 0 || idx >= static_cast<int>(g_eng.commandStatus.size())) return 1;
    return g_eng.commandStatus[idx];
}

int EngPortrait(i32, void*) { return g_eng.portraitId; }
int g_lastDescId = -1;
void EngRender(char*, int id, void*) { g_lastDescId = id; }

int EngMap(u8, void*) { return 0; } // building available
i32 EngSel(i32 subject, const char*, i32, void*) { return subject; }
int EngModal(const char* res, int primary, int, void*) {
    g_eng.modals.emplace_back(res);
    g_eng.modalPrimary.push_back(primary);
    return primary;
}

} // namespace

TEST(OfficeLaw3_E2E, FullPromotionAndDescriptionFlow) {
    g_eng = FakeEngine{};
    g_lastDescId = -1;

    // --- 1. Initialize the office-holder table. ---
    OfficeInitRecord table[kOfficeInitRecordCount];
    std::memset(table, 0xCD, sizeof(table));
    int initRet = OfficeInitHolderTable(table);
    CHECK_EQ(initRet, 7709);
    // Record 8 is seeded with office type 7; pick its holder for the flow.
    CHECK_EQ(static_cast<int>(table[8].type), 7);
    CHECK_EQ(static_cast<int>(table[8].holder), 8);
    CHECK_EQ(table[8].city, -1);

    // --- 2. Resolve a staff role-template for that office's type via table A. ---
    // role id 7 lives at index 69 in table A.
    int roleIdx = OfficeFindRoleTemplate(0, 7);
    CHECK_EQ(roleIdx, 69);
    CHECK_EQ(static_cast<int>(kRoleTableA[roleIdx]), 7);
    // master variant (table B): role id 7 lives at index 63.
    CHECK_EQ(OfficeFindRoleTemplate(1, 7), 63);

    // --- 3. Run the promotion through the await-result flow. ---
    OfficeFlowHooks fh{&EngPromote, &EngType, &EngRefresh, &EngStatus, nullptr};
    OfficeSetFlowHooks(fh);
    bool promoted = OfficeAwaitPromoteResult(/*person*/ 8, /*from*/ -1, /*to*/ 0);
    CHECK(promoted);                 // guild head, command resolved to success (1)
    CHECK_EQ(g_eng.refreshTicks, 2); // spun exactly twice before status went 1

    // --- 4. Render a law-violation description for the (now promoted) officeholder. ---
    GesetzSetDescHooks(&EngPortrait, &EngRender, nullptr);
    // A "high" op-class violation (op 19) with the singular phrasing (lowFlag 0).
    GesetzDescResult desc = GesetzFormatDescription(nullptr, /*op*/ 19,
                                                    /*value*/ 0, /*subject*/ 8,
                                                    /*subValue*/ 0, /*lowFlag*/ 0);
    CHECK(desc.valid);
    CHECK_EQ(desc.textId, 4244);     // 4243 + 1
    CHECK_EQ(g_lastDescId, 4244);    // the render leaf saw the same id

    // --- 5. Open the person-selection window, then surface the law-book info modal. ---
    GesetzUiHooks uh{&EngMap, &EngSel, &EngModal, nullptr};
    GesetzSetUiHooks(uh);
    i32 selected = GesetzOpenPersonSelectionIfValid(/*subject*/ 8, /*bldg*/ 2,
                                                    "Anklage", /*arg*/ 0);
    CHECK_EQ(selected, 8);           // building available -> selection returned subject

    i32 infoResult = GesetzShowLawBookInfoDialog(/*recordBase*/ desc.textId);
    CHECK_EQ(infoResult, desc.textId + 1);
    CHECK_EQ(static_cast<int>(g_eng.modals.size()), 1);
    CHECK(g_eng.modals[0] == "Gesetze\\Gesetzbuch_Info");
    CHECK_EQ(g_eng.modalPrimary[0], desc.textId + 1);

    // A bad application surfaces the error modal with the default id.
    i32 errResult = GesetzShowApplicationErrorDialog(0);
    CHECK_EQ(errResult, kDefaultErrorTextId);
    CHECK_EQ(static_cast<int>(g_eng.modals.size()), 2);
    CHECK(g_eng.modals[1] == "Gesetze\\Antrag_Fehler");
}

TEST(OfficeLaw3_E2E, NonGuildHeadPromotionSkipsSpin) {
    g_eng = FakeEngine{};
    g_eng.promotedType = 3;          // not a guild head
    OfficeFlowHooks fh{&EngPromote, &EngType, &EngRefresh, &EngStatus, nullptr};
    OfficeSetFlowHooks(fh);
    CHECK(OfficeAwaitPromoteResult(2, 0, 1));
    CHECK_EQ(g_eng.refreshTicks, 0); // short-circuited, no polling

    OfficeFlowHooksReset();          // leave globals clean for other suites
    GesetzDescHooksReset();
    GesetzUiHooksReset();
}
