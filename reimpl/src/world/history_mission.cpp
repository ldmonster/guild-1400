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

// ---------------------------------------------------------------------------
// RewardSummary / Completion / Offer decode helpers + bodies (W16 reconstruct).
// ---------------------------------------------------------------------------

namespace {
// gilde.exe 0x539fd8 — the four per-line frame loops share one body. With audio on:
//   handle = PlayPositionalSample(-8, slot, "AUFTRAEGE_ALLGEMEIN");
//   while (VoiceIsPlaying(handle)) {
//     RunFrameLoop;
//     if (dword_75BF38 != -1 && childId == dword_62D22C) { if playing StopVoice; break; }
//   }
// With audio off (dword_62EB38 + 250 > dword_62EB38, i.e. always while ticks advance):
//   do RunFrameLoop while ((dword_75BF38 == -1 || childId != dword_62D22C) && deadline > tick);
// The 4th line is identical except the early-exit returns Form_Destroy directly and
// it uses the ERFOLG sample. We model the deadline math (250-tick budget) exactly.
void RunRewardLine(const MissionDialogHooks& h, int form, int frameArg,
                   int slot, const char* sample, int childId) {
    MissionDialogFrame f;
    if (h.audioIsInitialized && h.audioIsInitialized()) {
        int handle = h.playPositionalSample
                         ? h.playPositionalSample(-8 /*0xFFFFFFF8*/, slot, sample)
                         : 0;
        while (h.voiceHandleIsPlaying ? h.voiceHandleIsPlaying(handle) : 0) {
            if (h.runFrameLoop) h.runFrameLoop(frameArg, frameArg);
            if (h.readFrame) h.readFrame(&f);
            // if (dword_75BF38 != -1 && childId == dword_62D22C) { stop; break; }
            if (f.lastDialogResult != kMissionDialogNone && childId == f.clickedObjectId) {
                if (h.voiceHandleIsPlaying && h.voiceHandleIsPlaying(handle) && h.stopVoice)
                    h.stopVoice(1);
                break;
            }
        }
    } else {
        // deadline = tick0 + 250; loop while skip not requested AND deadline > tick.
        const int tick0    = h.readGameTick ? h.readGameTick() : 0;
        const unsigned deadline = static_cast<unsigned>(tick0) + 250u;
        // 0x53a092: the `>=` guard (deadline >= tick0) is always true for +250.
        do {
            if (h.runFrameLoop) h.runFrameLoop(frameArg, frameArg);
            if (h.readFrame) h.readFrame(&f);
            const bool clicked =
                f.lastDialogResult != kMissionDialogNone && childId == f.clickedObjectId;
            if (clicked) break;
            const unsigned tick =
                static_cast<unsigned>(h.readGameTick ? h.readGameTick() : 0);
            if (deadline <= tick) break;
        } while (true);
    }
}
}  // namespace

// gilde.exe 0x539fd8 — VIBE_Mission_RunRewardSummary.
int MissionRunRewardSummary(const u8* rewardRecord, int frameArg) {
    const MissionDialogHooks& h = GetMissionDialogHooks();
    if (h.voiceQueueFlushAll) h.voiceQueueFlushAll(frameArg);   // VoiceQueue_FlushAll(a3)

    int form = h.createForm ? h.createForm("special\\mission") : -1;
    if (h.centerChildWindows) h.centerChildWindows(form);
    if (h.selectWindow) h.selectWindow(form, 0);
    if (h.renderText) h.renderText(0x17A8);   // line 1 heading
    if (h.selectWindow) h.selectWindow(form, 2);
    if (h.renderText) h.renderText(0u);       // RenderRichString(&unk_623D6C)
    int childId = -1;
    if (h.renderText) {
        h.renderText(0x7D);
        if (h.getChildObjectId) childId = h.getChildObjectId(form, 0x7D);
    }

    RunRewardLine(h, form, frameArg, 0, "AUFTRAEGE_ALLGEMEIN", childId);
    if (h.selectWindow) h.selectWindow(form, 1);
    if (h.renderText) h.renderText(0x17A9);
    RunRewardLine(h, form, frameArg, 1, "AUFTRAEGE_ALLGEMEIN", childId);
    if (h.selectWindow) h.selectWindow(form, 1);
    if (h.renderText) h.renderText(0x17AA);
    RunRewardLine(h, form, frameArg, 2, "AUFTRAEGE_ALLGEMEIN", childId);

    if (h.selectWindow) h.selectWindow(form, 1);
    if (h.renderText) h.renderText(0u);       // &unk_623D6C
    if (h.renderText) h.renderText(0x17AB);
    // RenderRichString(*(reward+4) + 2): substitution id (inert when no record).
    if (h.renderTextArg && rewardRecord) {
        unsigned subId =
            static_cast<unsigned>(rewardRecord[4] | (rewardRecord[5] << 8) |
                                  (rewardRecord[6] << 16) | (rewardRecord[7] << 24)) + 2u;
        h.renderTextArg(0u, subId);
    }
    // 4th line: "_AUFTRAEGE_ERFOLG_HS_%.2d" with reward[+12]; frameArg |= 0x80.
    RunRewardLine(h, form, frameArg | 0x80, 0, "_AUFTRAEGE_ERFOLG_HS_..", childId);

    if (h.destroyForm) h.destroyForm(form);
    return form;   // return VIBE_Form_Destroy(v5)
}

// gilde.exe 0x53ac34 — VIBE_Mission_RunCompletionDialog.
MissionCompletionOutcome MissionRunCompletionDialog(int outcomeCode,
                                                    i32 activeMissionId,
                                                    int frameArg) {
    const MissionDialogHooks& h = GetMissionDialogHooks();
    int form = h.createForm ? h.createForm("special\\mission") : -1;
    if (h.centerChildWindows) h.centerChildWindows(form);
    if (h.selectWindow) h.selectWindow(form, 0);
    // a1: BYTE1 |= 1; LOBYTE &= 0xF6 -> reward summary flags.
    MissionRunRewardSummary(nullptr, frameArg);
    if (h.selectWindow) h.selectWindow(form, 1);
    if (h.renderText) h.renderText(0x17A2);    // RenderRichString(0x17A2, *v1)

    // dword_75BF38 = -1; do { if (dword_63CC24 == -1) dword_631614 = 1; } while (loop)
    MissionDialogFrame f;
    do {
        if (MissionCompletionStep(activeMissionId)) break;   // dword_63CC24==-1 -> close
        if (h.readFrame) h.readFrame(&f);
        if (!(h.runFrameLoop ? h.runFrameLoop(frameArg | 0x80, frameArg | 0x80) : 0))
            break;
    } while (true);

    if (h.destroyForm) h.destroyForm(form);
    return MissionDecodeCompletion(outcomeCode);   // mission_rules 0x53ad50 switch
}

bool MissionOfferGiveButtonPresent(i32 historySeed) {
    return static_cast<unsigned>(historySeed) < 4u;   // v31 < 4 (unsigned compare)
}

// gilde.exe 0x53a854 — VIBE_Mission_RunOfferDialog.
MissionOfferAction MissionRunOfferDialog(const u8* rewardRecord, int frameArg,
                                         i32 historySeed,
                                         i32 giveId, i32 declineId, i32 abandonId) {
    const MissionDialogHooks& h = GetMissionDialogHooks();
    // a1: BYTE1 |= 1; LOBYTE &= 0xF6.
    MissionRunRewardSummary(rewardRecord, frameArg);

    int form = h.createForm ? h.createForm("special\\mission") : -1;
    if (h.centerChildWindows) h.centerChildWindows(form);
    if (h.selectWindow) h.selectWindow(form, 0);
    if (h.renderText) h.renderText(0x17A1);

    // The give button (6053) is only rendered when historySeed < 4.
    int give = MissionOfferGiveButtonPresent(historySeed) ? giveId : -1;
    int decline = declineId;   // 6054 (keep)
    int abandon = abandonId;   // 6055 (abandon)

    MissionOfferAction result = MissionOfferAction::kIdle;
    MissionDialogFrame f;
    while (h.runFrameLoop ? h.runFrameLoop(frameArg | 0x80, frameArg | 0x80) : 0) {
        if (h.readFrame) h.readFrame(&f);
        MissionOfferAction a = MissionOfferStep(f, give, decline, abandon);
        if (a != MissionOfferAction::kIdle) {
            result = a;   // latches dword_631614 in the original; close the loop
            break;
        }
    }

    if (h.destroyForm) h.destroyForm(form);
    // Post-loop: v21 (give-when-not-keep) -> Failure; v33 (abandon) -> reload.
    return result;
}

}  // namespace guild::world
