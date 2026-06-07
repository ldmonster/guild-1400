#pragma once
#include "guild/common/types.h"
#include "sim/command.h"
#include "sim/types.h"

// gilde.exe — Command APPLY handlers, FIFTH batch (namespace guild::sim).
//
// A fifth, NON-overlapping set of the 96-entry dispatch jump table
// (funcs_4941F4 @0x631298, invoked from VIBE_Command_ExecCommands @0x494088).
// Batches 1-4 own:
//   batch1 (command_apply):  0x16 0x17 0x18 0x19 0x1A 0x24 0x25 0x41 0x56 0x5A 0x5B 0x5D
//   batch2 (command_apply2): 0x0D 0x0E 0x0F 0x10 0x1D 0x2B 0x3F 0x44 0x45 0x46 0x5C 0x5E
//   batch3 (command_apply3): 0x27 0x28 0x29 0x2A 0x2E 0x2F 0x30 0x31 0x32 0x33 0x34 0x35
//                            0x36 0x37 0x3D 0x3E 0x43 0x48 0x49 0x4A 0x4B 0x50 0x51 0x52
//                            0x55 0x59 0x5F
//   batch4 (command_apply4): 0x01 0x02 0x04 0x08 0x09 0x15 0x1F 0x21 0x22 0x23 0x26 0x2D
//                            0x38 0x39 0x3C 0x40 0x42 0x47 0x4D 0x4E 0x57 0x58
//
// This file owns the building/person CREATE + LIFECYCLE + LINK opcodes plus the
// state-gate / sys-message / office / move / use opcodes, registering ONLY those,
// so all five registries compose without clobbering each other:
//
//   framing / state gate
//   0x00 ExHandleStateGate     49 644C  gate: only opcodes 0/5/6/7 ack here
//   0x03 ExHandleStateGate     49 644C  (same handler — init/sync framing)
//
//   building create / upgrade / remove (the building lifecycle)
//   0x0A ExCreateBuildingDirect 49 6520  CreateGebaeude(type, owner) + copy fields
//   0x3A ExGebUpgrade          49 AFF0  level-up a building (record + render leaf)
//   0x3B ExRemoveBuilding      49 B4F4  remove a building (occupants + record)
//   0x4C ExCreateGebaeude      49 C19C  CreateGebaeude under a person + place mesh
//
//   person create
//   0x0B ExCreatePersonA       49 6614  CreateAndSpawn from parent record
//   0x0C ExCreatePersonB       49 6714  CreateAndSpawn (spouse) + family fields
//
//   object move / use
//   0x13 ExMoveObjectBetweenLists 49 790C  unlink a scene node, relink under a new owner
//   0x14 ExUseObjectCheck      49 7AD0  consume one stockpiled object if count==1
//
//   sys message / office / building links
//   0x20 ExSysMessage          49 8AB4  big switch over engine globals (time/flags/...)
//   0x2C ExAssignPersonToOffice 49 9638  assign/release an Amt office slot (leaf)
//   0x4F ExSetCharacterChatBuffer 49 C6AC  write a talk action's chat buffer
//   0x53 ExRemoveBuildingLink  49 C944  clear a trade/supply link (type-300 node)
//   0x54 ExUpdateBuildingLinks 49 CAC4  add/remove/clear a group link (type-301 node)
//
// Common ABI (recovered from the originals, all __usercall): packet record base
// -> CommandPacket&, payload at +0x10; the 10-byte ACK/status entry -> AckEntry*
// (may be null). Handlers stamp it: status 2 = in-progress, 1 = applied; +1 =
// sub-tag, +6 = result ptr/id. Return 0 == applied, nonzero == target not found /
// rejected (the dispatcher ignores the value; the ACK carries the outcome).
//
// The -2/-3/-4 "last-created object/scene/trade id" remap tokens
// (dword_631288/63128C/631290) are the SAME globals batch 1 exposes
// (g_lastObjectId / g_lastSceneId / g_lastTradeId in command_apply.h); we reuse
// them. The create handlers WRITE them: 0x0A/0x4C set g_lastSceneId (dword_63128C),
// 0x0B/0x0C set g_lastObjectId (dword_631288), mirroring the originals.
//
// The two big create LEAVES (VIBE_Building_CreateGebaeude @0x586fb8 and
// VIBE_Person_CreateAndSpawn @0x58da70) and the render/scene/script/office leaves
// these handlers invoke are reached through mockable hooks (building_create.h,
// person_create.h, and the hooks declared below) with faithful default backends
// that model just the observable record mutation. The full leaves are DEFERRED.

namespace guild::sim {

// ---------------------------------------------------------------------------
// Opcode -> handler constants for THIS batch (jump-table indices @0x631298).
// ---------------------------------------------------------------------------
enum ApplyOpcode5 : u8 {
    kOp5HandleStateGate0       = 0x00, // 0
    kOp5CreatePersonA          = 0x0B, // 11
    kOp5CreatePersonB          = 0x0C, // 12
    kOp5CreateBuildingDirect   = 0x0A, // 10
    kOp5HandleStateGate3       = 0x03, // 3
    kOp5MoveObjectBetweenLists = 0x13, // 19
    kOp5UseObjectCheck         = 0x14, // 20
    kOp5SysMessage             = 0x20, // 32
    kOp5AssignPersonToOffice   = 0x2C, // 44
    kOp5GebUpgrade             = 0x3A, // 58
    kOp5RemoveBuilding         = 0x3B, // 59
    kOp5CreateGebaeude         = 0x4C, // 76
    kOp5SetCharacterChatBuffer = 0x4F, // 79
    kOp5RemoveBuildingLink     = 0x53, // 83
    kOp5UpdateBuildingLinks    = 0x54, // 84
};

// ===========================================================================
// Module-global engine state the originals mutate (the game's file globals).
// Held here as the fifth batch's owned state; tests reset via ResetApply5State.
// ===========================================================================

// dword_764CE0 == -1 standalone flag mirror (several handlers latch a "current
// command" global only when networked). Default: standalone(-1).
void Apply5_SetStandalone(bool v);
bool Apply5_Standalone();

// --- ExSysMessage (opcode 0x20) global state -------------------------------
// The big switch mutates many engine globals. We model the ones whose observable
// effect is a simple scalar/record write (the others — terrain rebuild, sky init,
// interior scene swap — are render leaves, routed through the SysMessage hook).
extern u8       g_sysGlobalFlags;   // byte_63CC28 (case 2 sets bits 3/4)
extern i32      g_sysLoadFlag;      // dword_11AA480 (case 2)
extern GameTime g_sysGameTime;      // qword_13CE852 (case 3 writes the clock)
extern u8       g_sysByte63CC1D;    // byte_63CC1D  (case 6)
extern i32      g_sysDword764CF4;   // dword_764CF4 (case 7)
extern i32      g_sysActivePlayer;  // dword_63CC24 (case 8 set / case 0xA clear, -1 free)
extern u8       g_sysByte63C8F4;    // byte_63C8F4  (case 9)
extern i32      g_sysDword122F49C;  // dword_122F49C (case 9)
extern i32      g_sysDword63CC30;   // dword_63CC30 (case 0xA, set 1)
extern i32      g_sysDword63CC70;   // dword_63CC70 (case 0xF)
extern u8       g_sysName63CC74[64];// byte_63CC74  (case 0xF string copy)
extern i32      g_sysDword63127C;   // dword_63127C (case 0x11 increment)
extern i32      g_sysCutTable50[8]; // dword_13CEC50 (case 0x10 paired insert)
extern i32      g_sysCutTable54[8]; // dword_13CEC54 (case 0x10)
extern i32      g_sysDword631294;   // dword_631294 (case 0x13)
// word_63CC5C — the current player index (read by several handlers). -1 == none.
extern i16      g_currentPlayer;    // word_63CC5C

// VIBE_Person_FindRecordById is reused from entity.h for case 8.
// The render/scene/sky/heightmap/interior leaves of ExSysMessage (cases 0/4/0xC)
// are routed through this hook (default: records the call type for tests).
using SysMessageLeafFn = void (*)(int caseType);
void SetSysMessageLeafHook(SysMessageLeafFn fn);

// --- ExGebUpgrade / ExRemoveBuilding / ExCreateGebaeude render leaves --------
// These handlers' bodies are dominated by render/scene-graph leaf calls
// (LoadObjectGroup, SetWorldTranslation, AlignMeshToTerrain, BuildGebaeudePath,
// RefreshFlagAnimation, …). We invoke them through a single "place mesh" hook that
// records which building record was (re)placed; the deterministic record mutations
// are done inline. Default backend records the call (CreateLog).
using BuildingPlaceMeshFn = void (*)(int op, u8* buildingRec);
void SetBuildingPlaceMeshHook(BuildingPlaceMeshFn fn);

// VIBE_Building_InitWorkerCapacities @0x586ed8 — re-init a building's worker caps
// (called by ExGebUpgrade after the level bump). Default: no-op (record-neutral).
using InitWorkerCapsFn = void (*)(u8* buildingRec);
void SetInitWorkerCapsHook(InitWorkerCapsFn fn);

// VIBE_Building_FreeAndUnlink @0x586d6c — free a building record (ExRemoveBuilding).
// Default backend clears the record's alive byte in g_objects.
using BuildingFreeFn5 = void (*)(u8* buildingRec);
void SetBuildingFreeHook(BuildingFreeFn5 fn);

// VIBE_Building_DetachAndDestroyOccupant @0x588d00 — detach a scene node that
// belongs to the building (ExRemoveBuilding occupant loop). Default: records count.
using DetachOccupantFn = void (*)(SceneNode* node);
void SetDetachOccupantHook(DetachOccupantFn fn);

// --- ExAssignPersonToOffice (Amt) leaf --------------------------------------
// The office assign/release machinery (VIBE_Amt_*) is a separate cluster not in
// src/. ExAssignPersonToOffice resolves the target person then delegates to the Amt
// leaf, which returns the (modeled) slot record. We model it as one hook returning
// a slot pointer (or null). Default backend uses a small modeled office table.
struct OfficeSlot {
    i32 holderId;   // slot[1] (dword index 1): assigned person id (-1 free)
    i32 type;       // slot[0]: office type
    i32 extra;      // slot[4]: extra payload (a1+29)
    bool used;
};
constexpr int kOfficeSlots = 16;
using OfficeAssignFn = OfficeSlot* (*)(i32 personId, i32 officeType, i32 coord, i32 mode);
void SetOfficeAssignHook(OfficeAssignFn fn);
OfficeSlot* Apply5_OfficeSlot(int i);

// --- ExSetCharacterChatBuffer (0x4F) leaf -----------------------------------
// Resolves a live actor by id, checks its current action is a "talk" node (type 58)
// and writes up to N chat words into the node's buffer. The actor + action-node
// system is the character module; we model it via a hook returning a chat-buffer
// pointer + capacity (or null if no talk action). Default: a modeled buffer.
struct ChatTarget {
    i32* capacity;  // node+240 : the buffer capacity slot (written by the handler)
    u8*  buffer;    // node+244 : the chat byte buffer
    int  bufferLen; // model bound for the buffer
};
using ChatFindFn = const ChatTarget* (*)(i32 actorId);
void SetChatFindHook(ChatFindFn fn);
bool Apply5_SeedChatTarget(i32 actorId, int capacity); // test seed; false if full

// --- ExMoveObjectBetweenLists / link leaves ---------------------------------
// VIBE_GameObject_RemoveByProt @0x5859b4 — remove a stockpiled sub-object by proto
// from a container's child list (ExUseObjectCheck). Default: records the call.
using RemoveByProtFn = void (*)(i32 containerToken, i16 proto);
void SetRemoveByProtHook(RemoveByProtFn fn);

// Observable record of the create/lifecycle leaf calls (tests verify what ran).
struct CreateLog {
    int createBuildingCount;
    int createPersonCount;
    int gebUpgradeCount;
    int removeBuildingCount;
    int placeMeshCount;
    int officeAssignCount;
    int detachOccupantCount;
    int removeByProtCount;
    i32 lastBuildingId;
    i32 lastPersonId;
    int sysMessageLeafCount;
    int lastSysLeafCase;
};
const CreateLog& Apply5_CreateLog();

// Reset every modeled table + hook + global to defaults (test helper).
void ResetApply5State();

// ===========================================================================
// Handlers. Each takes the received packet and the dispatcher ACK (may be null).
// ===========================================================================

// gilde.exe 0x49644C — opcodes 0x00 / 0x03. State gate: return 1 (reject) unless
// the opcode byte is 0/5/6/7; else stamp ack=1, return 0.
int ExHandleStateGate(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x496520 — opcode 0x0A. Resolve owner index from id(+0x16, -1/-2/-3/-4
// remap, 0xFFFF if -1); CreateGebaeude(type=byte +0x14, owner); set g_lastSceneId =
// new id; write word +0x1A into building word +90; if dword +0x1C != 0 copy 0x30
// bytes from +0x20 into building+101. ack +0=1,+1=2,+6=building. Returns 1 on fail.
int ExCreateBuildingDirect(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x496614 — opcode 0x0B. (Optionally) resolve a parent building record
// by id(+0x1F, remap); CreateAndSpawn(kind=HIBYTE(+0x11), parentA=+0x15,
// owner=word+0x1D, parentB=+0x19, parentRec, a6=HIBYTE(+0x20), a7=HIBYTE(+0x21),
// a8=HIBYTE(+0x22)); set g_lastObjectId = new person id. ack +0=1,+1=1,+6=record.
int ExCreatePersonA(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x496714 — opcode 0x0C. CreateAndSpawn(kind=(ack==null)+6,
// parentA=+0x14, owner=word+0x1C, parentB=+0x18, none, a6=HIBYTE(+0x1B), a7=0,
// a8=HIBYTE(+0x20)); copy a 16-byte name from +0x25 into record+48; family fields;
// set g_lastObjectId = new id. ack +0=1,+1=1,+6=record. Returns 1 on fail.
int ExCreatePersonB(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49790C — opcode 0x13. Resolve three entity ids (+0x14 dst owner,
// +0x10 src owner, +0x18 moved-node id) with -2/-3/-4 remap; unlink the moved
// scene node from the src owner's child list and relink it under the dst owner.
// ack +0=1. Returns 1 if any resolve fails / node not found.
int ExMoveObjectBetweenLists(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x497AD0 — opcode 0x14. Resolve scene entity by id(+0x10, remap); must
// be type 42 or 278; QueryFind a child by proto HIWORD(+0x12); if its count(+7)==1
// remove it (RemoveByProt). ack +0=1. Returns 1 on miss / count!=1.
int ExUseObjectCheck(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x498AB4 — opcode 0x20. Sys-message: switch on the subtype byte at
// +0x10; mutate the corresponding engine global (time/flags/active-player/name/...)
// or route a render-leaf case through the hook. ack +0 = result (1 or 2), +1=0,+6=0.
int ExSysMessage(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x499638 — opcode 0x2C. Resolve target person by id(+0x14); assign or
// release an Amt office slot (delegated to the office hook); on success stash the
// holder id + extra payload into the slot. ack +0=status,+1=5,+6=slot.
int ExAssignPersonToOffice(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49AFF0 — opcode 0x3A. Resolve building by owner id(+0x10); reject if
// already top level (AiPlayer table +583 >= +584); else bump level (++marker),
// recompute the capacity byte +92, re-init worker caps, (re)place the mesh (leaf).
// ack +0=2,+1=0,+6=0. Returns 1 on miss / already-max.
int ExGebUpgrade(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49B4F4 — opcode 0x3B. Resolve building by owner id(+0x10); detach &
// destroy its scene occupants; clear any person rows pointing at it; free the
// record (leaf). ack +0=2. Returns 1 if the building is missing.
int ExRemoveBuilding(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49C19C — opcode 0x4C. Resolve owner person by id(+0x10);
// CreateGebaeude(type=byte +0x18, owner=record marker word); set g_lastSceneId =
// new id; place the mesh at the bauplatz (leaf). ack +0=1,+1=2,+6=building.
// Returns 1 if the owner / create fails.
int ExCreateGebaeude(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49C6AC — opcode 0x4F. Find the actor; if its current action node is a
// talk node (type 58) with a buffer, set the buffer capacity (+0x2C) and copy up to
// 2 chat words from +0x30 into the buffer. ack +0=1. Returns 1/2 on miss.
int ExSetCharacterChatBuffer(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49C944 — opcode 0x53. Tag-dispatched ("rmpl"/"rml " little-endian
// tags at +0x10): clear a building's trade/supply link node (type-300) fields, or
// clear one matching link entry. ack +0=2 then 1 on success. Returns 1 on miss.
int ExRemoveBuildingLink(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49CAC4 — opcode 0x54. Tag-dispatched (4 tags at +0x10): add a member
// id to a group link node (type-301), or remove / clear one. ack +0=2 then 1.
// Returns 1 on miss / full / duplicate.
int ExUpdateBuildingLinks(CommandPacket& pkt, AckEntry* ack);

// ---------------------------------------------------------------------------
// Registry. Adds ONLY this batch's opcodes to a CommandQueue dispatch table —
// call it IN ADDITION TO RegisterApplyHandlers / 2 / 3 / 4; the five sets are
// disjoint so order does not matter.
// ---------------------------------------------------------------------------
void RegisterApplyHandlers5(CommandQueue& q);

// Apply a single packet directly (bypassing the queue) for this batch's opcodes.
// Unknown/guarded opcodes return -1 without touching state.
int ApplyPacket5(CommandPacket& pkt, AckEntry* ack);

} // namespace guild::sim
