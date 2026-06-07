#pragma once
// Tutorial runtime panels + the remaining Mission reward/history-list dialog cores.
// Faithful 1:1 ports of the deterministic decision/animation/math cores extracted
// from the UNTRANSLATED slice of the VIBE_Tutorial_* and VIBE_Mission_* families
// (the step-chain progression rules already live in world/tutorial.{h,cpp}; the
// dialog decode cores in world/mission_rules + world/mission_dialog; the
// Special/Failure/Info drivers in world/history_mission). What was still
// untranslated and is recovered here:
//
//   Tutorial runtime-panel + progress + highlight-arrow cores (gilde.exe):
//     VIBE_Tutorial_ResetChapterPointer       0x597b80
//     VIBE_Tutorial_Shutdown                  0x597da8  (state reset)
//     VIBE_Tutorial_OpenMainEventPanel        0x5971ac  (guard)
//     VIBE_Tutorial_CloseEventPanel           0x597270  (guard)
//     VIBE_Tutorial_SetDialogTexts            0x59729c  (guard)
//     VIBE_Tutorial_ShowReminderPanel         0x597514  (guard + voice gate)
//     VIBE_Tutorial_ShowDonePanel             0x5975f4  (guard + voice gate)
//     VIBE_Tutorial_CheckStateAndStopVoice    0x59744c  (state machine)
//     VIBE_Tutorial_CreateProgressSlider      0x597700  (slider geometry)
//     VIBE_Tutorial_UpdateProgressSlider      0x5977a8  (progress fraction math)
//     VIBE_Tutorial_DrawHighlightArrow        0x59b450  (4-phase anim state machine)
//     VIBE_Tutorial_OpenActiveCharBuilding    0x596fa4  (early-out guard)
//
//   Mission reward-summary + history-list dialog cores (gilde.exe):
//     VIBE_Mission_RunRewardSummary           0x539fd8  (4-stage voiceover sequence)
//     VIBE_Mission_RunChooseHistoryDialog     0x538b28  (click -> slot hit-test)
//     VIBE_Mission_RunHistoryRewardDialog     0x538db4  (click -> slot hit-test +
//                                                        disable/select count)
//
// The Form/Text/Voice/Audio/GameLogic/Widget/Coord callees are engine GUI glue with
// no reconstructed sibling; they are routed through an installable hooks struct with
// inert defaults defined in this library .cpp (the MissionDialogHooks pattern). The
// genuinely deterministic parts (guard return codes, fraction math, animation phase
// transitions, click hit-tests, the voiceover text-id/sample-name sequence) are pure.
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Tutorial runtime state (off_5953F0). This is the SAME block world/tutorial.h
// models for the step-chain rules; here we name the runtime-panel fields the
// untranslated functions touch (dword indices and matching byte offsets):
//   +0  (byte)   phaseByte   (UpdateProgressSlider: *(_BYTE*)off == 10)
//   idx4  (+16)  voiceHandle  (active narrated voice; 0 == none)
//   idx5  (+20)  step         (current step record; 0 == none)
//   idx6  (+24)  panelHost    (active-chapter request / event-panel host)
//   idx7  (+28)  chapterId    (-1 sentinel; matched vs clickedObjectId)
//   idx8  (+32)  reminderForm
//   idx12 (+48)  doneForm
//   idx20 (+80)  arrowTarget  (current highlight target index)
//   idx21 (+84)  arrowPhase   (1 idle / 2 approach / 3 travel / 4 hold)
//   idx22 (+88)  arrowTick    (phase start tick)
//   idx23 (+92)  arrowObj     (-1 == no arrow widget)
//   idx25 (+100) sliderForm   (-1 == none)
//   idx26 (+104) sliderObj
// (We expose just the fields the recovered cores read/write.)
// ===========================================================================
struct TutorialRuntime {
    u8  phaseByte   = 0;    // +0
    int voiceHandle = 0;    // +16
    int stepValid   = 0;    // +20 != 0 (a step record is present)
    int panelHost   = 0;    // +24 (event-panel host slot; 0 == none)
    int chapterId   = -1;   // +28
    int reminderForm = -1;  // +32
    int doneForm    = -1;   // +48
    int arrowTarget = 0;    // +80
    int arrowPhase  = 1;    // +84
    int arrowTick   = 0;    // +88
    int arrowObj    = -1;   // +92
    int sliderForm  = -1;   // +100
    int sliderObj   = 0;    // +104
};

// gilde.exe 0x597b80 — VIBE_Tutorial_ResetChapterPointer: clears the panel host
// (+24 = 0) and re-arms the chapter-id sentinel (+28 = -1).
void TutorialResetChapterPointer(TutorialRuntime& rt);

// gilde.exe 0x597da8 — VIBE_Tutorial_Shutdown (state-reset tail). Zeroes the
// phase byte, voice/step/host fields and the +28 chapter-id sentinel, and clears
// the slider form (-1). The sample-bank unload + form-destroy + FreeStepChain are
// engine side effects (routed through the hooks); this models the field reset.
void TutorialShutdownReset(TutorialRuntime& rt);

// ---------------------------------------------------------------------------
// Panel-open / close guard return codes (the engine return value -4 / -1 / 0).
// ---------------------------------------------------------------------------
// gilde.exe 0x5971ac — VIBE_Tutorial_OpenMainEventPanel:
//   if (!panelHost || *(panelHost+116)) return -4;     // no host, or already open
//   ... create slot ...; if (newSlot == 0) return -1;  // create failed
//   return 0;
// `hostOpen` is *(panelHost+116) != 0 (a panel is already mounted on the host).
// `createOk` is whether the slot creation produced a child (the hooked create).
// Returns -4 / -1 / 0 exactly as the original.
int TutorialOpenMainEventPanelResult(int panelHost, bool hostOpen, bool createOk);

// gilde.exe 0x597270 — VIBE_Tutorial_CloseEventPanel:
//   v1 = panelHost; if (!v1 || !*(v1+116)) return -4;  // nothing to close
//   destroy; return 0;
int TutorialCloseEventPanelResult(int panelHost, bool hostOpen);

// gilde.exe 0x59729c — VIBE_Tutorial_SetDialogTexts:
//   v0 = panelHost; if (!v0) return -4;
//   v1 = *(v0+116); if (!v1) return -4;     // no mounted panel
//   ... render ...; return 0;
int TutorialSetDialogTextsResult(int panelHost, bool hostOpen);

// ---------------------------------------------------------------------------
// Reminder / Done panel show-gate (gilde.exe 0x597514 / 0x5975f4).
// ---------------------------------------------------------------------------
// Both panels are shown only when there is a current step (+20 != 0) AND that
// step carries the matching text id:
//   ShowReminder: *(step+20) != 0   ShowDone: *(step+28) != 0
// When shown, a positional voice is (re)started only when the step's voice
// pointer is set (ShowReminder: *(step+24); ShowDone: *(step+32)) — a playing
// voice is stopped first and voiceHandle cleared. This classifies the action.
enum class TutorialPanelAction : int {
    kSkip       = 0,   // no step / no panel text -> do nothing
    kShowSilent = 1,   // render the panel, no voice (voice ptr == 0)
    kShowVoiced = 2,   // render the panel and (re)start the narrated voice
};
// `stepValid` is +20 != 0; `panelTextId` is the step's reminder/done text id
// (*(step+20) / *(step+28)); `voicePtr` is the step's reminder/done voice
// (*(step+24) / *(step+32), 0 == silent).
TutorialPanelAction TutorialClassifyShowPanel(int stepValid, int panelTextId, int voicePtr);

// ---------------------------------------------------------------------------
// CheckStateAndStopVoice state machine (gilde.exe 0x59744c).
// ---------------------------------------------------------------------------
// Guard: if (!panelHost || !*(panelHost+116)) return -4;           (no panel)
//        if (!*dword_632270 || **dword_632270 != 0x87) return 0;   (wrong screen)
// Then two completion checks, both of which (when matched) stop the active voice
// and return 1; otherwise 0:
//   if (dword_75BF38 != -1):  matched when dword_62D22C == chapterId  (+28)
//   else:                     matched when byte_67225C == 28
// `dialogResult` is dword_75BF38 (-1 none), `clickedId` is dword_62D22C,
// `menuState` is byte_67225C, `chapterId` is rt +28.
enum class TutorialVoiceCheck : int {
    kNoPanel   = -4,  // guard failed (no mounted panel)
    kIgnore    = 0,   // wrong screen, or no completion match
    kStopped   = 1,   // completion matched -> active voice stopped, handle cleared
};
TutorialVoiceCheck TutorialCheckStateAndStopVoice(int panelHost, bool hostOpen,
                                                  bool screenActive,
                                                  int dialogResult, int clickedId,
                                                  int chapterId, u8 menuState);

// ---------------------------------------------------------------------------
// Progress slider geometry + fraction (gilde.exe 0x597700 / 0x5977a8).
// ---------------------------------------------------------------------------
// CreateProgressSlider centers a 400-wide slider:  x = screenWidth/2 - 200, y=5.
// Returns the slider's left x for a given screen width (the recovered geometry).
constexpr int kTutorialSliderWidth = 400;
constexpr int kTutorialSliderY     = 5;
int TutorialSliderLeftX(int screenWidth);

// UpdateProgressSlider value (the slider counts DOWN from 400 to 0 as the step's
// duration elapses):
//   elapsed = (nowTick == startTick) ? 0 : nowTick - startTick;   (unsigned)
//   if (elapsed >= duration) elapsed = duration;                  (clamp)
//   value = (double)(duration - elapsed) / (double)duration * 400.0f;
// `nowTick`/`startTick` are dword_62EB38 / step start tick; `duration` is the
// step's +68 field (>0). Returns the slider value (0..400) for SetValueOrText.
constexpr float kTutorialSliderScale = 400.0f;   // flt_626DE4 == 400.0
int TutorialProgressSliderValue(u32 nowTick, u32 startTick, u32 duration);

// True when UpdateProgressSlider is in its active branch: a step is present
// (+20 != 0) AND the phase byte is 10 (kTutPhaseActive). Otherwise it tears the
// slider down. (Mirrors the *(_DWORD*)(off+20) && *(_BYTE*)off == 10 gate.)
constexpr u8 kTutPhaseActive = 10;
bool TutorialSliderActive(int stepValid, u8 phaseByte);

// ---------------------------------------------------------------------------
// Highlight-arrow animation state machine (gilde.exe 0x59b450).
// ---------------------------------------------------------------------------
// The arrow widget cycles a 4-phase bob over a 44-byte arrow record per target:
//   rec+32 (float) approachDur   (phase 2 -> 3 threshold)
//   rec+36 (float) travelDur     (phase 3 bezier span; rec+4..rec+28 are the
//                                 cubic-bezier control points)
//   rec+40 (float) holdDur       (phase 4 -> 2 threshold)
// Phase byte is rt +84 (2 approach / 3 travel / 4 hold; 1 == idle/just (re)created).
// Each tick (now = dword_62EB38, start = rt +88):
//   phase 2: if (now < start + approachDur) stay;   else phase=3, start=now
//   phase 3: if (now < start + travelDur)   travel; else phase=4, start=now
//   phase 4: if (now < start + holdDur)      stay;  else phase=2, start=now
// `now`/`start` are ticks; the *Dur are the rec floats. Mutates rt.arrowPhase /
// rt.arrowTick and returns what the tick does (so the caller drives the widget).
enum class TutorialArrowResult : int {
    kStay    = 0,   // threshold not reached; arrow held in place
    kTravel  = 1,   // phase 3 active: bezier-position the arrow this tick
    kAdvance = 2,   // phase boundary crossed; arrowTick reset to now
};
TutorialArrowResult TutorialArrowStep(TutorialRuntime& rt, u32 now,
                                      float approachDur, float travelDur,
                                      float holdDur);
// Phase-3 travel fraction:  (now - arrowTick) / travelDur  (clamped to [0,1)
// before the boundary; the boundary itself advances to phase 4). Pure helper for
// the bezier evaluation (VIBE_Math_CubicBezierPoint).
float TutorialArrowTravelFraction(u32 now, u32 arrowTick, float travelDur);

// gilde.exe 0x596fa4 — VIBE_Tutorial_OpenActiveCharBuilding early-out guard.
//   if (begin) return begin;                       // non-zero arg short-circuits
//   if (!activeCharPtr) proceed;                   // no active char -> proceed
//   else if (classByte(activeChar) != 2) proceed;  // not the gated class -> proceed
//   else short-circuit (do nothing).
// Returns true when the function proceeds to open the building (the QueryBegin
// path); false when it short-circuits. `begin` is the al argument; `hasActiveChar`
// is dword_631744 != 0; `activeCharClass` is *(building+589*charIndex) (the class
// byte at +0; gate value 2).
bool TutorialOpenBuildingProceeds(int begin, bool hasActiveChar, u8 activeCharClass);

// ===========================================================================
// Mission RunRewardSummary voiceover sequence (gilde.exe 0x539fd8).
// ===========================================================================
// The reward summary plays a four-stage panel. Three header lines and a body:
//   line 0  text id 0x17A8   voice sample slot 0   ("AUFTRAEGE_ALLGEMEIN")
//   line 1  text id 0x17A9   voice sample slot 1
//   line 2  text id 0x17AA   voice sample slot 2
//   body    text ids 0x17AB then (*(rec+4) + 2)    voice "_AUFTRAEGE_ERFOLG_HS_%.2d"
//                                                  formatted with *(rec+12).
// Each stage waits on the voice (or, when audio is uninitialised, a 250-tick
// timeout: dword_62EB38 + 250), abortable by a click on the panel's child id.
constexpr unsigned kMissionRewardLine0TextId = 0x17A8;
constexpr unsigned kMissionRewardLine1TextId = 0x17A9;
constexpr unsigned kMissionRewardLine2TextId = 0x17AA;
constexpr unsigned kMissionRewardBodyTextId  = 0x17AB;
constexpr int      kMissionRewardTimeoutTicks = 250;   // v44

// The body text id the summary renders after 0x17AB:  *(rec+4) + 2.
// (rec is the matched descriptor; +4 is its value field used as a text id.)
int MissionRewardBodyTextId(int descriptorValueField);

// Formats the body voice sample name ("_AUFTRAEGE_ERFOLG_HS_%.2d", rec+12) into
// `out` (capacity `cap`); returns out. `voiceIndex` is *(rec+12). %.2d -> zero
// padded to width 2 (negative/large values follow printf width semantics).
char* MissionRewardVoiceSample(char* out, unsigned cap, int voiceIndex);

// Per-stage timeout deadline when audio is uninitialised:  nowTick + 250.
// The wait loop runs while (deadline > dword_62EB38) — i.e. for 250 ticks.
u32 MissionRewardTimeoutDeadline(u32 nowTick);

// True when a reward-summary stage's wait loop should abort early: a panel click
// landed on the summary's child object id.  (dword_75BF38 != -1 && clickedId ==
// childId.) `dialogResult` is dword_75BF38, `clickedId` is dword_62D22C.
bool MissionRewardSkipRequested(int dialogResult, int clickedId, int childId);

// ===========================================================================
// History-list dialog click hit-tests (gilde.exe 0x538b28 / 0x538db4).
// ===========================================================================
// Both dialogs build a radio list of option object ids and, on a confirm click
// (dword_75BF38 == 1210), map the clicked id (dword_62D22C) to byte_63C8F4 (the
// mode the follow-up RunChooseMissionDialog receives). Sentinel -1 == cancel.
//
// RunChooseHistoryDialog 0x538b28 — 6 history slots + a cancel button:
//   click == cancelId -> -1   (close)
//   click == opt[i]   ->  i   (i in 0..5)
//   no match          ->  kMissionHistoryNoChange (leave byte_63C8F4 untouched)
// Pass the option ids in `optIds[6]` and the cancel id; returns the mode (-1..5)
// or kMissionHistoryNoChange when nothing matched.
constexpr int kMissionHistoryNoChange = -2;
int MissionChooseHistoryHit(const int optIds[6], int cancelId, int clickedId);

// RunHistoryRewardDialog 0x538db4 — a cancel id, a back id, and 5 reward options:
//   click == cancelId || click == backId -> -1   (close)
//   click == opt[i]   ->  i   (i in 0..4)
//   no match          ->  kMissionHistoryNoChange
// (The original's opt0 branch computes `v15 ^ dword_62D22C` which is 0 on the
// match — i.e. mode 0. We model that as the literal index 0.)
int MissionHistoryRewardHit(const int optIds[5], int cancelId, int backId, int clickedId);

// RunHistoryRewardDialog disable/seed count (gilde.exe 0x538ec6..). Disables one
// option per already-completed slot and seeds the radio selection:
//   completed > 0 -> sel = 2;  +1 for each of completed in {2,3,4};
//   completed > 4 -> sel = 0   (all consumed; reset to first option).
// This is the SAME rule world/mission_rules.cpp::MissionHistoryRewardSelection
// already recovers; reused there. Exposed here only as the upper option count
// actually disabled (min(completed,5)) for the dialog's enable loop.
int MissionHistoryRewardDisableCount(int completed);

// ===========================================================================
// Installable engine hooks (inert defaults in tutorial_mission.cpp).
// ===========================================================================
// The Form/Text/Voice/Audio/GameLogic/Widget/Coord leaves the recovered drivers
// invoke. Real builds install the engine's; tests install controlled fakes. All
// defaults are inert (no-ops / 0 / first-frame exits) so the deterministic cores
// run without the engine present.
struct TutorialMissionHooks {
    // Tutorial panel/slider/arrow side effects.
    void (*selectWindow)(int form, int sub)                  = nullptr;
    int  (*renderText)(unsigned id)                          = nullptr;
    void (*setObjectsVisible)(int objId, int visible)        = nullptr;
    void (*setStatusBanner)()                                = nullptr;
    int  (*voiceIsPlaying)(int handle)                       = nullptr;  // 1 == playing
    void (*stopVoice)(int handle, int fade)                  = nullptr;
    int  (*playVoice)(int chan, int slot, int idx, const char* sample) = nullptr;
    int  (*createForm)(const char* name)                     = nullptr;
    void (*destroyForm)(int form)                            = nullptr;
    int  (*setSliderValue)(int obj, int lo, int hi, int v)   = nullptr;
    // Mission reward / history-list side effects.
    int  (*audioIsInitialized)()                             = nullptr;  // 0 == off
    int  (*runFrameLoop)()                                   = nullptr;  // !=0 keep
    u32  (*gameTick)()                                       = nullptr;  // dword_62EB38
};
void SetTutorialMissionHooks(const TutorialMissionHooks* hooks);
const TutorialMissionHooks& GetTutorialMissionHooks();

} // namespace guild::world
