// Faithful 1:1 ports — see tutorial_mission.h for the source-function map and the
// per-core argument/return semantics. Engine GUI/voice/frame leaves are routed
// through TutorialMissionHooks (inert defaults below).
#include "world/tutorial_mission.h"

#include <cstdio>

namespace guild::world {

// ---------------------------------------------------------------------------
// Hooks — inert defaults (defined in THIS library .cpp so every test executable
// links a definition; tests install their own).
// ---------------------------------------------------------------------------
namespace {
const TutorialMissionHooks  kInertHooks{};   // all-null
const TutorialMissionHooks* g_hooks = &kInertHooks;
} // namespace

void SetTutorialMissionHooks(const TutorialMissionHooks* hooks) {
    g_hooks = hooks ? hooks : &kInertHooks;
}
const TutorialMissionHooks& GetTutorialMissionHooks() { return *g_hooks; }

// ===========================================================================
// Tutorial state reset.
// ===========================================================================
// 0x597b80 — VIBE_Tutorial_ResetChapterPointer.
void TutorialResetChapterPointer(TutorialRuntime& rt) {
    rt.panelHost = 0;    // *(off+24) = 0
    rt.chapterId = -1;   // off[7] = -1
}

// 0x597da8 — VIBE_Tutorial_Shutdown (field-reset tail). The original clears the
// phase byte and the +4..+20 dwords, the +28 chapter-id sentinel, and the slider
// form (-1). (The sample-bank unload / FreeStepChain / Form_Destroy are engine
// side effects handled by the caller via the hooks.)
void TutorialShutdownReset(TutorialRuntime& rt) {
    rt.phaseByte   = 0;     // *(_BYTE*)v3 = 0
    rt.voiceHandle = 0;     // +16 = 0
    rt.stepValid   = 0;     // +20 = 0
    rt.panelHost   = 0;     // +24 = 0
    rt.chapterId   = -1;    // +28 = -1
    rt.sliderForm  = -1;    // +32 (slider form) = -1
}

// ===========================================================================
// Panel guards (return codes match the originals exactly).
// ===========================================================================
// 0x5971ac — VIBE_Tutorial_OpenMainEventPanel.
int TutorialOpenMainEventPanelResult(int panelHost, bool hostOpen, bool createOk) {
    if (!panelHost || hostOpen)   // !v1 || *(v1+116)
        return -4;
    if (!createOk)                // v3 = *(host+116); if (!v3) return -1
        return -1;
    return 0;
}

// 0x597270 — VIBE_Tutorial_CloseEventPanel.
int TutorialCloseEventPanelResult(int panelHost, bool hostOpen) {
    if (!panelHost || !hostOpen)  // !v1 || !*(v1+116)
        return -4;
    return 0;
}

// 0x59729c — VIBE_Tutorial_SetDialogTexts.
int TutorialSetDialogTextsResult(int panelHost, bool hostOpen) {
    if (!panelHost)               // !v0
        return -4;
    if (!hostOpen)                // v1 = *(v0+116); if (!v1) return -4
        return -4;
    return 0;
}

// ===========================================================================
// Reminder / Done panel show-gate (0x597514 / 0x5975f4 share the structure).
// ===========================================================================
TutorialPanelAction TutorialClassifyShowPanel(int stepValid, int panelTextId, int voicePtr) {
    if (!stepValid || !panelTextId)   // if (v1) { if (*(step+20|28)) { ... } }
        return TutorialPanelAction::kSkip;
    // Panel rendered. Voice (re)started only when the step carries a voice ptr.
    if (!voicePtr)                    // if (*(step+24|32)) { play }
        return TutorialPanelAction::kShowSilent;
    return TutorialPanelAction::kShowVoiced;
}

// ===========================================================================
// 0x59744c — VIBE_Tutorial_CheckStateAndStopVoice.
// ===========================================================================
TutorialVoiceCheck TutorialCheckStateAndStopVoice(int panelHost, bool hostOpen,
                                                  bool screenActive,
                                                  int dialogResult, int clickedId,
                                                  int chapterId, u8 menuState) {
    // if (!v1 || !*(v1+116)) return -4;
    if (!panelHost || !hostOpen)
        return TutorialVoiceCheck::kNoPanel;
    // if (!*dword_632270 || **dword_632270 != 0x87) return 0;   (wrong screen)
    if (!screenActive)
        return TutorialVoiceCheck::kIgnore;
    if (dialogResult != -1) {                 // dword_75BF38 != -1
        if (clickedId == chapterId)           // dword_62D22C == off[7]
            return TutorialVoiceCheck::kStopped;
        return TutorialVoiceCheck::kIgnore;
    }
    // else branch: byte_67225C != 28 -> 0; == 28 -> stop.
    if (menuState != 28)
        return TutorialVoiceCheck::kIgnore;
    return TutorialVoiceCheck::kStopped;
}

// ===========================================================================
// Progress slider geometry + fraction (0x597700 / 0x5977a8).
// ===========================================================================
int TutorialSliderLeftX(int screenWidth) {
    // x = (screenWidth >> 1)/... actually  screenWidth/2 - 200 (400-wide slider).
    return screenWidth / 2 - kTutorialSliderWidth / 2;
}

int TutorialProgressSliderValue(u32 nowTick, u32 startTick, u32 duration) {
    if (duration == 0)
        return 0;
    // v5 = (now == start) ? 0 : now - start;  (the original's two-branch select)
    u32 elapsed = (nowTick == startTick) ? 0u : (nowTick - startTick);
    // if (elapsed >= duration) elapsed = duration;  (v9 clamp)
    if (elapsed >= duration)
        elapsed = duration;
    // value = (double)(duration - elapsed) / (double)duration * 400.0f;
    double v = static_cast<double>(duration - elapsed)
             / static_cast<double>(duration)
             * static_cast<double>(kTutorialSliderScale);
    return static_cast<int>(v);   // truncation toward zero (FPU store to int)
}

bool TutorialSliderActive(int stepValid, u8 phaseByte) {
    return stepValid != 0 && phaseByte == kTutPhaseActive;
}

// ===========================================================================
// 0x59b450 — VIBE_Tutorial_DrawHighlightArrow (4-phase animation state machine).
// ===========================================================================
float TutorialArrowTravelFraction(u32 now, u32 arrowTick, float travelDur) {
    if (travelDur == 0.0f)
        return 0.0f;
    // v44 = (double)(now - arrowTick) / travelDur;
    return static_cast<float>(static_cast<double>(now - arrowTick)
                              / static_cast<double>(travelDur));
}

TutorialArrowResult TutorialArrowStep(TutorialRuntime& rt, u32 now,
                                      float approachDur, float travelDur,
                                      float holdDur) {
    // phase 2 (approach): if (now < start + approachDur) stay; else -> 3.
    if (rt.arrowPhase == 2) {
        double bound = static_cast<double>(static_cast<u32>(rt.arrowTick)) + approachDur;
        if (static_cast<double>(now) < bound)
            return TutorialArrowResult::kStay;
        rt.arrowPhase = 3;
        rt.arrowTick  = static_cast<int>(now);
        // falls into phase 3 this same tick in the original.
    }
    // phase 3 (travel): if (now < start + travelDur) travel; else -> 4.
    if (rt.arrowPhase == 3) {
        double bound = static_cast<double>(static_cast<u32>(rt.arrowTick)) + travelDur;
        if (static_cast<double>(now) < bound)
            return TutorialArrowResult::kTravel;
        rt.arrowPhase = 4;
        rt.arrowTick  = static_cast<int>(now);
    }
    // phase 4 (hold): if (now < start + holdDur) stay; else -> 2.
    if (rt.arrowPhase == 4) {
        double bound = static_cast<double>(static_cast<u32>(rt.arrowTick)) + holdDur;
        if (static_cast<double>(now) < bound)
            return TutorialArrowResult::kStay;
        rt.arrowPhase = 2;
        rt.arrowTick  = static_cast<int>(now);
        return TutorialArrowResult::kAdvance;
    }
    // phase 2/3 that crossed a boundary above returns kAdvance.
    return TutorialArrowResult::kAdvance;
}

// ===========================================================================
// 0x596fa4 — VIBE_Tutorial_OpenActiveCharBuilding (early-out guard).
// ===========================================================================
bool TutorialOpenBuildingProceeds(int begin, bool hasActiveChar, u8 activeCharClass) {
    if (begin)                       // if (!(_BYTE)Begin) { ... }  -> nonzero short-circuits
        return false;
    // if (!dword_631744 || classByte != 2) proceed
    if (!hasActiveChar)
        return true;
    return activeCharClass != 2;
}

// ===========================================================================
// 0x539fd8 — VIBE_Mission_RunRewardSummary (deterministic sequence parts).
// ===========================================================================
int MissionRewardBodyTextId(int descriptorValueField) {
    return descriptorValueField + 2;   // *(_DWORD*)(v42 + 4) + 2
}

char* MissionRewardVoiceSample(char* out, unsigned cap, int voiceIndex) {
    if (!out || cap == 0)
        return out;
    // VIBE_Crt_Sprintf_0(v39, "_AUFTRAEGE_ERFOLG_HS_%.2d", *(rec+12));
    std::snprintf(out, cap, "_AUFTRAEGE_ERFOLG_HS_%.2d", voiceIndex);
    return out;
}

u32 MissionRewardTimeoutDeadline(u32 nowTick) {
    return nowTick + static_cast<u32>(kMissionRewardTimeoutTicks);   // dword_62EB38 + 250
}

bool MissionRewardSkipRequested(int dialogResult, int clickedId, int childId) {
    return dialogResult != -1 && clickedId == childId;   // 75BF38 != -1 && 62D22C == child
}

// ===========================================================================
// History-list click hit-tests (0x538b28 / 0x538db4).
// ===========================================================================
int MissionChooseHistoryHit(const int optIds[6], int cancelId, int clickedId) {
    if (clickedId == cancelId)            // ChildObjectId (cancel) -> -1
        return -1;
    for (int i = 0; i < 6; ++i) {
        if (optIds && optIds[i] == clickedId)
            return i;
    }
    return kMissionHistoryNoChange;       // byte_63C8F4 left untouched
}

int MissionHistoryRewardHit(const int optIds[5], int cancelId, int backId, int clickedId) {
    // The original checks back (v12) and cancel (ChildObjectId) first -> both -1.
    if (clickedId == backId || clickedId == cancelId)
        return -1;
    for (int i = 0; i < 5; ++i) {
        if (optIds && optIds[i] == clickedId)
            return i;
    }
    return kMissionHistoryNoChange;
}

int MissionHistoryRewardDisableCount(int completed) {
    if (completed < 0)
        return 0;
    if (completed > 5)
        return 5;     // only 5 reward options exist (opt1..opt5 disabled at a1>4)
    return completed;
}

} // namespace guild::world
