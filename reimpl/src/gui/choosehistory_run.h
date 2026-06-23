#pragma once
// guild::gui — the "choose history / perspective" screen, reconstructed 1:1.
//
//   VIBE_Menu_RunChooseHistory @0x52d684 — the screen the new-game funnel runs right after
//   ChooseCharacterIntroVariant (the difficulty pick). Form "Menu\\CHOOSEHISTORY"; its title
//   + options are VIBE_Text_RenderRichString(0x16CD, 5839,5840,5841, 5839,5840,5841, 1155)
//   == text entry `_M0_HISTORIE` with the three mode names `_M0_HISTORIE_MODUS+0..2`
//   substituted: a radio group of FOUR (three selectable perspective modes + a non-selecting
//   "Назад" back member).
//
// THE FAITHFUL CONTROL FLOW (recovered from the decompile):
//   form = GameTick_Finalize("Menu\\CHOOSEHISTORY"); CenterChildWindows; SelectWindow(0)
//   base = Text_RenderRichString(0x16CD, ...); id0..id3 = GetChildObjectId(form,0,base+k)
//   group = RadioGroup_Create(4, id0)
//   seed: switch(dword_12335AC) { 0 -> idx 2 ; 1 -> idx 0 ; 2 -> idx 1 } Selection_Update
//   while (GameLogic_RunFrameLoop(198, group)) {
//       InitStateReader()
//       if (dword_672230 || byte_67225C==1) dword_631614 = 1            ; window-close
//       if (dword_75BF38==1210 || byte_67225C==28) {                    ; OK / Enter
//           if      (dword_62D22C==id0)            History_SetActiveFlag(1)
//           else if (dword_62D22C==id1)            History_SetActiveFlag(2)
//           else if (dword_62D22C==id2||key==28)   History_SetActiveFlag(0)
//           re-derive Selection_Update from the active flag (flag 0->idx2,1->idx0,2->idx1)
//           v36 = Mission_RunChooseHistoryDialog()                       ; 0xFF == cancel
//           Form_SetObjectsVisible(form,0)
//           if (v36 != 0xFF) {                                           ; dialog confirmed
//               dword_122F4EC = v36
//               cutscene B_Persoenliches; <character spine> -> v8        ; RunChoosePlayer ->
//                                                                          ChooseCharacterIntro ->
//                                                                          (RunChooseCharacter |
//                                                                           ChooseProfession)
//           }
//           Form_SetObjectsVisible(form,1)                               ; dialog-cancel stays
//       } else if (dword_75BF38==1155) dword_631614 = 1                  ; back
//   }
//   RadioGroup_FreeSurface; Form_Destroy
//   if (v8) { word_63C740 |= 8; return v8; } else { word_63C740 = 0; return 0; }
//
// The radio screen + the seed/flag mapping is the precise 1:1 part. The post-dialog
// character SPINE (RunChoosePlayer @0x52ccd8 -> ChooseCharacterIntro @0x52e3d8 ->
// RunChooseCharacter @0x52bcd4 | ChooseProfession @0x52c50c, with the original's v9/v10
// retry loop) is modeled at the host boundary as RunCharacterSpine() returning the final v8
// (1 = start, 0 = abort), to be expanded into its own reconstructions next.
//
// HOST BOUNDARIES go through CharHistoryRunHooks with INERT DEFAULTS in the .cpp.
#include <cstdint>

namespace guild::gui {

inline constexpr int kChooseHistoryRadioMembers = 4;     // 3 modes + back
inline constexpr int kChooseHistoryTitleTextId  = 0x16CD; // _M0_HISTORIE
inline constexpr int kChooseHistoryConfirmId    = 1210;
inline constexpr int kChooseHistoryBackId       = 1155;
inline constexpr int kChooseHistoryEnterKey     = 28;
inline constexpr int kChooseHistoryCloseKey     = 1;
inline constexpr int kHistoryDialogCancel       = 0xFF;  // Mission_RunChooseHistoryDialog cancel

// dword_12335AC stored mode (0/1/2) -> the pre-selected radio index.
inline int ChooseHistory_SeedIndex(int mode) {
    return (mode == 0) ? 2 : (mode == 1) ? 0 : (mode == 2) ? 1 : -1;
}
// History_SetActiveFlag arg (1/2/0) -> the radio index it re-selects.
inline int ChooseHistory_FlagToIndex(int flag) {
    return (flag == 0) ? 2 : (flag == 1) ? 0 : 1;
}

struct ChooseHistoryState {
    int historyMode = 0;   // dword_12335AC seed (0/1/2)
    int historyFlag = -1;  // committed History_SetActiveFlag arg (1/2/0); -1 = none
    int close  = 0;        // dword_631614
    int result = 0;        // v8 (1 = the character spine reached "start")
};

struct CharHistoryRunHooks {
    virtual ~CharHistoryRunHooks() = default;

    virtual int  FormLoad(const char* name) { (void)name; return -1; }   // GameTick_Finalize
    virtual void FormCenter(int form) { (void)form; }
    virtual void FormSelectWindow(int form, int window) { (void)form; (void)window; }
    virtual int  RenderTitle(int textId) { (void)textId; return 0; }      // -> base child index
    virtual int  ChildObjectId(int form, int objIndex) { (void)form; return objIndex; }
    virtual int  RadioGroupCreate(int count, int firstId) { (void)count; (void)firstId; return 0; }
    virtual void SelectionUpdate(int group, int value) { (void)group; (void)value; }

    virtual int  RunFrameLoop(int frame) { (void)frame; return 0; }
    virtual void ReadInput(int frame) { (void)frame; }
    virtual bool WindowClosed(int frame) { (void)frame; return false; }
    virtual int  KeyCode(int frame) { (void)frame; return 0; }
    virtual int  ButtonId(int frame) { (void)frame; return 0; }
    virtual int  ClickedWidgetId(int frame) { (void)frame; return 0; }

    virtual void SetHistoryFlag(int flag) { (void)flag; }             // History_SetActiveFlag
    virtual void FormSetVisible(int form, int visible) { (void)form; (void)visible; }
    virtual int  RunHistoryDialog() { return kHistoryDialogCancel; }  // Mission_RunChooseHistoryDialog
    virtual int  RunCharacterSpine() { return 0; }                    // player->profession->character (v8)

    virtual void RadioFree(int group) { (void)group; }
    virtual void FormDestroy(int form) { (void)form; }
};

CharHistoryRunHooks* Menu_SetChooseHistoryHooks(CharHistoryRunHooks* hooks);

struct ChooseHistoryRecord {
    bool formLoaded = false;
    int  formId = -1, group = 0;
    int  radioIds[kChooseHistoryRadioMembers] = {0,0,0,0};
    int  frames = 0;
    bool committedMode = false;   // a perspective mode was picked (History flag set)
    int  chosenFlag = -1;         // the committed flag (1/2/0)
    bool dialogRan = false;       // RunHistoryDialog was invoked
    bool spineRan = false;        // RunCharacterSpine was invoked (dialog confirmed)
    bool cancelled = false;       // back / window-close
};

// gilde.exe 0x52d684 — VIBE_Menu_RunChooseHistory. Returns the character-spine v8 (1 = start,
// 0 = no start). `maxFrames` (>=0) bounds the headless loop.
int Menu_RunChooseHistory(ChooseHistoryState& st, ChooseHistoryRecord* rec, int maxFrames);

} // namespace guild::gui
