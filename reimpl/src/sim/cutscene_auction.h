#pragma once
// ===========================================================================
// cutscene_auction.{h,cpp} — the per-type AUCTION/LEASE cutscene (type 10) MAIN
// + its broadcast step (gilde.exe, namespace guild::sim).
// ===========================================================================
//
//   VIBE_Cutscene_Auction         (0x4a89f8) — the auction main (type 10, the
//                                              dword_11AE5C0[5*10] entry).
//   VIBE_Cutscene_BroadcastMessage(0x4a8930) — the type-10 step/broadcast
//                                              (dword_11AE5D0[5*10]).
//
// AUCTION MAIN (deterministic spine, render/scene/voice/script leaves removed):
//   The land-lease auction for a resource plot (Waldstück=11 / Steinbruch=12 /
//   Mine=13, the kind byte of the AiPlayer/type record dword_13CE294). It runs a
//   sealed multi-round ascending auction over the participant bidders:
//   1. resolve the plot owner (Person_QueryBegin) and its region kind; the kind
//      selects the plot label + the object texture set (Wald->0, Stein->1,
//      Mine->2).
//   2. snapshot the bidders (slot +52 ids -> resolve each; up to 8); RandInt(3)
//      picks the auctioneer flavour.
//   3. run up to 5 ROUNDS (v133 in [0,5), v131 the round "tick" starting at 2):
//        per round: RunParticipants collects each active bidder's bid
//        (dword_11AB094[i]) gated by their active flag (dword_11AB098[i]); track
//        the highest bid -> (winnerIndex, highBid). If >= 2 bidders remain, bump
//        the asking price by +32 and continue; if < 2 remain, stop (v128 = 1).
//   4. outcome: if no winner (winnerIndex <= -1) the plot is NOT leased; else the
//      winner takes the lease — the host commits a delta packet (bid -> field
//      0x69, time -> field 0x6D) + cmd56 (assign) + cmd16 (pay): the lease price
//      splits flt_61D700 (0.9) to the owner and flt_61D704 (0.1) elsewhere.
//
// BROADCAST (type-10 step): resolve the auctioned building; for each participant
// of kind 6/7 build a speech packet announcing the lease (text 7342). Returns 1
// if the building resolves.
//
// All scene/voice/script/command leaves route through AuctionCutsceneHooks. The
// bidders' per-round bids are supplied via the participant rows (the originals
// fill them from RunParticipants / the bidder AI — a leaf).
#include "guild/common/types.h"

#include <vector>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered auction constants.
//   bid increment per round   : +32   (v114[0] += 32 in VIBE_Cutscene_Auction)
//   round tick start          : 2     (v131 = 2, ++v131 per round)
//   max rounds                : 5     (v133 + 1 < 5)
//   lease split owner / other : flt_61D700 = 0.9 / flt_61D704 = 0.1
//   max bidders               : 8     (the v113[8] / v114[1..8] slots)
// ---------------------------------------------------------------------------
constexpr int    kAuctionBidIncrement = 32;    // v114[0] += 32
constexpr int    kAuctionMaxRounds     = 5;    // v133+1 < 5
constexpr int    kAuctionRoundTickBase = 2;    // v131 = 2
constexpr int    kAuctionMaxBidders    = 8;
// The lease split factors are 32-bit FLOATS in the binary (fmul ds:flt_*), so
// the product is (double)bid * (double)(float)k — the float rounding of 0.9/0.1
// is observable. flt_61D700 = 0x3f666666, flt_61D704 = 0x3dcccccd.
constexpr float  kAuctionLeaseOwnerF   = 0.89999997615814208984375f;  // flt_61D700
constexpr float  kAuctionLeaseOtherF   = 0.100000001490116119384765625f; // flt_61D704
constexpr double kAuctionLeaseOwner    = 0.9;  // flt_61D700 (legacy alias)
constexpr double kAuctionLeaseOther    = 0.1;  // flt_61D704 (legacy alias)
constexpr int    kAuctionMessageText   = 7342; // BroadcastMessage panel id

// Region kinds (the kind byte of the AiPlayer/type record dword_13CE294[589*k]).
enum class AuctionRegion : u8 {
    kForest = 11,   // WALDSTUECK, texture set 0
    kQuarry = 12,   // STEINBRUCH, texture set 1
    kMine   = 13,   // MINE,       texture set 2
};

// A bidder snapshot.
struct AuctionBidder {
    i32  personId = -1;   // slot +52 id
    bool resolves = true; // Person_FindRecordById != 0 (a gap bidder if false)
};

// One round's per-bidder bid input (mirrors dword_11AB094/98 the GUI fills).
struct AuctionBid {
    int  amount = 0;      // dword_11AB094[i] — the bid this round
    bool active = false;  // dword_11AB098[i] — bidder still in
};

// ---------------------------------------------------------------------------
// Leaf hooks.
// ---------------------------------------------------------------------------
struct AuctionCutsceneHooks {
    // VIBE_Cutscene_LoadScene("Versteigerung.ed3", …) + texture-set select.
    void (*loadScene)(const char* scene, int textureSet, void* ctx) = nullptr;
    // One round's presentation (round index, asking price, #active bidders).
    void (*onRound)(int round, int askPrice, int activeBidders, void* ctx) = nullptr;
    // The winner commit (winner id, winning bid) — the delta + cmd56 + cmd16.
    void (*onSold)(i32 winnerId, int winningBid, int toOwner, int toOther, void* ctx) = nullptr;
    // No-sale close (winnerIndex <= -1).
    void (*onNoSale)(void* ctx) = nullptr;
    void* ctx = nullptr;
};

struct AuctionOutcome {
    bool sold        = false;  // a winner was found
    int  winnerIndex = -1;     // v135 — index into the bidder array (-1 = no sale)
    i32  winnerId    = -1;     // winning bidder's person id
    int  winningBid  = 0;      // v114[0] / v113[winner]
    int  rounds      = 0;      // rounds actually run
    int  toOwner     = 0;      // winningBid * 0.9  (lease split)
    int  toOther     = 0;      // winningBid * 0.1
    bool aborted     = false;  // plot owner / region invalid
};

// ===========================================================================
// gilde.exe 0x4a8930 — VIBE_Cutscene_BroadcastMessage (type-10 step).
//   if (!Building_FindById(slot+124)) return 0;
//   for each participant (slot +52, partCount entries) of kind 6/7:
//       BuildSpeechPacket(person, text 7342).
//   return 1.
// `participantKinds` gives the kind byte of each participant; the BuildSpeech
// emit is the hook. Returns 1 if the building resolves, else 0.
int CutsceneBroadcastMessage(bool buildingResolves,
                             const i32* participants, const u8* participantKinds,
                             int participantCount,
                             void (*buildSpeech)(i32 personId, void* ctx),
                             void* ctx);

// Region-kind -> texture set (the LABEL_25 switch: 11->0, 12->1, 13->2).
int AuctionTextureSetForRegion(AuctionRegion region);

// ===========================================================================
// gilde.exe 0x4a89f8 — VIBE_Cutscene_Auction (deterministic core).
//   Run the multi-round ascending auction over `bidders`. `bidsByRound[r][i]` is
//   bidder i's bid + active flag in round r (the GUI/AI fills these — a leaf).
//   `bidsByRound.size()` caps the rounds (<= kAuctionMaxRounds). `startPrice` is
//   the opening ask. Returns the winner + the lease split, and fires the hooks.
AuctionOutcome CutsceneAuction(AuctionRegion region,
                               const std::vector<AuctionBidder>& bidders,
                               const std::vector<std::vector<AuctionBid>>& bidsByRound,
                               int startPrice,
                               const AuctionCutsceneHooks& hooks,
                               bool ownerResolves = true);

} // namespace guild::sim
