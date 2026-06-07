#include "sim/cutscene_auction.h"

// Faithful 1:1 port of the deterministic spine of VIBE_Cutscene_Auction
// (gilde.exe 0x4a89f8) + VIBE_Cutscene_BroadcastMessage (0x4a8930). The scene
// load, the auctioneer/bidder actor spawns, the voice banks
// (VERSTEIGERUNG_*.sbf) and the RunCombatScript waits are presentation leaves
// routed through AuctionCutsceneHooks; the round-by-round high-bid tracking, the
// bidder-count stop condition, the +32 ask increment and the lease split are
// reconstructed here.

namespace guild::sim {

// ---------------------------------------------------------------------------
// gilde.exe 0x4a89f8 (LABEL_25 region switch): region kind -> object texture set.
//   *v4 == 11 -> 0 (Wald),  == 12 -> 1 (Stein),  == 13 -> 2 (Mine).
// ---------------------------------------------------------------------------
int AuctionTextureSetForRegion(AuctionRegion region) {
    switch (region) {
    case AuctionRegion::kForest: return 0;
    case AuctionRegion::kQuarry: return 1;
    case AuctionRegion::kMine:   return 2;
    }
    return 0;
}

// ===========================================================================
// gilde.exe 0x4a8930 — VIBE_Cutscene_BroadcastMessage (type-10 step).
// ===========================================================================
int CutsceneBroadcastMessage(bool buildingResolves,
                             const i32* participants, const u8* participantKinds,
                             int participantCount,
                             void (*buildSpeech)(i32 personId, void* ctx),
                             void* ctx) {
    if (!buildingResolves)                      // if (!Building_FindById(...)) return 0
        return 0;
    for (int i = 0; i < participantCount; ++i) {
        u8 kind = participantKinds ? participantKinds[i] : 0;
        if (kind == 6 || kind == 7) {           // human/player class
            if (buildSpeech) buildSpeech(participants[i], ctx);
        }
    }
    return 1;
}

// ===========================================================================
// gilde.exe 0x4a89f8 — VIBE_Cutscene_Auction (deterministic core).
//
// Round loop (the `do { … } while (v79+1 < 5 && !v128)`):
//   v131 = 2; v133 = 0; v128 = 0;
//   do {
//     v114[11] = 10 - v133;                       // remaining rounds counter
//     RunParticipants(...);                       // fills dword_11AB094/98 (leaf)
//     bidders = 0; high = 0;
//     for each bidder i:
//       if (dword_11AB098[i] && resolves[i]) {    // active & present
//         bid = dword_11AB094[i]; v113[i] = bid; ++bidders;
//         if (high < bid) { high = bid; winnerIndex = i; v114[0] = bid; }
//       } else v113[i] = -1;
//     if (bidders >= 2) { v114[0] += 32; }        // ascending ask
//     else              { v128 = 1; }             // < 2 bidders -> stop
//     ++v133; ++v131;
//   } while (v133 < 5 && !v128);
// ===========================================================================
AuctionOutcome CutsceneAuction(AuctionRegion region,
                               const std::vector<AuctionBidder>& bidders,
                               const std::vector<std::vector<AuctionBid>>& bidsByRound,
                               int startPrice,
                               const AuctionCutsceneHooks& hooks,
                               bool ownerResolves) {
    AuctionOutcome out{};

    // ----- 1. resolve the plot owner / region. -----
    // if (!Begin) the whole body is skipped (no auction); the region switch
    // returns early on an unknown kind.
    if (!ownerResolves) {
        out.aborted = true;
        return out;
    }

    // ----- 2. scene load + texture set. -----
    int texSet = AuctionTextureSetForRegion(region);
    if (hooks.loadScene) hooks.loadScene("Versteigerung.ed3", texSet, hooks.ctx);

    int askPrice    = startPrice;    // v114[0]
    int winnerIndex = -1;            // v135
    int highBid     = 0;
    int stop        = 0;             // v128
    int round       = 0;             // v133

    int maxRounds = static_cast<int>(bidsByRound.size());
    if (maxRounds > kAuctionMaxRounds) maxRounds = kAuctionMaxRounds;

    // ----- 3. the bidding rounds. -----
    while (round < maxRounds && !stop) {
        const std::vector<AuctionBid>& bids = bidsByRound[round];

        int activeBidders = 0;       // v62
        int roundHigh     = 0;       // v61 (per-round high, resets each round)
        int roundWinner   = winnerIndex;

        int n = static_cast<int>(bidders.size());
        if (n > kAuctionMaxBidders) n = kAuctionMaxBidders;
        for (int i = 0; i < n; ++i) {
            bool present = bidders[i].resolves && bidders[i].personId >= 0;
            const AuctionBid b = (i < static_cast<int>(bids.size()))
                                     ? bids[i] : AuctionBid{};
            if (b.active && present) {
                ++activeBidders;
                if (roundHigh < b.amount) {
                    roundHigh   = b.amount;
                    roundWinner = i;
                    askPrice    = b.amount;   // v114[0] = bid
                }
            }
        }

        if (activeBidders >= 2) {
            // a contested round: record the leader, raise the ask, continue.
            winnerIndex = roundWinner;
            highBid     = roundHigh;
            askPrice   += kAuctionBidIncrement;          // v114[0] += 32
        } else if (activeBidders == 1) {
            // a single bidder still in -> they win; stop next.
            winnerIndex = roundWinner;
            highBid     = roundHigh;
            stop = 1;                                     // v128 = 1
        } else {
            // nobody left -> stop (winnerIndex keeps the prior leader, if any).
            stop = 1;
        }

        if (hooks.onRound) hooks.onRound(round, askPrice, activeBidders, hooks.ctx);
        ++round;
    }
    out.rounds = round;

    // ----- 4. outcome. -----
    if (winnerIndex <= -1) {                    // if (v135 <= -1) no sale
        out.sold = false;
        if (hooks.onNoSale) hooks.onNoSale(hooks.ctx);
        return out;
    }

    out.sold        = true;
    out.winnerIndex = winnerIndex;
    out.winnerId    = bidders[winnerIndex].personId;
    out.winningBid  = highBid;
    // lease split: 90% owner / 10% other (flt_61D700 / flt_61D704).
    out.toOwner = static_cast<int>(static_cast<double>(highBid) * kAuctionLeaseOwner);
    out.toOther = static_cast<int>(static_cast<double>(highBid) * kAuctionLeaseOther);

    if (hooks.onSold)
        hooks.onSold(out.winnerId, out.winningBid, out.toOwner, out.toOther, hooks.ctx);
    return out;
}

} // namespace guild::sim
