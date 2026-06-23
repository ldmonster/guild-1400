#pragma once
// guild::gui — the "choose player" identity wizard, reconstructed 1:1.
//
//   VIBE_Menu_RunChoosePlayer @0x52ccd8 — the first character-spine screen (run by
//   RunChooseHistory after the history dialog). Form "Menu\\CHOOSEPLAYER" (7 windows); a
//   PAGE WIZARD whose current page is `v2` (0..5), one form window per page:
//     page 0  Vorname        text entry  (window 1, title 0x16DC/0x16DD)  -> firstName
//     page 1  Nachname       text entry  (window 2, title 0x16DE)         -> familyName
//     page 2  Geschlecht     radio of 2  (window 3, title 0x16E1)         -> gender 0/1
//     page 3  Glauben        radio of 2  (window 4, title 0x16E4)         -> faith  0/1
//     page 4  Wappen         8-button    (window 5, title 0x16E8)         -> wappenIndex 0..7
//     page 5  (confirm)      window 6, title 0x7E — auto-commits (case 5 always sets v70=1)
//
// THE FAITHFUL CONTROL FLOW (recovered from the decompile):
//   - On entry the [Network] identity is read from gilde.INI: Name (Vorname), Familienname
//     (Nachname), Wappen (+1342), Geschlecht, Glauben.
//   - Each frame: the close edge (dword_672230 window-close, the back-button v61 == the
//     clicked widget, or byte_67225C==1 ESC) STEPS BACK one page (`--v2`) when v2>0, else
//     arms dword_631614 (exit, result v70 stays 0).
//   - Otherwise the page's own input advances it: Enter (28) commits a text page; a radio
//     click commits gender/faith; a wappen-gfx click (1342..1349) selects + advances.
//   - Page 5 (case 5) runs unconditionally the frame v2 reaches 5: it commits the wappen
//     into dword_122F4A4, sets v70=1 and arms dword_631614 (exit). So picking the wappen
//     ends the wizard.
//   - On exit the identity is written back to gilde.INI. Returns v70 (1 = completed).
//
// The PAGE STATE MACHINE + the per-page value collection is the 1:1 part. The per-page
// input source (text-field contents, the radio/wappen click) + the INI read/write go
// through ChoosePlayerRunHooks (INERT DEFAULTS in the .cpp), so the wizard links + is fully
// testable headless. The native (SDL) wizard — which needs a text-input stream — is the
// front end built on top of this.
#include <string>

namespace guild::gui {

// Form-window titles (rich-text markup ids) for documentation / the native front.
inline constexpr int kChoosePlayerTitleVorname  = 0x16DC;
inline constexpr int kChoosePlayerTitleVornameLbl= 0x16DD;  // arg 5855
inline constexpr int kChoosePlayerTitleNachname = 0x16DE;   // arg 5856
inline constexpr int kChoosePlayerTitleGender   = 0x16E1;
inline constexpr int kChoosePlayerTitleFaith    = 0x16E4;
inline constexpr int kChoosePlayerTitleWappen   = 0x16E8;
inline constexpr int kChoosePlayerTitleConfirm  = 0x7E;
inline constexpr int kChoosePlayerPageCount     = 6;        // 0..5 (page 5 = auto-commit)
inline constexpr int kChoosePlayerConfirmPage   = 5;
inline constexpr int kChoosePlayerWappenCount    = 8;

// The reconstructed identity + page (the [Network] block + v2 / v70).
struct ChoosePlayerState {
    int         page = 0;          // v2 (0..5)
    std::string firstName;         // String / [Network] Name
    std::string familyName;        // byte_122F4CA / [Network] Familienname
    int         gender = 0;        // byte_122F4A8 (0/1)
    int         faith  = 0;        // byte_122F4A9 (0/1)
    int         wappenIndex = 0;   // dword_122F4A4 - 1342 (0..7)
    int         close  = 0;        // dword_631614
    int         result = 0;        // v70 (1 = completed)
};

// One frame's interaction with the CURRENT page (what the host detected).
enum class PlayerPageAction {
    kNone,      // nothing this frame
    kAdvance,   // the page was completed (Enter / radio click / wappen click)
    kBack,      // back button / window-close / ESC -> step back (or exit at page 0)
};

struct ChoosePlayerRunHooks {
    virtual ~ChoosePlayerRunHooks() = default;

    // VIBE_GameTick_Finalize(form) / Form_Destroy.
    virtual int  FormLoad(const char* name) { (void)name; return -1; }
    virtual void FormDestroy(int form) { (void)form; }
    // GetPrivateProfileString/Int([Network] …) -> seed st; WritePrivateProfileString on exit.
    virtual void ReadIniDefaults(ChoosePlayerState& st) { (void)st; }
    virtual void WriteIni(const ChoosePlayerState& st) { (void)st; }

    // VIBE_GameLogic_RunFrameLoop — 0 ends the wizard.
    virtual int  RunFrameLoop(int frame) { (void)frame; return 0; }
    // The action the host detected for `page` this frame.
    virtual PlayerPageAction PageAction(int page, int frame) { (void)page; (void)frame; return PlayerPageAction::kNone; }
    // The value a value-page yields when it ADVANCES:
    virtual std::string GetText(int page) { (void)page; return std::string(); }  // page 0/1
    virtual int  GetChoice(int page) { (void)page; return 0; }                   // page 2/3/4
};

ChoosePlayerRunHooks* Menu_SetChoosePlayerHooks(ChoosePlayerRunHooks* hooks);

struct ChoosePlayerRecord {
    bool formLoaded = false;
    int  formId = -1;
    int  frames = 0;
    int  maxPageReached = 0;
    bool committed = false;     // v70 == 1
    bool cancelled = false;     // exited from page 0 without completing
};

// gilde.exe 0x52ccd8 — VIBE_Menu_RunChoosePlayer. Returns 1 on a completed identity, 0 on
// cancel. `maxFrames` (>=0) bounds the headless loop.
int Menu_RunChoosePlayer(ChoosePlayerState& st, ChoosePlayerRecord* rec, int maxFrames);

} // namespace guild::gui
