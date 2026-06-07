#include "world/guild_rank.h"

// Faithful 1:1 port of the Level-1 and Level-3 guild-eligibility predicates
// (gilde.exe 0x481bcc / 0x481cf0). Reuses the rank->category map and the
// GuildPlayer view from world/guild.h. The Person-table master query is a leaf
// (modeled by the caller's predicate); the standing-byte threshold differs per
// level and is documented in the header.

namespace guild::world {

// gilde.exe 0x481bcc — VIBE_Amt_GetGuildEligibility.
GuildEligibility GuildGetEligibility(const GuildPlayer& p) {
    // v1 = *(a1+361); if ( v1 < 0x1E || v1 > 0x21 ) return 0;
    if (p.rank < kGuildRankMin || p.rank > kGuildRankMax)
        return GuildEligibility::kNotGuild;
    // if ( (*(a1+459) & 4) != 0 ) return -1;
    if ((p.flags459 & kGuildL1FlagInGuild) != 0)
        return GuildEligibility::kAlreadyInGuild;
    // if ( *(a1+404) >= 3 ) return 1; else return -2;
    if (p.money >= kGuildL1JoinCost)
        return GuildEligibility::kEligible;
    return GuildEligibility::kInsufficientFunds;
}

// gilde.exe 0x481cf0 — VIBE_Amt_CheckGuildRankLevel3.
GuildEligibility GuildCheckRankLevel3(const GuildPlayer& p,
                                      GuildMasterPredicate masterPresent,
                                      void* ctx) {
    // if ( *(a1+361) < 0x1E ) return 0;  v3 > 0x21 -> return 0;
    if (p.rank < kGuildRankMin || p.rank > kGuildRankMax)
        return GuildEligibility::kNotGuild;

    // switch maps 30..33 -> 23/24/25/26 (same map Level2 uses).
    int category = GuildRankToQueryCategory(p.rank);
    if (category < 0)
        return GuildEligibility::kNotGuild;

    // Begin = VIBE_Person_QueryBegin(.., category); fail if no master / flagged /
    // standing byte (+583) < 3 -> return 0. (Level3 needs >= 3; the predicate folds
    // that threshold in.)
    if (!masterPresent || !masterPresent(category, ctx))
        return GuildEligibility::kNotGuild;

    // if ( (*(a1+459) & 0x10) != 0 ) return -1;  (already in the guild)
    if ((p.flags459 & kGuildL3FlagInGuild) != 0)
        return GuildEligibility::kAlreadyInGuild;

    // if ( *(a1+404) >= 8 ) return 1;  else return -2;
    if (p.money >= kGuildL3JoinCost)
        return GuildEligibility::kEligible;
    return GuildEligibility::kInsufficientFunds;
}

} // namespace guild::world
