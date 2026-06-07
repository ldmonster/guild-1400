#pragma once
// world_economy3 — the deterministic decision kernels extracted from the guild /
// office "amt" dialog shells. The full functions in gilde.exe are GUI + network
// coupled (VIBE_GameTick_Finalize / VIBE_Form_* / VIBE_Text_RenderRichString /
// VIBE_GameLogic_RunFrameLoop frame pumps), but each carries a small, load-bearing
// piece of pure math/dispatch that decides WHAT the shell shows. Those kernels are
// recovered here byte-for-byte; the GUI/network plumbing is routed through an
// installable WorldEconomy3Hooks struct with inert defaults (defined in the .cpp)
// so the kernels stay standalone-testable.
//
// Translated (kernels of):
//   VIBE_Amt_RunElectionCandidateWindow 0x557a40 — candidate office-status ID map
//       (held/free name 1596/1597; the output-tier rating 1602/1603/1604/1605 with
//        the <100 floor 1605; the title text-id select 0x9D vs "$Z$[%s$]").
//   VIBE_Guild_CheckLevel3AndShowDialog  0x521384 — rank -> Level3 dialog A/B/C and
//        the rank-validity dispatch (==1 dialog, ==-1 messagebox, else skill check).
//   VIBE_Guild_RunLevel3ContactLoop      0x521508 — def-kind -> contact status id
//        (30->4751, 31/33->4733, 32->4743).
//   VIBE_Guild_ShowLevel3OfficeDialogA/B/C 0x520f98/0x5210e4/0x521234 — the shared
//        promoted-flag -> office-name text base (+560 vs +525) used to render the
//        office title inside each dialog.
//   VIBE_Guild_ShowLevel2JoinDialog      0x520838 — the guild-join fee:
//        max(160, totalWealth * 0.01).
//   VIBE_Amt_RunAgendaWindow             0x5584c8 — the per-holder agenda bucket
//        (state 2 -> member row, state 3 -> successor row, else skipped).
#include <cstddef>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// The 536-byte "office object" record (gilde.exe word_12CE910 @0x12CE910, 768
// entries). Only the fields these kernels read are modeled, at their real offsets:
//   +0   word   personId        (word_12CE910)
//   +4   dword  objectId        (dword_12CE914)
//   +9   byte   promotedFlag    (LOBYTE dword_12CE919)  non-zero -> +560 name base
//   +12  byte   heldFlag        (record+12)             0 -> "free" name, else held
//   +360 byte   category        (byte_12CEA78)
//   +361 byte   defKind         (byte_12CEA79)  guild office def-kind (30..33)
// ===========================================================================
constexpr int kEcoOfficeRecordStride = 536;

// ---------------------------------------------------------------------------
// Shared office-name text base (VIBE_Guild_ShowLevel3OfficeDialog* / Level2 join /
// agenda all do: base = promoted ? defKind+560 : defKind+525).
//   gilde.exe: if (LOBYTE(dword_12CE919[...])) v = defKind+560; else v = defKind+525.
// ---------------------------------------------------------------------------
constexpr int kOfficeNameBasePromoted = 560;
constexpr int kOfficeNameBasePlain    = 525;
int GuildOfficeNameTextId(unsigned promoted, unsigned defKind);

// ===========================================================================
// VIBE_Amt_RunElectionCandidateWindow 0x557a40 — the candidate-row status mapping.
// ===========================================================================
// The held/free office name id from the record+12 "held" byte:
//   heldFlag == 0 -> 1597 ("free"),  else -> 1596 ("held").
//   gilde.exe: (*(_BYTE *)(rec+12) == 0) + 1596.
constexpr int kOfficeNameFree = 1597;
constexpr int kOfficeNameHeld = 1596;
int GuildCandidateOfficeNameId(unsigned heldFlag);

// The candidate's office-output rating tier. The original branches on the float
// VIBE_Building_ComputeCurrentOutput(rec) against 100 / 350 / 650 (the constants
// 1120403456 / 1135542272 / 1143111680 are the IEEE bit patterns of 100.0 / 350.0
// / 650.0). Faithful thresholds and ids:
//      out <  100 -> 1605
//   100 <= out <  350 -> 1604
//   350 <= out <  650 -> 1603
//   650 <= out        -> 1602
// (NOTE the verbatim nesting: >=100 then >=350 then >=650 -> 1602 else 1603; the
// >=350 else -> 1604; the <100 else -> 1605.)
constexpr float kCandidateOutputTier1 = 100.0f;
constexpr float kCandidateOutputTier2 = 350.0f;
constexpr float kCandidateOutputTier3 = 650.0f;
int GuildCandidateOutputRatingId(float output);

// The window title text id select: with `hasSelection` (v45) the original renders
// id 0x9D with the selected name; otherwise the "$Z$[%s$]" form. Returns the
// numeric text id used for the 0x9D path, or 0 to signal the "$Z$[%s$]" string form.
constexpr int kElectionTitleSelectedId = 0x9D;  // 157
int GuildElectionTitleTextId(bool hasSelection);

// The category gate the window enforces before collecting: a1 in (0,7) collects by
// category; a1 == 7 collects elective offices; anything else aborts (return).
enum class ElectionCollectMode : int {
    kAbort           = 0,  // a1 <= 0 || (a1 > 6 && a1 != 7)
    kByCategory      = 1,  // 0 < a1 < 7
    kElectiveOffices = 2,  // a1 == 7
};
ElectionCollectMode GuildElectionCollectMode(int category);

// ===========================================================================
// VIBE_Guild_CheckLevel3AndShowDialog 0x521384 — rank -> dialog dispatch.
// ===========================================================================
// Outer dispatch on VIBE_Amt_CheckGuildRankLevel3's return code:
//   ==  1 -> open the office dialog (sub-select by def-kind below)
//   == -1 -> show a message box
//   else  -> skill-requirement check
enum class Level3Action : int {
    kShowOfficeDialog = 0,  // code == 1
    kShowMessageBox   = 1,  // code == -1
    kCheckSkill       = 2,  // else
};
Level3Action GuildLevel3RankAction(int rankCheckCode);

// Which office dialog the def-kind byte selects when the rank check passes:
//   30 -> A,  31 -> B,  33 -> B,  32 -> C,  other -> none.
enum class Level3Dialog : int {
    kNone = 0,
    kDialogA = 1,  // def-kind 30
    kDialogB = 2,  // def-kind 31 or 33
    kDialogC = 3,  // def-kind 32
};
Level3Dialog GuildLevel3DialogForDefKind(unsigned defKind);

// ===========================================================================
// VIBE_Guild_RunLevel3ContactLoop 0x521508 — def-kind -> contact status id.
//   30 -> 4751,  31 -> 4733,  33 -> 4733,  32 -> 4743,  other -> 0.
// ===========================================================================
constexpr int kLevel3ContactId30   = 4751;
constexpr int kLevel3ContactId3133 = 4733;
constexpr int kLevel3ContactId32   = 4743;
int GuildLevel3ContactStatusId(unsigned defKind);

// ===========================================================================
// VIBE_Guild_ShowLevel2JoinDialog 0x520838 — the guild-join fee.
// ===========================================================================
// fee = (totalWealth * 0.01 > 160.0) ? totalWealth * 0.01 : 160.0, truncated to int.
//   gilde.exe: flt_62235C = 0.01 (the 1% rate), flt_622360 = 160.0 (the floor).
//   The original recomputes wealth in the > branch; we take the already-computed
//   wealth (it is the same value).
constexpr float  kGuildJoinFeeRate  = 0.0099999997764825821f; // flt_62235C (float 0.01)
constexpr double kGuildJoinFeeFloor = 160.0;                   // flt_622360
int GuildLevel2JoinFee(int totalWealth);

// ===========================================================================
// VIBE_Amt_RunAgendaWindow 0x5584c8 — per-holder agenda bucket.
// ===========================================================================
// The 24-byte office-holder entry's +16 state byte buckets the agenda rows:
//   state 2 -> member row,  state 3 -> successor row,  else skipped.
enum class AgendaBucket : int {
    kSkip      = 0,
    kMember    = 1,  // state == 2
    kSuccessor = 2,  // state == 3
};
constexpr int kAgendaHolderStride   = 24;
constexpr u8  kAgendaStateMember    = 2;
constexpr u8  kAgendaStateSuccessor = 3;
AgendaBucket GuildAgendaBucket(u8 holderState);

// ===========================================================================
// Hooks — the GUI/network/sim leaves the shells call. Inert defaults are defined
// in world_economy3.cpp; tests (and the live wiring) install their own. The
// kernels above don't call these — they're for callers wiring the full shells.
// ===========================================================================
struct WorldEconomy3Hooks {
    // VIBE_Building_ComputeCurrentOutput 0x57d26c — a candidate office's current
    // output (used by the rating tier). Default returns 0 (-> tier 1605).
    float (*buildingCurrentOutput)(const void* officeRecord) = nullptr;
    // VIBE_Person_ComputeTotalWealth 0x591f7c — a person's liquid+asset wealth
    // (drives the Level2 join fee). Default returns 0 (-> fee floor 160).
    int (*personTotalWealth)(int personId, const void* extra) = nullptr;
};

// Install / read the active hooks (returns the previous set so tests can restore).
const WorldEconomy3Hooks& WorldEconomy3GetHooks();
WorldEconomy3Hooks WorldEconomy3SetHooks(const WorldEconomy3Hooks& h);

// Convenience wrappers that route through the hooks then apply the recovered
// kernels — these mirror the exact value the shells compute per candidate.
//   GuildCandidateRatingViaHook: buildingCurrentOutput(rec) -> rating id.
int GuildCandidateRatingViaHook(const void* officeRecord);
//   GuildLevel2JoinFeeViaHook: personTotalWealth(id, extra) -> fee.
int GuildLevel2JoinFeeViaHook(int personId, const void* extra);

} // namespace guild::world
