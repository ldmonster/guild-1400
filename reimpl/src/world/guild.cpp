#include "world/guild.h"

// Faithful 1:1 port of the guild rank/eligibility rule (gilde.exe 0x481c18) and
// the Level-2 dialog dispatch decision (0x520ac8). GUI/network shells deferred.

namespace guild::world {

// gilde.exe 0x481c18 — rank -> guild-master query category switch.
int GuildRankToQueryCategory(u8 rank) {
    switch (rank) {
        case 0x1E: return 23;  // 30
        case 0x1F: return 24;  // 31
        case 0x20: return 25;  // 32
        case 0x21: return 26;  // 33
        default:   return -1;
    }
}

// gilde.exe 0x481c18 — VIBE_Amt_CheckGuildRankLevel2.
GuildEligibility GuildCheckRankLevel2(const GuildPlayer& p,
                                      GuildMasterPredicate masterPresent,
                                      void* ctx) {
    // if ( *(a1+361) < 0x1E ) return 0;  v3 > 0x21 -> return 0;
    if (p.rank < kGuildRankMin || p.rank > kGuildRankMax)
        return GuildEligibility::kNotGuild;

    int category = GuildRankToQueryCategory(p.rank);
    if (category < 0)                       // the switch default -> return 0
        return GuildEligibility::kNotGuild;

    // Begin = VIBE_Person_QueryBegin(.., category); fail if no master / flagged /
    // standing byte (+583) < 2 -> return 0.
    if (!masterPresent || !masterPresent(category, ctx))
        return GuildEligibility::kNotGuild;

    // if ( (*(a1+459) & 8) != 0 ) return -1;  (already in the guild)
    if ((p.flags459 & kGuildFlagInGuild) != 0)
        return GuildEligibility::kAlreadyInGuild;

    // if ( *(a1+404) >= 5 ) return 1;  else return -2;
    if (p.money >= kGuildJoinCost)
        return GuildEligibility::kEligible;
    return GuildEligibility::kInsufficientFunds;
}

// gilde.exe 0x520ac8 — VIBE_Guild_CheckLevel2Eligibility dispatch.
GuildLevel2Action GuildLevel2Dispatch(GuildEligibility code) {
    if (code == GuildEligibility::kEligible)        // v1 == 1
        return GuildLevel2Action::kShowJoinDialog;
    if (code == GuildEligibility::kAlreadyInGuild)  // v1 == -1
        return GuildLevel2Action::kShowMessageBox;
    return GuildLevel2Action::kCheckSkill;          // else
}

} // namespace guild::world
