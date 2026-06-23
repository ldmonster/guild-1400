// guild::play — VIBE_Tutorial_HideDonePanelAndStopVoice reconstruction.
// gilde.exe 0x5976a8. Disasm-faithful: the +0x10 voice handle is gated, stopped
// if playing, and cleared; the +0x30 panel form is selected then hidden.

#include "play/cutscene_recon2_tutorial.h"

namespace guild::play {

void Tutorial_HideDonePanelAndStopVoice(TutorialController& ctrl, const TutorialHooks& h) {
    // 0x5976b2: eax = ecx[+0x30]; VIBE_Form_SelectWindow(form, 0).
    if (h.selectWindow) h.selectWindow(ctrl.panelForm);
    // 0x5976bf: VIBE_Text_RenderRichString("$C").
    if (h.renderRichString) h.renderRichString(kTutorialClearToken);
    // 0x5976cc: VIBE_Form_SetObjectsVisible(form, 0).
    if (h.setObjectsVisible) h.setObjectsVisible(ctrl.panelForm);

    // 0x5976d1: edx = ecx[+0x10]; if ( !edx ) return.
    if (ctrl.voiceHandle != 0) {
        // 0x5976dd: VIBE_Audio_VoiceIsPlaying(voice); jz skip stop.
        const bool playing = h.voiceIsPlaying ? h.voiceIsPlaying(ctrl.voiceHandle) : false;
        if (playing) {
            // 0x5976eb: VIBE_Audio_StopVoice(voice, 0).
            if (h.stopVoice) h.stopVoice(ctrl.voiceHandle);
        }
        // 0x5976f0: *off_5953F0[+0x10] = 0.
        ctrl.voiceHandle = 0;
    }
}

} // namespace guild::play
