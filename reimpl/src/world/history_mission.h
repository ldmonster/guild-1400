#pragma once
// VIBE_Mission_Run*Dialog drivers — faithful 1:1 ports of the mission-dialog frame
// loops that were NOT yet reconstructed (the bodies; their deterministic decode
// cores already live in world/mission_rules.{h,cpp} and world/mission_dialog.{h,cpp}).
//
// Source functions (gilde.exe, imagebase 0x400000):
//   VIBE_Mission_RunSpecialDialog        0x5387c8  ("special mission" vergabe panel)
//   VIBE_Mission_RunChooseMissionDialog  0x538950  (radio list of a mission type)
//   VIBE_Mission_RunInfoDialog           0x53a41c  (info ack panel)
//   VIBE_Mission_RunFailureDialog        0x539e8c  (failure panel)
//   VIBE_Mission_RunRewardSummary        0x539fd8  (3-line reward voiceover panel)
//   VIBE_Mission_RunCompletionDialog     0x53ac34  (completion -> outcome switch)
//   VIBE_Mission_RunOfferDialog          0x53a854  (give/keep/abandon offer)
//
// Each driver is the engine's Form/Text/Voice/Audio/GameLogic/Command frame loop;
// those callees are routed through an installable MissionDialogHooks struct with
// inert defaults defined in history_mission.cpp (the CutsceneMiscHooks /
// ScriptImportHooks pattern). The deterministic, golden-vector-testable parts are
// kept as pure helpers: the descriptor scan (reused from mission_rules), the
// per-tick frame-loop exit/select decode, and the post-loop outcome/return decode.
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered dialog runtime globals (modeled as an injected per-frame snapshot).
// ===========================================================================
// The frame loops poll three engine globals each iteration:
//   dword_75BF38  lastDialogResult  (-1 none; 1210 accept; 1155 decline)
//   dword_62D22C  clickedObjectId   (id of the form child just clicked, or -1)
//   dword_672230  skipGate          (close/skip request -> latch the exit flag)
//   byte_67225C   menuState         (==1 -> ChooseMission treats as cancel)
// and latch dword_631614 (the loop-exit flag) when the dialog should close.
constexpr i32 kMissionDialogAccept  = 1210;   // dword_75BF38 == 1210
constexpr i32 kMissionDialogDecline = 1155;   // dword_75BF38 == 1155
constexpr i32 kMissionDialogNone    = -1;

struct MissionDialogFrame {
    i32 lastDialogResult = kMissionDialogNone;  // dword_75BF38
    i32 clickedObjectId  = -1;                  // dword_62D22C
    i32 skipGate         = 0;                   // dword_672230
    u8  menuState        = 0;                   // byte_67225C
};

// ===========================================================================
// VIBE_Mission_RunSpecialDialog 0x5387c8 — per-tick exit decode.
// ===========================================================================
// while (RunFrameLoop) {
//   if (dword_672230 || dword_75BF38 == 1155) dword_631614 = 1;     // close
//   if (dword_75BF38 == 1210) { v3 = 1; dword_631614 = 1; }         // confirm -> ret 1
// }
// Returns true when the dialog should exit (latch dword_631614). `outConfirmed`
// is set to 1 only on the accept (1210) branch — it is the dialog's return value.
bool MissionSpecialStep(const MissionDialogFrame& f, int* outConfirmed);

// ===========================================================================
// VIBE_Mission_RunFailureDialog 0x539e8c / VIBE_Mission_RunInfoDialog 0x53a41c.
// ===========================================================================
// Both ack-only panels latch the exit flag on the close gate or the 1210 confirm:
//   if (dword_672230) dword_631614 = 1;
//   if (dword_75BF38 == 1210) dword_631614 = 1;     (Info uses else-if; same effect)
// Returns true when the panel should close. (Info has no decline path; identical
// exit condition.)
bool MissionAckStep(const MissionDialogFrame& f);

// ===========================================================================
// VIBE_Mission_RunChooseMissionDialog 0x538950 — per-tick decode.
// ===========================================================================
// while (RunFrameLoop) {
//   if (dword_672230 || byte_67225C == 1 || dword_75BF38 == 1155) dword_631614 = 1;
//   if (dword_75BF38 != -1) { ... resolve radio selection -> run special dialog }
// }
// Returns the action for this tick. `radioSelectedId` is the resolved button id
// from the radio group (dword_62D22C compared to it inside); pass -1 when none.
enum class MissionChooseAction : int {
    kIdle      = 0,   // nothing to do this tick
    kExit      = 1,   // close gate hit -> latch dword_631614
    kActivate  = 2,   // a radio button matching the click was activated
};
MissionChooseAction MissionChooseStep(const MissionDialogFrame& f,
                                      i32 radioSelectedId);

// ===========================================================================
// VIBE_Mission_RunCompletionDialog 0x53ac34 — pre-loop / post-loop decode.
// ===========================================================================
// Pre-tick: if (dword_63CC24 == -1) dword_631614 = 1;   (mission cleared -> close)
// Post-loop: switch(outcome) -> Failure(1)/Info(2)/LoadSession(3)  (mission_rules).
// Returns true when the completion loop should latch its exit flag this tick.
bool MissionCompletionStep(i32 activeMissionId);

// ===========================================================================
// VIBE_Mission_RunOfferDialog 0x53a854 — per-tick button decode.
// ===========================================================================
// Three buttons resolved from the form (give / keep / abandon). On a click
// (dword_75BF38 != -1) the matching button id selects the path:
//   click == giveButtonId    -> kKeepNoChange (just latches exit; v_keep handled)
//   click == keepButtonId    -> kDecline      (byte_63C8F4=-1, send flag 0)
//   click == abandonButtonId -> kAbandon      (byte_63C8F4=-1, send flag 1, reload)
// (giveButtonId only exists when historySeed < 4; pass -1 when absent.)
enum class MissionOfferAction : int {
    kIdle    = 0,
    kGive    = 1,   // ChildObjectId (the "give/register" button)
    kDecline = 2,   // v34 (keep / decline)
    kAbandon = 3,   // v13 (abandon -> session reload)
};
MissionOfferAction MissionOfferStep(const MissionDialogFrame& f,
                                    i32 giveButtonId,
                                    i32 declineButtonId,
                                    i32 abandonButtonId);
// True when the offer outcome reloads the session (the abandon path sets
// dword_63CC30 = 1 and runs InitOrLoadSession).
bool MissionOfferTriggersReload(MissionOfferAction action);

// ===========================================================================
// Installable engine hooks (inert defaults in history_mission.cpp).
// ===========================================================================
// The Form/Text/Voice/Audio/GameLogic/Command callees the drivers invoke. Real
// builds install the engine's; tests install controlled fakes. All defaults are
// inert (RunFrameLoop returns 0 -> loops fall straight through).
struct MissionDialogHooks {
    // VIBE_GameTick_Finalize(0,0,name) -> the new form handle (or -1).
    int  (*createForm)(const char* name)                         = nullptr;
    void (*centerChildWindows)(int form)                         = nullptr;
    void (*selectWindow)(int form, int sub)                      = nullptr;
    int  (*renderText)(unsigned id)                              = nullptr;
    int  (*getChildObjectId)(int form, int textResult)           = nullptr;
    void (*playVoice)(int chan, int slot, const char* sample)    = nullptr;
    int  (*audioIsInitialized)()                                 = nullptr;
    int  (*voiceIsPlaying)(int slot)                             = nullptr;
    void (*stopVoice)(int fade)                                  = nullptr;
    // VIBE_GameLogic_RunFrameLoop: advances one frame; returns nonzero to keep
    // looping. The fake also publishes the current MissionDialogFrame snapshot.
    int  (*runFrameLoop)(int a, int b)                           = nullptr;
    void (*destroyForm)(int form)                                = nullptr;
    // Snapshot accessor the drivers poll each tick (engine globals).
    void (*readFrame)(MissionDialogFrame* out)                   = nullptr;
};
void SetMissionDialogHooks(const MissionDialogHooks* hooks);
const MissionDialogHooks& GetMissionDialogHooks();

// ===========================================================================
// Faithful driver bodies (use the hooks; reuse mission_rules / mission helpers).
// ===========================================================================
// VIBE_Mission_RunSpecialDialog 0x5387c8 — runs the vergabe panel for the mission
// subtype `value`; returns 1 if the player confirmed (1210), else 0.
int MissionRunSpecialDialog(u8 value, int frameArg);

// VIBE_Mission_RunFailureDialog 0x539e8c — runs the failure panel for `value`.
// Returns 0 (the original returns the audio-stop result, an inert side value).
int MissionRunFailureDialog(u8 value, int frameArg);

// VIBE_Mission_RunInfoDialog 0x53a41c — runs the info-ack panel; returns the
// destroy result (modeled as the form handle that was closed).
int MissionRunInfoDialog(int frameArg);

} // namespace guild::world
