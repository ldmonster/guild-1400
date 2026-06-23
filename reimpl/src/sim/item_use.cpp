#include "sim/item_use.h"

#include "util/math_rng_float.h"  // guild::util::RandomFloatScaled (VIBE_Math_RandomFloatScaled)

namespace guild::sim {

namespace {

// ---- inert / identity defaults for the cross-module leaves --------------------------
ItemSlotRecord DefFindSlot(i16) { ItemSlotRecord r; r.present = false; return r; }
double DefRandomFloatScaled() { return guild::util::RandomFloatScaled(); }
i32  DefEffect(u32, const ItemUseActor&, const ItemUseArgs&) { return 1; }
void DefDispatch(int, i16) {}
void DefPermGrant(i32, i32) {}
void DefObjectTrigger(const ItemUseArgs&, i32) {}
void DefUseCommand(i32, i16) {}
void DefViolation(u8, i32, i32) {}
void DefShowPanel(bool, i32, const ItemUseActor&, i16) {}

ItemUseHooks g_hooks = {
    &DefFindSlot, &DefRandomFloatScaled, &DefEffect, &DefDispatch,
    &DefPermGrant, &DefObjectTrigger, &DefUseCommand, &DefViolation, &DefShowPanel,
};

} // namespace

void SetItemUseHooks(const ItemUseHooks& hooks) {
    g_hooks = hooks;
    // Fill any null entries with the defaults so callers can install a partial set.
    if (!g_hooks.findSlotByItemId)   g_hooks.findSlotByItemId   = &DefFindSlot;
    if (!g_hooks.randomFloatScaled)  g_hooks.randomFloatScaled  = &DefRandomFloatScaled;
    if (!g_hooks.effectCallback)     g_hooks.effectCallback     = &DefEffect;
    if (!g_hooks.dispatchPanelEvent) g_hooks.dispatchPanelEvent = &DefDispatch;
    if (!g_hooks.queuePermGrant)     g_hooks.queuePermGrant     = &DefPermGrant;
    if (!g_hooks.queueObjectTrigger) g_hooks.queueObjectTrigger = &DefObjectTrigger;
    if (!g_hooks.queueUseCommand)    g_hooks.queueUseCommand    = &DefUseCommand;
    if (!g_hooks.evaluateViolation)  g_hooks.evaluateViolation  = &DefViolation;
    if (!g_hooks.showUseObjectPanel) g_hooks.showUseObjectPanel = &DefShowPanel;
}

const ItemUseHooks& GetItemUseHooks() { return g_hooks; }

// gilde.exe 0x5671f4 — VIBE_Item_UseObjectAction.
i32 Item_UseObjectAction(ItemUseActor& actor, ItemUseArgs& args) {
    // 0x567203: if (!a1) return -1;  — actor null. (Modeled: actor is a reference, but
    // the original also rejects a null args frame at 0x567207.)
    if (!args.present)                       // 0x567207: if (!a2) return -2;
        return kItemUseBadArgs;

    args.suppressed = 0;                     // 0x567209: a2[4] = 0;

    if (!args.itemId)                        // 0x567210: if (!*a2) return -3;
        return kItemUseNoItemId;

    // 0x567237: SlotByItemId = FindSlotByItemId(**a2);
    ItemSlotRecord slot = g_hooks.findSlotByItemId(args.itemId);
    if (!slot.present)                        // 0x567240: if (!slot) return -4;
        return kItemUseNoSlot;

    // 0x56724f: the ENTIRE effect body runs only when (actorMask & slotPermMask) == 0;
    //           when the bits overlap (AND != 0) the use is blocked -> return -5.
    if ((actor.permMask & static_cast<u32>(slot.permMask)) != 0)
        return kItemUseBlocked;

    // 0x5674ee: v7 = (item != 373) || RandomFloatScaled() >= 0.66;
    //           a2[3] = v7;  — the success/spin flag.
    bool success = (args.itemId != kItemIdGated) ||
                   (g_hooks.randomFloatScaled() >= kItem373SuccessThreshold);
    args.spinResult = success ? 1 : 0;       // 0x567279

    i32 result;                              // v14
    // 0x56727c: if (slot+0x14 (effect cb)) { v14 = cb(); if (!v14) return v14; }
    //           The original tests the FULL +0x14 dword (a function pointer); the
    //           law-violation kind comes from slot+0x08, not from this field.
    if (slot.effectAndKind) {
        result = g_hooks.effectCallback(slot.effectAndKind, actor, args);
        if (!result)                          // 0x567296: if (!v14) return v14;
            return result;
    } else {
        result = 1;                           // 0x5674f5: v14 = 1;
    }

    // 0x5672aa: Interaction_DispatchPanelEvent(0x2D, 0, item, 0);
    g_hooks.dispatchPanelEvent(0x2D, args.itemId);

    // 0x5672af: if (slot+0x10 (perm mask)) QueueRequestArgs25(actor.id, 488, mask, 4, 0);
    if (slot.permMask != 0)
        g_hooks.queuePermGrant(actor.id, slot.permMask);

    // 0x5672cf: if (slot+0x04 (object trigger)) { delta packet ...; QueueRequestState22(); }
    if (slot.objectTrigger != 0) {
        g_hooks.queueObjectTrigger(args, slot.objectTrigger);
    } else {
        // 0x56742e: if (!slot+0x04) QueueRequest17(-1, actor.id, 1, item, byte_6477A1, 0);
        g_hooks.queueUseCommand(actor.id, args.itemId);
    }

    // 0x567457: if (slot+0x08 (law kind) != -1) { evaluate violation }
    if (slot.lawKind != -1) {
        // 0x56747e/0x56751a: the "kind" arg is HIBYTE(*(DWORD*)((char*)slot+5)),
        // i.e. the byte at slot+0x08 == LOBYTE(slot.lawKind).
        u8 kind = static_cast<u8>(slot.lawKind & 0xFF);
        // 0x567459: office record at actor+0x170; if present use *(office+1) as city id,
        // else -1.
        i32 cityId = actor.officeRecord ? actor.officeCityId : -1;
        g_hooks.evaluateViolation(kind, actor.id, cityId);
    }

    // 0x567489: if (actor.typeByte == 6 && !args.suppressed) { show use-object panel }
    if (actor.typeByte == 6 && !args.suppressed) {
        g_hooks.showUseObjectPanel(args.spinResult == 1, slot.objectTrigger, actor,
                                   args.itemId);
    }

    return result;                            // 0x5674b8
}

} // namespace guild::sim
