#pragma once
// ===========================================================================
// tutorial_recon3_stepvoice — the last two untranslated VIBE_Tutorial_* runtime
// panel leaves:
//
//   VIBE_Tutorial_ShowStepWithVoice   gilde.exe 0x597300
//   VIBE_Tutorial_HideReminderPanel   gilde.exe 0x5975c8
//
// All the sibling panel cores (ShowReminderPanel 0x597514, ShowDonePanel 0x5975f4,
// SetDialogTexts 0x59729c, CheckStateAndStopVoice 0x59744c, the progress slider and
// highlight-arrow cores …) already live in world/tutorial_mission.{h,cpp}. These two
// were referenced only as enum comments in gui/hud_draw.h (the drag-drop command
// enum) and were never reconstructed; this file completes the family.
//
// Both functions are dominated by engine GUI/voice glue — the SAME callees the
// existing tutorial_mission cores route through:
//   VIBE_Form_SelectWindow         0x41e4cc  (Vulkan/windowing, rule 3)
//   VIBE_Form_SetObjectsVisible    0x41d634  (rule 3)
//   VIBE_Text_RenderRichString     0x59d6e8  (rule 3 text raster)
//   VIBE_Audio_VoiceIsPlaying      0x4471b0  (SDL audio, rule 5)
//   VIBE_Audio_StopVoice           0x447508  (rule 5)
//   VIBE_Voice_PlayPositionalSample 0x581fc0 (rule 5)
//   VIBE_Tutorial_SetDialogTexts   0x59729c  (already reconstructed sibling)
//
// What is GENUINE, DETERMINISTIC game logic — and is reconstructed 1:1 here:
//   * ShowStepWithVoice's guard / return-code chain (-4 when no step or when
//     SetDialogTexts fails), and its two-way decision:
//       (a) which TEXT pair gets rendered  (fixed pair 0x1D6B/0x1D6C  vs  the
//           step's own text id and id+1), and
//       (b) the VOICE (re)start decision (stop the playing handle + clear it,
//           then play the step's sample) — gated on the step's voice pointer.
//   * HideReminderPanel is PURE glue (3 unconditional shell calls, no branch); it
//     has no extractable decision core, so it is exposed only as a hooks-routed
//     action with inert defaults, matching the deferred-glue convention.
//
// The state block is the same off_5953F0 runtime that world/tutorial_mission.h
// models (TutorialRuntime). Field indices used below:
//   idx1 (+1, byte)  phaseByte    (off_5953F0[1]; ==10 or ==11 selects the fixed pair)
//   idx4 (+16)       voiceHandle  (active narrated voice; 0 == none)
//   idx5 (+20)       step         (current step record; 0 == none)  [the +5 guard]
// Step record fields (read by ShowStepWithVoice):
//   *(step+8)   kindByte    (==1 or ==2 -> eligible for the fixed text pair)
//   *(step+12)  textId      (rendered text id; the second window gets textId+1)
//   *(step+16)  hasVoice    (non-zero -> a narrated sample should play)
//   *(step+...) sampleName  (passed to VIBE_Voice_PlayPositionalSample)
// ===========================================================================
#include "guild/common/types.h"

namespace guild::play {

using f32 = float;

// ---------------------------------------------------------------------------
// ShowStepWithVoice — decision cores (the engine return/branch semantics).
// ---------------------------------------------------------------------------

// gilde.exe 0x597300 — VIBE_Tutorial_ShowStepWithVoice guard chain.
//   if (!step)                      return -4;   // *((_DWORD*)off+5) == 0
//   r = SetDialogTexts();           if (r == -4) return -4;  // propagate
//   ... render + voice ... ;        return 0;
// `stepValid`   = step pointer (+20) != 0.
// `dialogTexts` = the int SetDialogTexts() returned (-4 fail / 0 ok), already
//                 reconstructed as TutorialSetDialogTextsResult in tutorial_mission.
// Returns -4 on either guard, otherwise 0.
int TutorialShowStepGuard(int stepValid, int dialogTexts);

// The two TEXT branches the original renders into windows 1 and 2.
enum class StepTextBranch : int {
    kFixedPair = 0,   // render 0x1D6B into win1, 0x1D6C into win2
    kStepPair  = 1,   // render *(step+12) into win1, *(step+12)+1 into win2
};

// gilde.exe 0x597300 — text-branch select. The fixed pair is used only when the
// step kind byte is 1 or 2 AND the runtime phase byte (off[1]) is 10 or 11;
// otherwise the step's own text id pair is used.
//   v2 = *(step+8);
//   if ((v2==1 || v2==2) && (off[1]==10 || off[1]==11)) -> fixed; else -> step.
// `stepKind` = *(step+8); `phaseByte` = off_5953F0[1].
StepTextBranch TutorialShowStepTextBranch(int stepKind, u8 phaseByte);

// The fixed text-id pair (rich-string ids) rendered into windows 1 and 2.
constexpr u32 kStepFixedTextWin1 = 0x1D6B;  // VIBE_Text_RenderRichString(0x1D6B)
constexpr u32 kStepFixedTextWin2 = 0x1D6C;  // VIBE_Text_RenderRichString(0x1D6C)

// gilde.exe 0x597300 — voice (re)start decision (only taken on the kStepPair branch).
//   if (*(step+16)) {                         // step has a narrated sample
//       if (voiceHandle) {                    // a voice is already mounted
//           if (VoiceIsPlaying(voiceHandle))  StopVoice(voiceHandle,0);
//           voiceHandle = 0;                  // off[4] = 0
//       }
//       voiceHandle = PlayPositionalSample(0xFFFFFFF9, 0, -1, sampleName);
//   }
enum class StepVoiceAction : int {
    kNoVoice      = 0,  // *(step+16) == 0  -> leave voice untouched
    kPlayOnly     = 1,  // had no mounted handle -> just play the new sample
    kStopThenPlay = 2,  // had a mounted handle -> stop (if playing) + clear, then play
};
// `hasVoice`    = *(step+16) != 0.
// `voiceHandle` = off[4]   (current narrated voice; 0 == none mounted).
StepVoiceAction TutorialShowStepVoiceAction(int hasVoice, int voiceHandle);

// The fixed args the original passes to VIBE_Voice_PlayPositionalSample for a
// tutorial step sample:  (0xFFFFFFF9, 0, -1, sampleName).
constexpr int kStepVoiceChannel  = static_cast<int>(0xFFFFFFF9);  // -7
constexpr int kStepVoiceFlag     = 0;
constexpr int kStepVoiceLoop     = -1;

// ---------------------------------------------------------------------------
// HideReminderPanel — pure glue, exposed as a hooks-routed action.
// ---------------------------------------------------------------------------
// gilde.exe 0x5975c8 — VIBE_Tutorial_HideReminderPanel:
//   VIBE_Form_SelectWindow(*(off+8), 0);          // select reminder form, page 0
//   VIBE_Text_RenderRichString("$C");             // clear the rich-text region
//   return VIBE_Form_SetObjectsVisible(*(off+32), 0);  // hide the panel objects
// No branch / no state read other than the form pointers; nothing deterministic to
// extract. Routed through TutorialStepVoiceHooks (inert default) so callers can
// install the real GUI backend; the inert default returns 0 like the original tail.
struct TutorialStepVoiceHooks {
    virtual ~TutorialStepVoiceHooks() = default;
    // 0x5975c8 — hide the reminder panel (select win, clear "$C", hide objects).
    // Returns the VIBE_Form_SetObjectsVisible result (0 on success).
    virtual int HideReminderPanel(int reminderForm, int panelObjects) {
        (void)reminderForm; (void)panelObjects; return 0;
    }
};
void SetTutorialStepVoiceHooks(TutorialStepVoiceHooks* hooks);
const TutorialStepVoiceHooks& GetTutorialStepVoiceHooks();

// gilde.exe 0x5975c8 — thin wrapper that routes through the installed hooks.
int TutorialHideReminderPanel(int reminderForm, int panelObjects);

} // namespace guild::play
