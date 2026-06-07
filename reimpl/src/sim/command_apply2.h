#pragma once
#include "guild/common/types.h"
#include "sim/command.h"
#include "sim/types.h"

// gilde.exe — Command APPLY handlers, SECOND batch (namespace guild::sim).
//
// This is a second, NON-overlapping set of the ~120 VIBE_Command_Ex* jump-table
// targets (jump table funcs_4941F4 @0x631298, invoked from
// VIBE_Command_ExecCommands @0x494088). The FIRST batch lives in
// command_apply.{h,cpp} and owns opcodes
//   0x16 0x17 0x18 0x19 0x1A 0x24 0x25 0x41 0x56 0x5A 0x5B 0x5D.
// This file owns a DIFFERENT, coherent set and registers ONLY those opcodes, so
// the two registries compose without clobbering each other:
//
//   0x0D ExBindObjectProto       0x49 6888  estate-ownership transfer (delegate)
//   0x0E ExRemapAndValidateObject 0x49 68D4  remap id -> remove building/scene
//   0x0F ExRemapObjectPair       0x49 6978  remove-from-src + add-to-dst object move
//   0x10 ExUpdateObjectPair      0x49 6A90  decrement-src + add-to-dst object move
//   0x1D ExSetHE                 0x49 8890  write a "He" (herald/event) record
//   0x2B ExSelectObject          0x49 9558  resolve entity -> toggle selection
//   0x3F ExSetObjectState        0x49 BBF8  copy 0x80 state bytes into a bldg slot
//   0x44 ExAssignOffice          0x49 BD74  assign office to candidate (delegate)
//   0x45 ExTransferOffice        0x49 BDB0  transfer office holdership (delegate)
//   0x46 ExReleaseOffice         0x49 BDEC  apply Gesetz/law + notify (delegate)
//   0x5C ExSwapOfficeHolders     0x49 D1BC  swap two office holders (delegate)
//   0x5E ExUpsertTradeEntry      0x49 D2E8  upsert a trade-route entry (type-202 node)
//
// Common ABI (recovered from the originals, all __usercall):
//   * packet record base in EAX -> modeled as CommandPacket&. Payload at +0x10
//     (kFPayload), but several of these read fields at byte offsets the codec
//     placed past +0x10 (the original builders staged the payload there).
//   * the 10-byte ACK/status entry in EDX -> AckEntry* (may be null). Handlers
//     stamp it: status 2 = in-progress, 1 = applied, +1 = sub-tag, +6 = result
//     pointer/id (we keep +1/+6 = 0). A few set status to (result==0)+1.
//   * return 0 == applied, nonzero == target not found / rejected (the dispatcher
//     ignores the value; the ACK byte carries the outcome).
//
// The -2/-3/-4 "last-created object/scene/trade id" remap tokens
// (dword_631288/63128C/631290) are the SAME globals the first batch exposes —
// they live in command_apply.{h,cpp} (g_lastObjectId / g_lastSceneId /
// g_lastTradeId). We reuse them rather than redefine.
//
// Deferred leaves (render/scene/inventory/He/office subsystems not present in
// src/) are reached through mockable function-pointer hooks declared below, with
// faithful default backends that model just the record mutation the apply path
// performs. The full leaf functions are listed as deferred in the module report.

namespace guild::sim {

// ---------------------------------------------------------------------------
// Opcode -> handler constants for THIS batch (jump-table indices @0x631298).
// ---------------------------------------------------------------------------
enum ApplyOpcode2 : u8 {
    kOp2BindObjectProto      = 0x0D, // 13
    kOp2RemapValidateObject  = 0x0E, // 14
    kOp2RemapObjectPair      = 0x0F, // 15
    kOp2UpdateObjectPair     = 0x10, // 16
    kOp2SetHE                = 0x1D, // 29
    kOp2SelectObject         = 0x2B, // 43
    kOp2SetObjectState       = 0x3F, // 63
    kOp2AssignOffice         = 0x44, // 68
    kOp2TransferOffice       = 0x45, // 69
    kOp2ReleaseOffice        = 0x46, // 70
    kOp2SwapOfficeHolders    = 0x5C, // 92
    kOp2UpsertTradeEntry     = 0x5E, // 94
};

// ===========================================================================
// Modeled leaf records + hooks.
// ===========================================================================

// --- "He" handler/event entry (VIBE_He_FindFirstHandlerByFilter @0x4c63f8) ---
// ExSetHE writes 13 fields into this record. The original record is large; we
// model only the bytes the handler touches (it scatter-writes dword/word fields
// at the offsets below, all relative to the He record base).
//   [+68]  (dword idx 17)  field0   <- payload +20
//   [+72]  (dword idx 18)  field1   <- payload +24
//   [+76]  (dword idx 19)  field2   <- payload +28
//   [+80]  (word  idx 40)  field3w  <- payload +32 (word)
//   [+82]  (dword)         field4   <- payload +34
//   [+86]  (dword)         field5   <- payload +38
//   [+90]  (dword)         field6   <- payload +42
//   [+94]  (word  idx 47)  field7w  <- payload +46 (word)
//   [+96]  (dword idx 24)  field8   <- payload +48
//   [+100] (dword idx 25)  field9   <- payload +52
//   [+104] (dword idx 26)  field10  <- payload +56
//   [+108] (word  idx 54)  field11w <- payload +60 (word)
//   [+112] (dword idx 28)  field12  <- (payload +59 dword) >> 24
// (The original also qmemcpy's a 0xA0 scratch block at +172; that is UI state,
// not part of the deterministic record, so it is omitted.)
constexpr int kHeRecordBytes = 176; // through +112 + slack (the orig record is larger)
struct HeRecord {
    u8 bytes[kHeRecordBytes];
};

// VIBE_He_FindFirstHandlerByFilter @0x4c63f8 — find the first He entry whose id
// (filter slot, payload +16) matches; returns its base or null. Default: linear
// find over a small modeled He table (see command_apply2.cpp). Tests install the
// real/spy backend.
using HeFindFn = HeRecord* (*)(i32 id);
void SetHeFindHook(HeFindFn fn);

// --- Building production/storage slot (VIBE_Building_FindSlotByProt @0x5851fc)-
// ExSetObjectState copies 0x80 bytes of state into the slot. We model the slot
// as a flat 0x80-byte record located by (typeByte, proto) via the hook.
constexpr int kBuildingSlotBytes = 0x80;
struct BuildingSlot {
    u8 bytes[kBuildingSlotBytes];
};
// VIBE_Building_FindSlotByProt @0x5851fc — locate a slot for (typeByte, proto).
// Default: linear find over a small modeled slot table. Returns null on miss.
using SlotFindFn = BuildingSlot* (*)(u8 typeByte, i16 proto);
void SetSlotFindHook(SlotFindFn fn);

// --- Trade-route entry (a type-202 GameObject node under a building) ---------
// ExUpsertTradeEntry finds (by partner id, node+42) or creates a type-202 node
// under the owner building, then writes its trade fields. We model the node as a
// flat record with the exact field offsets the handler uses (node-relative):
//   [+28] dword field0     <- payload +24
//   [+32] dword field1     <- payload +28
//   [+36] dword field2     <- payload +32
//   [+40] word  field3w    <- payload +36 (word)
//   [+42] dword partnerId  <- payload +38  (also the lookup key)
//   [+50] dword field5     <- payload +46
//   [+54] byte  field6b    <- payload +50 (byte)
//   [+55] byte  pct        <- (payload +51 byte) + node[+55], clamped to [.,100]
constexpr int kTradeNodeBytes = 64;
struct TradeNode {
    u8 bytes[kTradeNodeBytes];
};
// Find-or-create a type-202 trade node under building `ownerBuildingId` matching
// `partnerId`. The two leaves are VIBE_GameObject_QueryFind (find existing) and
// VIBE_GameObject_AddObjekt (create). Modeled together: returns an existing node
// with matching partnerId, else allocates a new one (setting g_lastTradeId), or
// null if the owner building does not exist / the pool is full.
using TradeNodeFindOrAddFn = TradeNode* (*)(i32 ownerBuildingId, i32 partnerId);
void SetTradeNodeHook(TradeNodeFindOrAddFn fn);

// --- Estate / office / law leaves (return a small result code) ---------------
// VIBE_Person_TransferEstateOwnership @0x58c4a8 — returns a person INDEX (>=0)
// on success, or <0 on failure. ExBindObjectProto stamps the ack from the sign.
using EstateTransferFn = i32 (*)(i32 fromId, const i32* spec);
void SetEstateTransferHook(EstateTransferFn fn);

// VIBE_Office_AssignToCandidate @0x47e4e0, VIBE_Office_TransferHoldership
// @0x47e870, VIBE_Office_SwapHolders @0x47ec64, VIBE_Gesetz_ApplyAndNotify
// @0x4c24f8 — each returns 0 on success (the handlers stamp ack=(ret==0)+1).
using OfficeOpFn = i32 (*)(const i32* spec);
void SetOfficeAssignHook(OfficeOpFn fn);
void SetOfficeTransferHook(OfficeOpFn fn);
void SetOfficeSwapHook(OfficeOpFn fn);
using LawApplyFn = i32 (*)();
void SetLawApplyHook(LawApplyFn fn);

// --- Object-move leaves (VIBE_GameObject_* stock mutation) -------------------
// These move quantities of an item between containers; they return nonzero on
// success for the Remap variant (0x0F) and are void for the Update variant
// (0x10). We model the actual stock change via a hook with an explicit signature
// so tests can observe the moved amount.
//   VIBE_GameObject_RemoveObjektAmount @0x5863b4 (returns success)
//   VIBE_GameObject_AddObjektToParent  @0x5862a4 (returns success)
//   VIBE_GameObject_DecrementObjektStock @0x586458 (void)
using ObjStockOpFn = int (*)(i32 containerId, i32 proto, i32 amount);
void SetRemoveObjektHook(ObjStockOpFn fn);
void SetAddObjektHook(ObjStockOpFn fn);
void SetDecrementObjektHook(ObjStockOpFn fn);

// --- Building / scene removal leaves (0x0E) ----------------------------------
//   VIBE_Building_FreeAndUnlink   @0x586d6c (returns nonzero on FAILURE here)
//   VIBE_Building_RemoveAndCleanup @0x5894b0 (void)
using BuildingFreeFn = int (*)(i32 objectId);
void SetBuildingFreeHook(BuildingFreeFn fn);
using BuildingRemoveFn = void (*)(i32 sceneId);
void SetBuildingRemoveHook(BuildingRemoveFn fn);

// --- Selection-toggle leaf (0x2B) --------------------------------------------
// VIBE_GameObject_RemoveById @0x585a30 — toggles selection of a sub-object;
// returns 0 on success, nonzero error code otherwise. ExSelectObject propagates
// the return. Default: returns 0 (success).
using SelectionToggleFn = int (*)(i32 containerId, i32 subId);
void SetSelectionToggleHook(SelectionToggleFn fn);

// Reset every modeled table + every hook to its default backend (test helper).
void ResetApply2State();

// Test/seed access to the modeled leaf tables (not in the original; the live
// game's He / building-slot / trade-node records live in their own subsystems).
// Seed a record so the default find hook resolves it; returns its base (null if
// the modeled table is full). Apply2_FindTradeNode locates a node a trade upsert
// created so a test can verify the written fields.
HeRecord*     Apply2_SeedHe(i32 id);
BuildingSlot* Apply2_SeedSlot(u8 type, i16 proto);
TradeNode*    Apply2_FindTradeNode(i32 owner, i32 partner);

// ===========================================================================
// Handlers. Each takes the received packet and the dispatcher ACK (may be null).
// ===========================================================================

// gilde.exe 0x496888 — opcode 0x0D. Transfer estate ownership (payload +16 id,
// +20 spec ptr) via the estate-transfer leaf; ack +0=1, +1=(idx>=0), +6=person.
int ExBindObjectProto(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x4968D4 — opcode 0x0E. Remap the +0x10 id (-2/-3/-4), resolve the
// entity, then: if object -> Building_FreeAndUnlink (fail => return 1); if scene
// -> return 1; else (person column) -> Building_RemoveAndCleanup. ack +0=1.
int ExRemapAndValidateObject(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x496978 — opcode 0x0F. Remap +0x14 (src) and +0x10 (dst) ids; move
// amount(+0x1D) of proto(table[+0x1C byte]) from src then to dst via the two
// object-stock leaves; any failure returns 1. ack +0=1.
int ExRemapObjectPair(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x496A90 — opcode 0x10. Like 0x0F but uses the decrement-then-add
// leaves (no failure short-circuit). ack +0=1.
int ExUpdateObjectPair(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x498890 — opcode 0x1D. Find the He record by id (+0x10) and write
// 13 fields from the payload (offsets above). ack +0=1,+1=4,+6=record on hit.
int ExSetHE(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x499558 — opcode 0x2B. Resolve entity (+0x10), then toggle the
// selection of sub-object (+0x18) within it via the selection leaf. ack +0=2 on
// entry, 1 on success. Returns 0 / -1 (no entity) / -2 (sub not found) / leaf err.
int ExSelectObject(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49BBF8 — opcode 0x3F. Locate a building slot by (typeByte +0x10,
// proto = (dword @+0x0F) >> 16) and copy 0x80 state bytes from payload +0x11.
// Returns 1 if the slot is not found. ack +0=1.
int ExSetObjectState(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49BD74 — opcode 0x44. Office assign (payload +16) via leaf; ack
// status = (ret==0)+1. Returns (ret==0).
int ExAssignOffice(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x49BDB0 — opcode 0x45. Office transfer; same shape as 0x44.
int ExTransferOffice(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x49BDEC — opcode 0x46. Gesetz/law apply + notify; ack status =
// (ret==0)+1; the handler itself always returns 0.
int ExReleaseOffice(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x49D1BC — opcode 0x5C. Office holder swap (payload +16); ack +0=2
// on entry then 1 on success. Returns (ret==0).
int ExSwapOfficeHolders(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49D2E8 — opcode 0x5E. Upsert a trade-route entry. Requires the
// partner building (+0x26) and owner building (+0x14) to exist; find-or-create a
// type-202 node under the owner, write its 8 trade fields (offsets above), and
// clamp the percentage byte to [.,100]. ack +0=2 on entry then 1 on success.
int ExUpsertTradeEntry(CommandPacket& pkt, AckEntry* ack);

// ---------------------------------------------------------------------------
// Registry. Adds ONLY this batch's opcodes to a CommandQueue dispatch table —
// it must be called IN ADDITION TO RegisterApplyHandlers (batch 1); the two sets
// are disjoint so order does not matter.
// ---------------------------------------------------------------------------
void RegisterApplyHandlers2(CommandQueue& q);

// Apply a single packet directly (bypassing the queue) for this batch's opcodes.
// Unknown/guarded opcodes return -1 without touching state.
int ApplyPacket2(CommandPacket& pkt, AckEntry* ack);

} // namespace guild::sim
