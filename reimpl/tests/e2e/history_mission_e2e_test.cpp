// E2E: drive the mission-dialog driver bodies (history_mission.cpp) across a full
// flow with installed fake hooks. Exercises the descriptor scan, the per-tick
// decode, and the create/render/frame-loop/destroy lifecycle for the Special,
// Failure and Info panels. No real engine — the hooks publish a scripted sequence
// of frame snapshots so the deterministic exit logic can be observed end-to-end.
#include "test.h"

#include <vector>

#include "world/event.h"
#include "world/history_mission.h"

using namespace guild;
using namespace guild::world;

namespace {

// Scripted engine fake. The frame loop walks a queue of MissionDialogFrame
// snapshots; each runFrameLoop() returns 1 until the queue is empty, advancing
// the snapshot the driver then reads.
struct FakeEngine {
    int created = 0, destroyed = 0, frames = 0;
    int lastForm = 100;
    std::vector<MissionDialogFrame> script;
    size_t cursor = 0;
    MissionDialogFrame current;
    bool voicePlaying = false;
    int  stopFade = 0;
    int  renderCount = 0;
};
FakeEngine* g_e = nullptr;

int   fakeCreate(const char*) { ++g_e->created; return g_e->lastForm; }
void  fakeCenter(int) {}
void  fakeSelect(int, int) {}
int   fakeRender(unsigned) { ++g_e->renderCount; return 1; }
int   fakeChild(int, int) { return 0; }
void  fakePlay(int, int, const char*) { g_e->voicePlaying = true; }
int   fakeAudioInit() { return 0; }
int   fakeVoicePlaying(int) { return g_e->voicePlaying ? 1 : 0; }
void  fakeStop(int fade) { g_e->stopFade = fade; g_e->voicePlaying = false; }
void  fakeDestroy(int) { ++g_e->destroyed; }
void  fakeRead(MissionDialogFrame* out) { *out = g_e->current; }
int   fakeRun(int, int) {
    ++g_e->frames;
    if (g_e->cursor >= g_e->script.size()) return 0;  // loop ends
    g_e->current = g_e->script[g_e->cursor++];
    return 1;
}

MissionDialogHooks MakeHooks() {
    MissionDialogHooks h;
    h.createForm = fakeCreate;
    h.centerChildWindows = fakeCenter;
    h.selectWindow = fakeSelect;
    h.renderText = fakeRender;
    h.getChildObjectId = fakeChild;
    h.playVoice = fakePlay;
    h.audioIsInitialized = fakeAudioInit;
    h.voiceIsPlaying = fakeVoicePlaying;
    h.stopVoice = fakeStop;
    h.runFrameLoop = fakeRun;
    h.destroyForm = fakeDestroy;
    h.readFrame = fakeRead;
    return h;
}

MissionDialogFrame Snap(i32 last, i32 click, i32 skip, u8 menu) {
    MissionDialogFrame f;
    f.lastDialogResult = last; f.clickedObjectId = click;
    f.skipGate = skip; f.menuState = menu;
    return f;
}

}  // namespace

// ---------------------------------------------------------------------------
// Special dialog: player clicks accept on the 3rd frame -> returns 1, voice
// stopped, form destroyed.
// ---------------------------------------------------------------------------
TEST(HistoryMissionE2E, SpecialAcceptFlow) {
    EventTableLoadDefault();   // populate descriptor table (count 48)
    FakeEngine eng;
    g_e = &eng;
    MissionDialogHooks hooks = MakeHooks();
    SetMissionDialogHooks(&hooks);

    // value 0 exists in the default image (row 0 value byte); scan resolves it.
    eng.script = {
        Snap(kMissionDialogNone, -1, 0, 0),     // idle
        Snap(kMissionDialogNone, -1, 0, 0),     // idle
        Snap(kMissionDialogAccept, -1, 0, 0),   // confirm -> exit, ret 1
        Snap(kMissionDialogNone, -1, 0, 0),     // (not reached)
    };

    int ret = MissionRunSpecialDialog(/*value*/ 0, /*frameArg*/ 42);

    CHECK_EQ(ret, 1);
    CHECK_EQ(eng.created, 1);
    CHECK_EQ(eng.destroyed, 1);
    CHECK_EQ(eng.frames, 3);          // looped 3 times then broke
    CHECK_EQ(eng.voicePlaying, false);// voice stopped on confirm
    CHECK_EQ(eng.stopFade, 25);
    CHECK(eng.renderCount > 0);

    SetMissionDialogHooks(nullptr);
    g_e = nullptr;
}

// ---------------------------------------------------------------------------
// Special dialog: player declines (1155) -> returns 0.
// ---------------------------------------------------------------------------
TEST(HistoryMissionE2E, SpecialDeclineFlow) {
    EventTableLoadDefault();
    FakeEngine eng;
    g_e = &eng;
    MissionDialogHooks hooks = MakeHooks();
    SetMissionDialogHooks(&hooks);

    eng.script = {
        Snap(kMissionDialogNone, -1, 0, 0),
        Snap(kMissionDialogDecline, -1, 0, 0),   // decline -> exit, ret 0
    };

    int ret = MissionRunSpecialDialog(0, 1);
    CHECK_EQ(ret, 0);
    CHECK_EQ(eng.frames, 2);
    CHECK_EQ(eng.destroyed, 1);

    SetMissionDialogHooks(nullptr);
    g_e = nullptr;
}

// ---------------------------------------------------------------------------
// Failure + Info ack panels: skip gate / accept close them.
// ---------------------------------------------------------------------------
TEST(HistoryMissionE2E, FailureAndInfoClose) {
    EventTableLoadDefault();
    FakeEngine eng;
    g_e = &eng;
    MissionDialogHooks hooks = MakeHooks();
    SetMissionDialogHooks(&hooks);

    eng.script = { Snap(kMissionDialogNone, -1, 0, 0),
                   Snap(kMissionDialogAccept, -1, 0, 0) };
    int fr = MissionRunFailureDialog(0, 7);
    CHECK_EQ(fr, 0);
    CHECK_EQ(eng.destroyed, 1);
    CHECK_EQ(eng.frames, 2);

    // Info: closes on skip gate.
    eng = FakeEngine{};
    eng.script = { Snap(kMissionDialogNone, -1, 0, 0),
                   Snap(kMissionDialogNone, -1, 1, 0) };  // skip gate
    int ir = MissionRunInfoDialog(3);
    CHECK_EQ(ir, eng.lastForm);   // returns the destroyed form handle
    CHECK_EQ(eng.destroyed, 1);
    CHECK_EQ(eng.frames, 2);

    SetMissionDialogHooks(nullptr);
    g_e = nullptr;
}

// ---------------------------------------------------------------------------
// Inert defaults: with no hooks installed the loops fall straight through.
// ---------------------------------------------------------------------------
TEST(HistoryMissionE2E, InertDefaults) {
    SetMissionDialogHooks(nullptr);
    CHECK_EQ(MissionRunSpecialDialog(3, 0), 0);
    CHECK_EQ(MissionRunFailureDialog(3, 0), 0);
    CHECK_EQ(MissionRunInfoDialog(0), -1);   // createForm inert -> form = -1
}
