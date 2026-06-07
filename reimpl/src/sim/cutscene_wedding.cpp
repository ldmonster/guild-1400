#include "sim/cutscene_wedding.h"

#include <cstdio>
#include <cstring>

// Faithful 1:1 port of the deterministic spine of VIBE_Cutscene_Wedding
// (gilde.exe 0x4a73a4) + VIBE_Cutscene_CheckMarriageEligible (0x4a7118). The
// chapel scene load, the hochzeit-*.esc script playback and the per-panel timed
// waits (RunTimedScript / RunScriptUntilSkip with the WeddingExit poll) are
// presentation leaves, routed through WeddingCutsceneHooks; the panel-id ORDER
// and the marriage-command / quest mutations are reconstructed here.

namespace guild::sim {

// ---------------------------------------------------------------------------
// gilde.exe @0x4a73a4 — the ceremony text-panel id sequence (the order of the
// VIBE_Text_RenderRichString calls, each followed by a RunTimedScript wait):
//   5814 (vows, both) / 5817 / 5816 (ring) / 5815 / 5816 (ring) / 5818 (kiss).
// ---------------------------------------------------------------------------
const int kWeddingPanelIds[kWeddingPanelCount] = { 5814, 5817, 5816, 5815, 5816, 5818 };

namespace {
void FireMarriageCommand(const WeddingCutsceneHooks& h, i32 personId) {
    // VIBE_Command_QueueRequestArgs25(personId, 456, 0, 4, 0x40000)
    if (h.marriageCommand)
        h.marriageCommand(personId, kMarriageFieldOp, kMarriageWidth,
                          kMarriageBit, h.ctx);
}
} // namespace

// ===========================================================================
// gilde.exe 0x4a7118 — VIBE_Cutscene_CheckMarriageEligible.
//   RecordById = FindById(slot+52);  v6 = FindById(slot+56);
//   if (RecordById && v6 && (RecordById+3>>24) != (v6+3>>24)) {
//     kind = RecordById+2;
//     if (kind == 6 || kind == 7) { BuildSpeechPacket(...); }       // vow line
//     if (kind == 6 || kind == 7 || kind == 5) {                    // notify
//       for each other person of kind 6/7 not == groom/bride: He_SendEntityMessage
//     }
//     if (kind == 6 || kind == 7) return 1;                         // eligible
//   }
//   if (RecordById) QueueRequestArgs25(groom, 456, 0, 4, 0x40000);  // cancel
//   if (v6)         QueueRequestArgs25(bride, 456, 0, 4, 0x40000);
//   return 0;
// ===========================================================================
int CutsceneCheckMarriageEligible(const WeddingPerson& a, const WeddingPerson& b,
                                  const i32* otherPlayers, int otherCount,
                                  const WeddingCutsceneHooks& hooks) {
    bool bothResolve = a.resolves && b.resolves && a.personId >= 0 && b.personId >= 0;
    if (bothResolve && a.factionTag != b.factionTag) {
        u8 kind = a.kind;
        bool firstIsPlayer = (kind == 6 || kind == 7);
        // notify path covers kinds 6/7/5 (the betrothal broadcast).
        if (firstIsPlayer || kind == 5) {
            for (int i = 0; i < otherCount; ++i) {
                i32 pid = otherPlayers[i];
                if (pid != a.personId && pid != b.personId && hooks.notifyBetrothal)
                    hooks.notifyBetrothal(pid, a.personId, b.personId, hooks.ctx);
            }
        }
        if (firstIsPlayer)
            return 1;                          // eligible
    }
    // ineligible -> cancel the pending betrothal for whichever resolved.
    if (a.resolves && a.personId >= 0) FireMarriageCommand(hooks, a.personId);
    if (b.resolves && b.personId >= 0) FireMarriageCommand(hooks, b.personId);
    return 0;
}

// ===========================================================================
// gilde.exe 0x4a73a4 — VIBE_Cutscene_Wedding (deterministic core).
// ===========================================================================
WeddingOutcome CutsceneWedding(const WeddingPerson& a, const WeddingPerson& b,
                               const char* aName, const char* bName,
                               const WeddingCutsceneHooks& hooks) {
    WeddingOutcome out{};

    // ----- 1. resolve both spouses. -----
    if (!a.resolves || !b.resolves || a.personId < 0 || b.personId < 0) {
        out.aborted = true;
        return out;
    }

    // ----- 2. quest hook for any kind-6 spouse. -----
    // if (RecordById+2 == 6) Mission_TrackCrimeProgress(RecordById);
    if (a.kind == 6 && hooks.trackCrimeProgress)
        hooks.trackCrimeProgress(a.personId, hooks.ctx);
    if (b.kind == 6 && hooks.trackCrimeProgress)
        hooks.trackCrimeProgress(b.personId, hooks.ctx);

    // ----- 3. the two marriage commands (set-married delta for each). -----
    FireMarriageCommand(hooks, a.personId);
    FireMarriageCommand(hooks, b.personId);
    out.married = true;

    // ----- 4. couple display name "%s %s %i" (the +9 bit swaps the order). -----
    //   if (RecordById+9) { swap(v5,v7); sprintf(buf,"%s %s %i", brideName, groomName, 1); }
    //   else              {              sprintf(buf,"%s %s %i", groomName, brideName, 1); }
    const char* first  = aName ? aName : "";
    const char* second = bName ? bName : "";
    if (a.isFemale) { const char* t = first; first = second; second = t; }
    std::snprintf(out.coupleName, sizeof(out.coupleName), "%s %s %i", first, second, 1);

    // ----- 5. play the scripted ceremony. -----
    if (hooks.loadScene)
        hooks.loadScene("KIRCHE_HOCHZEIT.ed3", out.coupleName, hooks.ctx);
    for (int i = 0; i < kWeddingPanelCount; ++i) {
        if (hooks.onPanel) hooks.onPanel(kWeddingPanelIds[i], hooks.ctx);
    }
    return out;
}

} // namespace guild::sim
