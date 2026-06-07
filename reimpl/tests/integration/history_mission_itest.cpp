#include "test.h"

// Integration: drive history_mission's MissionRunSpecialDialog driver against the
// REAL reconstructed descriptor-lookup sibling (world/mission_rules.cpp's
// MissionFindDescriptorByValue, gilde.exe 0x5387f1) scanning the REAL shared
// descriptor table (world/event.cpp's g_eventTable / g_eventTableCount,
// dword_63CD48). NOT a mock: the driver's name-text-id is computed from the
// descriptor index the genuine lookup returns over the genuine table, and we
// install a recording renderText hook to observe exactly the id (value+1) the
// real lookup drove. The MissionDialogHooks Form/Voice/FrameLoop leaves have no
// reconstructed sibling (engine GUI glue), so they are recording/scripted fakes;
// the descriptor decision is the real sibling end to end.
#include "world/history_mission.h"
#include "world/mission_rules.h"   // REAL sibling: MissionFindDescriptorByValue
#include "world/event.h"           // REAL table: g_eventTable / g_eventTableCount

#include <vector>

using namespace guild;
using namespace guild::world;

namespace {
// Recording fake for the engine GUI/voice/frame leaves (no reconstructed sibling).
struct DialogRec {
    int  form = 0;
    bool centered = false, selected = false, voicePlayed = false, destroyed = false;
    std::vector<unsigned> rendered;   // every renderText id, in order
    int  framesLeft = 0;              // RunFrameLoop returns nonzero this many times
    MissionDialogFrame frame;         // the snapshot readFrame publishes each tick
} g_rec;

int  HCreateForm(const char*)                 { return (g_rec.form = 7777); }
void HCenter(int)                             { g_rec.centered = true; }
void HSelect(int, int)                        { g_rec.selected = true; }
int  HRenderText(unsigned id)                 { g_rec.rendered.push_back(id); return 0; }
void HVoice(int, int, const char*)            { g_rec.voicePlayed = true; }
void HDestroy(int)                            { g_rec.destroyed = true; }
int  HRunFrame(int, int)                      { return g_rec.framesLeft-- > 0 ? 1 : 0; }
void HReadFrame(MissionDialogFrame* out)      { if (out) *out = g_rec.frame; }

MissionDialogHooks MakeHooks() {
    MissionDialogHooks h{};   // all-null inert
    h.createForm  = &HCreateForm;
    h.centerChildWindows = &HCenter;
    h.selectWindow = &HSelect;
    h.renderText  = &HRenderText;
    h.playVoice   = &HVoice;
    h.runFrameLoop = &HRunFrame;
    h.destroyForm = &HDestroy;
    h.readFrame   = &HReadFrame;
    return h;
}

// Stamp a known descriptor at index `idx` with subtype byte `value`.
void SetDescriptor(int idx, u8 value, u8 category) {
    g_eventTable[idx].word0    = idx;
    g_eventTable[idx].value    = value;
    g_eventTable[idx].category = category;
}
} // namespace

// The driver scans the REAL table via the REAL lookup; the matched descriptor's
// value+1 is the first rendered text id. Player confirms (1210) -> driver returns 1.
TEST(HistoryMissionItest, SpecialDialogUsesRealDescriptorLookup) {
    EventTableReset();
    SetDescriptor(0, 17, 1);
    SetDescriptor(1, 42, 2);    // the subtype we run
    SetDescriptor(2, 99, 3);
    g_eventTableCount = 3;

    // Cross-check: the real sibling locates subtype 42 at index 1.
    int idx = MissionFindDescriptorByValue(42);
    CHECK_EQ(idx, 1);

    g_rec = DialogRec{};
    g_rec.framesLeft = 1;
    g_rec.frame.lastDialogResult = kMissionDialogAccept;   // 1210 -> confirm
    MissionDialogHooks h = MakeHooks();
    SetMissionDialogHooks(&h);

    int rc = MissionRunSpecialDialog(/*value=*/42, /*frameArg=*/0);

    CHECK_EQ(rc, 1);                       // accept (1210) -> confirmed
    CHECK(g_rec.centered);
    CHECK(g_rec.selected);
    CHECK(g_rec.voicePlayed);
    CHECK(g_rec.destroyed);
    // First render is the descriptor name id = value+1 (43), driven by the real lookup.
    CHECK(g_rec.rendered.size() >= 3);
    if (g_rec.rendered.size() >= 3) {
        CHECK_EQ(g_rec.rendered[0], 43u);  // g_eventTable[1].value + 1
        CHECK_EQ(g_rec.rendered[1], 0x7Du);
        CHECK_EQ(g_rec.rendered[2], 0x7Eu);
    }

    SetMissionDialogHooks(nullptr);
}

// A subtype absent from the REAL table => the real lookup returns -1 => the driver
// renders name-id 0 (the inert no-descriptor path). Decline (1155) -> returns 0.
TEST(HistoryMissionItest, SpecialDialogNoDescriptorRendersZero) {
    EventTableReset();
    SetDescriptor(0, 10, 1);
    g_eventTableCount = 1;

    CHECK_EQ(MissionFindDescriptorByValue(200), -1);   // real lookup miss

    g_rec = DialogRec{};
    g_rec.framesLeft = 1;
    g_rec.frame.lastDialogResult = kMissionDialogDecline;   // 1155 -> close, not confirmed
    MissionDialogHooks h = MakeHooks();
    SetMissionDialogHooks(&h);

    int rc = MissionRunSpecialDialog(/*value=*/200, 0);

    CHECK_EQ(rc, 0);                       // declined -> not confirmed
    CHECK(g_rec.rendered.size() >= 1);
    if (!g_rec.rendered.empty())
        CHECK_EQ(g_rec.rendered[0], 0u);   // no descriptor -> name id 0

    SetMissionDialogHooks(nullptr);
}

// The failure-dialog driver shares the same real lookup; with a match it renders
// value+1 then 0x7D, and the ack frame loop closes on the skip gate.
TEST(HistoryMissionItest, FailureDialogUsesRealDescriptorLookup) {
    EventTableReset();
    SetDescriptor(0, 5, 0);
    SetDescriptor(1, 88, 4);
    g_eventTableCount = 2;

    CHECK_EQ(MissionFindDescriptorByValue(88), 1);

    g_rec = DialogRec{};
    g_rec.framesLeft = 1;
    g_rec.frame.skipGate = 1;              // close gate -> ack step exits
    MissionDialogHooks h = MakeHooks();
    SetMissionDialogHooks(&h);

    int rc = MissionRunFailureDialog(/*value=*/88, 0);

    CHECK_EQ(rc, 0);                       // failure dialog returns inert 0
    CHECK(g_rec.destroyed);
    CHECK(g_rec.rendered.size() >= 2);
    if (g_rec.rendered.size() >= 2) {
        CHECK_EQ(g_rec.rendered[0], 89u);  // value 88 + 1
        CHECK_EQ(g_rec.rendered[1], 0x7Du);
    }

    SetMissionDialogHooks(nullptr);
}
