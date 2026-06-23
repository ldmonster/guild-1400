#include "gui/choosecharacter_intro_run.h"

namespace guild::gui {

// Inert default hooks (so the function links in the unified build headless).
namespace {
CharIntroRunHooks  g_defaultHooks;
CharIntroRunHooks* g_hooks = &g_defaultHooks;
}  // namespace

CharIntroRunHooks* Menu_SetCharIntroHooks(CharIntroRunHooks* hooks) {
    CharIntroRunHooks* prev = g_hooks;
    g_hooks = hooks ? hooks : &g_defaultHooks;
    return prev;
}

// gilde.exe 0x52e4e0 — VIBE_Menu_ChooseCharacterIntroVariant.
int Menu_RunChooseCharacterIntroVariant(CharIntroState& st, CharIntroRecord* rec, int maxFrames) {
    CharIntroRunHooks* h = g_hooks;

    // ---- build (0x52e4e7..0x52e5b9) ----
    const int form = h->FormLoad("menu\\choosecharacter_intro");   // GameTick_Finalize(form)
    h->FormCenter(form);                                            // Form_CenterChildWindows
    int sel = st.introVariant & 0xFF;                              // dword_63C744 = (u8)byte_12335BA

    const int base = h->RenderTitle(kCharIntroTitleTextId);        // Text_RenderRichString(0x16CC) -> v8
    int ids[kCharIntroRadioMembers];
    for (int k = 0; k < kCharIntroRadioMembers; ++k)               // Form_GetChildObjectId(form,0,base+k)
        ids[k] = h->ChildObjectId(form, base + k);
    const int group = h->RadioGroupCreate(kCharIntroRadioMembers, ids[0]);  // RadioGroup_Create(6, id0)
    h->SelectionUpdate(group, sel);                                // Selection_Update(group, sel)

    if (rec) {
        rec->formLoaded = (form != -1);
        rec->formId = form;
        rec->group = group;
        for (int k = 0; k < kCharIntroRadioMembers; ++k) rec->radioIds[k] = ids[k];
        rec->frames = 0; rec->confirmed = false; rec->cancelled = false; rec->chosenVariant = -1;
    }

    st.close = 0; st.result = 0;

    // ---- the do/while RunFrameLoop loop (0x52e5d0..0x52e60e) ----
    int frame = 0;
    while (true) {
        if (maxFrames >= 0 && frame >= maxFrames) break;           // headless bound
        if (!h->RunFrameLoop(frame)) break;                        // dword_631614 -> RunFrameLoop returns 0
        h->ReadInput(frame);                                       // InitStateReader

        // window-close edge: dword_672230 || byte_67225C == 1 -> close (ret stays 0).
        const int key = h->KeyCode(frame);                         // byte_67225C
        if (h->WindowClosed(frame) || key == kCharIntroCloseKey) {
            st.close = 1;
            if (rec && !rec->cancelled) rec->cancelled = true;
        }

        const int btn = h->ButtonId(frame);                        // dword_75BF38
        const int clicked = h->ClickedWidgetId(frame);             // dword_62D22C
        if (btn == kCharIntroConfirmId || key == kCharIntroEnterKey) {
            // OK / Enter (0x52e63e..0x52e6d3): the selection (dword_63C744) is updated ONLY
            // when the active widget (dword_62D22C) matches a radio member id0..id4. But the
            // COMMIT is UNCONDITIONAL — the disasm's last compare (0x52e6c7 cmp esi,id4; jnz
            // loc_52E64C) jumps to the commit point loc_52E64C even when nothing matched. So
            // OK/Enter always sets eax=1 / dword_631614=1 and exits, carrying whatever
            // selection is current (the seed, if no member was the active widget this frame).
            int variant = -1;
            for (int k = 0; k < kCharIntroVariantCount; ++k)
                if (clicked == ids[k]) { variant = k; break; }
            if (variant >= 0)
                sel = variant;                                     // dword_63C744 = k
            st.result = 1;                                         // eax = 1 (loc_52E64C)
            st.close = 1;                                          // dword_631614 = 1
            if (rec) { rec->confirmed = true; rec->chosenVariant = sel; }
        } else if (btn == kCharIntroBackId) {
            st.close = 1;                                          // back -> exit, ret 0
            if (rec && !rec->cancelled) rec->cancelled = true;
        }

        ++frame;
        if (rec) rec->frames = frame;
        if (st.close) break;                                       // dword_631614 set -> loop exits
    }

    // ---- persist + cleanup (0x52e5f0..0x52e60e) ----
    st.introVariant = sel;                                         // byte_12335BA = dword_63C744
    h->RadioFree(group);                                           // RadioGroup_FreeSurface
    h->FormDestroy(form);                                          // Form_Destroy
    return st.result;                                              // eax (1 = committed, 0 = cancel)
}

// gilde.exe 0x52e3d8 — VIBE_Menu_ChooseCharacterIntro (the spine router).
int Menu_RunChooseCharacterIntro(CharIntroRecord* rec, int maxFrames) {
    CharIntroRunHooks* h = g_hooks;

    const int form = h->FormLoad("menu\\choosecharacter_intro");   // GameTick_Finalize
    h->FormCenter(form);                                           // Form_CenterChildWindows
    h->RenderTitle(kCharIntroChooseTitleId);                       // 0x16EC heading + body
    // (the 0x16ED second body line is rendered conditionally by the host; cosmetic here)
    const int base = h->RenderTitle(kCharIntroChooseOptsId);       // 0x16EE -> the two buttons
    const int id0 = h->ChildObjectId(form, base + 0);
    const int id1 = h->ChildObjectId(form, base + 1);
    const int group = h->RadioGroupCreate(2, id0);                 // RadioGroup_Create(2, id0)

    if (rec) {
        rec->formLoaded = (form != -1); rec->formId = form; rec->group = group;
        rec->radioIds[0] = id0; rec->radioIds[1] = id1;
        rec->frames = 0; rec->confirmed = false; rec->cancelled = false; rec->chosenVariant = -1;
    }

    int result = kCharIntroChooseAuto;   // v2 = 0 (default / back button)
    int frame = 0;
    while (true) {
        if (maxFrames >= 0 && frame >= maxFrames) break;
        if (!h->RunFrameLoop(frame)) break;
        h->ReadInput(frame);

        const int key = h->KeyCode(frame);
        if (h->WindowClosed(frame) || key == kCharIntroCloseKey)   // window-close / ESC -> -1
            result = kCharIntroChooseBack;
        const int btn = h->ButtonId(frame);
        bool close = (h->WindowClosed(frame) || key == kCharIntroCloseKey);
        if (btn == kCharIntroConfirmId || key == kCharIntroEnterKey) {   // OK / Enter -> 1
            result = kCharIntroChooseManual; close = true;               // loc_52E4AB: esi = 1
        } else if (btn == kCharIntroBackId) {                            // back 1155
            // 0x52e4d3: only arms dword_631614 — esi (result) is LEFT UNTOUCHED. So back is
            // 0 in the common case (esi inits 0) but stays -1 if window-close already fired
            // this frame. Do NOT force result to 0 here, or that edge would diverge.
            close = true;
        }
        ++frame;
        if (rec) rec->frames = frame;
        if (close) break;
    }

    if (rec) {
        rec->confirmed = (result == kCharIntroChooseManual);
        rec->cancelled = (result == kCharIntroChooseBack);
        rec->chosenVariant = result;
    }
    h->RadioFree(group);
    h->FormDestroy(form);
    return result;
}

} // namespace guild::gui
