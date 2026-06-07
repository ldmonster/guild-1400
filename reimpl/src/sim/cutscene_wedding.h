#pragma once
// ===========================================================================
// cutscene_wedding.{h,cpp} — the per-type WEDDING/MARRIAGE cutscene (type 6)
// MAIN + its participant-check step (gilde.exe, namespace guild::sim).
// ===========================================================================
//
//   VIBE_Cutscene_Wedding            (0x4a73a4) — the wedding main (type 6, the
//                                                 dword_11AE5C0[5*6] entry).
//   VIBE_Cutscene_CheckMarriageEligible (0x4a7118) — the type-6 step/ready
//                                                 predicate (dword_11AE5D0[5*6]).
//   VIBE_Cutscene_WeddingExit        (0x4a729c) — the skip/abort poll (leaf).
//
// WEDDING MAIN (deterministic spine, render/script/voice leaves removed):
//   1. resolve both spouses (Person_FindRecordById of slot +52 / +56); if either
//      is missing the cutscene aborts (returns 0, no marriage).
//   2. for each spouse of kind 6 (human player char): Mission_TrackCrimeProgress
//      (a quest hook).
//   3. emit the two marriage commands: QueueRequestArgs25(spouse, 456, 0, 4,
//      0x40000) for each — the lockstep "now married" delta (modelled by the
//      command hook).
//   4. build the couple display name "<groom> <bride> 1" (the +9 "is-female" bit
//      of the FIRST spouse decides which is listed first / the swap).
//   5. play the scripted ceremony: load "KIRCHE_HOCHZEIT.ed3", run the
//      hochzeit-*.esc scripts interleaved with the fixed sequence of timed text
//      panels (5814 vows, 5817, 5816 ring, 5815, 5816, 5818 kiss) — the panel-id
//      ORDER is recovered 1:1 below; the playback is the host's job (leaves).
//
// CHECK-MARRIAGE-ELIGIBLE (type-6 step):
//   resolve both candidates; eligible (return 1) iff they resolve, belong to
//   DIFFERENT player factions ((+3>>24) differ), and the first is a human/player
//   class (kind 6 or 7). On eligibility it broadcasts the betrothal notice to all
//   other human players (He_SendEntityMessage). On INELIGIBILITY it requests the
//   "cancel betrothal" command (QueueRequestArgs25(…,456,…)) for each. We expose
//   the deterministic predicate; the notify/cancel sends route through the hook.
#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered marriage-command constants (QueueRequestArgs25 operands).
//   VIBE_Command_QueueRequestArgs25(personId, 456, 0, 4, 0x40000)
// 456 = the "marital status" field op; 0x40000 = the married bit. Used both by
// the wedding main (set married) and CheckMarriageEligible's cancel path.
// ---------------------------------------------------------------------------
constexpr int kMarriageFieldOp = 456;     // arg2
constexpr int kMarriageWidth   = 4;       // arg4
constexpr u32 kMarriageBit     = 0x40000; // arg5

// The wedding ceremony text-panel id sequence (recovered from RenderRichString
// call order in VIBE_Cutscene_Wedding @0x4a73a4). Each is a timed panel between
// the .esc script segments. (Some ids repeat — the ring exchange shows 5816 for
// both spouses.)
constexpr int kWeddingPanelCount = 6;
extern const int kWeddingPanelIds[kWeddingPanelCount];   // 5814,5817,5816,5815,5816,5818

// Marriage-candidate input (the fields the originals read off a Person record).
struct WeddingPerson {
    i32 personId   = -1;   // the entity id (Person +4)
    u8  kind       = 0;    // Person +2 kind byte (6 = human player char, 7 official)
    u8  factionTag = 0;    // Person +3>>24 — player faction tag (differ => valid)
    bool isFemale  = false;// Person +9 nonzero — listed-first / name-swap bit
    bool resolves  = true; // Person_FindRecordById != 0
};

// ---------------------------------------------------------------------------
// Leaf hooks — the wedding main's scene/script/voice/command side effects.
// ---------------------------------------------------------------------------
struct WeddingCutsceneHooks {
    // VIBE_Cutscene_LoadScene("KIRCHE_HOCHZEIT.ed3", …) — load the chapel scene.
    void (*loadScene)(const char* scene, const char* coupleName, void* ctx) = nullptr;
    // One timed ceremony panel (the text-panel id, in order).
    void (*onPanel)(int panelId, void* ctx) = nullptr;
    // VIBE_Mission_TrackCrimeProgress(spouse) — the quest hook for a kind-6 spouse.
    void (*trackCrimeProgress)(i32 personId, void* ctx) = nullptr;
    // The lockstep marriage command (QueueRequestArgs25 set-married / cancel).
    void (*marriageCommand)(i32 personId, int fieldOp, int width, u32 bit, void* ctx) = nullptr;
    // CheckMarriageEligible's betrothal-notice broadcast to other players.
    void (*notifyBetrothal)(i32 toPlayerId, i32 groomId, i32 brideId, void* ctx) = nullptr;
    void* ctx = nullptr;
};

// Result of running the wedding cutscene.
struct WeddingOutcome {
    bool married = false;     // both resolved -> the marriage commands fired
    bool aborted = false;     // a spouse failed to resolve
    char coupleName[64] = {}; // "<first> <second> 1" (groom/bride ordered by +9)
};

// ===========================================================================
// gilde.exe 0x4a7118 — VIBE_Cutscene_CheckMarriageEligible (type-6 step).
//   eligible iff both resolve, factionTag differs, and the first is kind 6/7.
//   On eligibility, broadcasts the betrothal notice to each other human player
//   in `otherPlayers`; on ineligibility, fires the cancel command for each.
// Returns 1 (eligible) / 0 (not). `otherPlayers` lists the (playerId) of every
// other human player char (kind 6/7) to notify (the byte_12CE912 scan).
int CutsceneCheckMarriageEligible(const WeddingPerson& a, const WeddingPerson& b,
                                  const i32* otherPlayers, int otherCount,
                                  const WeddingCutsceneHooks& hooks);

// ===========================================================================
// gilde.exe 0x4a73a4 — VIBE_Cutscene_Wedding (deterministic core).
//   Validate both spouses, fire the quest hooks + the two marriage commands,
//   build the couple name, then play the fixed ceremony panel sequence. Returns
//   the outcome (married / aborted + the couple name).
WeddingOutcome CutsceneWedding(const WeddingPerson& a, const WeddingPerson& b,
                               const char* aName, const char* bName,
                               const WeddingCutsceneHooks& hooks);

} // namespace guild::sim
