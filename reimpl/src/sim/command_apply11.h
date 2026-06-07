#pragma once
#include "guild/common/types.h"
#include "sim/command.h"
#include "sim/command_pending.h"   // PendingState (StagePendingBlock state)
#include "sim/command_inherit.h"   // QueueRequestSlotReset28 / SlotResetScratch

// command_apply11 — the remaining console/cheat-driven VIBE_Command_Queue* leaf
// builders (namespace guild::sim). Every name/address below was checked, both by
// address (UPPERCASE + split forms) AND by bare name, case-insensitively against
// command_apply..command_apply10, command_codec, command_builders*,
// command_pending, command_inherit, command_receive and command_unit_orders;
// none of the functions defined here is defined there.
//
// This file completes the 0x4FB000-region "debug command" emitters — the
// handlers a console/cheat string ("-PLUS_500" etc.) routes into. They split
// into two shapes, both deterministic field-packing:
//
//   (A) Opcode-28 slot-reset emitters. Each zeroes a 248-byte stack scratch
//       (SetGrayColorThunk(0,248) == a dword-fill memset), writes a small set of
//       fields at recovered byte offsets, then hands the scratch to the REAL
//       reconstructed QueueRequestSlotReset28 (command_inherit.cpp @0x4948c8),
//       which forces scratch.words[14] to -1 if 0, stages the 248 bytes through
//       StagePendingBlock, and EnqueuePackets an opcode-28 header:
//         QueueGiveGold         0x4fb284   (opcode field 78)
//         QueueAdjustReputation 0x4fb300   (opcode field 80)
//         QueueSpawnGuard       0x4fb210   (opcode field 125)
//         QueueSpawnSelected    0x4fb0f0   (opcode field 83, count gate)
//         QueueRemoveAllCarried 0x4fb180   (opcode field 127, selection scan)
//
//   (B) String-parse + selection-scan emitters that, per matched person/object,
//       emit a real QueueRequest16 / QueueRequest17 / QueueRequestCoord27 packet
//       (all reconstructed in command_codec.cpp) or a State22 delta packet:
//         QueueRevealSelected        0x4fb89c
//         QueueAdjustAllPersonStat   0x4fb52c
//         QueueAdjustBuildingStat    0x4fb37c
//         QueueAdjustSelectedStat    0x4fb614
//         QueueMovePersonsToCoord    0x4fb754
//         QueueRevealAllPersons      0x4fbc40
//         QueueSetSelectedFlag       0x4fbbcc   (BeginDeltaPacket + State22)
//         QueueSetJusticeSeverity    0x4fbd00   (Gesetz clamp/apply)
//         QueueAdjustJusticeSeverity 0x4fbe04   (Gesetz clamp/apply)
//
// Cross-module leaves are NOT reconstructed in this wave, so they are routed
// through ONE installable hooks struct with inert default implementations
// defined in this library .cpp (the CutsceneMiscHooks / ApplyTargetHooks
// pattern). Tests install spies; the unified build links the inert defaults.
// Already-reconstructed callees (QueueRequest16/17/Coord27, QueueRequestSlotReset28,
// the DeltaWriter, util::StrCmpNoCaseN) are reused, never duplicated.

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered game-state constants the emitters read out of file globals. The
// originals read these from dword_6498E4 (the local player record; +4 = player
// id), qword_13CE852/unk_13CE85A.. (a 14-byte GameTime stamp) and the selection
// table dword_12CE914 (selected object ids). We capture them in a context the
// host fills from its live globals; tests fill deterministic values.
// ---------------------------------------------------------------------------
struct DebugCmdCtx {
    // dword_6498E4 + 4 — local player id. Stamped into scratch offset +8 of the
    // opcode-28 emitters.
    i32 playerId = -1;

    // The 14-byte GameTime stamp (qword_13CE852 || unk_13CE85A(4) || unk_13CE85E(2)).
    // Copied verbatim into the scratch at offset +0x28; WORD2 (bytes +0x2C..+0x2D)
    // is then overwritten with a per-emitter discriminator.
    u8 gameTime[14] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0};

    // dword_12CE914[i] — id of the i-th selected object (stride 4 dwords in the
    // 134-dword scene record, i.e. dword_12CE914[i*134] in the original). Used by
    // the selection-scan emitters. For a small test we expose a flat accessor.
    i32 (*selectedId)(int index) = nullptr;       // default: returns 0
    int (*selectionActive)(int index) = nullptr;  // byte_12CEA76||byte_12CEA79
};

// ---------------------------------------------------------------------------
// Cross-module hooks. All defaults are inert (parse 0 / find nothing / no-op).
// ---------------------------------------------------------------------------
struct DebugCmdHooks {
    // VIBE_Util_ParseInt @0x5dc070 — parse a leading signed decimal. Default 0.
    i32 (*parseInt)(const char* s) = nullptr;

    // VIBE_Util_ToLower @0x5f0c00 — in-place lower-case. Default: no-op.
    void (*toLower)(char* s) = nullptr;

    // VIBE_World_CountActiveObjects @0x5839f0 — active scene-object count. Default 0.
    i16 (*countActiveObjects)() = nullptr;

    // VIBE_Gesetz_GetRecord @0x4c244c — load law record `lawId` into out[?]; the
    // emitters read out[1] (apply flag), out[+4] (lo bound) and out[+8] (hi bound).
    // Returns nonzero on success. Default: returns 0 (no record).
    struct GesetzRecord { u8 applyFlag; i32 lo; i32 hi; };
    int (*gesetzGetRecord)(u8 lawId, GesetzRecord* out) = nullptr;

    // VIBE_Gesetz_RequestApply @0x4c247c — queue a law-severity change. Default no-op.
    void (*gesetzRequestApply)(int a, u8 lawId, i32 value) = nullptr;
};

// Install spies (returns the previously-installed set). Pass null to reset to inert.
DebugCmdHooks  SetDebugCmdHooks(const DebugCmdHooks& h);
DebugCmdHooks& GetDebugCmdHooks();

// ---------------------------------------------------------------------------
// (A) Opcode-28 slot-reset emitters. `pending` carries the StagePendingBlock
// state (one per session). Each returns 1 (or the EnqueuePacket result where the
// original returns it verbatim).
// ---------------------------------------------------------------------------

// gilde.exe 0x4fb284 — VIBE_Command_QueueGiveGold. Scratch: byte[+4]=78,
// dword[+8]=ctx.playerId, dword[+12]=-1, byte[+0x36]=1, dword[+0x9C]=1000,
// gameTime@+0x28 with word@+0x2C := 7. Returns 1.
i32 QueueGiveGold(CommandQueue& q, PendingState& pending, const DebugCmdCtx& ctx);

// gilde.exe 0x4fb300 — VIBE_Command_QueueAdjustReputation. byte[+4]=80,
// dword[+8]=playerId, dword[+12]=-1, byte[+0x36]=2, dword[+0x58]=-1,
// dword[+0x9C]=1000, gameTime@+0x28 with word@+0x2C := 16. a2(ecx)=-1. Returns 1.
i32 QueueAdjustReputation(CommandQueue& q, PendingState& pending, const DebugCmdCtx& ctx);

// gilde.exe 0x4fb210 — VIBE_Command_QueueSpawnGuard. byte[+4]=125,
// dword[+8]=playerId, dword[+12]=-1, byte[+0x36]=1, gameTime(bytes +0x28..+0x33)
// then word@+0x2C := 8, dword@+0x2E := 0. Returns the EnqueuePacket result.
i32 QueueSpawnGuard(CommandQueue& q, PendingState& pending, const DebugCmdCtx& ctx);

// gilde.exe 0x4fb0f0 — VIBE_Command_QueueSpawnSelected. Gate: arg[0]=='-' and
// parseInt(arg+1) > 0. byte[+4]=83, dword[+8]=playerId, dword[+12]=-1,
// byte[+0x36]=1, byte[+0x58]=0, word@+0x2C := 12, dword@+0x2E := 0. Returns 1.
i32 QueueSpawnSelected(CommandQueue& q, PendingState& pending, const DebugCmdCtx& ctx,
                       const char* arg);

// gilde.exe 0x4fb180 — VIBE_Command_QueueRemoveAllCarried. byte[+4]=127,
// gameTime@+0x30, word@+0x34 := 8, dword@+0x36 := 0, byte[+0x3E]=1, dword[+0x14]=-1;
// then scans 768 scene records: for each whose class byte (byte_12CE912[+0,stride
// 536]) is 6 or 7, sets dword[+0x10]:=its id (dword_12CE914) and emits a reset.
// Modelled via ctx.selectedId/selectionActive (host wires the live table). Returns 1.
i32 QueueRemoveAllCarried(CommandQueue& q, PendingState& pending, const DebugCmdCtx& ctx);

// ---------------------------------------------------------------------------
// (B) String-parse + scan emitters. They reuse the reconstructed request
// builders; the per-record entity scan is driven through `ctx`. Each returns 1
// on success (a packet was considered) and 0 on a parse/gate failure, exactly as
// the originals. `scaleX` is the recovered per-unit float multiplier.
// ---------------------------------------------------------------------------

// gilde.exe 0x4fb89c — VIBE_Command_QueueRevealSelected. Selection index `a2`
// (< 8); finds the person record; emits a Building stock adjust of
// -(currentOutput+1). Cross-module (Person/Building/Coord) so the whole body is
// the hook `revealSelected`; default inert. Returns 1 if it ran, 0 on gate.
i32 QueueRevealSelected(CommandQueue& q, const DebugCmdCtx& ctx, i32 selBase, i32 a2);

// gilde.exe 0x4fb52c — VIBE_Command_QueueAdjustAllPersonStat. arg must be "-N";
// scales N by ctx scale, then for each of 768 persons (active && class<10) emits
// QueueRequest16(personId, ?, scaledWealth, 0). Person wealth + scan via ctx.
i32 QueueAdjustAllPersonStat(CommandQueue& q, const DebugCmdCtx& ctx, const char* arg);

// gilde.exe 0x4fb37c — VIBE_Command_QueueAdjustBuildingStat. Parses
// "MINUS|PLUS<...>_N", maps sign to ctx table, finds the player guild building,
// emits one QueueRequest16 with the scaled amount. Returns 1/0.
i32 QueueAdjustBuildingStat(CommandQueue& q, const DebugCmdCtx& ctx, const char* arg);

// gilde.exe 0x4fb614 — VIBE_Command_QueueAdjustSelectedStat. Selection `a2`,
// person record found; parse "MINUS|PLUS..._N"; scaled by held currency; emits
// QueueRequest16 with from/to swapped depending on the sign slot. Returns 1/0.
i32 QueueAdjustSelectedStat(CommandQueue& q, const DebugCmdCtx& ctx, i32 selBase,
                            i32 a2, const char* arg);

// gilde.exe 0x4fb754 — VIBE_Command_QueueMovePersonsToCoord. Selection `a2`,
// person found; parse "MINUS|PLUS..._N"; v10 = signTable[slot]*2*N; for each of
// 768 active persons emits QueueRequestCoord27(personId, sceneId, v10). Returns 1/0.
i32 QueueMovePersonsToCoord(CommandQueue& q, const DebugCmdCtx& ctx, i32 selBase,
                            i32 a2, const char* arg);

// gilde.exe 0x4fbc40 — VIBE_Command_QueueRevealAllPersons. arg "-XXXX..."; needs
// strlen(arg+1) >= 4; lower-cases the tail; active = countActiveObjects(); for
// each of 768 active persons emits QueueRequest17(sceneId, -1, 1, active, flag, 0).
i32 QueueRevealAllPersons(CommandQueue& q, const DebugCmdCtx& ctx, const char* arg);

// gilde.exe 0x4fbbcc — VIBE_Command_QueueSetSelectedFlag. arg "-N"; selection
// `a2`; person found; BeginDeltaPacket(person) + AppendRawField(width1,count1,
// value=(u8)N, offset 433) + State22 emit. Uses the supplied DeltaWriter (real).
i32 QueueSetSelectedFlag(CommandQueue& q, DeltaWriter& dw, const DebugCmdCtx& ctx,
                         i32 selBase, i32 a2, const char* arg);

// gilde.exe 0x4fbd00 — VIBE_Command_QueueSetJusticeSeverity. arg must match one
// of two "RECHTSPRECHUNG_HAERTE"-class names then "_N"; loads the law record,
// clamps N to [lo,hi] (the original's exact two-stage clamp), and calls
// gesetzRequestApply. Returns 1/0.
i32 QueueSetJusticeSeverity(const DebugCmdCtx& ctx, i32 lawSel, const char* arg);

// gilde.exe 0x4fbe04 — VIBE_Command_QueueAdjustJusticeSeverity. Parses
// "MINUS|PLUS" then a law name then "_N"; sign = +1/-1; value = curr + sign*N,
// clamped to [lo,hi]; calls gesetzRequestApply. Returns 1/0.
i32 QueueAdjustJusticeSeverity(const DebugCmdCtx& ctx, i32 lawSel, const char* arg);

// ---------------------------------------------------------------------------
// Exposed packing helper (also used by the unit tests as the golden oracle).
// Builds the 248-byte opcode-28 scratch body the (A) emitters stage. `opByte`
// is the byte written at scratch+4; the rest follow the per-emitter recipe.
// ---------------------------------------------------------------------------
void PackSlotResetScratch(SlotResetScratch& scratch, const DebugCmdCtx& ctx,
                          u8 opByte, i32 word2Hi, i32 word8, i32 word12,
                          u8 byte0x36, i32 word0x58, i32 word0x9C,
                          bool hasWord0x58, bool hasWord0x9C);

} // namespace guild::sim
