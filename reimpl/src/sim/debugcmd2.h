#pragma once
// ===========================================================================
// debugcmd2.{h,cpp} — second wave of the VIBE_DebugCmd_* event-command family
// (gilde.exe, namespace guild::sim). Companion to debugcmd.{h,cpp}.
// ===========================================================================
//
// debugcmd.cpp translated the dispatcher (0x5711ec) plus a representative four
// handlers and the shared deterministic helpers (DebugCmdRandomModulo,
// DebugCmdScaledGold, the DebugCmdPerson adapter and the DebugCmdHooks leaf
// plumbing). This file translates the rest of the *self-contained* handlers:
// the "Scaled" wealth-roll family (B..M), the three "IfNotState14" variants and
// the "CheckType" gate. Every one of them has the canonical shape
//
//   rec = Person_FindRecordById(cmd->personId);  if (!rec) return 1;
//   (optional eligibility gate -> return 1024)
//   roll  = Math_RandomModulo(N);
//   gold  = (int)(Person_ComputeTotalWealth(rec) * (roll + base) * scale);
//   [optional second Math_RandomModulo(M) consumed for the message arg]
//   Text_RenderFormattedMessage(buf, textId, ...);          // GUI leaf
//   QueueRequest16(a1, a2, gold, market);                   // command leaf
//   He_SendEntityMessage(rec.id, rec.id, ..., buf, 1418, 0);// GUI/voice leaf
//   return 0;
//
// The deterministic decision math (RNG roll order + wealth-scaled gold) is
// reproduced exactly; the double constants are recovered from the data section
// (see debugcmd2.cpp). The GUI/voice/command side effects route through the same
// DebugCmdHooks installed in debugcmd.cpp, so the whole family stays testable in
// isolation with no Person array.
//
// The two distinct QueueRequest16 argument orderings observed in the originals
// are preserved per-handler:
//   group A  QueueRequest16(personId, -1, gold, market)
//   group B  QueueRequest16(-1, personId, gold, market)
//
// Handlers that iterate raw global arrays / unbuilt subsystems
// (SpawnEntityByOfficeCategory, SpawnEntityFromHandlerList, *NearNearest, the
// *PaletteRange*, *WithCoord*, *FromOwnedList*, *PerHandler, *PerOwnedItem and
// the BroadcastMsgToHandlers* set) are deferred — see the DEFERRED list in
// debugcmd2.cpp.
#include "sim/debugcmd.h"   // DebugCmdPerson, DebugCmdHooks, helpers, codes

namespace guild::sim {

// ---------------------------------------------------------------------------
// "Scaled" wealth-roll handlers. Each: find person (else 1) -> roll(N) ->
// gold = wealth*(roll+base)*scale -> emit cmd16 + entity message; return 0.
// The per-handler (N, base, scale) tuple is listed in debugcmd2.cpp.
// ---------------------------------------------------------------------------
// gilde.exe 0x571898 — VIBE_DebugCmd_SpawnEntityScaledB (table index 5).
i32 DebugCmdSpawnEntityScaledB(i32 personId);
// gilde.exe 0x571990 — VIBE_DebugCmd_SpawnEntityScaledC (table index 6).
i32 DebugCmdSpawnEntityScaledC(i32 personId);
// gilde.exe 0x571bb4 — VIBE_DebugCmd_SpawnEntityScaledD (table index 8).
i32 DebugCmdSpawnEntityScaledD(i32 personId);
// gilde.exe 0x571c98 — VIBE_DebugCmd_SpawnEntityScaledE (table index 9).
i32 DebugCmdSpawnEntityScaledE(i32 personId);
// gilde.exe 0x571d7c — VIBE_DebugCmd_SpawnEntityScaledF (table index 10).
i32 DebugCmdSpawnEntityScaledF(i32 personId);
// gilde.exe 0x571e60 — VIBE_DebugCmd_SpawnEntityScaledG (table index 11).
i32 DebugCmdSpawnEntityScaledG(i32 personId);
// gilde.exe 0x572038 — VIBE_DebugCmd_SpawnEntityScaledH (table index 13).
i32 DebugCmdSpawnEntityScaledH(i32 personId);
// gilde.exe 0x572120 — VIBE_DebugCmd_SpawnEntityScaledI (table index 14).
i32 DebugCmdSpawnEntityScaledI(i32 personId);
// gilde.exe 0x572308 — VIBE_DebugCmd_SpawnEntityScaledJ (table index 16).
//   gate: rec->officeRank(+358) != 0 OR rec->officeAlt(+361) != 0 (else 1024).
i32 DebugCmdSpawnEntityScaledJ(i32 personId);
// gilde.exe 0x572538 — VIBE_DebugCmd_SpawnEntityScaledK (table index 18).
i32 DebugCmdSpawnEntityScaledK(i32 personId);
// gilde.exe 0x57261c — VIBE_DebugCmd_SpawnEntityScaledL (table index 19).
i32 DebugCmdSpawnEntityScaledL(i32 personId);
// gilde.exe 0x572700 — VIBE_DebugCmd_SpawnEntityScaledM (table index 20).
i32 DebugCmdSpawnEntityScaledM(i32 personId);

// ---------------------------------------------------------------------------
// "IfNotState14" handlers: same shape, but gated on rec->officeRank(+358)==14.
// ---------------------------------------------------------------------------
// gilde.exe 0x5713a4 — VIBE_DebugCmd_SpawnEntityIfNotState14A (table index 1).
i32 DebugCmdSpawnEntityIfNotState14A(i32 personId);
// gilde.exe 0x5716a0 — VIBE_DebugCmd_SpawnEntityIfNotState14B (table index 3).
i32 DebugCmdSpawnEntityIfNotState14B(i32 personId);
// gilde.exe 0x571f44 — VIBE_DebugCmd_SpawnEntityIfNotState14C (table index 12).
i32 DebugCmdSpawnEntityIfNotState14C(i32 personId);

// ---------------------------------------------------------------------------
// "CheckType" handler: gated on BuildingType_GroupFromCode(rec->buildType)==7,
// like SpawnEntityIfNotType7 in debugcmd.cpp (the adapter's buildType already
// holds the mapped group code, so the gate tests == 7 directly).
// ---------------------------------------------------------------------------
// gilde.exe 0x572204 — VIBE_DebugCmd_SpawnEntityCheckType (table index 15).
i32 DebugCmdSpawnEntityCheckType(i32 personId);

// ---------------------------------------------------------------------------
// Trivial constant-return leaf.
// ---------------------------------------------------------------------------
// gilde.exe 0x57477c — VIBE_DebugCmd_RetFail1024 (table index 40). return 1024.
i32 DebugCmdRetFail1024();

// ---------------------------------------------------------------------------
// Extend the funcs_5766CB index -> handler map (debugcmd.cpp owns indices 0/4/
// 32/33; this resolves the indices translated here, else nullptr).
// ---------------------------------------------------------------------------
i32 (*DebugCmd2NpcTableEntry(int npcActionIndex))(i32 personId);

} // namespace guild::sim
