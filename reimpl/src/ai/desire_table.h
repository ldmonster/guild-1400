#pragma once
// AiNeeds "desire" / attribute helpers + a handful of intrigue NpcAction wrappers
// for the Guild simulation (gilde.exe). namespace guild::ai.
//
// Functions recovered here (all in the 0x471xxx / 0x4793xx range, the AiNeeds
// cluster that prior agents deferred because of its runtime-table coupling):
//
//   VIBE_AiNeeds_LookupAttributeIndex  0x4794e4
//       Maps a desire/attribute NAME (e.g. "GELD", "VERGNUEGEN") to its numeric
//       index 0..13. Pure string table; -1 (and a debug printf) on miss. Fully
//       self-contained — the only leaf is the case-insensitive string compare,
//       reproduced locally to match VIBE_Util_StrCmpNoCase.
//
//   VIBE_AiNeeds_ComputeWeights        0x47936c
//       Builds a per-good price-ratio table for an AI "buy plan" record: it looks
//       up the record's behavior-category id in the recovered 23-row goods table
//       (dword_6496A9, 21-byte stride), copies that row's non-zero good-ids into
//       the record, then for each good computes (cachedPrice / basePrice) and
//       tracks the running sum, the max-ratio slot and the min-ratio slot. The two
//       market-price leaves (cached + base) and the truncate-to-int conversion are
//       injected through DesirePriceEnv so the table walk + min/max/avg logic is
//       exercised byte-faithfully and testably.
//
//   VIBE_NpcAction_PerformEnterBuilding  0x471840   -> 40 / reject
//   VIBE_NpcAction_PerformOpenDoorLarge  0x471dfc   -> 43 / reject
//   VIBE_NpcAction_PerformOpenDoorSmall  0x471f24   -> 44 / reject
//   VIBE_NpcAction_PerformUseBack        0x471cb4   -> 42 / 0 (+ a queued command)
//       Tiny "execute intrigue action then return its label code" wrappers (the
//       same shape intrigue.cpp already ports for EvalActionLabel/EvalSlanderLabel).
//       The underlying Exec* command emitters (ExecThreaten/ExecSlander/
//       FormatSlanderLabel) are deferred string/command leaves, so their accept/
//       reject result is supplied by the caller and the command emission is routed
//       through a hook. This keeps the return-code RULE faithful and testable.
#include "guild/common/types.h"

namespace guild::ai {

// ---------------------------------------------------------------------------
// LookupAttributeIndex
// ---------------------------------------------------------------------------

// gilde.exe 0x4794e4 — VIBE_AiNeeds_LookupAttributeIndex
//   (__usercall: al=ret, ecx=this(unused scratch), eax=name)
// Returns the attribute index for `name` (case-insensitive) or -1 on no match.
// Order (and thus index) is fixed by the binary's compare chain:
//   APS=0 UNVERSEHRTHEIT=1 WOHNUNG=2 GELD=3 BERUF=4 VERGNUEGEN=5 ANSEHEN=6
//   AMT=7 BILDUNG=8 RECHTSCHAFFENHEIT=9 GEMEINHEIT=10 SICHERHEIT=11
//   FORTPFLANZUNG=12 TRAEGHEIT=13
// On miss the binary sprintf's an "Unbekanntes Beduerfnis: %s" debug message; we
// reproduce the -1 result (the message has no observable effect on the sim).
i8 LookupAttributeIndex(const char* name);

// ---------------------------------------------------------------------------
// ComputeWeights — AI buy-plan price-ratio table
// ---------------------------------------------------------------------------

// The AI "buy plan" record ComputeWeights operates on. The binary addresses it as
// a raw byte blob (a1@<eax>); we model the exact field offsets it reads/writes.
// Three parallel per-good arrays start mid-record (basePrice @+0x1C, cachedPrice
// @+0x44, ratio @+0x6C), each stride 4 — matching the `ecx += 4` walk.
struct DesirePlan {
    u8  categoryId   = 0;          // a1[0]   — *a1: behavior-category id (table key)
    i32 goodCount    = 0;          // a1[4]   — # of good-ids copied from the row
    u16 goodIds[8]   = {0,0,0,0,0,0,0,0}; // a1[8..]  — good-id list (8 word slots)
    // Parallel per-good arrays (indexed 0..goodCount-1):
    i32 basePrice[8]   = {0,0,0,0,0,0,0,0}; // a1[0x1C + 4*i]  ([ecx+1Ch])
    i32 cachedPrice[8] = {0,0,0,0,0,0,0,0}; // a1[0x44 + 4*i]  ([ecx+44h])
    float ratio[8]     = {0,0,0,0,0,0,0,0}; // a1[0x6C + 4*i]  ([ecx+6Ch])
    float avgRatio   = 0.0f;       // a1[0x94] — sum / goodCount
    i32 maxRatioSlot = 0;          // a1[0x98] — slot with the largest ratio
    i32 minRatioSlot = 0;          // a1[0x9C] — slot with the smallest ratio
};

// The two market-price leaves + the truncate-to-int conversion ComputeWeights
// calls per good. Defaults model an empty market (price 1 / 1 => ratio 1).
//   cached: VIBE_Building_LookupCachedMarketPrice(goodId, byte_6477A1)
//   base:   VIBE_Building_ComputeMarketPrice(goodId, 100)
// Both results are run through VIBE_Coord_ConvertX (truncate toward zero) before
// the int store, exactly as `(int)d` in C++.
struct DesirePriceEnv {
    virtual ~DesirePriceEnv() = default;
    virtual double CachedMarketPrice(u16 goodId) = 0; // -> stored as (int)
    virtual double BaseMarketPrice(u16 goodId) = 0;   // -> stored as (int)
};

// gilde.exe 0x47936c — VIBE_AiNeeds_ComputeWeights  (__usercall: eax=ret, eax=a1)
// Returns 1 on success, 0 when the category id is not in the goods table or the
// row has no goods (matches the binary's `v7 ^ v6`/`xor eax,ebp` zero results).
int ComputeWeights(DesirePlan& plan, DesirePriceEnv& env);

// Number of rows in the recovered goods table (dword_6496A9). Exposed for tests.
constexpr int kDesireGoodsRows = 23;

// ---------------------------------------------------------------------------
// Intrigue NpcAction "perform" wrappers (return-code rules).
// ---------------------------------------------------------------------------

// Command-emit hook for PerformUseBack (the only wrapper that queues a command on
// success). Mirrors VIBE_Command_QueueRequestArgs25(targetId, 484, 512, 4, 0).
using UseBackCmdFn = void (*)(i32 targetEntityId);
void SetUseBackCmdHook(UseBackCmdFn fn);

// gilde.exe 0x471840 — VIBE_NpcAction_PerformEnterBuilding
//   if (ExecThreaten()) return 40; else return rejectCode;  (EvalRejectStub())
u8 PerformEnterBuilding(bool execThreatenOk, u8 rejectCode);

// gilde.exe 0x471dfc — VIBE_NpcAction_PerformOpenDoorLarge
//   if (FormatSlanderLabel()) return 43; else return rejectCode;
u8 PerformOpenDoorLarge(bool slanderFormatted, u8 rejectCode);

// gilde.exe 0x471f24 — VIBE_NpcAction_PerformOpenDoorSmall
//   if (FormatSlanderLabel()) return 44; else return rejectCode;
u8 PerformOpenDoorSmall(bool slanderFormatted, u8 rejectCode);

// gilde.exe 0x471cb4 — VIBE_NpcAction_PerformUseBack
//   if (!ExecSlander(actor)) return 0;
//   QueueRequestArgs25(actor.entityId, 484, 512, 4, 0);  return 42;
// `execSlanderOk` is the ExecSlander result; `targetEntityId` is *(actor+4) (the
// entity id the command targets), forwarded to the hook on success.
u8 PerformUseBack(bool execSlanderOk, i32 targetEntityId);

} // namespace guild::ai
