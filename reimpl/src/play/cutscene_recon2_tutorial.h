#pragma once
// guild::play — tutorial "done" panel hide + voice stop (cutscene_recon2 cluster).
//
// gilde.exe 0x5976a8 — VIBE_Tutorial_HideDonePanelAndStopVoice
//   (__usercall, eax = result, ebx = arg0). The arg0 (ebx) only flows into the
//   VIBE_Audio_VoiceIsPlaying call as a spurious extra register arg and is unused
//   by the observable logic.
//
// off_5953F0 points at the global tutorial-controller record. The fields touched:
//   +0x10 (dword)  active tutorial-voice handle (0 == none)
//   +0x30 (dword)  the tutorial form/window handle (the "done" panel)
//
// Behavior (disasm at 0x5976a8):
//   ecx = *off_5953F0;
//   VIBE_Form_SelectWindow(ecx[+0x30], 0);                 // select the panel form
//   VIBE_Text_RenderRichString("$C");                      // emit the clear-text token
//   VIBE_Form_SetObjectsVisible(ecx[+0x30], 0);            // hide the panel's objects
//   if ( ecx[+0x10] ) {                                    // a voice is bound
//       if ( VIBE_Audio_VoiceIsPlaying(ecx[+0x10]) )
//           VIBE_Audio_StopVoice(ecx[+0x10], 0);           // stop it
//       (*off_5953F0)[+0x10] = 0;                          // clear the handle
//   }
//
// RULE 5 BOUNDARY: voice playback is SDL audio elsewhere; here it is an inert hook.
// Form/text are pure UI side effects modeled as inert hooks. The control LOGIC
// (the +0x10 null-gate, the is-playing-then-stop, the handle clear) is 1:1.

#include "guild/common/types.h"
#include <cstdint>
#include <functional>

namespace guild::play {

using guild::i32;

// aC_10 == "$C" : the rich-string clear token emitted while hiding the panel.
inline constexpr const char* kTutorialClearToken = "$C";

// The fields of *off_5953F0 the routine reads/writes.
struct TutorialController {
    i32 voiceHandle = 0;   // +0x10 : active tutorial voice (0 == none)
    i32 panelForm   = 0;   // +0x30 : the "done" panel form/window handle
};

// Inert engine hooks (rule-5 audio boundary + UI side effects).
struct TutorialHooks {
    std::function<void(i32 form)>           selectWindow;       // VIBE_Form_SelectWindow(form,0)
    std::function<void(const char* tok)>    renderRichString;   // VIBE_Text_RenderRichString
    std::function<void(i32 form)>           setObjectsVisible;  // VIBE_Form_SetObjectsVisible(form,0)
    std::function<bool(i32 voice)>          voiceIsPlaying;     // VIBE_Audio_VoiceIsPlaying
    std::function<void(i32 voice)>          stopVoice;          // VIBE_Audio_StopVoice(voice,0)
};

// gilde.exe 0x5976a8 — VIBE_Tutorial_HideDonePanelAndStopVoice.
void Tutorial_HideDonePanelAndStopVoice(TutorialController& ctrl, const TutorialHooks& h);

} // namespace guild::play
