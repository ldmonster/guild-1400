// hud_recon_selaction.cpp — gilde.exe 0x54e7b4 VIBE_Hud_BuildSelectedObjectAction.
// 1:1 translation of the dispatch predicate + entity-array iteration + info branch.
#include "hud_recon_selaction.h"

namespace guild {
namespace play {

namespace {
SelActionFreeActHook g_freeAct = nullptr;
void*                g_freeActCtx = nullptr;
SelActionTooltipHook g_tooltip = nullptr;
void*                g_tooltipCtx = nullptr;
} // namespace

void Hud_SetSelActionFreeActHook(SelActionFreeActHook hook, void* ctx) {
    g_freeAct = hook;
    g_freeActCtx = ctx;
}
void Hud_SetSelActionTooltipHook(SelActionTooltipHook hook, void* ctx) {
    g_tooltip = hook;
    g_tooltipCtx = ctx;
}

SelActionResult Hud_BuildSelectedObjectAction(const SelActionInput& in,
                                              bool (*entityActive)(int)) {
    SelActionResult out;

    // if ( dword_631724 )  — a selection list must be live.
    if (!in.selectionActive)
        return out; // kNothing

    // SlotByObjectId = VIBE_Amt_FindSlotByObjectId(a2, dword_631724);
    const SelActionSlot& slot = in.slot;

    // Free-act gate:
    //   dword_6317B0 && SlotByObjectId && (slot[13] > 1 || dword_63C7B8) && dword_67221C
    if (in.buildModeActive && slot.valid &&
        (slot.rank > 1 || in.buildOverride) && in.entityArrayReady) {
        out.kind = SelActionKind::kFreeAct;
        // for ( i = 0; i != 411648; i += 536 )  if ( byte_12CEA98[i] ) { ... }
        for (int i = 0; i < kSelActionEntityCount; ++i) {
            bool active = entityActive ? entityActive(i) : false;
            if (!active)
                continue;
            ++out.freeActEntityVisits;
            // VIBE_Command_QueueRequestSlotReset28(...) + Voice_PlayCraftFavorComment()
            if (g_freeAct)
                g_freeAct(i, slot.field0, g_freeActCtx);
        }
        return out;
    }

    // else if ( SlotByObjectId )
    if (slot.valid) {
        if (slot.rank <= 1) {
            // VIBE_Widget_SetTooltipText(dword_8C38E0)  — generic "no info".
            out.kind = SelActionKind::kTooltipGeneric;
            if (g_tooltip)
                g_tooltip(out.kind, -1, g_tooltipCtx);
        } else {
            // sprintf(dword_8C38E4, dword_8C584C[2 * (slot[8] >> 16)]); SetTooltipText
            out.kind = SelActionKind::kTooltipNamed;
            out.resourceNameIndex = SelAction_ResourceNameIndex(slot.field8);
            if (g_tooltip)
                g_tooltip(out.kind, out.resourceNameIndex, g_tooltipCtx);
        }
    }
    return out;
}

} // namespace play
} // namespace guild
