#pragma once
// guild::sim — item "use object" action: the STATE EFFECT of using an inventory item.
//
// gilde.exe 0x5671f4 — VIBE_Item_UseObjectAction(actor@eax, args@edx)
//   (__usercall, eax = result). `args` is a small word/dword frame:
//     args[0] (word)   = the item prototype id (fed to Inventory_FindSlotByItemId)
//     args[3] (word)   <- OUTPUT: the "spin / success" flag the caller reads
//     args[4] (word)   <- cleared to 0 on entry; a suppression flag the caller may set
//   and (for the delta-packet path) args is also the target entity base: the original
//   reads *(WORD*)args (the entity id) and field offsets at args + {28,33,32,45,49,51,55}.
//
// This is the item-use *game-state mutation*, reconstructed 1:1:
//   1. validate actor / args / item id (returns -1/-2/-3),
//   2. find the inventory slot for the item (FindSlotByItemId); -4 if none,
//   3. check the actor's permission mask (+0x1E8) against the slot's required mask
//      (+0x10); if blocked return -5,
//   4. roll the "success/spin" flag: item 373 fails the roll iff RandomFloatScaled() <
//      dbl_624E20 (0.66); every other item succeeds,
//   5. run the slot's optional effect callback (+0x14); abort early if it returns 0,
//   6. dispatch the panel event (0x2D),
//   7. queue the permission-grant command (QueueRequestArgs25) when the slot carries a
//      perm-grant mask (+0x10),
//   8. when the slot carries an "object trigger" id (+0x04): build a delta packet that
//      writes that id + a set of fixed fields into the target entity, then commit it
//      (QueueRequestState22); OTHERWISE queue the plain use command (QueueRequest17),
//   9. when the slot carries a law-violation kind (+0x08 != -1): evaluate the violation
//      against the actor's current city office (actor +0x170),
//  10. when the actor is the local player (actor[+2] == 6) and the use wasn't suppressed:
//      optionally format the success message and show the use-object panel.
//
// The window/widget/text plumbing (DispatchPanelEvent, Panel_ShowUseObject,
// Text_RenderFormattedMessage) and the law subsystem (Gesetz_EvaluateViolation) are
// cross-module / GUI leaves owned by other clusters; they are routed through the
// installable ItemUseHooks so this effect is testable in isolation.  The RNG draw uses
// the same crt::RandNext sequence the rest of the sim uses (determinism-critical).

#include "guild/common/types.h"

namespace guild::sim {

using guild::i16;
using guild::i32;
using guild::u32;

// gilde.exe dbl_624E20 — the item-373 success threshold (0.66).
inline constexpr double kItem373SuccessThreshold = 0.66;
// gilde.exe — the special item id with a random success gate.
inline constexpr i16 kItemIdGated = 373;

// The inventory slot record (FindSlotByItemId result, 24-byte / 12-word stride at
// word_63D1D8). The use action reads these dword fields:
struct ItemSlotRecord {
    i16 itemId = 0;            // +0x00  word_63D1D8[k] (the prototype id key)
    i32 objectTrigger = 0;     // +0x04  *((DWORD*)slot+1): delta-packet object id (0 = none)
    i32 lawKind = -1;          // +0x08  *((DWORD*)slot+2): law-violation kind (-1 = none)
    i32 unused0c = 0;          // +0x0C  *((DWORD*)slot+3)
    i32 permMask = 0;          // +0x10  *((DWORD*)slot+4): perm-grant mask (0 = none)
    u32 effectAndKind = 0;     // +0x14  *((DWORD*)slot+5): the effect callback (0 = none).
                               //         (The law-violation "kind" arg comes from
                               //          LOBYTE(lawKind) at slot+0x08, NOT from this field.)
    bool present = false;      // FindSlotByItemId returned non-null
};

// The actor (acting person ctx) view — the fields Item_UseObjectAction reads.
struct ItemUseActor {
    i32 id = 0;                // +0x04  the entity id used by the command builders
    u8  typeByte = 0;          // +0x02  == 6 => the local player (drives the panel show)
    u32 permMask = 0;          // +0x1E8 (488) the actor's permission mask
    i32 officeRecord = 0;      // +0x170 (368): the actor's active office record ptr (0 = none)
    i32 officeCityId = 0;      // *(office+1): the city id for the law evaluation
};

// The args frame Item_UseObjectAction is handed (the request packet).
struct ItemUseArgs {
    i16 itemId = 0;            // args[0]: item prototype id
    i32 spinResult = 0;        // args[3] (output): the success/spin flag (0 / 1)
    i32 suppressed = 0;        // args[4] (output, cleared on entry): host-set suppression
    bool present = true;       // args non-null
};

// Result codes (the original's literal returns).
enum ItemUseResult : i32 {
    kItemUseBadActor   = -1,   // !actor
    kItemUseBadArgs    = -2,   // !args
    kItemUseNoItemId   = -3,   // !args[0]
    kItemUseNoSlot     = -4,   // FindSlotByItemId == null
    kItemUseBlocked    = -5,   // perm mask blocked the use
    // >=0 == the effect callback's return (1 default).
};

// Cross-module leaves routed through hooks (inert/identity defaults in item_use.cpp).
struct ItemUseHooks {
    // VIBE_Inventory_FindSlotByItemId @0x54f04c — locate the slot for `itemId`.
    // Default: a record with present=false.
    ItemSlotRecord (*findSlotByItemId)(i16 itemId) = nullptr;
    // VIBE_Math_RandomFloatScaled @0x58b910 — (double)(int)RandNext() * flt_62675C.
    // Default: uses crt::RandNext() so the draw order matches the sim RNG.
    double (*randomFloatScaled)() = nullptr;
    // slot effect callback (slot +0x14 low 24b). Returns 0 to abort, else the result.
    // Default: returns 1.
    i32 (*effectCallback)(u32 effectId, const ItemUseActor& actor,
                          const ItemUseArgs& args) = nullptr;
    // VIBE_Interaction_DispatchPanelEvent @0x595b98 (0x2D, 0, itemId, 0). Default: inert.
    void (*dispatchPanelEvent)(int code, i16 itemId) = nullptr;
    // VIBE_Command_QueueRequestArgs25 @0x494810 — (actor.id, 488, permMask, 4, 0). Inert.
    void (*queuePermGrant)(i32 actorId, i32 permMask) = nullptr;
    // The delta-packet path (BeginDeltaPacket + 7x AppendDeltaField + QueueRequestState22)
    // for the slot's objectTrigger id. Inert default.
    void (*queueObjectTrigger)(const ItemUseArgs& args, i32 objectTrigger) = nullptr;
    // VIBE_Command_QueueRequest17 @0x49465c — the plain "use item" command. Inert.
    void (*queueUseCommand)(i32 actorId, i16 itemId) = nullptr;
    // VIBE_Gesetz_EvaluateViolation @0x4c2c5c — (kind, 1, -1, actor.id, cityId).
    // Inert default.
    void (*evaluateViolation)(u8 kind, i32 actorId, i32 cityId) = nullptr;
    // VIBE_Panel_ShowUseObject @0x567170 — show the use-object result panel (+ the
    // success message). Inert default.
    void (*showUseObjectPanel)(bool success, i32 objectTrigger, const ItemUseActor& actor,
                               i16 itemId) = nullptr;
};

// Install (test-time) hooks; null entries fall back to the inert/identity defaults.
void SetItemUseHooks(const ItemUseHooks& hooks);
const ItemUseHooks& GetItemUseHooks();

// gilde.exe 0x5671f4 — VIBE_Item_UseObjectAction. `args` is mutated in place
// (args.spinResult / args.suppressed). Returns the result code (see ItemUseResult) or
// the effect callback's value.
i32 Item_UseObjectAction(ItemUseActor& actor, ItemUseArgs& args);

} // namespace guild::sim
