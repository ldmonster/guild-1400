#pragma once
// Guild — the rank / eligibility rules core of the guild-join/promote dialogs.
// The dialog shells (VIBE_Guild_ShowLevel1/2/3*Dialog, the contact loops) are GUI
// + network and are DEFERRED (see the module report). Recovered here byte-for-byte
// is the load-bearing eligibility predicate underneath them:
//
//   VIBE_Amt_CheckGuildRankLevel2 (gilde.exe 0x481c18) — given the player's
//   character record it (a) gates on the held rank byte (+361) being a guild rank
//   (30..33), (b) maps that rank to a guild-master query category {30->23, 31->24,
//   32->25, 33->26}, (c) requires a present guild master with sufficient standing
//   (need byte +583 >= 2), then returns:
//      -1  already in the guild        (flags +459 & 8 set)
//       1  eligible to join            (money +404 >= 5)
//      -2  insufficient funds          (money +404 < 5)
//       0  not a guild rank / no master / unfit
//   VIBE_Guild_CheckLevel2Eligibility (0x520ac8) dispatches on this code (1 ->
//   show join dialog, -1 -> message box, else -> skill-requirement check).
//
// The Person-table reads (master query, the need/standing byte) are sim-owned, so
// the rule takes a small GuildPlayer view + a settable "guild-master present"
// predicate that mirrors VIBE_Person_QueryBegin's result.
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Guild ranks and their guild-master query categories.
// ===========================================================================
// Rank byte at character record +361. A guild rank is 30..33 (0x1E..0x21).
constexpr u8 kGuildRankMin = 30;   // 0x1E
constexpr u8 kGuildRankMax = 33;   // 0x21

// Rank -> guild-master query category (the switch in CheckGuildRankLevel2):
//   30->23, 31->24, 32->25, 33->26.  Returns -1 for a non-guild rank.
int GuildRankToQueryCategory(u8 rank);

// ===========================================================================
// Eligibility result codes (the exact return values of CheckGuildRankLevel2).
// ===========================================================================
enum class GuildEligibility : int {
    kNotGuild        = 0,   // rank not 30..33 / no qualifying master / unfit
    kEligible        = 1,   // may join (money >= 5)
    kAlreadyInGuild  = -1,  // flags +459 & 8 set
    kInsufficientFunds = -2,// money +404 < 5
};

constexpr u8  kGuildFlagInGuild = 0x8; // (+459 & 8) -> already in guild
constexpr i32 kGuildJoinCost    = 5;   // (+404 >= 5) -> can afford to join

// Player view (filled from the live character record).
struct GuildPlayer {
    u8  rank;        // +361  held rank byte
    u8  flags459;    // +459  flags byte (bit3 == in-guild)
    i32 money;       // +404  liquid money
};

// A guild-master presence predicate. The original calls VIBE_Person_QueryBegin
// for the mapped category and checks the master exists, is unflagged (+90 & 1==0)
// and has standing (+583 >= 2). Here the caller supplies the boolean result of
// that query so the rule stays standalone-testable.
//   `category` is GuildRankToQueryCategory(rank).
using GuildMasterPredicate = bool (*)(int category, void* ctx);

// gilde.exe 0x481c18 — VIBE_Amt_CheckGuildRankLevel2.
// Returns the eligibility code for `p`, consulting `masterPresent(category)` for
// the guild-master gate. `masterPresent` may be null (treated as "no master").
GuildEligibility GuildCheckRankLevel2(const GuildPlayer& p,
                                      GuildMasterPredicate masterPresent,
                                      void* ctx);

// ===========================================================================
// Dialog dispatch (VIBE_Guild_CheckLevel2Eligibility 0x520ac8 decision portion).
// ===========================================================================
enum class GuildLevel2Action : int {
    kShowJoinDialog = 0,  //  code 1  -> VIBE_Guild_ShowLevel2JoinDialog
    kShowMessageBox = 1,  //  code -1 -> VIBE_Dialog_ShowMessageBox
    kCheckSkill     = 2,  //  else    -> VIBE_Dialog_CheckSkillRequirement
};

// Maps an eligibility code to the dialog action the dispatcher takes.
GuildLevel2Action GuildLevel2Dispatch(GuildEligibility code);

} // namespace guild::world
