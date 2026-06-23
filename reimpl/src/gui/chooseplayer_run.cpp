#include "gui/chooseplayer_run.h"

namespace guild::gui {

namespace {
ChoosePlayerRunHooks  g_defaultHooks;
ChoosePlayerRunHooks* g_hooks = &g_defaultHooks;
}  // namespace

ChoosePlayerRunHooks* Menu_SetChoosePlayerHooks(ChoosePlayerRunHooks* hooks) {
    ChoosePlayerRunHooks* prev = g_hooks;
    g_hooks = hooks ? hooks : &g_defaultHooks;
    return prev;
}

// gilde.exe 0x52ccd8 — VIBE_Menu_RunChoosePlayer.
int Menu_RunChoosePlayer(ChoosePlayerState& st, ChoosePlayerRecord* rec, int maxFrames) {
    ChoosePlayerRunHooks* h = g_hooks;

    h->ReadIniDefaults(st);                       // [Network] seeds
    const int form = h->FormLoad("Menu\\CHOOSEPLAYER");
    st.page = 0; st.result = 0; st.close = 0;
    if (rec) {
        rec->formLoaded = (form != -1); rec->formId = form;
        rec->frames = 0; rec->maxPageReached = 0; rec->committed = false; rec->cancelled = false;
    }

    int frame = 0;
    while (true) {
        if (maxFrames >= 0 && frame >= maxFrames) break;
        if (!h->RunFrameLoop(frame)) break;

        if (st.page >= kChoosePlayerConfirmPage) {
            // case 5: reaching the confirm page commits unconditionally (v70 = 1).
            st.result = 1; st.close = 1;
        } else {
            const PlayerPageAction a = h->PageAction(st.page, frame);
            if (a == PlayerPageAction::kBack) {
                if (st.page > 0) --st.page;        // step back a page
                else st.close = 1;                 // exit at page 0 (result stays 0)
            } else if (a == PlayerPageAction::kAdvance) {
                switch (st.page) {                 // collect this page's value
                    case 0: st.firstName  = h->GetText(0); break;
                    case 1: st.familyName = h->GetText(1); break;
                    case 2: st.gender = h->GetChoice(2) & 1; break;
                    case 3: st.faith  = h->GetChoice(3) & 1; break;
                    case 4: {
                        int w = h->GetChoice(4);
                        if (w < 0) w = 0; if (w >= kChoosePlayerWappenCount) w = kChoosePlayerWappenCount - 1;
                        st.wappenIndex = w;
                    } break;
                    default: break;
                }
                ++st.page;
            }
        }

        if (rec && st.page > rec->maxPageReached) rec->maxPageReached = st.page;
        ++frame;
        if (rec) rec->frames = frame;
        if (st.close) break;
    }

    h->FormDestroy(form);
    h->WriteIni(st);                              // the original always writes [Network] on exit
    if (rec) { rec->committed = (st.result == 1); rec->cancelled = (st.result == 0); }
    return st.result;
}

} // namespace guild::gui
