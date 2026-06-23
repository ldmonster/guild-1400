// See wire_dialogtut.h. Binds the bindable fields of the Dialog/Tutorial/Gesetz-Desc
// bridges to real reconstructed leaves. Glue only.
#include "world/wire_dialogtut.h"

#include "world/tutorial_mission.h"   // TutorialMissionHooks / Set/GetTutorialMissionHooks
#include "world/office_law3.h"        // GesetzSetDescHooks / kNoPortrait

#include "sim/actionqueue.h"          // sim::g_gameTick (dword_62EB38)
#include "sim/entity.h"               // sim::PersonFindRecordById
#include "sim/types.h"                // sim::Person (record word@+0 == marker)
#include "guild/common/types.h"       // u32 / i32

namespace guild::world {

namespace {

// -------------------------------------------------------------------------
// TutorialMissionHooks.gameTick — reads the global game-tick clock dword_62EB38,
// reconstructed as sim::g_gameTick. The hook signature is u32(*)().
// -------------------------------------------------------------------------
u32 WdtGameTick() {
    return guild::sim::g_gameTick;
}

// -------------------------------------------------------------------------
// GesetzSetDescHooks.portrait — VIBE_Gesetz_FormatDescription* leaf 0x4c2e74 et al.:
//   RecordById = VIBE_Person_FindRecordById(id);
//   v16 = RecordById ? *RecordById : -2;   // record word@+0
// i.e. resolve the offender person id to its record and read the leading i16
// (Person::marker, the portrait/name id), else kNoPortrait (-2). Byte-faithful:
// *(__int16*)rec == rec->marker.
int WdtGesetzPortraitId(i32 personId, void* /*ctx*/) {
    const guild::sim::Person* rec = guild::sim::PersonFindRecordById(personId);
    if (!rec) return kNoPortrait;            // -2, matching "record == null" path
    return static_cast<int>(rec->marker);    // record word@+0
}

// Process-lifetime hook storage for the seed-from-defaults TutorialMission table
// (the global hook ptr references this; must outlive the install).
TutorialMissionHooks g_tutMission{};

} // namespace

void InstallRealDialogTutWiring() {
    // --- TutorialMissionHooks (tutorial_mission.h) ---------------------------
    // Seed from the module's current (inert) defaults so the many GUI / voice /
    // audio / frame-loop fields keep their safe stubs; override only gameTick.
    g_tutMission = GetTutorialMissionHooks();
    g_tutMission.gameTick = &WdtGameTick;
    // selectWindow / renderText / setObjectsVisible / setStatusBanner /
    // voiceIsPlaying / stopVoice / playVoice / createForm / destroyForm /
    // setSliderValue (rule 3/4/5 GUI+voice) and audioIsInitialized / runFrameLoop
    // (engine frame pump): no clean reconstructed target -> inert.
    SetTutorialMissionHooks(&g_tutMission);

    // --- GesetzDescHooks (office_law3.h) -------------------------------------
    // Bind the portrait/name resolver to the real person-record lookup; the
    // format-render leaf (VIBE_Text_RenderFormattedMessage, rule 3 text raster)
    // stays inert (null -> module default).
    GesetzSetDescHooks(&WdtGesetzPortraitId, /*render=*/nullptr, /*ctx=*/nullptr);

    // --- NOT installed (zero faithfully-bindable field) ----------------------
    //   SetLocationDialogHooks    (world/location3.h)               — all GUI/voice/
    //     command-queue; findExistingRequest is a lossy 2-arg view over a 3-pair He
    //     filter + a +43 field compare (rule 8) -> module inert default kept.
    //   SetMissionDialogHooks     (world/history_mission.h)         — all Form/Text/
    //     Voice/Audio/GameLogic frame-loop glue (rule 3/4/5) -> inert kept.
    //   SetTutorialStepVoiceHooks (play/tutorial_recon3_stepvoice.h)— HideReminderPanel
    //     is pure GUI glue (rule 3) -> inert kept.
    //   SetMissionNameHooks       (gui/mission_load_run.h)          — ALREADY wired by
    //     world/wire_meister_loc.cpp (Menu_SetMissionNameHooks) -> skipped.
}

} // namespace guild::world
