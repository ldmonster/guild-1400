#pragma once
// gilde.exe — the LAST untranslated leaves of the VIBE_Command_* namespace: the
// label-carrying order builders + the cursor-click order dispatcher (namespace
// guild::sim, MODULE: command). These sit on the same command-issuing front-end
// path as combat_packets.{h,cpp} (VIBE_Command_Build*Packet) and reuse that
// module's OrderStage / CombatOrderContext / CombatOrderHandle / RequestBuildOp80
// wholesale.
//
// SCOPE (the genuine remainder of VIBE_Command_* — the namespace is now EXHAUSTED;
// the other 7 leaves are already done in combat_packets / charaction_walk /
// script_import2 / combat_packets's RequestBuildOp80):
//
//   * VIBE_Command_BuildLabeledMoveCommand @0x488d4c — issue a LABELLED move
//     (order kind 4). Unlike BuildMoveToPacket this does NOT enqueue: it
//     world->tile-projects a bone-chain point, finds/allocs the persistent 44-byte
//     order slot, copies a 2-byte-stride label string into slot+0x10, stamps the
//     target id / kind / destination tile directly into the slot, and returns 1.
//   * VIBE_Command_BuildConquerCommand @0x488df4 — issue a CONQUER/ware order
//     (kind 6). Like the labelled move but it ASSEMBLES a 44-byte staging block
//     (label at +0x10, tile bytes at +0x20/+0x21) and ENQUEUES it via
//     RequestBuildOp80. Gated on *(target+0x1AC)==0 (early -1 otherwise).
//   * VIBE_Command_IssueOnObject @0x488ff0 — the cursor-click order router. Reads
//     the current cursor mode + the selected order-label and dispatches to the
//     right builder: attack (when the picked object is a unit, +0x217==2), or one
//     of move / conquer / labelled-move keyed off the label prefix
//     ("sp_ESCAPE" / "sp_CONQUER" / "WARE"), or — with no object under the
//     cursor — a raycast-to-ground move built directly into the slot.
//
// CROSS-CLUSTER LEAVES that combat_packets already abstracts (heightmap /
// transform / slot-alloc / object-def) are reused through its CombatOrderContext.
// IssueOnObject's extra game-state reads (cursor mode latch, picked object, the
// selected label text, the "in a local battle" flag, weapon-class compare) are the
// VIBE_* leaves with no reconstructed home yet, so they are routed through
// IssueOnObjectHooks with inert defaults defined in command_apply12.cpp (the
// established CutsceneMiscHooks / ObjLifeHooks pattern). Tests install their own.
#include "guild/common/types.h"
#include "sim/command.h"
#include "sim/combat_packets.h"   // OrderStage / CombatOrderContext / RequestBuildOp80

#include <functional>

namespace guild::sim {

// ---------------------------------------------------------------------------
// gilde.exe 0x488d4c — VIBE_Command_BuildLabeledMoveCommand
//   (eax=handleKey, edx=target, ebx=bonePoint).
// ---------------------------------------------------------------------------
//
// The original transforms `bonePoint` (a3, a3+19) through the bone chain, projects
// it to a tile, then writes the order DIRECTLY into the persistent 44-byte slot
// FindOrAllocSlot returns (it does NOT enqueue):
//   slot[0]    = *(target+4)         (target id)
//   slot[4]    = 4                   (order kind; LOBYTE)
//   slot[0xC]  = 0                   (pre-clear)
//   slot[0x10] = label, copied with a 2-byte stride until a NUL src byte
//   slot[0x20] = WorldToTile out X   (var_14 / v16[0])
//   slot[0x24] = WorldToTile out Z   (var_1C — the height/Z out param)
// Returns 1 on success, 0 if the tile projection fails.
//
// We expose the built slot through `slot` (a fresh zeroed OrderStage models the
// freshly-alloc'd, zero-cleared slot the original writes into). `label` is the
// 2-byte-stride source string (in the binary the selected order-label dword_631720,
// a wide-ish "char, byte" interleave). `ctx` supplies transformPoint / worldToTile /
// findOrAllocSlot exactly as combat_packets uses them.
i32 BuildLabeledMoveCommand(const CombatOrderHandle& h, i32 target,
                            const float* bonePoint, const char* label,
                            OrderStage& slot, const CombatOrderContext& ctx);

// ---------------------------------------------------------------------------
// gilde.exe 0x488df4 — VIBE_Command_BuildConquerCommand
//   (eax=handleKey, edx=target, ebx=bonePoint).
// ---------------------------------------------------------------------------
//
// Like the labelled move but ASSEMBLES a 44-byte staging block and enqueues it via
// RequestBuildOp80. Early-outs -1 when the tile projection fails OR the target's
// +0x1AC dword is non-zero (the "already busy / can't conquer" gate). Staging:
//   stage[0]    = *(target+4)
//   stage[4]    = 6                  (order kind)
//   stage[0xC]  = 0                  (pre-clear of the slot copy)
//   stage[0x10] = label (2-byte stride)
//   stage[0x20] = (u8) WorldToTile out X    (single byte! var_20 low byte)
//   stage[0x21] = (u8) WorldToTile out Z    (single byte! var_1C low byte)
// Returns RequestBuildOp80's ring slot, or -1.
i32 BuildConquerCommand(CommandQueue& q, const CombatOrderHandle& h, i32 target,
                        const float* bonePoint, const char* label,
                        i32 targetBusyFlag, const CombatOrderContext& ctx);

// ---------------------------------------------------------------------------
// gilde.exe 0x488ff0 — VIBE_Command_IssueOnObject (eax=handleKey, edx=target).
// ---------------------------------------------------------------------------
//
// The cursor-click order router. The original reads a clutch of game globals;
// those with no reconstructed home are gathered into this hooks struct. Defaults
// are inert (mode off => the function returns 0 without issuing anything), so a
// test installs only the slots it exercises.
struct IssueOnObjectHooks {
    // dword_67221C — the cursor-order ARMED latch. When 0 the router does nothing
    // and returns 0. Default: 0 (disarmed).
    std::function<bool()> orderArmed;

    // dword_631720 — the picked object / selected order-label pointer. The router
    // branches on whether it is non-null. When it names a UNIT (object class) the
    // attack path is taken; otherwise it is treated as an order-label STRING. We
    // model the two readings the rule needs: the picked-object handle (for the unit
    // branch's class/test) and the label text (for the prefix matches). Default:
    // null (no pick) -> the ground-raycast branch.
    std::function<i32()>          pickedObject;   // raw handle (0 == none)
    std::function<const char*()>  pickedLabel;    // the label text at that handle

    // *(dword_631720+535)==2 — the picked object is a battle UNIT (class byte).
    // Default: false (treat as a label string).
    std::function<bool(i32 picked)> pickedIsUnit;

    // *(picked+512) — the unit's attacker/owner record the attack packet targets
    // (v5 in the original). Default: 0.
    std::function<i32(i32 picked)> pickedUnitOwner;

    // The attack-eligibility gate: ( *(target+364) != *(owner+364) || byte_671D96 )
    // — different team OR the friendly-fire override flag. Default: false (no attack).
    std::function<bool(i32 target, i32 owner)> attackAllowed;

    // VIBE_Heightmap_RaycastFromCursor(map, sx, sz, &out) @0x5c67b8 — project the
    // current cursor screen position to a ground tile. Writes outX/outZ. Default:
    // fails (no ground move issued).
    std::function<bool(i32& outX, i32& outZ)> raycastGround;

    // VIBE_Hud_SetStatusBannerText(dword_8C6F2C) @0x4bcdcc — the "can't go there"
    // banner on a failed ground raycast. Default: no-op (records nothing).
    std::function<void()> rejectBanner;

    // VIBE_Util_StrncmpN @0x5e9ee0 — the strncmp the original uses to classify the
    // selected label ("sp_CONQUER" prefix, "WARE" prefix). Default: nullptr ->
    // IssueOnObject falls back to its built-in libc-equivalent. The live wiring (and
    // the integration test) forward this into the REAL reconstructed util::StrncmpN.
    std::function<int(const char* a, const char* b, int n)> labelStrncmp;
};

// Install the active hooks (nullptr restores the inert defaults). Mirrors
// SetCommandApply8Hooks etc.
void SetIssueOnObjectHooks(const IssueOnObjectHooks* hooks);

// gilde.exe 0x488ff0. Returns 1 when an order was issued (or the ground-move
// branch succeeded), 0 otherwise. `attackParam` is BuildAttackPacket's a5 (the
// original passes 0). `ctx` is forwarded to whichever builder fires. `outSlot`
// receives the slot bytes for the direct-write branches (labelled move + ground
// move) so callers/tests can inspect what landed in the persistent slot.
char IssueOnObject(CommandQueue& q, const CombatOrderHandle& h, i32 target,
                   const CombatOrderContext& ctx, OrderStage& outSlot);

// Order-packet kind bytes recovered here (the wire/slot kind, LOBYTE(stage[1])).
// Distinct names from combat_packets's OrderPacketKind to avoid re-declaration;
// these two are exclusive to this module.
enum LabelOrderKind : u8 {
    kOrderKindGroundMove  = 1,   // IssueOnObject's cursor-raycast direct move
    kOrderKindLabeledMove = 4,   // BuildLabeledMoveCommand
    kOrderKindConquer     = 6,   // BuildConquerCommand
};

// Slot field offsets for the LABELLED (direct-write) order, recovered from
// BuildLabeledMoveCommand's stores. The conquer STAGING block packs the tile as two
// BYTES at +0x20/+0x21 instead (see the .cpp).
enum LabeledSlotOffset : u32 {
    kSlotTargetId = 0x00,   // *(target+4)
    kSlotKind     = 0x04,   // order kind byte
    kSlotPreClear = 0x0C,   // cleared to 0
    kSlotLabel    = 0x10,   // 2-byte-stride label text
    kSlotTileX    = 0x20,   // dword tile X
    kSlotTileZ    = 0x24,   // dword tile Z
};

// Conquer staging tile bytes (single-byte stores in the original).
enum ConquerStageOffset : u32 {
    kConquerTileXByte = 0x20,
    kConquerTileZByte = 0x21,
};

// Copy a 2-byte-stride ("char,byte" interleaved) label into `dst` until a NUL
// source byte, mirroring the original's `do { *d=*s; if(!*s) break; d[1]=s[1];
// s+=2; d+=2; } while(s[-1]);` loop. Exposed for golden testing. Writes at most
// `dstCap` bytes. Returns bytes written (including the trailing pair).
u32 CopyStrideLabel(u8* dst, u32 dstCap, const char* src);

} // namespace guild::sim
