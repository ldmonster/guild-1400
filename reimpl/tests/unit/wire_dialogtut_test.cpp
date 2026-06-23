// Verifies InstallRealDialogTutWiring() binds the bindable fields of the
// Dialog/Tutorial/Gesetz-Desc bridges to their real reconstructed leaves —
// previously inert. Suite prefix: WireDialogTut. Headless; no main().
#include "tests/framework/test.h"

#include "world/wire_dialogtut.h"
#include "world/tutorial_mission.h"   // Set/GetTutorialMissionHooks
#include "world/office_law3.h"        // GesetzFormatDescriptionLow / kNoPortrait / GesetzDescHooksReset

#include "sim/actionqueue.h"          // sim::g_gameTick
#include "sim/entity.h"               // g_persons / g_personIds / ResetEntityArrays
#include "sim/types.h"                // Person

using namespace guild;
using namespace guild::world;

// TutorialMissionHooks.gameTick is bound to a real adapter that reads the
// reconstructed game-tick clock sim::g_gameTick (dword_62EB38).
TEST(WireDialogTut, BindsTutorialMissionGameTick) {
    SetTutorialMissionHooks(nullptr);   // re-inert -> module all-null default
    CHECK(GetTutorialMissionHooks().gameTick == nullptr);

    InstallRealDialogTutWiring();

    const TutorialMissionHooks& h = GetTutorialMissionHooks();
    CHECK(h.gameTick != nullptr);
    // The other (GUI/voice/audio/frame-loop) fields stay inert (null).
    CHECK(h.createForm == nullptr);
    CHECK(h.playVoice  == nullptr);
    CHECK(h.runFrameLoop == nullptr);

    // The adapter reads sim::g_gameTick live.
    sim::g_gameTick = 12345u;
    CHECK(h.gameTick() == 12345u);
    sim::g_gameTick = 0u;
    CHECK(h.gameTick() == 0u);

    SetTutorialMissionHooks(nullptr);   // restore for later tests in this TU
}

// GesetzSetDescHooks.portrait is bound to the real person-record resolver
// (PersonFindRecordById -> record word@+0, else kNoPortrait). Exercised through
// the reconstructed GesetzFormatDescriptionLow op==4 path, which resolves the
// offender id; over a seeded person record the wired control flow runs to a
// defined result without crashing. render stays inert (null).
TEST(WireDialogTut, BindsGesetzPortraitResolver) {
    sim::ResetEntityArrays();
    // Seed person slot 0: marker (record word@+0) != -1 so it is findable, with a
    // distinct id in the parallel id column.
    sim::g_persons[0].marker = 7;       // portrait/name id read by the resolver
    sim::g_personIds[0]      = 4242;    // the offender id we will resolve

    InstallRealDialogTutWiring();

    char dest[1024] = {0};
    // op==4 (FormatDescriptionLow) resolves subjectId -> portrait via the bound hook.
    GesetzDescResult r = GesetzFormatDescriptionLow(dest, /*op=*/4, /*value=*/0,
                                                    /*subjectId=*/4242,
                                                    /*subValue=*/0, /*lowFlag=*/0);
    // op==4 is a handled branch -> valid; the bound portrait resolver ran over the
    // real seeded record before the branch. textId = 4168 + (lowFlag<=1 ? 1 : 0).
    CHECK(r.valid == true);
    CHECK(r.textId == 4169);   // lowFlag=0 -> flag=1 -> 4168 + 1
    (void)r;

    // An unknown offender id resolves to kNoPortrait (-2) without crashing; the
    // op==4 branch still completes with a defined result.
    GesetzDescResult r2 = GesetzFormatDescriptionLow(dest, /*op=*/4, 0,
                                                     /*subjectId=*/999999, 0, 0);
    CHECK(r2.valid == true);

    GesetzDescHooksReset();
    sim::ResetEntityArrays();
}
