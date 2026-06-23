#include "gui/choosehistory_run.h"

namespace guild::gui {

namespace {
CharHistoryRunHooks  g_defaultHooks;
CharHistoryRunHooks* g_hooks = &g_defaultHooks;
}  // namespace

CharHistoryRunHooks* Menu_SetChooseHistoryHooks(CharHistoryRunHooks* hooks) {
    CharHistoryRunHooks* prev = g_hooks;
    g_hooks = hooks ? hooks : &g_defaultHooks;
    return prev;
}

// gilde.exe 0x52d684 — VIBE_Menu_RunChooseHistory.
int Menu_RunChooseHistory(ChooseHistoryState& st, ChooseHistoryRecord* rec, int maxFrames) {
    CharHistoryRunHooks* h = g_hooks;

    // ---- build ----
    const int form = h->FormLoad("Menu\\CHOOSEHISTORY");
    h->FormCenter(form);
    h->FormSelectWindow(form, 0);
    const int base = h->RenderTitle(kChooseHistoryTitleTextId);
    int ids[kChooseHistoryRadioMembers];
    for (int k = 0; k < kChooseHistoryRadioMembers; ++k)
        ids[k] = h->ChildObjectId(form, base + k);
    const int group = h->RadioGroupCreate(kChooseHistoryRadioMembers, ids[0]);

    const int seedIdx = ChooseHistory_SeedIndex(st.historyMode);   // 0->2,1->0,2->1
    if (seedIdx >= 0) h->SelectionUpdate(group, seedIdx);

    if (rec) {
        rec->formLoaded = (form != -1); rec->formId = form; rec->group = group;
        for (int k = 0; k < kChooseHistoryRadioMembers; ++k) rec->radioIds[k] = ids[k];
        rec->frames = 0; rec->committedMode = false; rec->chosenFlag = -1;
        rec->dialogRan = false; rec->spineRan = false; rec->cancelled = false;
    }

    st.close = 0; st.result = 0; st.historyFlag = -1;

    int frame = 0;
    while (true) {
        if (maxFrames >= 0 && frame >= maxFrames) break;
        if (!h->RunFrameLoop(frame)) break;
        h->ReadInput(frame);

        const int key = h->KeyCode(frame);
        if (h->WindowClosed(frame) || key == kChooseHistoryCloseKey) {
            st.close = 1;
            if (rec && !rec->cancelled) rec->cancelled = true;
        }

        const int btn = h->ButtonId(frame);
        const int clicked = h->ClickedWidgetId(frame);
        if (btn == kChooseHistoryConfirmId || key == kChooseHistoryEnterKey) {
            // Map the active radio to the History flag (id0->1, id1->2, id2/Enter->0).
            int flag = -1;
            if      (clicked == ids[0]) flag = 1;
            else if (clicked == ids[1]) flag = 2;
            else if (clicked == ids[2] || key == kChooseHistoryEnterKey) flag = 0;
            if (flag >= 0) {
                h->SetHistoryFlag(flag);                 // History_SetActiveFlag
                st.historyFlag = flag;
                h->SelectionUpdate(group, ChooseHistory_FlagToIndex(flag));
                if (rec) { rec->committedMode = true; rec->chosenFlag = flag; }

                const int dlg = h->RunHistoryDialog();   // Mission_RunChooseHistoryDialog
                if (rec) rec->dialogRan = true;
                h->FormSetVisible(form, 0);
                if (dlg != kHistoryDialogCancel) {       // dialog confirmed -> the character spine
                    st.result = h->RunCharacterSpine();  // v8 (player->profession->character)
                    if (rec) rec->spineRan = true;
                    st.close = 1;                        // spine resolved -> exit
                }
                h->FormSetVisible(form, 1);              // dialog-cancel: form re-shown, stay
            }
            // no mode matched -> loop (no commit)
        } else if (btn == kChooseHistoryBackId) {
            st.close = 1;
            if (rec && !rec->cancelled) rec->cancelled = true;
        }

        ++frame;
        if (rec) rec->frames = frame;
        if (st.close) break;
    }

    h->RadioFree(group);
    h->FormDestroy(form);
    return st.result;   // v8 (word_63C740 |= 8 on success in the original)
}

} // namespace guild::gui
