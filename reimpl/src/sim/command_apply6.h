#pragma once
#include <vector>

#include "guild/common/types.h"
#include "sim/command.h"
#include "sim/trade_sell.h"
#include "sim/types.h"

// gilde.exe — Command APPLY handlers, SIXTH (final) batch (namespace guild::sim).
//
// The LAST deferred Command_Ex apply handlers + the group-framing opcodes, which
// close the 96-entry dispatch jump table (funcs_4941F4 @0x631298, invoked from
// VIBE_Command_ExecCommands @0x494088). Batches 1-5 own all opcodes EXCEPT the
// eight below; this batch registers ONLY those, so all six registries compose
// without clobbering each other (grep-verified disjoint):
//
//   0x05 ExGroupBegin            49 42C0  group-begin frame (ExecCommandGroup)
//   0x06 ExGroupEnd              49 42C0  group-end frame
//   0x07 ExGroupSkip             49 42C0  group skip marker
//   0x11 ExSellObjekt            49 6B90  the sell APPLY engine (trade_sell core)
//   0x12 ExComputeSellableAmount 49 7538  produce-and-sell APPLY (trade_sell core)
//   0x1B ExComputeObjectCoords   49 818C  the relation-matrix mutation
//   0x1C ExShowMessageBox        49 8678  He alloc + chat string buffer
//   0x1E ExAdvanceGameTick       49 8954  the turn cascade
//
// Common ABI (recovered, all __usercall): packet record base -> CommandPacket&
// (payload begins at +0x10); the 10-byte ACK/status entry -> AckEntry* (may be
// null). Handlers stamp it (a2[0]=status, a2[1]=sub-tag, a2[6]=result ptr/id) and
// return 0 == applied, 1 == rejected (the dispatcher ignores the value; the ACK
// carries the outcome).
//
// The -2/-3/-4 "last-created object/scene/trade id" remap tokens
// (dword_631288/63128C/631290) are the SAME globals batch 1 exposes
// (g_lastObjectId / g_lastSceneId / g_lastTradeId in command_apply.h); we reuse
// them. ExSellObjekt's only g_lastTradeId (dword_631290) store is the original's
// LABEL_58 latch `dword_631290 = *(v55+2)` (the dest stock NODE id), which lives
// 1:1 inside the dest storage phase (buildingtype_callers'
// Sell_EnsureDestStorageNode, installed by WireBuildingCallers); the handler
// itself never writes it.
//
// 0x11/0x12 reuse the deterministic sell/produce cores already ported in
// trade_sell.{h,cpp} (TradeSellObjektResolve / TradeComputeSellableAmount, which
// in turn reuse inventory_capacity + inventory_wealth + building_production). The
// apply handlers here only DECODE the packet payload (byte-exact offsets), apply
// the -2/-3/-4 remap, resolve the source/destination containers (through a
// mockable resolve hook — the entity-array + scene-tree leaves are a separate
// cluster), delegate to the core, and stamp the ACK. The deep render/HUD/voice +
// scene AddObjekt/RemoveObjekt leaves stay routed through trade_sell's TradeCmd
// hook (DEFERRED; see the module report).
//
// 0x1B is fully self-contained: a 768x768 signed-byte relation matrix mutation
// over two grids (dword_123D6CD primary, byte_1333110 secondary) plus the Person
// id/alive columns. Translated 1:1 against a host/test-seeded RelationState.
//
// 0x1C routes the He record allocation through handler_entry's HandlerTable (a
// mockable hook) and models the chat-string buffer copy. 0x1E commits the packet
// GameTime to the clock (when newer) and fans the turn cascade out through gated
// subsystem hooks (wired to the turn_driver / gametick clock where present).

namespace guild::sim {

// ---------------------------------------------------------------------------
// Opcode -> handler constants for THIS batch (jump-table indices @0x631298).
// ---------------------------------------------------------------------------
enum ApplyOpcode6 : u8 {
    kOp6GroupBegin            = 0x05, // 5
    kOp6GroupEnd              = 0x06, // 6
    kOp6GroupSkip             = 0x07, // 7
    kOp6SellObjekt            = 0x11, // 17
    kOp6ComputeSellableAmount = 0x12, // 18
    kOp6ComputeObjectCoords   = 0x1B, // 27
    kOp6ShowMessageBox        = 0x1C, // 28
    kOp6AdvanceGameTick       = 0x1E, // 30
    kOp6MoveObjectToRoom      = 0x39, // 57 — declared-but-unimplemented in batch 4
};

// ===========================================================================
// Module-global engine state these handlers mutate (the game's file globals).
// Tests reset via ResetApply6State.
// ===========================================================================

// dword_764CE0 == -1 standalone flag mirror: several handlers latch a "current
// command" global (dword_649890 / dword_632244) only when networked. Default
// standalone(-1) => not latched.
void Apply6_SetStandalone(bool v);
bool Apply6_Standalone();

// dword_649890 (ExSellObjekt / ExComputeSellableAmount latch, networked only).
extern i32 g_curSellCmd;     // dword_649890
// dword_632244 (ExShowMessageBox latch, networked only — the He ordinal source).
extern i32 g_msgBoxCmd;      // dword_632244

// byte_6477A1 — the local player id (market-price arg for the produce path).
extern u8  g_localPlayer;    // byte_6477A1

// ---------------------------------------------------------------------------
// 0x11 / 0x12 sell/produce container-resolve hook.
//
// The originals resolve the source + destination through the entity-array +
// scene-tree cluster (VIBE_GameObject_ResolveEntityById / ResolveOwnerOrParentB /
// QueryFind), then feed the deterministic core in trade_sell. That cluster is
// owned elsewhere; here we route the resolution through one hook that, given the
// decoded ids + proto + qty, fills the trade_sell resolve struct. The default
// backend uses a tiny modeled container table so the apply path round-trips and
// is testable in isolation.
// ---------------------------------------------------------------------------
struct SellDecoded {
    i32 destOwnerId = -1;   // a1+16 (after remap)
    i32 srcOwnerId  = -1;   // a1+20 (after remap)
    i16 proto       = 0;    // HIWORD(a1+22)
    i32 qty         = 0;    // a1+31 (requested amount)
    i32 rawMaterial = 0;    // a1+35 (raw-material multiplier; 0 == plain transfer)
    u8  goodByte    = 0;    // a1+30 (index into the raw-material proto table)
    i32 cmdCount    = 0;    // a1+8
};
using SellResolveFn = bool (*)(const SellDecoded& d, SellResolve& r);
void SetSellResolveHook(SellResolveFn fn);

struct SellableDecoded {
    i32 sourceId    = -1;   // a1+16 (after remap)
    i16 recipeType  = 0;    // HIWORD(a1+18) — the recipe/output prototype
    i16 outProto    = 0;    // == recipeType (the produced good)
    i32 startQty    = 0;    // a1+22 (the initial production cap, v4)
    u8  player      = 0;    // byte_6477A1
    i32 cmdCount    = 0;    // a1+8
};
using SellableResolveFn = bool (*)(const SellableDecoded& d, SellableResolve& r);
void SetSellableResolveHook(SellableResolveFn fn);

// ---------------------------------------------------------------------------
// 0x1B relation-matrix state.
//
// Two 768x768 signed-byte relation grids (row-major, stride 768 BYTES — the
// original reads cell (i,j) as the signed high byte of the dword whose low
// byte sits at base-3 + 768*i + j, i.e. the byte at base + 768*i + j):
//   matrixA == byte @0x123D6D0 (addressed as dword_123D6CD>>24) : primary
//              attitude grid. ONE global in the binary — matrixA ALIASES
//              world::g_relationMatrix (world/relation.h), the same grid the
//              VIBE_Relation_LookupMatrixEntry reader (0x5942fc) and its setter
//              address with the identical (768*i + j) arithmetic.
//   matrixB == byte_1333110 : secondary/mood grid (same geometry; read via
//              the unk_133310D>>24 alias and a movsx in case 3).
//
// The person columns the handler consults are NOT modeled here: in the binary
// they are the live Person array itself —
//   id           dword_12CE914[134*i] == g_persons[i].id        (record +4)
//   alive marker word_12CE910[268*i]  == g_persons[i].marker    (record +0;
//                                        -1 == free slot)
//   slot id      dword_12CEB1C[134*i] == dword at record +0x20C (the case-3
//                                        exclusion key)
// so ExComputeObjectCoords reads sim::g_persons (sim/entity.h) directly. The
// create-person apply handlers (batch 5) populate the very same records, which
// is what lets the new-game opcode-27 packets resolve their targets.
// ---------------------------------------------------------------------------
constexpr int kRelPersons   = 768;             // 0x300
constexpr int kRelCells     = kRelPersons * kRelPersons; // 589824
// Byte offset of the case-3 exclusion key inside the 536-byte Person record
// (dword_12CEB1C == word_12CE910 + 0x20C).
constexpr int kRelPersonSlotIdOff = 0x20C;
struct RelationState {
    // 768*768 byte grid @0x123D6D0 (dword_123D6CD>>24) — ALIASES the single
    // world::g_relationMatrix backing store (bound in the .cpp), exactly as the
    // binary has one grid shared by the 0x1B handler and the 0x5942fc reader.
    i8*              matrixA;
    std::vector<i8>  matrixB; // 768*768 — byte_1333110 relation byte
    RelationState();
    void Reset();
    i8&  A(int i, int j) { return matrixA[i * kRelPersons + j]; }
    i8&  B(int i, int j) { return matrixB[i * kRelPersons + j]; }
};
RelationState& Apply6_Relations();

// ---------------------------------------------------------------------------
// 0x1C He record allocation hook.
//
// ExShowMessageBox builds a He descriptor from the engine global unk_1077B60
// (the staged message-box template), allocates a He/handler record (via
// VIBE_He_AllocHandlerEntry), and — for a kind-17 record with flag 0x10 — copies
// the 2-byte-stride chat string(s) from byte_1077C58 into a freshly allocated
// buffer hung off the record (record dword 31 = ptr, dword 32 = len+1). We model
// the alloc through a hook returning a HeAllocResult (a record token + the kind
// byte + flag byte) and copy the chat string into a modeled buffer.
// ---------------------------------------------------------------------------
struct HeAllocResult {
    i32 record = 0;      // opaque record token (0 == alloc failed)
    u8  kind   = 0;      // *record (17 => has chat buffer)
    u8  flag10 = 0;      // record[240] & 0x10 (1 => two strings concatenated)
};
// desc: the 0xF8-byte staged template (unk_1077B60 image); curFlags: the v4 byte
// the original threads into the alloc when not networked.
using HeAllocFn = HeAllocResult (*)(const u8* desc, u8 curFlags);
void SetHeAllocHook(HeAllocFn fn);

// VIBE_Person_FindRecordById gate (networked branch, flag 0x10000). Returns the
// person kind byte (record+2), or -1 if not found. Default: -1 (no person).
using MsgBoxPersonKindFn = int (*)(i32 personId);
void SetMsgBoxPersonKindHook(MsgBoxPersonKindFn fn);

// The two message strings the box concatenates (byte_1077C58 + its trailing
// string). Tests seed them; the handler copies them into the modeled buffer.
void Apply6_SeedMessageBoxStrings(const char* first, const char* second);

// ---------------------------------------------------------------------------
// 0x1E turn-cascade subsystem gates + hooks.
//
// ExAdvanceGameTick commits the packet's 14-byte GameTime to the clock when it is
// newer (GameTime_Compare), then fans out: He handlers (gate dword_63C8E8),
// calendar+demand+threat (gate dword_63C8E4), needs+meister AI + player turns
// (gate dword_63C8E0), plus the always-run light/inventory/HUD passes. Each pass
// is a leaf in another cluster; we route them through one cascade hook recording
// which passes ran, and expose the gates as state so the host can wire them to
// the real subsystems (turn_driver / gametick).
// ---------------------------------------------------------------------------
struct TickGates {
    i32 heHandlers = 0;   // dword_63C8E8 (run He_RunAllHandlers)
    i32 calendar   = 0;   // dword_63C8E4 (calendar clock + goods demand + threat)
    i32 needsAi    = 0;   // dword_63C8E0 (char needs + meister AI + player turns)
};
TickGates& Apply6_TickGates();
// The clock the handler commits into (mirror of qword_13CE852 + trailing word).
extern GameTime g_tickClock;       // unk_13CE852 (14-byte image)
extern i32      g_tickSubCounter;  // dword_62EB98 (reset to 0 each advance)

// Cascade pass identifiers (recorded by the cascade hook in order).
enum class TickPass {
    kHeHandlers,        // VIBE_He_RunAllHandlers
    kCalendarClock,     // VIBE_GameTick_AdvanceCalendarClock
    kGoodsDemand,       // VIBE_Economy_ComputeGoodsDemand
    kThreatStats,       // VIBE_Combat_AccumulateThreatStats (every 6th day)
    kCharNeeds,         // VIBE_Character_UpdateAllNeeds
    kLightGray,         // VIBE_Light_SetGrayColorThunk (always)
    kMeisterAi,         // VIBE_Ai_EvaluateMeister (per live person)
    kPlayerTurns,       // VIBE_GameLogic_UpdatePlayerTurns
    kProductionTimers,  // VIBE_Inventory_TickProductionTimers (always)
    kMarkOwned,         // VIBE_Hud_MarkOwnedObjects (always)
};
using TickCascadeFn = void (*)(TickPass pass);
void SetTickCascadeHook(TickCascadeFn fn);
// Live-person count for the meister-AI sub-loop (mirrors the 768-slot scan over
// word_12CE910; the default model runs `count` passes). Tests seed it.
void Apply6_SeedLivePersonCount(int n);

// Observable log of the leaf calls (tests verify what ran).
struct Apply6Log {
    int sellCommitCount = 0;
    int sellableCommitCount = 0;
    int relationMutateCount = 0;
    int heAllocCount = 0;
    int msgBoxChatLen = 0;       // bytes copied into the chat buffer (+1)
    int tickAdvanceCount = 0;
    int tickPassCount = 0;
    i32 lastSellMoved = 0;
    i32 lastSellableProduced = 0;
    i32 lastSellableProceeds = 0;
    std::vector<TickPass> tickPasses;
    std::vector<char>     msgBoxBuffer;  // the modeled chat buffer image
};
const Apply6Log& Apply6_GetLog();

// Reset every modeled table + hook + global to defaults (test helper).
void ResetApply6State();

// ===========================================================================
// Handlers. Each takes the received packet and the dispatcher ACK (may be null).
// ===========================================================================

// gilde.exe 0x496B90 — opcode 0x11. The sell APPLY engine. Decode dest/src owner
// ids (+16/+20, remap), proto (HIWORD +22), qty (+31), raw-material multiplier
// (+35); resolve the containers; delegate the deterministic transfer to
// TradeSellObjektResolve. On a nonzero moved qty: ack[0]=1; if dest != -1 then
// ack[1]=3, ack[6]=dest record. Returns 0 on apply, 1 on reject.
int ExSellObjekt(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x497538 — opcode 0x12. Produce-and-sell APPLY. Decode source id
// (+16, remap), recipe/output prototype (HIWORD +18), start cap (+22); resolve;
// delegate to TradeComputeSellableAmount. On producing: ack[0]=1; if source !=
// -1 then ack[1]=3, ack[6]=output record. On reject ack[1]=6 (no-capacity) or
// 8 (nothing producible). Returns 0/1.
int ExComputeSellableAmount(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49818C — opcode 0x1B. Relation-matrix mutation. Decode the two
// person ids (+16/+20, remap, written back), the mode dword (+28), the delta
// dword (+24), the float (+32) and the band index (+36); resolve the persons in
// the LIVE g_persons table (first id match wins, then the marker word gates);
// mutate the 768x768 relation grids per the original's mode switch (0 = pair
// delta on A, 1 = pair delta on A spread into B, 2 = pair delta on A + B
// zeroed, 3 = column scale of B by the float with trunc-to-zero rounding
// (VIBE_Coord_ConvertX, RC=chop), 4 = global decay band over A). Any other
// mode resolves both persons and acks WITHOUT mutating (the 0x498640
// `test ebp,ebp; jnz` fall-through). ack[0]=1. Returns 0 on apply, 1 if a
// referenced person id is unknown / its slot is free.
//
// This is the apply side of VIBE_Command_QueueRequestCoord27 @0x494878 — in
// particular the six mode-0 delta-127 packets the new-game commit
// (VIBE_Command_EnqueueInheritanceTransfer @0x5336f0, 0x533930..0x5339ae)
// enqueues between player/father/mother.
int ExComputeObjectCoords(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x498678 — opcode 0x1C. Build the He message-box record from the
// staged template; networked: gate on the target person's kind + latch the
// command; allocate the He record; for a kind-17 record copy the chat string(s)
// into a fresh buffer. ack[0]=1, ack[1]=4, ack[6]=record. Returns 0 on apply,
// 1 on reject / alloc fail.
int ExShowMessageBox(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x498954 — opcode 0x1E. Advance the game tick: if the packet's
// GameTime (+16, 14 bytes) is newer than the clock, commit it and run the gated
// turn cascade; stamp ack[0]=1 either way. Returns 0.
int ExAdvanceGameTick(CommandPacket& pkt, AckEntry* ack);

// ---------------------------------------------------------------------------
// 0x39 ExMoveObjectToRoom (opcode 57). gilde.exe 0x49AF00. This opcode is
// declared (prototype only) in command_apply4.h but was left UNIMPLEMENTED /
// unregistered there, so it is a genuine gap in the 96-entry table. Batch 6
// fills it (no clash: no other batch registers 0x39). To avoid coupling to
// batch 4's dangling declaration we give it the distinct name below.
//
//   if (!Person_QueryBegin(room, id@+16)) return 1;          // resolve room
//   person = FindRecordById(id@+20); if (!person && id!=-1) return 1;
//   if (AiPlayerType[room.kind] == 1) {                       // capacity room
//       cap = City_GetDistrictCoord(room).x;
//       if (room.occupants(+101) >= cap) return 1;            // full
//       room.occupants++; }
//   oldRoom = person.room(+92);                               // detach from old
//   if (oldRoom && AiPlayerType[oldRoom.kind]==1 && oldRoom.occupants>0)
//       oldRoom.occupants--;
//   person.room(+92) = room;                                  // re-parent
//   ack[0]=1, ack[1]=0, ack[6]=0.  return 0.
// The Person/room resolution + AiPlayer type table + district-coord leaves are
// routed through one hook returning a modeled MoveRoom view; the occupant-count
// + re-parent arithmetic is translated 1:1.
// ---------------------------------------------------------------------------
struct MoveRoomView {
    bool roomResolved = false;   // Person_QueryBegin(id@+16) != 0
    bool personResolved = false; // FindRecordById(id@+20) != 0 (or id == -1)
    bool personIsNull = false;   // person ptr null AND id == -1 (allowed: detach)
    bool roomIsCapacity = false; // AiPlayerType[room.kind] == 1
    int  roomCapacity = 0;       // City_GetDistrictCoord(room).x (LOWORD)
    int* roomOccupants = nullptr;// &room[+101] (in/decremented)
    bool oldRoomIsCapacity = false; // AiPlayerType[oldRoom.kind] == 1
    int* oldRoomOccupants = nullptr;// &oldRoom[+101] (decremented if > 0)
    int* personRoomLink = nullptr;  // &person[+92] (set to the new room token)
    int  newRoomToken = 0;          // value written into person[+92]
};
using MoveRoomResolveFn = bool (*)(i32 roomId, i32 personId, MoveRoomView& v);
void SetMoveRoomResolveHook(MoveRoomResolveFn fn);

// gilde.exe 0x49AF00 — opcode 0x39. Re-parent a person into a (capacity-checked)
// room, fixing the old/new room occupant counts. ack[0]=1,[1]=0,[6]=0. Returns
// 0 on apply, 1 if the room is missing / full / the person is unknown.
int ExMoveObjectToRoom6(CommandPacket& pkt, AckEntry* ack);

// ---------------------------------------------------------------------------
// Group framing (opcodes 5/6/7). The originals are handled positionally inside
// VIBE_Command_ExecCommands (0x494088): opcode 5 marks a group-begin, 6 ends a
// group (runs VIBE_Command_ExecCommandGroup over the framed members), 7 is a
// skip. The reimpl's CommandQueue::ExecCommands already does the positional
// framing; here we (a) provide the faithful ExecCommandGroup core for the
// direct-apply path and the queue to call, and (b) register thin 5/6/7 apply
// handlers that stamp the ACK so the framing opcodes are no longer "unset" in
// the dispatch table (closing the 96/96 coverage). The begin/skip handlers are
// pure ACK markers; the end handler runs the group if a member list is supplied.
// ---------------------------------------------------------------------------

// A group member for the direct-apply ExecCommandGroup path. `pkt` is the member
// packet; the group runs from a group-begin to the first group-end (opcode 6).
// gilde.exe 0x4942C0 — VIBE_Command_ExecCommandGroup. Dispatches each member
// (begin..end inclusive of the end marker) through `apply`; if all succeed marks
// every member's status byte 1, else 2. Returns 1 (the original always returns 1).
using GroupApplyFn = int (*)(CommandPacket& pkt, AckEntry* ack);
int ExecCommandGroup(std::vector<CommandPacket*>& members,
                     std::vector<AckEntry*>& acks, GroupApplyFn apply);

int ExGroupBegin(CommandPacket& pkt, AckEntry* ack);
int ExGroupEnd(CommandPacket& pkt, AckEntry* ack);
int ExGroupSkip(CommandPacket& pkt, AckEntry* ack);

// ---------------------------------------------------------------------------
// Registry. Adds ONLY this batch's opcodes (5/6/7/0x11/0x12/0x1B/0x1C/0x1E) to a
// CommandQueue dispatch table — call it IN ADDITION TO RegisterApplyHandlers /
// 2 / 3 / 4 / 5; the six sets are disjoint so order does not matter.
// ---------------------------------------------------------------------------
void RegisterApplyHandlers6(CommandQueue& q);

// Apply a single packet directly (bypassing the queue) for this batch's opcodes.
// Unknown/guarded opcodes return -1 without touching state.
int ApplyPacket6(CommandPacket& pkt, AckEntry* ack);

} // namespace guild::sim
