// Golden-vector unit tests for the wave-19 recruitment-collection / office-list /
// session-timer / guild-law person-selection harvest (gui/recruit_office) and the
// estate-ownership transfer (world/estate_transfer). All against synthetic states.
#include "test.h"

#include "gui/recruit_office.h"
#include "world/estate_transfer.h"
#include "sim/entity.h"
#include "sim/person.h"
#include "sim/types.h"

#include <cstring>

using namespace guild;
using namespace guild::gui;
using guild::sim::Person;
using guild::sim::g_persons;
using guild::sim::g_personIds;
using guild::sim::PersonSetWord;
using guild::sim::PersonSetByte;
using guild::sim::PersonSetDword;
using guild::sim::PersonGetDword;
using guild::sim::PersonGetWord;
using guild::sim::PersonGetByte;

// ===========================================================================
// RecruitCollectNearbyCandidates (0x55d530)
// ===========================================================================
namespace {
struct FakeQuery {
    static u16 slots[guild::gui::kRecruitMaxCandidates];
    static int  slotCount;
    static int  lastFilter;
    static int Find(const u16*, int maxPeople, int filter, float, float, u16* out) {
        lastFilter = filter;
        int n = slotCount < maxPeople ? slotCount : maxPeople;
        for (int i = 0; i < n; ++i) out[i] = slots[i];
        return n;
    }
};
u16 FakeQuery::slots[guild::gui::kRecruitMaxCandidates] = {};
int FakeQuery::slotCount = 0;
int FakeQuery::lastFilter = 0;
}

TEST(RecruitCollect, FilterConstantSelectsByByte9) {
    RecruitCollectHooks h{};
    h.setGrayColor = [](int, int) {};
    h.findPeopleByPalette = &FakeQuery::Find;
    RecruitCollectSetHooks(h);

    u16 ref = 5;
    i32 out[guild::gui::kRecruitMaxCandidates] = {};
    FakeQuery::slotCount = 0;

    // refByte9 != 0 -> filter = 21513.
    RecruitCollectNearbyCandidates(&ref, /*refByte9*/ 7, out);
    CHECK_EQ(FakeQuery::lastFilter, 21513);
    // refByte9 == 0 -> filter = 21514.
    RecruitCollectNearbyCandidates(&ref, /*refByte9*/ 0, out);
    CHECK_EQ(FakeQuery::lastFilter, 21514);

    RecruitCollectResetHooks();
}

TEST(RecruitCollect, MapsSlotsThroughIdColumn) {
    guild::sim::ResetEntityArrays();
    // person id column: slot 3 -> id 1001, slot 8 -> id 1002.
    g_personIds[3] = 1001;
    g_personIds[8] = 1002;

    RecruitCollectHooks h{};
    h.setGrayColor = [](int, int) {};
    h.findPeopleByPalette = &FakeQuery::Find;
    RecruitCollectSetHooks(h);

    FakeQuery::slots[0] = 3;
    FakeQuery::slots[1] = 8;
    FakeQuery::slotCount = 2;

    u16 ref = 0;
    i32 out[guild::gui::kRecruitMaxCandidates] = {};
    int n = RecruitCollectNearbyCandidates(&ref, 1, out);
    CHECK_EQ(n, 2);
    CHECK_EQ(out[0], 1001);
    CHECK_EQ(out[1], 1002);

    RecruitCollectResetHooks();
}

// ===========================================================================
// OfficeShowCandidateListWithRoles (0x555f8c)
// ===========================================================================
TEST(OfficeList, EmptyCountReturnsZero) {
    OfficeListRow rows[8] = {};
    int stride = -7, anchor = -7;
    int r = OfficeShowCandidateListWithRoles(0, rows, 99, &stride, &anchor);
    CHECK_EQ(r, 0);
    CHECK_EQ(stride, -7);  // untouched
}

TEST(OfficeList, RowGeometryAndPageDescriptor) {
    OfficeListRow rows[3] = {};
    int stride = 0, anchor = 0;
    int r = OfficeShowCandidateListWithRoles(3, rows, /*anchorIn*/ 0x1234,
                                             &stride, &anchor);
    CHECK_EQ(r, 1);
    // x = 75 always; y = 112*i + 40.
    CHECK_EQ(rows[0].x, 75);  CHECK_EQ(rows[0].y, 40);
    CHECK_EQ(rows[1].x, 75);  CHECK_EQ(rows[1].y, 152);
    CHECK_EQ(rows[2].x, 75);  CHECK_EQ(rows[2].y, 264);
    // page descriptor: stride 112, anchor passthrough.
    CHECK_EQ(stride, 112);
    CHECK_EQ(anchor, 0x1234);
}

// ===========================================================================
// OfficeRenderSessionTimer (0x49d910)
// ===========================================================================
TEST(OfficeTimer, FormatsScaledTickDelta) {
    char buf[64] = {};
    // delta = 5000 ticks; elapsed = 14*5000 = 70000 ms = 1:10:0.
    OfficeRenderSessionTimer(buf, sizeof(buf), /*now*/ 6000, /*start*/ 1000);
    CHECK(std::strcmp(buf, " 1 : 10 :   0 ms") == 0);

    // delta = 100 ticks; elapsed = 1400 ms = 0:1:400.
    OfficeRenderSessionTimer(buf, sizeof(buf), 100, 0);
    CHECK(std::strcmp(buf, " 0 :  1 : 400 ms") == 0);
}

// ===========================================================================
// GesetzClassifyState + GesetzHarvestSelectionCandidates (0x55a224)
// ===========================================================================
TEST(GesetzHarvest, StateClassification) {
    CHECK(GesetzClassifyState(1).scanSpouseTable);
    CHECK(GesetzClassifyState(2).scanSpouseTable);
    CHECK(!GesetzClassifyState(3).scanSpouseTable);
    // child-table set: 4..9, 11..14, 16, 18..22.
    CHECK(GesetzClassifyState(4).scanChildTable);
    CHECK(GesetzClassifyState(9).scanChildTable);
    CHECK(!GesetzClassifyState(10).scanChildTable);
    CHECK(GesetzClassifyState(11).scanChildTable);
    CHECK(GesetzClassifyState(14).scanChildTable);
    CHECK(!GesetzClassifyState(15).scanChildTable);
    CHECK(GesetzClassifyState(16).scanChildTable);
    CHECK(!GesetzClassifyState(17).scanChildTable);
    CHECK(GesetzClassifyState(18).scanChildTable);
    CHECK(GesetzClassifyState(22).scanChildTable);
    CHECK(!GesetzClassifyState(23).scanChildTable);
}

TEST(GesetzHarvest, SeedsSpouseAndChildThenScans) {
    guild::sim::ResetEntityArrays();
    // Subject is slot 0: id 500, spouse word(+37)=101, child word(+39)=102.
    Person& subj = g_persons[0];
    subj.marker = 0;
    PersonSetDword(&subj, 0x04, 500);     // id
    PersonSetWord(&subj, 0x25, 101);      // +37 spouse
    PersonSetWord(&subj, 0x27, 102);      // +39 child

    // Two persons bound to the subject via the spouse-link dword (+0x170)==500.
    g_persons[5].marker = 201;
    PersonSetDword(&g_persons[5], 0x170, 500);
    g_persons[6].marker = 202;
    PersonSetDword(&g_persons[6], 0x170, 500);
    // A person bound but whose marker duplicates the child word -> skipped.
    g_persons[7].marker = 102;
    PersonSetDword(&g_persons[7], 0x170, 500);

    u16 out[guild::gui::kGesetzMaxCandidates] = {};
    // state 1 -> spouse-table scan.
    int n = GesetzHarvestSelectionCandidates(&subj, /*state*/ 1, out);
    // seeds: 101 (spouse) + 102 (child) = 2 ; then 201, 202 (203 dup skipped).
    CHECK_EQ(n, 4);
    CHECK_EQ(out[0], 101);
    CHECK_EQ(out[1], 102);
    CHECK_EQ(out[2], 201);
    CHECK_EQ(out[3], 202);
}

TEST(GesetzHarvest, ChildTableScanSet) {
    guild::sim::ResetEntityArrays();
    Person& subj = g_persons[0];
    subj.marker = 0;
    PersonSetDword(&subj, 0x04, 700);
    PersonSetWord(&subj, 0x25, 0xFFFF);   // no spouse
    PersonSetWord(&subj, 0x27, 0xFFFF);   // no child
    g_persons[9].marker = 301;
    PersonSetDword(&g_persons[9], 0x16C, 700);  // child-link

    u16 out[guild::gui::kGesetzMaxCandidates] = {};
    // state 4 -> child-table scan (no seeds since both words are 0xFFFF).
    int n = GesetzHarvestSelectionCandidates(&subj, 4, out);
    CHECK_EQ(n, 1);
    CHECK_EQ(out[0], 301);
}

TEST(GesetzHarvest, NullSubjectAndCap) {
    u16 out[guild::gui::kGesetzMaxCandidates] = {};
    CHECK_EQ(GesetzHarvestSelectionCandidates(nullptr, 1, out), 0);
}

// ===========================================================================
// GesetzOpenPersonSelectionIfValid (0x55a9bc)
// ===========================================================================
TEST(GesetzOpen, GateLogic) {
    GesetzOpenResetHooks();
    // null subject -> false.
    CHECK(!GesetzOpenPersonSelectionIfValid(false, 3));
    // busy building (state != 0) -> false.
    GesetzOpenHooks h{};
    h.mapTypeToState = [](u8, void*) { return 1; };
    GesetzOpenSetHooks(h);
    CHECK(!GesetzOpenPersonSelectionIfValid(true, 3));
    // available -> true.
    h.mapTypeToState = [](u8, void*) { return 0; };
    GesetzOpenSetHooks(h);
    CHECK(GesetzOpenPersonSelectionIfValid(true, 3));
    GesetzOpenResetHooks();
}

// ===========================================================================
// PersonTransferEstateOwnership (0x58c4a8)
// ===========================================================================
namespace {
void SeedPerson(int slot, i16 marker, i32 id) {
    Person& p = g_persons[slot];
    std::memset(&p, 0, sizeof(Person));
    p.marker = marker;
    PersonSetDword(&p, 0x04, id);
    g_personIds[slot] = id;
}
}

TEST(EstateTransfer, MissingFromReturnsMinus1) {
    guild::sim::ResetEntityArrays();
    guild::world::EstateTransferResetHooks();
    CHECK_EQ(guild::world::PersonTransferEstateOwnership(999, 1000, 0), -1);
}

TEST(EstateTransfer, MissingToReturnsMinus2) {
    guild::sim::ResetEntityArrays();
    guild::world::EstateTransferResetHooks();
    SeedPerson(0, /*marker*/ 1, /*id*/ 500);
    CHECK_EQ(guild::world::PersonTransferEstateOwnership(500, /*missing*/ 999, 0), -2);
}

TEST(EstateTransfer, SplicesRecordsAndRepointsRelations) {
    guild::sim::ResetEntityArrays();
    guild::world::EstateTransferResetHooks();

    // FROM: slot 2, marker 10, id 500. TO: slot 4, marker 20, id 600.
    SeedPerson(2, /*marker*/ 10, /*id*/ 500);
    SeedPerson(4, /*marker*/ 20, /*id*/ 600);
    // Distinguishing fields the splice copies:
    PersonSetByte(&g_persons[2], 0x02, 3);     // FROM kind byte
    PersonSetByte(&g_persons[2], 0x0D, 0xAB);  // FROM +13
    PersonSetDword(&g_persons[2], 0x170, 0xDEAD); // FROM +0x170

    // A third person whose relation slot 0 references FROM id (500) -> should be
    // re-pointed to TO id (600). Another referencing TO (600) -> swapped to FROM.
    SeedPerson(7, 30, 700);
    PersonSetDword(&g_persons[7], 0x5C + 0, 500);  // relation[0] -> FROM
    PersonSetDword(&g_persons[7], 0x5C + 4, 600);  // relation[1] -> TO

    int fromSlot = guild::world::PersonTransferEstateOwnership(500, 600, 0);
    // returns the FROM slot index == (u16)fromMarker == 10.
    CHECK_EQ(fromSlot, 10);

    // FROM slot (10) now holds the NEW-TO image: marker spliced to FROM marker (10),
    // id spliced to FROM id (500), kind byte (3), +13 (0xAB), +0x170 (0xDEAD).
    Person& fromImg = g_persons[10];
    CHECK_EQ((int)fromImg.marker, 10);
    CHECK_EQ(PersonGetDword(&fromImg, 0x04), 500);
    CHECK_EQ((int)PersonGetByte(&fromImg, 0x02), 3);
    CHECK_EQ((int)PersonGetByte(&fromImg, 0x0D), 0xAB);
    CHECK_EQ((unsigned)PersonGetDword(&fromImg, 0x170), 0xDEADu);

    // TO slot (20) holds the NEW-FROM image: marker = TO marker (20), kind byte = 9.
    Person& toImg = g_persons[20];
    CHECK_EQ((int)toImg.marker, 20);
    CHECK_EQ((int)PersonGetByte(&toImg, 0x02), 9);

    // Relation re-point on the bystander.
    CHECK_EQ(PersonGetDword(&g_persons[7], 0x5C + 0), 600);  // 500 -> 600
    CHECK_EQ(PersonGetDword(&g_persons[7], 0x5C + 4), 500);  // 600 -> 500

    // Wealth/jail bookkeeping stamped on the FROM slot.
    CHECK_EQ(PersonGetDword(&fromImg, 0x194), 4);  // dword_12CEAA4 = 4
    CHECK_EQ(PersonGetDword(&fromImg, 0x1C8), 0);  // dword_12CEAD8 = 0
}

TEST(EstateTransfer, BuildingReparentAndDropFlag) {
    guild::sim::ResetEntityArrays();
    SeedPerson(2, /*marker*/ 10, /*id*/ 500);
    SeedPerson(4, /*marker*/ 20, /*id*/ 600);
    PersonSetByte(&g_persons[2], 0x02, 5);  // FROM kind == 5 -> dropFlag true

    // A live cat-2 building owned by FROM marker (10): type byte (+0) nonzero,
    // owner word (+39) = 10.
    guild::sim::g_objects[1].alive = 7;
    i16 ownerMarker = 10;
    std::memcpy(reinterpret_cast<u8*>(&guild::sim::g_objects[1]) + 39, &ownerMarker, 2);

    static int reparentCalls = 0;
    static bool dropSeen = false;
    reparentCalls = 0; dropSeen = false;
    guild::world::EstateTransferHooks h{};
    guild::world::EstateTransferResetHooks();
    h = guild::world::EstateTransferGetHooks();
    h.buildingMapTypeToCategory = [](u8) { return 2; };  // category 2 estate
    h.buildingSetObjectParent = [](guild::sim::ObjectRec*, u16, u16, int) {
        ++reparentCalls;
    };
    h.buildingRemoveAndCleanup = [](i16, bool drop) { dropSeen = drop; };
    guild::world::EstateTransferSetHooks(h);

    int r = guild::world::PersonTransferEstateOwnership(500, 600, 0);
    CHECK_EQ(r, 10);
    CHECK_EQ(reparentCalls, 1);   // exactly the one matching building
    CHECK(dropSeen);              // FROM kind == 5 set the drop flag

    guild::world::EstateTransferResetHooks();
}

// ===========================================================================
// Recruitment MODAL WINDOW DRIVERS (0x55d990 / 0x55db0c / 0x55de00)
// ===========================================================================
namespace {
// A scriptable hook harness: a queue of "last clicked" values consumed one per
// frame, plus capture slots for the side effects the drivers produce.
struct WinHarness {
    static int      clicks[16];   // dword_75BF38 / dword_62D22C feed
    static int      clickN;
    static int      clickIdx;
    static int      cancelEdge;   // dword_672230
    static int      frames;       // remaining gameLogicRunFrameLoop iterations
    static int      hireCost;     // recruitComputeCost return
    static int      hireConfirmClick; // value that means "confirm" (1210)
    static bool     hirePacketDone;
    static int      messageBoxes;
    static int      cmd15Calls;
    static int      gameTimeAdvances;
    static int      entity29Calls;
    static guild::gui::HeHandler handler;
    static guild::gui::HeHandler* handlerPtr;
    static int      heFindCalls;
    static int      rngDraws;       // count of randomModulo() calls
    static int      rngArgs[8];     // the n passed to each randomModulo() call

    static void Reset() {
        clickN = clickIdx = 0; cancelEdge = 0; frames = 1;
        hireCost = 5; hireConfirmClick = -1; hirePacketDone = true;
        messageBoxes = cmd15Calls = gameTimeAdvances = entity29Calls = 0;
        handler = guild::gui::HeHandler{}; handlerPtr = nullptr; heFindCalls = 0;
        rngDraws = 0; for (int& a : rngArgs) a = 0;
    }
    static int CountRng(int n) {
        if (rngDraws < 8) rngArgs[rngDraws] = n;
        ++rngDraws;
        return 0;
    }
    static int NextClick() {
        if (clickIdx < clickN) return clicks[clickIdx];
        return -1;
    }
    static int RunFrame(int, int, const void*) {
        if (clickIdx < clickN) ++clickIdx;
        return --frames > 0 ? 1 : 0;
    }
    static guild::gui::HeHandler* FindFirst(int) { ++heFindCalls; return handlerPtr; }
    static guild::gui::HeHandler* FindNext() { return nullptr; }
};
int WinHarness::clicks[16] = {};
int WinHarness::clickN = 0;
int WinHarness::clickIdx = 0;
int WinHarness::cancelEdge = 0;
int WinHarness::frames = 1;
int WinHarness::hireCost = 5;
int WinHarness::hireConfirmClick = -1;
bool WinHarness::hirePacketDone = true;
int WinHarness::messageBoxes = 0;
int WinHarness::cmd15Calls = 0;
int WinHarness::gameTimeAdvances = 0;
int WinHarness::entity29Calls = 0;
guild::gui::HeHandler WinHarness::handler = {};
guild::gui::HeHandler* WinHarness::handlerPtr = nullptr;
int WinHarness::heFindCalls = 0;
int WinHarness::rngDraws = 0;
int WinHarness::rngArgs[8] = {};

guild::gui::RecruitWindowHooks MakeWinHooks() {
    using namespace guild::gui;
    RecruitWindowHooks h{};
    RecruitWindowResetHooks();
    h = RecruitWindowGetHooks();
    h.gameTickFinalize       = [](int, int, const char*) { return 7; };
    h.gameLogicRunFrameLoop  = &WinHarness::RunFrame;
    h.readCancelEdge         = []() { return WinHarness::cancelEdge; };
    h.readLastClickedObject  = []() { return WinHarness::NextClick(); };
    h.readPrevSelectedRow    = []() { return -1; };
    h.recruitComputeCost     = [](int, int) { return WinHarness::hireCost; };
    h.playerPersonId         = []() { return 42; };
    h.queueHireRequest       = [](int) { return 99; };
    h.commandGetPacketStatus = [](int) { return WinHarness::hirePacketDone ? 1 : 0; };
    h.panelW                 = []() { return 600; };
    h.cardW                  = []() { return 150; };
    h.cardH                  = []() { return 200; };
    h.heFindFirst            = &WinHarness::FindFirst;
    h.heFindNext             = &WinHarness::FindNext;
    h.dialogShowMessageBox   = [](const char*, int, int) { ++WinHarness::messageBoxes; };
    h.enqueueCmd15           = [](int, int, int, int) { ++WinHarness::cmd15Calls; };
    h.gameTimeAdvance        = [](int, int, int, int) { ++WinHarness::gameTimeAdvances; };
    h.queueEntity29          = [](int, void*) { ++WinHarness::entity29Calls; return 5; };
    h.randomModulo           = [](int) { return 0; };
    return h;
}
} // namespace

TEST(RecruitHireConfirm, ConfirmQueuesAndSpinsToDone) {
    using namespace guild::gui;
    WinHarness::Reset();
    WinHarness::clicks[0] = 1210;   // confirm on the first frame
    WinHarness::clickN = 1;
    WinHarness::frames = 3;
    WinHarness::hirePacketDone = true;
    RecruitWindowSetHooks(MakeWinHooks());

    int r = RecruitRunHireConfirmDialog();
    CHECK_EQ(r, 1);                 // confirmed
    RecruitWindowResetHooks();
}

TEST(RecruitHireConfirm, CancelReturnsZero) {
    using namespace guild::gui;
    WinHarness::Reset();
    WinHarness::clicks[0] = 1155;   // cancel
    WinHarness::clickN = 1;
    WinHarness::frames = 2;
    RecruitWindowSetHooks(MakeWinHooks());

    int r = RecruitRunHireConfirmDialog();
    CHECK_EQ(r, 0);                 // cancelled
    RecruitWindowResetHooks();
}

TEST(RecruitHireConfirm, CancelEdgeQuitsLoop) {
    using namespace guild::gui;
    WinHarness::Reset();
    WinHarness::cancelEdge = 1;     // dword_672230 nonzero -> quit at once
    WinHarness::frames = 5;
    RecruitWindowSetHooks(MakeWinHooks());

    int r = RecruitRunHireConfirmDialog();
    CHECK_EQ(r, 0);
    RecruitWindowResetHooks();
}

TEST(RecruitCandidatePick, GridGeometryAndCollect) {
    using namespace guild::gui;
    guild::sim::ResetEntityArrays();
    g_personIds[3] = 1001;
    g_personIds[8] = 1002;
    // collect leaf via the recruit-collect hook.
    RecruitCollectHooks ch{};
    ch.setGrayColor = [](int, int) {};
    ch.findPeopleByPalette = &FakeQuery::Find;
    RecruitCollectSetHooks(ch);
    FakeQuery::slots[0] = 3; FakeQuery::slots[1] = 8; FakeQuery::slotCount = 2;

    WinHarness::Reset();
    WinHarness::cancelEdge = 1;     // quit immediately after the grid build
    WinHarness::frames = 1;
    // Capture the card geometry.
    static int capX[9]; static int capY[9]; static int capN;
    capN = 0;
    RecruitWindowHooks h = MakeWinHooks();
    h.hudBuildPersonCard = [](int x, int y, int, void*, int) {
        if (capN < 9) { capX[capN] = x; capY[capN] = y; ++capN; }
    };
    RecruitWindowSetHooks(h);

    u16 ref = 0;
    i8 r = RecruitRunCandidatePickWindow(&ref, /*refByte9*/ 1);
    CHECK_EQ(r, 2);                 // no hire -> default 2
    CHECK_EQ(capN, 2);             // exactly the two collected candidates
    // gap = 600 - 3*150 = 150; quarter = 37; half = (150%4)/2 = 1.
    // card 0 (col 0): x = 1 + 10*(-1) + 0 + 37*1 = 28 ; y = 5.
    CHECK_EQ(capX[0], 28);
    CHECK_EQ(capY[0], 5);
    // card 1 (col 1): x = 1 + 10*0 + 1*150 + 37*2 = 225 ; y = 5.
    CHECK_EQ(capX[1], 225);
    CHECK_EQ(capY[1], 5);

    RecruitWindowResetHooks();
    RecruitCollectResetHooks();
}

TEST(RecruitOffer, NoHandlerKindNot6Returns32) {
    using namespace guild::gui;
    WinHarness::Reset();
    WinHarness::handlerPtr = nullptr;
    RecruitWindowSetHooks(MakeWinHooks());

    // entityMarker words: [0]=marker, [1]=kind byte (5, not 6), [2..3]=entity id.
    u16 em[4] = { 0, 5, 100, 0 };
    u16 ref = 0;
    i8 r = RecruitRunRecruitmentOfferWindow(em, &ref, 1);
    CHECK_EQ((int)r, 32);
    RecruitWindowResetHooks();
}

TEST(RecruitOffer, NoHandlerKind6ForwardsToPick) {
    using namespace guild::gui;
    guild::sim::ResetEntityArrays();
    RecruitCollectHooks ch{};
    ch.setGrayColor = [](int, int) {};
    ch.findPeopleByPalette = &FakeQuery::Find;
    RecruitCollectSetHooks(ch);
    FakeQuery::slotCount = 0;       // no candidates -> pick returns 2

    WinHarness::Reset();
    WinHarness::handlerPtr = nullptr;
    WinHarness::cancelEdge = 1;     // pick window quits at once
    RecruitWindowSetHooks(MakeWinHooks());

    u16 em[4] = { 0, 6, 100, 0 };   // kind == 6
    u16 ref = 0;
    i8 r = RecruitRunRecruitmentOfferWindow(em, &ref, 1);
    CHECK_EQ((int)r, 2);            // forwarded to RunCandidatePickWindow -> 2
    RecruitWindowResetHooks();
    RecruitCollectResetHooks();
}

TEST(RecruitOffer, Mode4BribeAcceptMutatesLoyalty) {
    using namespace guild::gui;
    guild::sim::ResetEntityArrays();
    WinHarness::Reset();
    // handler: byte186 == 1 -> mode 4 ; loyalty 0, threshold 3.
    WinHarness::handler = HeHandler{};
    WinHarness::handler.entityId = 100;       // matches em entity id
    WinHarness::handler.byte186 = 1;          // offer pending -> mode 4
    WinHarness::handler.byte185 = 3;          // threshold
    WinHarness::handler.byte184 = 0;          // loyalty
    WinHarness::handler.dword47[0] = 4;       // offer-0 good count
    WinHarness::handlerPtr = &WinHarness::handler;

    RecruitWindowHooks h = MakeWinHooks();
    // make the first button object id == the clicked object so slot 0 is selected.
    h.formGetChildObjectId = [](int, int, int) { return 555; };
    h.dialogCheckResourceAmount = [](int, int) { return 1; }; // resources OK
    RecruitWindowSetHooks(h);

    WinHarness::clicks[0] = 555;    // clicked == button 0
    WinHarness::clickN = 1;
    WinHarness::frames = 2;

    u16 em[4] = { 0, 6, 100, 0 };   // kind 6, entity id 100
    u16 ref = 0;
    i8 r = RecruitRunRecruitmentOfferWindow(em, &ref, 1);
    CHECK_EQ((int)r, 2);
    // bonus count = rand(3)+1 = 1 (rand->0); loyalty 0 -> 1.
    CHECK_EQ((int)WinHarness::handler.byte184, 1);
    CHECK_EQ(WinHarness::cmd15Calls, 1);       // bribe good shipped
    CHECK_EQ(WinHarness::messageBoxes, 1);     // accept message
    CHECK_EQ((int)WinHarness::handler.byte186, 0); // offer cleared
    CHECK_EQ(WinHarness::gameTimeAdvances, 0); // 1 < threshold 3 -> no advance
    RecruitWindowResetHooks();
}

TEST(RecruitOffer, Mode4BribeMeetsThresholdAdvancesTime) {
    using namespace guild::gui;
    guild::sim::ResetEntityArrays();
    WinHarness::Reset();
    WinHarness::handler = HeHandler{};
    WinHarness::handler.entityId = 100;
    WinHarness::handler.byte186 = 1;          // mode 4
    WinHarness::handler.byte185 = 1;          // threshold 1
    WinHarness::handler.byte184 = 0;
    WinHarness::handler.dword47[0] = 4;
    WinHarness::handlerPtr = &WinHarness::handler;

    RecruitWindowHooks h = MakeWinHooks();
    h.formGetChildObjectId = [](int, int, int) { return 555; };
    h.dialogCheckResourceAmount = [](int, int) { return 1; };
    RecruitWindowSetHooks(h);

    WinHarness::clicks[0] = 555;
    WinHarness::clickN = 1;
    WinHarness::frames = 2;

    u16 em[4] = { 0, 6, 100, 0 };
    u16 ref = 0;
    (void)RecruitRunRecruitmentOfferWindow(em, &ref, 1);
    CHECK_EQ((int)WinHarness::handler.byte184, 1); // 0 + 1
    CHECK_EQ(WinHarness::gameTimeAdvances, 1);     // 1 >= threshold 1
    RecruitWindowResetHooks();
}

TEST(RecruitOffer, Mode4DeclineClearsOfferAndMessages) {
    using namespace guild::gui;
    guild::sim::ResetEntityArrays();
    WinHarness::Reset();
    WinHarness::handler = HeHandler{};
    WinHarness::handler.entityId = 100;
    WinHarness::handler.byte186 = 1;          // mode 4
    WinHarness::handlerPtr = &WinHarness::handler;

    static int callCount; callCount = 0;
    RecruitWindowHooks h = MakeWinHooks();
    // 4th button (decline) gets a distinct id; first three differ from the click.
    h.formGetChildObjectId = [](int, int, int) {
        // ids: 0->10, 1->11, 2->12, 3->13.
        return 10 + (callCount++);
    };
    RecruitWindowSetHooks(h);

    WinHarness::clicks[0] = 13;     // clicked == decline button (slot 3)
    WinHarness::clickN = 1;
    WinHarness::frames = 2;

    u16 em[4] = { 0, 6, 100, 0 };
    u16 ref = 0;
    (void)RecruitRunRecruitmentOfferWindow(em, &ref, 1);
    CHECK_EQ((int)WinHarness::handler.byte186, 0); // offer cleared on decline
    CHECK_EQ(WinHarness::messageBoxes, 1);          // decline message
    CHECK_EQ(WinHarness::cmd15Calls, 0);            // no good shipped
    RecruitWindowResetHooks();
}

// 0x55e241..0x55e39d — RNG-order golden. The original ALWAYS draws RandomModulo(3)
// first (0x55e241), then a second draw for the flavor string, regardless of the
// tier branch — even when tier>0 overwrites the count with the clamped tier. This
// pins exactly TWO draws and the clamped-tier count for a tier>=3 case.
//   tier = (goodType[0] - 5 - candRank)/2 + 1
//        = (20 - 5 - (-1))/2 + 1 = 16/2 + 1 = 9  -> clamp count to 3.
// Draws: rnd(3) [discarded count], rnd(3) [strIndex, tier>=3 arm]. Total 2.
TEST(RecruitOffer, Mode4BribeRngOrderAlwaysDrawsThrice) {
    using namespace guild::gui;
    guild::sim::ResetEntityArrays();
    WinHarness::Reset();
    WinHarness::handler = HeHandler{};
    WinHarness::handler.entityId   = 100;
    WinHarness::handler.byte186    = 1;     // mode 4
    WinHarness::handler.byte185    = 100;   // high threshold (no time advance)
    WinHarness::handler.byte184    = 0;     // loyalty
    WinHarness::handler.dword47[0] = 4;     // offer-0 good count
    WinHarness::handler.byte188[0] = 20;    // offer-0 good TYPE -> drives tier
    WinHarness::handlerPtr = &WinHarness::handler;

    RecruitWindowHooks h = MakeWinHooks();
    h.formGetChildObjectId      = [](int, int, int) { return 555; };
    h.dialogCheckResourceAmount = [](int, int) { return 1; };
    h.personComputeOfficeRank   = [](int) { return 0; };   // candRank = -1
    h.randomModulo              = &WinHarness::CountRng;
    RecruitWindowSetHooks(h);

    WinHarness::clicks[0] = 555;   // clicked == button 0 (slot 0 -> bribe)
    WinHarness::clickN = 1;
    WinHarness::frames = 2;

    u16 em[4] = { 0, 6, 100, 0 };
    u16 ref = 0;
    (void)RecruitRunRecruitmentOfferWindow(em, &ref, 1);

    // Exactly two RNG draws: rnd(3) discarded, then rnd(3) for the strIndex.
    CHECK_EQ(WinHarness::rngDraws, 2);
    CHECK_EQ(WinHarness::rngArgs[0], 3);   // ALWAYS rnd(3) first (0x55e241)
    CHECK_EQ(WinHarness::rngArgs[1], 3);   // tier>=3 -> rnd(3)+12 (0x55e398)
    // count = clamped tier = 3 ; loyalty 0 -> 3.
    CHECK_EQ((int)WinHarness::handler.byte184, 3);
    RecruitWindowResetHooks();
}
