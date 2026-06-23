#pragma once
// guild::gui — the "choose character intro variant" screen, reconstructed 1:1.
//
//   VIBE_Menu_ChooseCharacterIntroVariant @0x52e4e0 — the screen the NEW-GAME funnel runs
//   immediately after the city is confirmed (gui::Menu_RunChooseCity chains it, then
//   RunChooseHistory). Form "menu\\choosecharacter_intro" (window 128,72,441,490); its
//   content (title + the radio options) is injected by VIBE_Text_RenderRichString(0x16CC).
//
// THE FAITHFUL CONTROL FLOW (recovered from the decompile + the 0x52e60f tail):
//   form  = GameTick_Finalize(0,0,"menu\\choosecharacter_intro")   ; 0x52e4e7
//   Form_CenterChildWindows(form)                                   ; 0x52e4f3
//   dword_63C744 = (u8)byte_12335BA                                 ; seed the selection
//   base = Text_RenderRichString(0x16CC)                            ; -> v8 child-id base
//   id0..id5 = Form_GetChildObjectId(form, 0, base+0 .. base+5)     ; the six radio members
//   group = RadioGroup_Create(6, id0)                               ; 0x52e58c
//   Selection_Update(group, dword_63C744)                           ; show the seed choice
//   while (GameLogic_RunFrameLoop(198, group)) {                    ; 0x52e5d0
//       InitStateReader()
//       if (dword_672230 || byte_67225C == 1) dword_631614 = 1      ; window-close -> exit, ret 0
//       if (dword_75BF38 == 1210 || byte_67225C == 28) {            ; OK button / Enter
//           if      (dword_62D22C == id0) dword_63C744 = 0
//           else if (dword_62D22C == id1) dword_63C744 = 1
//           else if (dword_62D22C == id2) dword_63C744 = 2
//           else if (dword_62D22C == id3) dword_63C744 = 3
//           else if (dword_62D22C == id4) dword_63C744 = 4
//           ret = 1; dword_631614 = 1                               ; commit -> exit, ret 1
//       } else if (dword_75BF38 == 1155) dword_631614 = 1           ; back -> exit, ret 0
//   }
//   byte_12335BA = dword_63C744                                     ; persist the selection
//   RadioGroup_FreeSurface(group); Form_Destroy(form); return ret   ; 0x52e60f tail (eax)
//
// The return is 1 on a committed pick (OK/Enter on a radio member), 0 on window-close /
// the back button (1155). The caller (Menu_RunChooseCity) proceeds to RunChooseHistory
// only when this returns nonzero. The selection (0..4) round-trips through byte_12335BA.
//
// HOST BOUNDARIES — the form/radio build leaves, the RunFrameLoop tick and the input edge
// sources (dword_62D22C / dword_75BF38 / byte_67225C / dword_672230) — go through an
// installable CharIntroRunHooks block with INERT DEFAULTS in the .cpp, so the function
// links in the unified build and is fully testable headless.
#include <cstdint>

namespace guild::gui {

// The number of intro variants the screen selects between (radio members 0..4). The radio
// group is created with six members (the 6th is a non-selecting decoration in the form).
inline constexpr int kCharIntroVariantCount = 5;
inline constexpr int kCharIntroRadioMembers = 6;
inline constexpr int kCharIntroTitleTextId  = 0x16CC;  // 5836
inline constexpr int kCharIntroConfirmId    = 1210;    // _BUTTON_RED OK (dword_75BF38)
inline constexpr int kCharIntroBackId       = 1155;    // back button
inline constexpr int kCharIntroEnterKey     = 28;      // byte_67225C == 0x1C
inline constexpr int kCharIntroCloseKey     = 1;       // byte_67225C == 1 (window close)

// Reconstructed state: the byte_12335BA / dword_63C744 selection that round-trips.
struct CharIntroState {
    int introVariant = 0;   // seed in / result out (clamped to a byte, 0..4)
    int close  = 0;         // dword_631614 (armed on confirm OR cancel)
    int result = 0;         // the function's eax (1 = committed, 0 = cancelled)
};

// Host-boundary leaves (INERT DEFAULTS in the .cpp).
struct CharIntroRunHooks {
    virtual ~CharIntroRunHooks() = default;

    // VIBE_GameTick_Finalize(0,0,name) — build the form, return its handle (-1 == none).
    virtual int  FormLoad(const char* name) { (void)name; return -1; }
    // VIBE_Form_CenterChildWindows(form).
    virtual void FormCenter(int form) { (void)form; }
    // VIBE_Text_RenderRichString(textId) — inject the title + radio markup; returns the
    // base child-object index (v8) the GetChildObjectId calls index from.
    virtual int  RenderTitle(int textId) { (void)textId; return 0; }
    // VIBE_Form_GetChildObjectId(form, 0, objIndex) — the widget id for a child object.
    virtual int  ChildObjectId(int form, int objIndex) { (void)form; return objIndex; }
    // VIBE_RadioGroup_Create(count, firstId) — make a radio group; returns its handle.
    virtual int  RadioGroupCreate(int count, int firstId) { (void)count; (void)firstId; return 0; }
    // VIBE_Selection_Update(group, value) — pre-select radio member `value`.
    virtual void SelectionUpdate(int group, int value) { (void)group; (void)value; }

    // VIBE_GameLogic_RunFrameLoop(198, group) — present a frame; 0 ends the loop.
    virtual int  RunFrameLoop(int frame) { (void)frame; return 0; }
    // VIBE_InitStateReader() — latch this frame's input edges.
    virtual void ReadInput(int frame) { (void)frame; }
    virtual bool WindowClosed(int frame) { (void)frame; return false; }   // dword_672230
    virtual int  KeyCode(int frame) { (void)frame; return 0; }            // byte_67225C
    virtual int  ButtonId(int frame) { (void)frame; return 0; }           // dword_75BF38
    virtual int  ClickedWidgetId(int frame) { (void)frame; return 0; }    // dword_62D22C

    // VIBE_RadioGroup_FreeSurface(group) + VIBE_Form_Destroy(form).
    virtual void RadioFree(int group) { (void)group; }
    virtual void FormDestroy(int form) { (void)form; }
};

// Install hooks (null restores the inert defaults). Returns the previous hooks.
CharIntroRunHooks* Menu_SetCharIntroHooks(CharIntroRunHooks* hooks);

// Per-run trace/record for tests + the native bridge.
struct CharIntroRecord {
    bool formLoaded = false;
    int  formId = -1;
    int  group = 0;
    int  radioIds[kCharIntroRadioMembers] = {0,0,0,0,0,0};
    int  frames = 0;
    bool confirmed = false;     // a commit edge fired (OK/Enter on a radio)
    bool cancelled = false;     // window-close / back
    int  chosenVariant = -1;    // the committed 0..4 (==introVariant on confirm)
};

// gilde.exe 0x52e4e0 — VIBE_Menu_ChooseCharacterIntroVariant.
// Returns 1 on a committed pick, 0 on cancel. `maxFrames` (>=0) bounds the headless loop.
int Menu_RunChooseCharacterIntroVariant(CharIntroState& st, CharIntroRecord* rec, int maxFrames);

// ---------------------------------------------------------------------------
// gilde.exe 0x52e3d8 — VIBE_Menu_ChooseCharacterIntro: the spine ROUTER reached from
// RunChooseHistory's character spine. Same form "menu\\choosecharacter_intro", but the
// markup is `_M0_PERSOENLICH_CHARAKTER` (0x16EC heading + the conditional 0x16ED body +
// 0x16EE options) — "Выберите предков!": a two-button choice
//     %ia[Задать генеалогическое древо]  (OK / 1210 / Enter) -> return 1  (-> the dynasty
//                                          scene RunChooseCharacter @0x52bcd4)
//     %in[Автоматически]                 (back / 1155)        -> return 0  (-> ChooseProfession)
// window-close / ESC(1) -> return -1 (abort, the caller treats it as "back a step").
// The return is purely BUTTON-driven (the radio-of-2 is visual); v2 inits 0.
inline constexpr int kCharIntroChooseTitleId = 0x16EC;  // _M0_PERSOENLICH_CHARAKTER+0 (heading+body)
inline constexpr int kCharIntroChooseBodyId  = 0x16ED;  // +1 (conditional second body line)
inline constexpr int kCharIntroChooseOptsId  = 0x16EE;  // +2 (the two option buttons)
inline constexpr int kCharIntroChooseManual  = 1;       // return: define family tree
inline constexpr int kCharIntroChooseAuto    = 0;       // return: automatic (-> profession)
inline constexpr int kCharIntroChooseBack    = -1;      // return: window-close / ESC

// Returns 1 (manual dynasty), 0 (automatic), or -1 (abort). `maxFrames` (>=0) bounds the
// headless loop. Drives the same CharIntroRunHooks leaves (form/input).
int Menu_RunChooseCharacterIntro(CharIntroRecord* rec, int maxFrames);

} // namespace guild::gui
