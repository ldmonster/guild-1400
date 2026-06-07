#include "world/history_mission.h"

#include "world/event.h"          // g_eventTable / g_eventTableCount
#include "world/mission_rules.h"  // MissionFindDescriptorByValue (reused)

// Faithful 1:1 ports of the previously-untranslated VIBE_Mission_Run*Dialog frame
// loops. The deterministic decode cores already live in mission_rules.cpp /
// mission_dialog.cpp and are reused here; this file adds the per-tick exit/select
// decoders that the dialog bodies run, the driver bodies themselves, and the inert
// MissionDialogHooks defaults (the ScriptImportHooks / CutsceneMiscHooks pattern).

namespace guild::world {

// ---------------------------------------------------------------------------
// Installable hooks with inert defaults (defined in THIS library .cpp).
// ---------------------------------------------------------------------------
namespace {
const MissionDialogHooks* g_dialogHooks = nullptr;
MissionDialogHooks        g_inertDialogHooks{};   // all-null -> inert
}  // namespace

void SetMissionDialogHooks(const MissionDialogHooks* hooks) { g_dialogHooks = hooks; }
const MissionDialogHooks& GetMissionDialogHooks() {
    return g_dialogHooks ? *g_dialogHooks : g_inertDialogHooks;
}

// ---------------------------------------------------------------------------
// Per-tick decode helpers (pure; golden-vector testable).
// ---------------------------------------------------------------------------

// gilde.exe 0x5387c8 inner loop.
bool MissionSpecialStep(const MissionDialogFrame& f, int* outConfirmed) {
    bool exit = false;
    // if (dword_672230 || dword_75BF38 == 1155) dword_631614 = 1;
    if (f.skipGate || f.lastDialogResult == kMissionDialogDecline)
        exit = true;
    // if (dword_75BF38 == 1210) { v3 = 1; dword_631614 = 1; }
    if (f.lastDialogResult == kMissionDialogAccept) {
        if (outConfirmed) *outConfirmed = 1;
        exit = true;
    }
    return exit;
}

// gilde.exe 0x539e8c / 0x53a41c inner loops (ack-only panels).
bool MissionAckStep(const MissionDialogFrame& f) {
    // if (dword_672230) dword_631614 = 1;  if (dword_75BF38 == 1210) dword_631614 = 1;
    return f.skipGate != 0 || f.lastDialogResult == kMissionDialogAccept;
}

// gilde.exe 0x538950 inner loop.
MissionChooseAction MissionChooseStep(const MissionDialogFrame& f,
                                      i32 radioSelectedId) {
    // if (dword_672230 || byte_67225C == 1 || dword_75BF38 == 1155) dword_631614 = 1;
    if (f.skipGate || f.menuState == 1 || f.lastDialogResult == kMissionDialogDecline)
        return MissionChooseAction::kExit;
    // if (dword_75BF38 != -1) { ... if (dword_62D22C == radioId) run special ... }
    if (f.lastDialogResult != kMissionDialogNone) {
        if (radioSelectedId != -1 && f.clickedObjectId == radioSelectedId)
            return MissionChooseAction::kActivate;
    }
    return MissionChooseAction::kIdle;
}

// gilde.exe 0x53ac34 pre-tick guard.
bool MissionCompletionStep(i32 activeMissionId) {
    // if (dword_63CC24 == -1) dword_631614 = 1;
    return activeMissionId == -1;
}

// gilde.exe 0x53a854 inner loop button decode.
MissionOfferAction MissionOfferStep(const MissionDialogFrame& f,
                                    i32 giveButtonId,
                                    i32 declineButtonId,
                                    i32 abandonButtonId) {
    if (f.lastDialogResult == kMissionDialogNone)   // dword_75BF38 == -1 -> idle
        return MissionOfferAction::kIdle;
    const i32 click = f.clickedObjectId;            // dword_62D22C
    if (giveButtonId != -1 && click == giveButtonId)
        return MissionOfferAction::kGive;           // ChildObjectId == dword_62D22C
    if (click == declineButtonId)
        return MissionOfferAction::kDecline;        // v34 == dword_62D22C
    if (click == abandonButtonId)
        return MissionOfferAction::kAbandon;         // v13 == dword_62D22C
    return MissionOfferAction::kIdle;
}

bool MissionOfferTriggersReload(MissionOfferAction action) {
    return action == MissionOfferAction::kAbandon;   // sets dword_63CC30 -> reload
}

// ---------------------------------------------------------------------------
// Driver bodies — faithful structure over the hooks.
// ---------------------------------------------------------------------------

// gilde.exe 0x5387c8 — VIBE_Mission_RunSpecialDialog.
int MissionRunSpecialDialog(u8 value, int frameArg) {
    const MissionDialogHooks& h = GetMissionDialogHooks();
    int confirmed = 0;

    // Descriptor scan (0x5387f1): byte_63CD4C[24*i] == value, else v15 = 0.
    int idx = MissionFindDescriptorByValue(value);   // -1 when none

    int form = h.createForm ? h.createForm("special\\mission") : -1;  // VIBE_GameTick_Finalize
    if (h.centerChildWindows) h.centerChildWindows(form);
    if (h.selectWindow) h.selectWindow(form, 0);
    if (h.renderText) {
        // VIBE_Text_RenderRichString(*((_DWORD*)v15 + 1) + 1): the descriptor's
        // name text id (+1); inert when no descriptor matched.
        unsigned nameId = (idx >= 0)
            ? static_cast<unsigned>(g_eventTable[idx].value) + 1
            : 0u;
        h.renderText(nameId);
        h.renderText(0x7D);
        h.renderText(0x7E);
    }
    if (h.playVoice) h.playVoice(-7 /*0xFFFFFFF9*/, 0, "_AUFTRAEGE_VERGABE_HS_..");

    // while (RunFrameLoop) { decode }   (0x5388c9 .. 0x5388ff)
    MissionDialogFrame f;
    while (h.runFrameLoop ? h.runFrameLoop(frameArg, 1) : 0) {
        if (h.readFrame) h.readFrame(&f);
        if (MissionSpecialStep(f, &confirmed)) break;   // latch dword_631614
    }

    if (form != -1 && h.destroyForm) h.destroyForm(form);
    if (h.voiceIsPlaying && h.voiceIsPlaying(1) && h.stopVoice) h.stopVoice(25);
    return confirmed;   // v3
}

// gilde.exe 0x539e8c — VIBE_Mission_RunFailureDialog.
int MissionRunFailureDialog(u8 value, int frameArg) {
    const MissionDialogHooks& h = GetMissionDialogHooks();
    int idx = MissionFindDescriptorByValue(value);

    int form = h.createForm ? h.createForm("special\\mission") : -1;
    if (h.centerChildWindows) h.centerChildWindows(form);
    if (h.selectWindow) h.selectWindow(form, 0);
    if (h.renderText) {
        unsigned nameId = (idx >= 0)
            ? static_cast<unsigned>(g_eventTable[idx].value) + 1
            : 0u;
        h.renderText(nameId);
        h.renderText(0x7D);
    }
    if (h.playVoice) h.playVoice(-7, -1, "_AUFTRAEGE_VERGABE_HS_..");

    MissionDialogFrame f;
    while (h.runFrameLoop ? h.runFrameLoop(frameArg, 0) : 0) {
        if (h.readFrame) h.readFrame(&f);
        if (MissionAckStep(f)) break;
    }

    if (form != -1 && h.destroyForm) h.destroyForm(form);
    if (h.voiceIsPlaying && h.voiceIsPlaying(0) && h.stopVoice) h.stopVoice(25);
    return 0;   // original returns the inert StopVoice/IsPlaying result
}

// gilde.exe 0x53a41c — VIBE_Mission_RunInfoDialog.
int MissionRunInfoDialog(int frameArg) {
    const MissionDialogHooks& h = GetMissionDialogHooks();

    int form = h.createForm ? h.createForm("special\\mission") : -1;
    if (h.centerChildWindows) h.centerChildWindows(form);
    if (h.selectWindow) h.selectWindow(form, 0);
    if (h.renderText) h.renderText(0x17A6);
    if (h.selectWindow) h.selectWindow(form, 2);   // 0x53a466
    if (h.renderText) h.renderText(0x7D);

    // while (RunFrameLoop(a1|0x80, v4, a2))   (0x53a48f)
    MissionDialogFrame f;
    while (h.runFrameLoop ? h.runFrameLoop(frameArg | 0x80, form) : 0) {
        if (h.readFrame) h.readFrame(&f);
        if (MissionAckStep(f)) break;   // Info: same close condition
    }

    if (h.destroyForm) h.destroyForm(form);
    return form;   // return VIBE_Form_Destroy(v4)
}

}  // namespace guild::world
