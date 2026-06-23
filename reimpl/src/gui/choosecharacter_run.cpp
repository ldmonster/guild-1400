#include "gui/choosecharacter_run.h"

namespace guild::gui {

namespace {
CharSceneRunHooks  g_defaultHooks;
CharSceneRunHooks* g_hooks = &g_defaultHooks;
}  // namespace

CharSceneRunHooks* Menu_SetChooseCharacterHooks(CharSceneRunHooks* hooks) {
    CharSceneRunHooks* prev = g_hooks;
    g_hooks = hooks ? hooks : &g_defaultHooks;
    return prev;
}

// gilde.exe 0x52bcd4 — VIBE_Menu_RunChooseCharacter.
int Menu_RunChooseCharacter(ChooseCharacterState& st, ChooseCharacterRecord* rec, int maxFrames) {
    CharSceneRunHooks* h = g_hooks;
    h->SceneSetup();
    st.close = 0; st.result = 0;
    if (rec) { rec->frames = 0; rec->actorsPlaced = 0; rec->completed = false;
               rec->talentRan = false; rec->professionRan = false; rec->previewRan = false;
               rec->started = false; rec->cancelled = false; }

    int frame = 0;
    while (true) {
        if (maxFrames >= 0 && frame >= maxFrames) break;
        if (!h->RunFrameLoop(frame)) break;

        if (h->WindowClosed(frame)) {                    // dword_672230 -> close
            st.close = 1;
            if (rec && !rec->completed) rec->cancelled = true;
        }

        if (!ChooseCharacter_IsComplete(st.fillCounter)) {
            const ChooseCharActor a = h->PickActor(frame);   // dword_631720 -> actor
            if (a != ChooseCharActor::kNone) {
                const int slot = ChooseCharacter_ApplyActorClick(st.dynasty, a, &st.fillCounter);
                if (slot >= 0) {
                    h->PlayActorAnim(a, slot);               // Command_Handler animation
                    if (rec) ++rec->actorsPlaced;
                }
            }
        }

        if (ChooseCharacter_IsComplete(st.fillCounter)) {    // v7 >= 6 -> the chain
            if (rec) rec->completed = true;
            if (!h->ChooseCharacterTalent()) {               // talent cancelled -> abort
                if (rec) rec->talentRan = true;
                st.result = 0; st.close = 1;
            } else {
                if (rec) rec->talentRan = true;
                const bool prof = h->ChooseProfession();
                if (rec) rec->professionRan = true;
                bool preview = false;
                if (prof) { preview = h->BuildCharacterPreviewScene(); if (rec) rec->previewRan = true; }
                if (prof && preview) {                       // success -> start
                    st.result = 1; st.close = 1;
                }
                // else: form re-shown, the loop retries the chain next frame
            }
        }

        ++frame;
        if (rec) rec->frames = frame;
        if (st.close) break;
    }

    h->SceneTeardown();
    if (rec) rec->started = (st.result == 1);
    return st.result;
}

} // namespace guild::gui
