#pragma once
// ===========================================================================
// MeisterAi request-command builders + AiAction handlers/dispatch — pure
// packet-construction and routing reconstruction.
//
// gilde.exe addresses:
//   0x4c9330 VIBE_MeisterAi_RequestCmd58    (__thiscall this=AiPlayer)
//   0x4c937c VIBE_MeisterAi_RequestCmd59    (__usercall)
//   0x4c7164 VIBE_MeisterAi_RequestCmd109   ()
//   0x4c93f4 VIBE_MeisterAi_RequestCmd115   (__usercall)
//   0x475c74 VIBE_AiAction_HandleGuildhallTrigger (__usercall a1,a2,a3)
//   0x476140 VIBE_AiAction_HandleObjectType23      (__usercall)
//   0x476440 VIBE_AiAction_HandleObjectType14      (__usercall)
//   0x47cc68 VIBE_AiAction_DispatchSecondarySearch (__userpurge)
//
// These functions build a small fixed-layout request packet (a command id, an
// object/scene id, a -1 sentinel, a mode/flag byte, and — for some — the packed
// game-time qword) and hand it to the command queue, OR they classify a pair of
// scene objects and pick which handler/command to emit.
//
// COUPLED LEAVES (inert hooks — the real engine supplies these; we surface them
// as inputs so the PACKET CONTENTS and the ROUTING are deterministic, never faked):
//   * VIBE_Command_QueueRequestSlotReset28 / EnqueueCmd15 / QueueRequest17 /
//     QueueRequestArgs25 — the live command-queue emit; modeled by RequestPacket
//     capture (the packet the original would have queued).
//   * VIBE_He_FindFirstHandlerByFilter / FreeHandlerEntry — handler-list probe;
//     RequestCmd109/59/115 only emit when no matching handler exists (cmd109) or
//     after freeing the existing one (59/115). Surfaced via `existingHandler`.
//   * dword_12CE914[..] / dword_6498E4 — the AiPlayer's current scene-object id;
//     passed in as `objectId`.
//   * qword_13CE852 (+unk_13CE85A/85E) — packed current game time; passed in as
//     a GameTimeStamp the packet carries verbatim.
//   * VIBE_DebugCmd_DispatchByType — the per-search-type probe (returns a bitmask;
//     bit 1 = candidate found). For DispatchSecondarySearch we model the routing
//     over a `probe(type, slot)` functor + the search-type table dword_62EBC4.
// ===========================================================================
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Packed game-time stamp the request packets carry (qword_13CE852 + the two
// trailing fields the SlotReset28 packet appends). Stored verbatim.
// ---------------------------------------------------------------------------
struct GameTimeStamp {
    u64 packed = 0;   // qword_13CE852
    int extra  = 0;   // unk_13CE85A
    u16 tail   = 0;   // unk_13CE85E
};

// ---------------------------------------------------------------------------
// The request packet the MeisterAi RequestCmd* builders assemble before handing
// it to VIBE_Command_QueueRequestSlotReset28. Field layout mirrors the stack
// frame the decompile fills (cmdId, objectId, sentinel=-1, mode, [time]).
// ---------------------------------------------------------------------------
struct RequestPacket {
    u8  cmdId    = 0;    // v3/v4/v7/v10  (58 / 59 / 109 / 115)
    int objectId = 0;    // dword_12CE914[..] or dword_6498E4+4
    int sentinel = -1;   // always -1 (v5/v9/v6 = -1)
    u8  mode     = 0;    // v6/v13/v10   (1 for 58/59/115, 2 for 109)
    bool hasTime = false;
    GameTimeStamp time{};
};

// 0x4c9330 — RequestCmd58: cmd=58, obj=objectId, mode=1, no time.
RequestPacket BuildRequestCmd58(int objectId);

// 0x4c937c — RequestCmd59: if a matching handler exists it is freed first; then
// cmd=59, obj=objectId, mode=1, WITH time stamp. `freedExisting` reports whether
// the existing-handler free path was taken (it always still emits the packet).
RequestPacket BuildRequestCmd59(int objectId, const GameTimeStamp& now,
                                bool existingHandler, bool* freedExisting);

// 0x4c93f4 — RequestCmd115: same shape as 59 but obj = dword_6498E4+4 (passed in
// as objectId), cmd=115, mode=1, WITH time.
RequestPacket BuildRequestCmd115(int objectId, const GameTimeStamp& now,
                                 bool existingHandler, bool* freedExisting);

// 0x4c7164 — RequestCmd109: ONLY emits when no matching handler exists. cmd=109,
// obj=objectId, mode=2, WITH time. Returns true if a packet was produced (and
// fills *out); false (no emit) when a handler already exists.
bool BuildRequestCmd109(int objectId, const GameTimeStamp& now,
                        bool existingHandler, RequestPacket* out);

// ===========================================================================
// AiAction handlers — classify a pair of scene objects (a2/a3 = two object
// records) and emit a command. The object record fields the decompile reads:
//   type byte at +0, sub/level byte at +4, ids at +4/+8/+12/+16.
// We pass the salient fields as a small struct; the emitted command is captured.
// ===========================================================================

struct ObjPair {
    u8  aType = 0;   // *a2  (object kind)
    int aId4  = 0;   // *(a2+4)
    int aId12 = 0;   // *(a2+12)
    u8  bType = 0;   // *a3  (object kind)
    int bHas8 = 0;   // *(a3+8) (guildhall: nonzero gate)
    int bId4  = 0;   // *(a3+4)
};

// An emitted AiAction command (collapses the EnqueueCmd15 / SlotReset28 chains).
struct AiActionEmit {
    u8  cmd     = 0;   // SlotReset28 cmd byte (30 / 18 / ...)
    int srcId   = 0;
    int dstId   = 0;
    int amount  = 0;
    bool upgrade = false;
};

// 0x475c74 — Guildhall trigger. Returns the AiAction result code (57 on success,
// 0 otherwise) and, on the upgrade branch (aType==4 && bType==1), fills `*upgradeAmt`
// with the pure scaled worth:  trunc(SumFlaggedSlotsWorth * 0.3).
//   - aType==1 && bType==18 && b.has8 != 0 : "transfer" path → returns 57.
//   - aType==4 && bType==1 : guildhall-upgrade path → returns 57 (upgrade emitted).
//   - else returns 0.
// `flaggedWorth` = VIBE_Building_SumFlaggedSlotsWorth(...) (engine read, passed in).
int HandleGuildhallTrigger(const ObjPair& p, int flaggedWorth, int* upgradeAmt,
                           bool* didUpgrade);

// kGuildhallWorthScale = flt_61A6B4 = 0.3.
constexpr float kGuildhallWorthScale = 0.30000001f;

// 0x476140 — Object type 23. gate: a2.type==7 && a3.type==23. Returns 58 (and the
// SlotReset packet semantics) on match, else 0.
int HandleObjectType23(u8 aType, u8 bType);

// 0x476440 — Object type 14. gate: a1.type==7 && a2.type==14 && a person record
// exists. Returns 59 on match, else 0. `hasPersonRecord` is the FindRecordById(0)
// result (engine read).
int HandleObjectType14(u8 aType, u8 bType, bool hasPersonRecord);

// ===========================================================================
// 0x47cc68 — DispatchSecondarySearch. Routes to one of five finder functions by
// a search-type code, randomly when both a "type-1" and "type-2" candidate set is
// non-empty. The pure routing (which finder for which type code) plus the random
// selection between the two candidate lists is reconstructed; the finders and the
// per-type probe are inert hooks.
//
//   The search-type codes (dword_62EBC4 packs three bytes, read via HIBYTE of a
//   rolling dword — i.e. one byte per i in 0..2) map to finders:
//       0 → FindFactionPerson        (0x47bf98)
//       3 → FindAdjacentEntityLarge  (0x47c094)
//       4 → FindEligibleNeighbor     (0x47c164)
//       7 → FindNearbyBuilding       (0x47c23c)
//       8 → FindNearbyWealthyTarget  (0x47c314)
//   Any other code → no route.
// ===========================================================================
enum class SearchFinder {
    None,
    FactionPerson,       // type 0
    AdjacentEntityLarge, // type 3
    EligibleNeighbor,    // type 4
    NearbyBuilding,      // type 7
    NearbyWealthyTarget, // type 8
};

// Pure type-code → finder map (the switch in the binary).
SearchFinder FinderForType(int typeCode);

// The full routing decision for the "fresh search" path (a3.type == 0):
//   * collect candidates of mode-1 (probe(code,1) bit1 set) into list1,
//   * if a "follow" handler exists, collect mode-2 into list2,
//   * if both non-empty: roll a coin (rollCoin()==0 → pick list1 else list2),
//     else pick whichever is non-empty,
//   * from the picked list, pick a random index (rollIndex(n)), route via
//     FinderForType. Returns the chosen finder + the chosen type code.
// `probe(typeCode, mode)` returns the DebugCmd_DispatchByType bitmask (bit 1 =
// candidate). `searchTypes` is the 3-entry dword_62EBC4 type list.
struct SearchRoute {
    SearchFinder finder = SearchFinder::None;
    int typeCode = -1;
    int mode     = 0;   // 1 or 2 (which candidate list won)
};
SearchRoute RouteSecondarySearch(const std::array<int, 3>& searchTypes,
                                 bool hasFollowHandler,
                                 const std::function<int(int, int)>& probe,
                                 const std::function<int()>& rollCoin,
                                 const std::function<int(int)>& rollIndex);

} // namespace guild::sim
