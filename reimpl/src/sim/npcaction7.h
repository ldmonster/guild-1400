#pragma once
// NpcAction7 — the "social / staff-management debug-command" leaf family of the
// NpcAction cluster (gilde.exe). This batch covers:
//
//   * the three trivial relation-mood resolvers (Greet/Flirt/Compliment, the
//     entries reached after a UI target pick),
//   * the gossip/rumor spreaders that shuffle the five relation channels and
//     bump the first/two that are below the ceiling,
//   * the divorce flow (UI-driven, deferred — see report),
//   * the staff-management Cmd leaves that scan the Object/Building iterator
//     (Person_QueryBegin/IterNext) and the 768-slot Person array (word_12CE910,
//     stride 536) to pick a target, roll the shared CRT LCG for a delta, and
//     emit a build/queue command plus a localized status line:
//       HealCmd, AssignWorkCmd, SelectRoomCmd, CollectTargets,
//       AdjustStatCmd{A,B,C}.
//
// The deterministic cores — the bounded candidate-collection loops (real bounds
// recovered from disassembly, the Hex-Rays loop counters being optimizer-broken),
// the RandomModulo picks, the loyalty clamp (min(rel, roll)), and the float stat
// math ((roll + base) * 0.01, then * 100) — are translated 1:1 and golden-tested
// bit-exact against the LCG. Every cross-module leaf (Person lookup, the
// Object/Person iterators, category map, GameObject queries, command queue, text
// render, He message send, dword-array shuffle, relation-mood emit) is routed
// through NpcAction7Hooks so the bodies run in isolation; a test installs a
// recording mock, nullptr installs an inert default.
//
// Translated functions (absolute addresses, imagebase 0x400000):
//   0x56850c ResolveTargetAndGreet      0x568578 ResolveTargetAndFlirt
//   0x5685e4 ResolveTargetAndCompliment
//   0x568998 SpreadGossipToOne          0x568ec4 SpreadGossipToTwo
//   0x575414 HealCmd                    0x5755ac AssignWorkCmd
//   0x575804 CollectTargets             0x575c64 SelectRoomCmd
//   0x575948 AdjustStatCmdA             0x575a4c AdjustStatCmdB
//   0x575b58 AdjustStatCmdC
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// Cross-module leaf hooks for the NpcAction7 family.
// ---------------------------------------------------------------------------
// Records (Person 536B, Object/Building 169B) are treated as raw byte buffers,
// exactly as the originals do via *(_TYPE *)(base + off). Each hook returns a
// byte pointer so a test can supply a small mock buffer; nullptr/0 returns model
// an "absent"/empty world.
// ===========================================================================
struct NpcAction7Hooks {
    // VIBE_Person_FindRecordById(id) -> Person record base (536B), or nullptr.
    u8* (*findRecordById)(i32 id);

    // VIBE_Person_QueryBegin(player, a, b, personIndex) -> first Object/Building
    //   record (169B) of the iteration, or nullptr. The three social/heal callers
    //   pass (player, 1, 3, index); AdjustStat callers pass (player, 1, 1, id).
    u8* (*queryBegin)(int player, int a, int b, u16 index);
    // VIBE_Person_IterNext() -> next Object/Building record, or nullptr (end).
    u8* (*iterNext)();

    // VIBE_Building_MapTypeToCategory(typeByte) -> category (1 = house, 2 = work).
    int (*mapTypeToCategory)(int typeByte);

    // VIBE_GameObject_QueryFind / VIBE_GameObject_IterNext — used by CollectTargets
    //   and SelectRoomCmd. The QueryFind variants take 2..5 args in the original;
    //   we expose the two arities used here.
    //   QueryFind3(arg, a, b)            (SelectRoomCmd room scan, CollectTargets sub)
    //   QueryFind5(arg, a, b, c, d)      (CollectTargets primary)
    u8* (*gameObjectQueryFind3)(i32 arg, int a, int b);
    u8* (*gameObjectQueryFind5)(i32 arg, int a, int b, int c, int d);
    u8* (*gameObjectIterNext)();

    // The Person array substrate (word_12CE910, 768 slots × 536 bytes). The Heal
    //   and AssignWork scans walk this directly. base==nullptr models an empty
    //   array; capacity defaults to 768.
    u8* (*personTableBase)();
    int (*personTableCapacity)();

    // VIBE_Npc_AdjustRelationByMood(personRecord, kind) — relation-mood bump.
    void (*adjustRelationByMood)(u8* personRecord, int kind);

    // VIBE_Util_InitAndShuffleDwordArray(n, dst) — Fisher-Yates 0..n-1 over the LCG.
    void (*shuffleDwords)(int n, i32* dst);

    // VIBE_Command_RequestBuildOp67(id) — gossip "spread" command.
    void (*requestBuildOp67)(i32 id);
    // VIBE_Command_RequestBuildOp93(id, kind, id2, amount) — heal/relation delta.
    void (*requestBuildOp93)(i32 id, int kind, i32 id2, u8 amount);
    // VIBE_Command_QueueRequest17(idA, idB, count, kind, currency, flag) — work/room.
    void (*queueRequest17)(i32 idA, i32 idB, int count, int kind, u8 currency, int flag);

    // VIBE_Coord_ConvertX() — a side-effecting x87 stack op the originals call after
    //   loading the (int)double; modeled as a no-op (it does not alter the value).

    // VIBE_He_SendEntityMessage(idA, idB, textId) — status line; we collapse the
    //   rendered text to its base textId for testability.
    void (*sendEntityMessage)(i32 idA, i32 idB, i32 textId);
    // VIBE_He_SendQuickjumpMessage(idA, idB, textId, jumpId) — work/stat status line.
    void (*sendQuickjumpMessage)(i32 idA, i32 idB, i32 textId, i32 jumpId);

    // The currency byte the originals read from byte_6477A1 (active player's coin).
    u8 currencyByte;
};

void SetNpcAction7Hooks(const NpcAction7Hooks* hooks);
const NpcAction7Hooks& GetNpcAction7Hooks();

// Result codes the dispatcher sentinels these functions return verbatim.
constexpr int kNpc7Ok      = 0;
constexpr int kNpc7NoActor = 1;
constexpr int kNpc7Retry   = 1024;

// ---------------------------------------------------------------------------
// Person-table geometry (word_12CE910). FindRecordById (0x58bc6c) steps 536 and
// bounds at 536*768; the Heal/AssignWork scans use the same stride. The marker
// byte at +2 (byte_12CE912) and the id column at +4 (dword_12CE914) are read by
// the scans; dword_12CEA7C is the dword at +0x16c of the same record.
// ---------------------------------------------------------------------------
constexpr int kNpc7PersonStride   = 536;   // 0x218
constexpr int kNpc7PersonCapacity = 768;
constexpr int kNpc7PersonOwnerObjOff = 0x16c;  // dword_12CEA7C == word_12CE910 + 0x16c

// ===========================================================================
// Social relation resolvers (0x56850c / 0x568578 / 0x5685e4).
//   If descriptor[+2] == 6 (the "needs UI target" marker), the original opens an
//   office window to pick a target; here that branch is routed through the
//   queryBegin/find leaves and resolves to nullptr by default (no target). The
//   non-UI branch resolves descriptor[+4]'s id to a Person record and bumps the
//   relation by a fixed mood kind (1 = greet, 3 = flirt, 4 = compliment).
//   Returns 1 if a target was found and bumped, else 0.
// The descriptor is the He record (a1@eax); the resolved-id source is a2@edx.
int NpcAction7_ResolveTargetAndGreet(HeRecord* desc, HeRecord* src);
int NpcAction7_ResolveTargetAndFlirt(HeRecord* desc, HeRecord* src);
int NpcAction7_ResolveTargetAndCompliment(HeRecord* desc, HeRecord* src);

// ===========================================================================
// Gossip / rumor spreaders (0x568998 / 0x568ec4).
//   personRecord@eax is the gossiper's Person record (536B); desc@edx is the He
//   descriptor (desc[16] is set to 1 = "active", desc[0] points at the speaker
//   index word used for the message). Shuffle the 5 relation channels, walk them
//   in shuffled order, and for each whose relation byte (record+128+chan) is below
//   the ceiling 0xFC, bump the mood by that channel and remember it; ToOne stops
//   after the first match, ToTwo after two. If desc[+2]==6 (UI/local actor), emit
//   the spread command and render the rumor text. Always returns 1.
int NpcAction7_SpreadGossipToOne(u8* personRecord, HeRecord* desc);
int NpcAction7_SpreadGossipToTwo(u8* personRecord, HeRecord* desc);

// ===========================================================================
// Staff-management Cmd leaves.
// ---------------------------------------------------------------------------

// gilde.exe 0x575414 — VIBE_NpcAction_HealCmd(descriptor@eax).
//   Resolve actor by descriptor[+0]. Collect up to 8 category-1 (house) buildings
//   from the QueryBegin iterator; if none -> 1024. Pick RandomModulo(count); scan
//   the Person array for the slot whose marker(+2)==1 and ownerObj(+0x16c) equals
//   the picked building's id; require its stat byte (+0x81) > 0. delta =
//   min(stat, RandomModulo(0xA)+5); emit RequestBuildOp93(personId,1,_, -delta)
//   and a status line. Returns 0 / 1 (no actor) / 1024 (no candidate/target).
int NpcAction7_HealCmd(HeRecord* desc);

// gilde.exe 0x5755ac — VIBE_NpcAction_AssignWorkCmd(descriptor@eax, player@esi).
//   Resolve actor; collect up to 8 category-1/2 buildings; pick one. Then scan a
//   half of the Person array (forward/backward, RandomModulo(2)-chosen) for up to
//   16 employable persons (marker(+2) not 6/7 and < 10, slot live), pick one, and
//   queue a work-assignment command + status line. Returns 0 / 1 / 1024.
int NpcAction7_AssignWorkCmd(HeRecord* desc, int player);

// gilde.exe 0x575804 — VIBE_NpcAction_CollectTargets(personRecord@eax, out@edx,
//                                                     rate@stack).
//   Collect up to 8 category-1/2 buildings; if none -> 0. Pick one, write its id
//   to *out, find the building's workshop, iterate up to 12 of its rooms, and for
//   each whose count(+14) > 1 queue a proportional (count*rate, >=1) command.
//   Returns 1 on success, 0 if no candidate / no workshop.
int NpcAction7_CollectTargets(u8* personRecord, i32* out, float rate);

// gilde.exe 0x575c64 — VIBE_NpcAction_SelectRoomCmd(descriptor@eax).
//   Resolve actor; gate on RandomModulo(0x100) >= stat(+130). Iterate the actor's
//   rooms (object[+94]), collect up to 6 whose type-descriptor kind (+0) != 9,
//   pick one, queue a select-room command + status line. Returns 0 / 1 / 1024.
int NpcAction7_SelectRoomCmd(HeRecord* desc);

// gilde.exe 0x575948 / 0x575a4c / 0x575b58 — VIBE_NpcAction_AdjustStatCmd{A,B,C}.
//   roll = RandomModulo(N);  frac = (roll + base) * 0.01;  resolve actor;
//   CollectTargets(actor, scratch, frac); gate on QueryBegin(...); pct = (int)
//   (frac * 100);  render message + send. (A: N=0xD,base=13; B: N=0xF,base=5;
//   C: N=0x14,base=5.)  Returns 0 / 1 / 1024.
// descriptor@ecx (descriptor[0]=personId, descriptor[+4]=textBase); player@esi.
int NpcAction7_AdjustStatCmdA(HeRecord* desc, int player);
int NpcAction7_AdjustStatCmdB(HeRecord* desc, int player);
int NpcAction7_AdjustStatCmdC(HeRecord* desc, int player);

// ---------------------------------------------------------------------------
// Pure deterministic cores exposed for golden tests (no hooks; the building
// pointers come from a caller-supplied iterator callback).
// ---------------------------------------------------------------------------

// The (roll + base) * 0.01 fixed-stat fraction the AdjustStat leaves compute, and
// its * 100 (int) re-expansion. base/N per the three variants above.
double NpcAction7_StatFraction(int roll, double base);   // (roll + base) * 0.01
int    NpcAction7_StatPercent(double fraction);          // (int)(fraction * 100)

// The loyalty/heal clamp: delta = min(stat, RandomModulo(span) + lo) — but the
// original re-draws RandomModulo only in the clamped branch. Given the two draws
// (firstRoll already added to lo, secondRoll the re-draw), return the emitted
// delta for a stat value. Mirrors HealCmd / IncreaseLoyaltyCmd exactly.
int NpcAction7_HealDelta(int stat, int firstRoll, int secondRoll, int lo);

} // namespace guild::sim
