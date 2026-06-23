// ===========================================================================
// Unit tests for src/play/session_select.{h,cpp}.
//   VIBE_Object_ResolveQuickJumpContact (0x4b950c — the set-selection commit),
//   VIBE_Selection_ClearAll (0x4b94d8), the 0x4bc280 status-text latch tail,
//   and the SessionSelect layer (pick -> info -> clear) over synthetic rosters.
// Self-contained: coupled subsystems supplied as in-test hooks; the deselect
// drives the REAL play::Selection_Reset (its invariants are golden-tested in
// input_recon_select_test.cpp — here we drive them through the new layer).
// ===========================================================================
#include "tests/framework/test.h"
#include "play/session_select.h"
#include "gui/text/textdb.h"

#include <cstring>
#include <string>

using namespace guild;
using namespace guild::play;

namespace {

// Reset every module global the commit reads/writes between tests.
void ResetAllSelectState() {
    g_selectionContact = SelectionContactLatch{};
    g_selectionAnchorRecords = SelectionAnchorRecords{};
    g_selectionCommit = SelectionCommitState{};
    g_selectionOwners = SelectionOwnerRecords{};
    g_quickJump = QuickJumpRequest{};
    g_selectGate = SelectGateState{};
    g_selectionStatus = SelectionStatusLatch{};
    g_selectionAnchors = SelectionAnchors{};
    g_selectionOwnerA = 0;
    g_selectionOwnerB = nullptr;
    g_quickJumpError[0] = 0;
    Selection_SetCommitHooks(SelectCommitHooks{});
    Selection_SetStatusLatchHooks(StatusLatchHooks{});
    Selection_SetResetHooks(SelectionResetHooks{});
}

// Open the cursor gate (cursor strictly inside the viewport rect, no modal).
void OpenGate() {
    g_selectGate.g67221C = 1;
    g_selectGate.g62D4E8 = 0;
    g_selectGate.cursorX16 = 100 << 16;
    g_selectGate.cursorY16 = 100 << 16;
    g_selectGate.g63CC4C = 0;
    g_selectGate.g63CC50 = 0;
    g_selectGate.g63CC54 = 640;
    g_selectGate.g63CC58 = 480;
    g_selectGate.g75BF08 = -1;
    g_selectGate.g62D31C = -1;
}

struct CommitCounters {
    int boneTransforms = 0;
    int highlightHandlers = 0;
    int voiceComments = 0;
    int sweeps = 0;
    int marks = 0;
    u16 lastMarkedId = 0;
    int resets = 0;
};

CommitCounters g_ctr;

void InstallCountingHooks(u8 contactTypeByte, u16 selectionFlags,
                          u8* meshSlot = nullptr) {
    g_ctr = CommitCounters{};
    SelectCommitHooks h;
    h.objectTypeByte = [contactTypeByte](int) -> u8 { return contactTypeByte; };
    h.computeSelectionFlags = [selectionFlags](u16, u8*, u8*, u8*) -> u16 {
        return selectionFlags;
    };
    h.applyBoneTransform = [](u8*) { ++g_ctr.boneTransforms; };
    h.invokeHighlightHandler = [](u8*) { ++g_ctr.highlightHandlers; };
    h.voiceWorkerClickComment = [](u8*, u16) { ++g_ctr.voiceComments; };
    h.clearWorkerSelectionMarks = []() { ++g_ctr.sweeps; };
    h.markWorkerSelected = [](u16 id) { ++g_ctr.marks; g_ctr.lastMarkedId = id; };
    h.workerMeshPtr = [meshSlot](u16) -> u8* { return meshSlot; };
    h.selectionReset = []() {
        ++g_ctr.resets;
        Selection_Reset(0);
        g_selectionAnchorRecords = SelectionAnchorRecords{};
    };
    Selection_SetCommitHooks(h);
}

} // namespace

// ---------------------------------------------------------------------------
// 0x4b950c — object pick commits the anchors + side state.
// ---------------------------------------------------------------------------
TEST(SessionSelectCommit, ObjectPickSetsAnchors) {
    ResetAllSelectState();
    OpenGate();
    InstallCountingHooks(/*typeByte=*/2, /*flags=*/0);

    u8 obj[540] = {0};
    std::strcpy(reinterpret_cast<char*>(obj), "ob_BRUNNEN");
    obj[529] = 1; // highlightable
    u8 contact[64] = {0};
    *reinterpret_cast<i16*>(contact) = 7;

    g_selectionContact.g631724 = obj;
    g_selectionContact.g63172C = contact;
    g_selectionContact.g631730 = 1;
    g_selectionCommit.g631610 = 42;

    Selection_CommitContact();

    CHECK(g_selectionCommit.g631720 == obj);     // 0x4b96ae
    CHECK(g_selectionCommit.g11BC2F0 == contact);
    CHECK_EQ(g_selectionCommit.g11BC2F4, 1);
    CHECK(g_selectionCommit.g631E50 == obj);     // 0x4b976f
    CHECK(g_selectionAnchorRecords.a631740 == obj);
    CHECK(g_selectionAnchorRecords.a11BC274 == contact);
    CHECK_EQ(g_selectionAnchorRecords.a11BC278, 1);
    // highlight pulse fired (bit +529, not door-like)
    CHECK_EQ(g_ctr.boneTransforms, 1);
    CHECK_EQ(g_ctr.highlightHandlers, 1);
    CHECK(g_selectionCommit.g631728 == obj);     // 0x4b9729
    CHECK_EQ(g_selectionCommit.g63161C, 42);     // 0x4b975f frame latch
    CHECK_EQ(g_ctr.resets, 0);
}

// ---------------------------------------------------------------------------
// 0x4b950c — door-like targets ("tp_TUER" name / contact type 6 / a door
// record in 63173C) suppress the highlight pulse.
// ---------------------------------------------------------------------------
TEST(SessionSelectCommit, DoorSuppressesHighlight) {
    // (a) the "tp_TUER" name compare (VIBE_Util_StrCmp @0x5d3f10)
    ResetAllSelectState();
    OpenGate();
    InstallCountingHooks(2, 0);
    u8 door[540] = {0};
    std::strcpy(reinterpret_cast<char*>(door), "tp_TUER");
    door[529] = 1;
    g_selectionContact.g631724 = door;
    Selection_CommitContact();
    CHECK_EQ(g_ctr.boneTransforms, 0);
    CHECK_EQ(g_ctr.highlightHandlers, 0);
    CHECK(g_selectionCommit.g631728 == nullptr);

    // (b) contact type byte 6
    ResetAllSelectState();
    OpenGate();
    InstallCountingHooks(/*typeByte=*/6, 0);
    u8 obj[540] = {0};
    std::strcpy(reinterpret_cast<char*>(obj), "ob_X");
    obj[529] = 1;
    u8 contact[64] = {0};
    g_selectionContact.g631724 = obj;
    g_selectionContact.g63172C = contact;
    Selection_CommitContact();
    CHECK_EQ(g_ctr.boneTransforms, 0);

    // (c) a committed door record (dword_631738 from the latch's 63173C)
    ResetAllSelectState();
    OpenGate();
    InstallCountingHooks(2, 0);
    u8 obj2[540] = {0};
    std::strcpy(reinterpret_cast<char*>(obj2), "ob_Y");
    obj2[529] = 1;
    u8 doorRec[64] = {0};
    g_selectionContact.g631724 = obj2;
    g_selectionContact.g63173C = doorRec;
    Selection_CommitContact();
    CHECK(g_selectionCommit.g631738 == doorRec);
    CHECK_EQ(g_ctr.boneTransforms, 0);
}

// ---------------------------------------------------------------------------
// 0x4b950c — empty click (no hover latch) routes the REAL Selection_Reset:
// the four anchors clear and the person-query owner resolution runs (the
// invariants already golden-tested in input_recon_select_test).
// ---------------------------------------------------------------------------
TEST(SessionSelectCommit, EmptyClickRoutesSelectionReset) {
    ResetAllSelectState();
    OpenGate();

    // Default hooks (DefaultSelectionReset -> play::Selection_Reset(0)).
    int personQueries = 0;
    SelectionResetHooks rh;
    rh.personQueryBegin = [&personQueries](int) -> u8* {
        ++personQueries;
        return nullptr;
    };
    Selection_SetResetHooks(rh);

    g_selectionAnchors.g11BC274 = 11;
    g_selectionAnchors.g11BC278 = 22;
    g_selectionAnchors.g11BC260 = 33;
    g_selectionAnchors.g631740 = 44;
    u8 stale[8] = {0};
    g_selectionAnchorRecords.a631740 = stale;

    Selection_CommitContact();   // latch empty -> 0x4b9999 Selection_Reset

    CHECK_EQ(g_selectionAnchors.g11BC274, 0);
    CHECK_EQ(g_selectionAnchors.g11BC278, 0);
    CHECK_EQ(g_selectionAnchors.g11BC260, 0);
    CHECK_EQ(g_selectionAnchors.g631740, 0);
    CHECK(g_selectionAnchorRecords.a631740 == nullptr);  // pointer dual view
    CHECK_EQ(personQueries, 1);  // owner resolution ran (631744/631748 unset)
}

// 0x4b9993 — the build-mode byte byte_6317B4 suppresses the empty-click reset.
TEST(SessionSelectCommit, EmptyClickSkippedWhenBuildMode) {
    ResetAllSelectState();
    OpenGate();
    InstallCountingHooks(0, 0);
    g_selectionAnchors.g6317B4 = 1;
    g_selectionAnchors.g631740 = 44;
    Selection_CommitContact();
    CHECK_EQ(g_ctr.resets, 0);
    CHECK_EQ(g_selectionAnchors.g631740, 44);  // untouched
}

// ---------------------------------------------------------------------------
// 0x4b995a — the cursor/modal gate rejects out-of-rect or modal clicks
// (the entry stores still happen — they are above the gate).
// ---------------------------------------------------------------------------
TEST(SessionSelectCommit, GateRejects) {
    ResetAllSelectState();
    InstallCountingHooks(2, 0);
    u8 obj[540] = {0};
    obj[529] = 1;
    g_selectionContact.g631724 = obj;

    OpenGate();
    g_selectGate.cursorX16 = 700 << 16;   // outside g63CC54=640
    u8 staleF0[4] = {0};
    g_selectionCommit.g11BC2F0 = staleF0; // must still be cleared (0x4b9531)
    Selection_CommitContact();
    CHECK(g_selectionCommit.g11BC2F0 == nullptr);
    CHECK(g_selectionCommit.g631720 == nullptr);   // no commit
    CHECK(g_selectionAnchorRecords.a631740 == nullptr);
    CHECK_EQ(g_ctr.resets, 0);                     // and no reset either

    OpenGate();
    g_selectGate.g75BF08 = 5;             // modal widget
    Selection_CommitContact();
    CHECK(g_selectionCommit.g631720 == nullptr);
}

// ---------------------------------------------------------------------------
// 0x4b977c..0x4b983f — worker (person) selection: flags hi-byte bit 3 marks
// the worker, sets dword_6317B0/11BC270, plays the click voice, and stores the
// worker mesh twice (the second store is unconditional, exactly as compiled).
// ---------------------------------------------------------------------------
TEST(SessionSelectCommit, WorkerSelectMarks) {
    ResetAllSelectState();
    OpenGate();
    u8 meshSlot = 0;
    InstallCountingHooks(0, /*flags=*/0x0800, &meshSlot);

    u8 obj[540] = {0};
    std::strcpy(reinterpret_cast<char*>(obj), "ch_MESH");
    u8 person[540] = {0};
    *reinterpret_cast<u16*>(person) = 17;  // worker id
    person[8] = 1;                          // active byte (+8)
    g_selectionContact.g631724 = obj;
    g_selectionContact.g631734 = person;

    Selection_CommitContact();

    CHECK_EQ(g_ctr.sweeps, 1);             // 0x4b97ab clear sweep
    CHECK_EQ(g_ctr.marks, 1);              // 0x4b97e9 byte_12CEA98[536*17]=1
    CHECK_EQ((int)g_ctr.lastMarkedId, 17);
    CHECK_EQ(g_selectionCommit.g6317B0, 1);
    CHECK(g_selectionCommit.g11BC270 == person);
    CHECK_EQ(g_ctr.voiceComments, 1);      // 0x4b981d
    CHECK(g_selectionCommit.g62D098 == &meshSlot); // 0x4b983f second store
    // no contact flag -> the object anchor stays empty for a plain person
    CHECK(g_selectionAnchorRecords.a631740 == nullptr);
}

// flags hi-byte without bit 3: sweep still runs, no mark/voice, the
// unconditional mesh store still happens.
TEST(SessionSelectCommit, WorkerFlagsLowNoMark) {
    ResetAllSelectState();
    OpenGate();
    u8 meshSlot = 0;
    InstallCountingHooks(0, /*flags=*/0x0100, &meshSlot);
    u8 obj[540] = {0};
    u8 person[540] = {0};
    *reinterpret_cast<u16*>(person) = 3;
    person[8] = 1;
    g_selectionContact.g631724 = obj;
    g_selectionContact.g631734 = person;
    Selection_CommitContact();
    CHECK_EQ(g_ctr.sweeps, 1);
    CHECK_EQ(g_ctr.marks, 0);
    CHECK_EQ(g_ctr.voiceComments, 0);
    CHECK_EQ(g_selectionCommit.g6317B0, 0);
    CHECK(g_selectionCommit.g11BC270 == nullptr);
    CHECK(g_selectionCommit.g62D098 == &meshSlot); // unconditional tail store
}

// ---------------------------------------------------------------------------
// 0x4b98a2..0x4b98ef — a committed contact of object-type 29 voids the
// selection again (street/decor objects are unselectable).
// ---------------------------------------------------------------------------
TEST(SessionSelectCommit, Type29ContactClears) {
    ResetAllSelectState();
    OpenGate();
    InstallCountingHooks(/*typeByte=*/29, 0);
    u8 obj[540] = {0};
    u8 contact[64] = {0};
    g_selectionContact.g631724 = obj;
    g_selectionContact.g63172C = contact;
    g_selectionContact.g631730 = 1;
    Selection_CommitContact();
    CHECK(g_selectionAnchorRecords.a11BC274 == contact); // anchor still latched
    CHECK(g_selectionAnchorRecords.a631740 == nullptr);  // ...but selection void
    CHECK(g_selectionCommit.g11BC270 == nullptr);
    CHECK_EQ(g_selectionCommit.g6317B0, 0);
    CHECK_EQ(g_ctr.sweeps, 1);                           // 0x4b98d9 sweep
}

// ---------------------------------------------------------------------------
// 0x4b955f..0x4b9609 — a pending QuickJump request forces the commit even when
// the cursor gate fails, resolving the contact by handle name.
// ---------------------------------------------------------------------------
TEST(SessionSelectCommit, QuickJumpForcesCommit) {
    ResetAllSelectState();
    // gate CLOSED (g67221C = 0)
    static u8 resolved[540];
    std::memset(resolved, 0, sizeof resolved);
    std::strcpy(reinterpret_cast<char*>(resolved), "ob_KONTOR");

    InstallCountingHooks(2, 0);
    SelectCommitHooks h;  // re-install with a resolver on top
    h.objectTypeByte = [](int) -> u8 { return 2; };
    h.objectFindByHandle = [](const char* name) -> u8* {
        return std::strcmp(name, "ob_KONTOR") == 0 ? resolved : nullptr;
    };
    Selection_SetCommitHooks(h);

    u8 owner[16] = {0};
    g_selectionOwnerB = owner;            // dword_631748 (the v2 fallback)
    g_quickJump.g11BC27C = 1;
    g_quickJump.g11BC280 = owner;
    g_quickJump.g11BC284 = nullptr;       // room unset -> second match arm
    std::strcpy(g_quickJump.name, "ob_KONTOR");

    Selection_CommitContact();

    CHECK_EQ(g_quickJump.g11BC27C, 0);                    // request consumed
    CHECK(g_selectionContact.g631724 == resolved);        // 0x4b9602
    CHECK(g_selectionCommit.g631720 == resolved);         // forced commit ran
    CHECK(g_selectionCommit.g631E50 == resolved);

    // unresolvable name -> error string formatted (the original's v12 buffer)
    ResetAllSelectState();
    Selection_SetCommitHooks(h);
    g_selectionOwnerB = owner;
    g_quickJump.g11BC27C = 1;
    g_quickJump.g11BC280 = owner;
    std::strcpy(g_quickJump.name, "ob_FEHLT");
    Selection_CommitContact();
    CHECK(std::strstr(g_quickJumpError, "ob_FEHLT") != nullptr);
    CHECK_EQ(g_quickJump.g11BC27C, 0);
}

// 0x4b954d — a latched worker whose record's alive byte (+392) dropped is discarded.
TEST(SessionSelectCommit, StaleWorkerDropped) {
    ResetAllSelectState();
    u8 dead[540] = {0};      // +392 == 0
    g_selectionCommit.g11BC270 = dead;
    Selection_CommitContact();   // gate closed; the staleness check is above it
    CHECK(g_selectionCommit.g11BC270 == nullptr);

    u8 alive[540] = {0};
    alive[392] = 1;
    g_selectionCommit.g11BC270 = alive;
    Selection_CommitContact();
    CHECK(g_selectionCommit.g11BC270 == alive);
}

// ---------------------------------------------------------------------------
// 0x4b94d8 — VIBE_Selection_ClearAll.
// ---------------------------------------------------------------------------
TEST(SessionSelectClearAll, ClearsWorkerSelection) {
    ResetAllSelectState();
    InstallCountingHooks(0, 0);
    u8 person[540] = {0};
    u8 obj[8] = {0};
    g_selectionCommit.g11BC270 = person;
    g_selectionCommit.g6317B0 = 1;
    g_selectionAnchorRecords.a631740 = obj;
    g_selectionAnchors.g631740 = 1;
    const i32 r = Selection_ClearAll();
    CHECK_EQ(r, 411648);                       // the loop's final counter (eax)
    CHECK(g_selectionCommit.g11BC270 == nullptr);
    CHECK_EQ(g_selectionCommit.g6317B0, 0);
    CHECK(g_selectionAnchorRecords.a631740 == nullptr);
    CHECK_EQ(g_selectionAnchors.g631740, 0);
    CHECK_EQ(g_ctr.sweeps, 1);
}

// ---------------------------------------------------------------------------
// 0x4bc280 tail — the status-text latch fires once per owner change.
// ---------------------------------------------------------------------------
TEST(SessionSelectStatus, LatchFiresOncePerChange) {
    ResetAllSelectState();
    int resets = 0;
    StatusLatchHooks sh;
    sh.computeSelectionFlags = [](u16, u8*, u8*, u8*) -> u16 { return 0x1234; };
    sh.statusTextReset = [&resets]() { ++resets; };
    Selection_SetStatusLatchHooks(sh);

    u8 owner[8] = {0};
    g_selectionOwners.g631744 = owner;
    CHECK(Selection_UpdateStatusTextLatch());            // change -> fires
    CHECK_EQ(resets, 1);
    CHECK(g_selectionStatus.g631E54 == owner);           // 0x4bc501
    CHECK_EQ((int)(u16)g_selectionStatus.w631758, 0x1234); // 0x4bc4f1
    CHECK(!Selection_UpdateStatusTextLatch());           // same owner -> no fire
    CHECK_EQ(resets, 1);

    g_selectionStatus.g631754 = 1;                       // force flag
    CHECK(Selection_UpdateStatusTextLatch());
    CHECK_EQ(resets, 2);
    CHECK_EQ(g_selectionStatus.g631754, 0);              // 0x4bc507

    g_selectionOwners.g631744 = nullptr;                 // deselect (owner -> 0)
    CHECK(Selection_UpdateStatusTextLatch());
    CHECK_EQ(resets, 3);
    CHECK(g_selectionStatus.g631E54 == nullptr);
}

// ---------------------------------------------------------------------------
// SessionSelect flow over a synthetic roster: select -> info -> deselect.
// ---------------------------------------------------------------------------
namespace {

gui::text::TextDb MakeKindNameDb(int typeCode, const char* kindName) {
    gui::text::TextDb db;
    const int want = kSelectKindNameBias + kSelectKindNameStride * typeCode;
    for (int i = 0; i < want; ++i) db.Add("", "", 0);
    db.Add(kindName, "kindname", 0);
    return db;
}

} // namespace

TEST(SessionSelectFlow, PickInfoClear) {
    ResetAllSelectState();
    gui::text::TextDb db = MakeKindNameDb(/*typeCode=*/3, "Brunnen");
    SessionSelect sel(&db);

    SessionSelectEntry roster[2];
    roster[0].id = 901; roster[0].kind = 1; roster[0].typeCode = 3;
    roster[0].handle = "ob_BRUNNEN"; roster[0].name = "";
    roster[0].screenX = 320.0f; roster[0].screenY = 200.0f;
    roster[1].id = 902; roster[1].kind = 3; roster[1].name = "Hilde";

    ScenePickResult hit; hit.index = 0; hit.id = 901; hit.screenDist = 4.0f;
    sel.OnPick(hit, roster, 2, 320.0f, 200.0f, 24.0f);

    SessionSelect::Info info = sel.current();
    CHECK(info.has);
    CHECK_EQ(info.id, 901);
    CHECK_EQ(info.kind, 1);
    // no custom name -> the object-kind name from the REAL text key 1078+14*3
    CHECK_EQ(std::string(info.name), std::string("Brunnen"));

    SessionSelect::Highlight h = sel.highlight();
    CHECK(h.has);
    CHECK_EQ(h.id, 901);
    CHECK_EQ((int)h.screenX, 320);
    CHECK_EQ((int)h.screenY, 200);
    CHECK_EQ((int)h.radius, 24);

    // right-click / ESC: the real deselect
    sel.Clear();
    CHECK(!sel.current().has);
    CHECK(!sel.highlight().has);
    CHECK(g_selectionAnchorRecords.a631740 == nullptr);
    CHECK_EQ(g_selectionAnchors.g631740, 0);   // Selection_Reset really ran
    CHECK_EQ(g_selectionAnchors.g11BC274, 0);
    CHECK_EQ(g_selectionCommit.g6317B0, 0);    // Selection_ClearAll really ran
}

TEST(SessionSelectFlow, EmptyClickDeselects) {
    ResetAllSelectState();
    SessionSelect sel(nullptr);
    SessionSelectEntry roster[1];
    roster[0].id = 901; roster[0].kind = 1; roster[0].typeCode = 3;

    ScenePickResult hit; hit.index = 0; hit.id = 901;
    sel.OnPick(hit, roster, 1, 100.0f, 100.0f, 24.0f);
    CHECK(sel.current().has);

    ScenePickResult miss; miss.index = -1; miss.id = 0;
    sel.OnPick(miss, roster, 1, 50.0f, 50.0f, 24.0f);   // empty ground click
    CHECK(!sel.current().has);                          // 0x4b9999 reset path
    CHECK(g_selectionAnchorRecords.a631740 == nullptr);
    CHECK_EQ(g_selectionAnchors.g631740, 0);
}

TEST(SessionSelectFlow, PersonSelectAndName) {
    ResetAllSelectState();
    SessionSelect sel(nullptr);
    SessionSelectEntry roster[1];
    roster[0].id = 17; roster[0].kind = 3; roster[0].name = "Hilde";
    roster[0].selectionFlags = 0x0800;   // own worker -> selectable
    roster[0].screenX = 64.0f; roster[0].screenY = 48.0f;

    ScenePickResult hit; hit.index = 0; hit.id = 17;
    sel.OnPick(hit, roster, 1, 64.0f, 48.0f, 24.0f);

    SessionSelect::Info info = sel.current();
    CHECK(info.has);
    CHECK_EQ(info.kind, 3);
    // person name via FormatItemLabelWithIcon kind=4 (record name at +48):
    // plain person (no job bytes) emits "%s" of the +48 field.
    CHECK_EQ(std::string(info.name), std::string("Hilde"));
    CHECK(sel.workerSelected(17));       // byte_12CEA98[536*17] mark
    CHECK_EQ(g_selectionCommit.g6317B0, 1);

    sel.Clear();
    CHECK(!sel.workerSelected(17));      // the 0x4b94d8 sweep cleared the mark
}

TEST(SessionSelectFlow, ForeignWorkerNotMarked) {
    ResetAllSelectState();
    SessionSelect sel(nullptr);
    SessionSelectEntry roster[1];
    roster[0].id = 21; roster[0].kind = 3; roster[0].name = "Knecht";
    roster[0].selectionFlags = 0x0000;   // foreign -> hi-byte bit 3 clear

    ScenePickResult hit; hit.index = 0; hit.id = 21;
    sel.OnPick(hit, roster, 1, 10.0f, 10.0f, 24.0f);
    CHECK(!sel.workerSelected(21));
    CHECK(!sel.current().has);           // nothing stuck (no anchor, no worker)
}

TEST(SessionSelectFlow, CustomNamePreferred) {
    ResetAllSelectState();
    gui::text::TextDb db = MakeKindNameDb(/*typeCode=*/5, "Werkstatt");
    SessionSelect sel(&db);
    SessionSelectEntry roster[1];
    roster[0].id = 31; roster[0].kind = 1; roster[0].typeCode = 5;
    roster[0].name = "Meisterei Anno";   // record+5 custom name

    ScenePickResult hit; hit.index = 0; hit.id = 31;
    sel.OnPick(hit, roster, 1, 10.0f, 10.0f, 24.0f);
    CHECK_EQ(std::string(sel.current().name), std::string("Meisterei Anno"));
}

TEST(SessionSelectFlow, Type29EntryRejected) {
    ResetAllSelectState();
    SessionSelect sel(nullptr);
    SessionSelectEntry roster[1];
    roster[0].id = 41; roster[0].kind = 1; roster[0].typeCode = 29; // decor/street

    ScenePickResult hit; hit.index = 0; hit.id = 41;
    sel.OnPick(hit, roster, 1, 10.0f, 10.0f, 24.0f);
    CHECK(!sel.current().has);           // 0x4b98c7 type-29 void
    CHECK(!sel.highlight().has);
}
