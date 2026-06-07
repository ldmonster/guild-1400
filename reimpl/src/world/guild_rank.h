#pragma once
// Guild — the remaining rank/eligibility predicates that sit alongside
// VIBE_Amt_CheckGuildRankLevel2 (world/guild.h). These are the LEVEL-1 and
// LEVEL-3 siblings of that rule, recovered byte-for-byte from gilde.exe:
//
//   * VIBE_Amt_GetGuildEligibility  0x481bcc — the LEVEL-1 gate. No guild-master
//     query: just the guild-rank check (+361 in 30..33), an "already joined" flag
//     (+459 & 4), and an affordability check (+404 >= 3).
//   * VIBE_Amt_CheckGuildRankLevel3 0x481cf0 — the LEVEL-3 gate. Same shape as
//     Level2 but with the stronger thresholds: guild-master standing byte (+583)
//     >= 3, the "already joined" flag at (+459 & 0x10), and money (+404 >= 8).
//
// Reuses world/guild.h's GuildPlayer view, GuildEligibility codes and the rank ->
// guild-master query-category map. The Person-table master query is sim-owned, so
// (like Level2) the Level3 rule takes a settable "qualifying master present"
// predicate; Level1 needs none.
#include "guild/common/types.h"
#include "world/guild.h"   // GuildPlayer, GuildEligibility, GuildMasterPredicate

namespace guild::world {

// ---------------------------------------------------------------------------
// Level-1 affordability flag/threshold (gilde.exe 0x481bcc).
// ---------------------------------------------------------------------------
constexpr u8  kGuildL1FlagInGuild = 0x4; // (+459 & 4)  -> already in the guild
constexpr i32 kGuildL1JoinCost    = 3;   // (+404 >= 3) -> can afford to join

// gilde.exe 0x481bcc — VIBE_Amt_GetGuildEligibility.
//   if (rank < 30 || rank > 33) return 0;          // not a guild rank
//   if (flags459 & 4)           return -1;          // already in the guild
//   if (money >= 3)             return 1;           // eligible
//   return -2;                                       // insufficient funds
// Returns the same GuildEligibility codes as Level2 (0/-1/1/-2).
GuildEligibility GuildGetEligibility(const GuildPlayer& p);

// ---------------------------------------------------------------------------
// Level-3 flag/threshold (gilde.exe 0x481cf0).
// ---------------------------------------------------------------------------
constexpr u8  kGuildL3FlagInGuild = 0x10; // (+459 & 0x10) -> already in the guild
constexpr i32 kGuildL3JoinCost    = 8;    // (+404 >= 8)   -> can afford to join
constexpr int kGuildL3MasterStanding = 3; // master standing byte (+583) >= 3

// gilde.exe 0x481cf0 — VIBE_Amt_CheckGuildRankLevel3.
// Same structure as GuildCheckRankLevel2 (world/guild.h): gate on guild rank,
// map rank -> master query category, require a qualifying master present, then:
//   (+459 & 0x10) set -> -1 (already in guild)
//   (+404 >= 8)       ->  1 (eligible)
//   else              -> -2 (insufficient funds)
// `masterPresent(category)` must fold in the Level-3 standing threshold (+583 >= 3).
// May be null (treated as "no master").
GuildEligibility GuildCheckRankLevel3(const GuildPlayer& p,
                                      GuildMasterPredicate masterPresent,
                                      void* ctx);

} // namespace guild::world
