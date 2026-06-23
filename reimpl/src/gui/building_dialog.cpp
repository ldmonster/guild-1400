#include "gui/building_dialog.h"

namespace guild::gui {

// gilde.exe dword_53B454 — the tech-effects category order table, byte-exact.
//   1 2 11 12 9 5 6 3 4 10 13 14 15 16 17 7 8 (0 sentinel)
const u8 kTechCategoryOrder[kTechCategoryCount + 1] = {
    0x01, 0x02, 0x0b, 0x0c, 0x09, 0x05, 0x06, 0x03, 0x04,
    0x0a, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x07, 0x08, 0x00,
};

namespace {

BuildingCommandSink  g_defaultSink;
BuildingCommandSink* g_sink = &g_defaultSink;

// VIBE_Object_GetChildObjectId returns the dialog's text-render object id; the dialogs
// fetch consecutive child ids from a base (v10, v10+1, ...). We assign a stable base so
// the relative offsets are reproducible and the wiring can match them.
constexpr int kChildBase = 1000; // any nonzero base; only relative offsets matter

} // namespace

void BuildingDialog_SetCommandSink(BuildingCommandSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

BuildingCommandSink* BuildingDialog_CommandSink() {
    return g_sink;
}

// ===========================================================================
// Layout builders — DATA/LAYOUT.
// ===========================================================================

// gilde.exe 0x54a734 — SellPreview.
//   form = "Panel\Gebaeude_Verkaufen_pergament"; SelectWindow(form,2);
//   worth = Money_ConvertToDisplayCoord(ComputeRoomWorth(b, condition), currency);
//   obj = RenderRichString(5080, ...); GetChildObjectId(obj); SetValueOrText(obj, worth)
DialogLayout BuildingDialog_BuildSellPreview(const BuildingState& b) {
    DialogLayout l{};
    l.form = kFormSellPergament;
    l.textId = kTextSellPreview;
    l.loopForm = kLoopFormConfirm;
    l.displayWorth = b.roomWorth;        // Money_ConvertToDisplayCoord(roomWorth, cur)
    l.childCount = 1;
    l.childIds[0] = kChildBase;          // the price object (clicked w/ id 1210)
    return l;
}

// gilde.exe 0x54ad7c — ConfirmSell.
//   form = sell-pergament; text 5087; 1 child = ChildObjectId compared vs dword_62D22C.
DialogLayout BuildingDialog_BuildConfirmSell(const BuildingState& b) {
    DialogLayout l{};
    l.form = kFormSellPergament;
    l.textId = kTextConfirmSell;
    l.loopForm = kLoopFormConfirm;
    l.displayWorth = b.roomWorth;
    l.childCount = 1;
    l.childIds[0] = kChildBase;
    return l;
}

// gilde.exe 0x54bc50 — SellLand.
//   if (!CheckBuildRequirements) -> messagebox 0 (we surface childCount=0).
//   else: form = sell-pergament; SelectWindow(form,2); text 5105; 1 child; worth.
DialogLayout BuildingDialog_BuildSellLand(const BuildingState& b) {
    DialogLayout l{};
    l.form = kFormSellPergament;
    l.textId = kTextSellLand;
    l.loopForm = kLoopFormConfirm;
    if (!b.meetsRequirements) {
        l.childCount = 0; // requirements failed -> ShowMessageBox(0); no dialog built
        return l;
    }
    l.displayWorth = b.roomWorth;
    l.childCount = 1;
    l.childIds[0] = kChildBase;
    return l;
}

// gilde.exe 0x54a908 — Renovate.
//   if ((100 - condition) <= 0) -> messagebox 4 (childCount=0).
//   else: form = renovate; text 5083; 4 child objects (obj, obj+1, obj+2, obj+3):
//     [0] full renovate, [1]/[2] partial-cost variants, [3] cancel.
DialogLayout BuildingDialog_BuildRenovate(const BuildingState& b) {
    DialogLayout l{};
    l.form = kFormRenovate;
    l.textId = kTextRenovate;
    l.loopForm = kLoopFormConfirm;
    int wear = 100 - b.condition;        // v3 = 100 - (cond>>24)
    if (wear <= 0) {
        l.childCount = 0;                // nothing to renovate -> messagebox 4
        return l;
    }
    // worth scaled by wear (the original multiplies ComputeRoomWorth by wear * rate).
    l.displayWorth = b.roomWorth * wear;
    l.childCount = 4;
    for (int i = 0; i < 4; ++i) l.childIds[i] = kChildBase + i; // v10 .. v10+3
    return l;
}

// gilde.exe 0x54c3c0 — ExtinguishFire.
//   form = extinguish; SelectWindow(form,1) text 5111 (title);
//   SelectWindow(form,2) text 5112 (body) -> 3 child buttons (obj, obj+1, obj+2)
//   levels 1 / 2 / 4 with success thresholds 0.2 / 0.5 / 0.8.
DialogLayout BuildingDialog_BuildExtinguishFire(const BuildingState& /*b*/) {
    DialogLayout l{};
    l.form = kFormExtinguish;
    l.textId = kTextFireBody;
    l.loopForm = kLoopFormConfirm;
    l.childCount = 3;
    for (int i = 0; i < 3; ++i) l.childIds[i] = kChildBase + i; // obj, obj+1, obj+2
    return l;
}

// gilde.exe 0x54be50 — ShowTechEffects (read-only).
//   form = tech-effects; SelectWindow(form,1) text 5107; SelectWindow(form,3) text 5108
//   -> OK child (1210); SelectWindow(form,2) -> one slider per active category that has
//   a workstation. childCount = number of sliders + 1 (the OK object at childIds[0]).
DialogLayout BuildingDialog_BuildTechEffects(const BuildingState& /*b*/,
                                             const bool* hasWorkstation) {
    DialogLayout l{};
    l.form = kFormTechEffects;
    l.textId = kTextTechBody;
    l.loopForm = kLoopFormConfirm;
    l.childIds[0] = kChildBase;  // the OK object (slot 3, id 1210)
    int sliders = 0;
    // Walk the category order table; build a slider for each category with a workstation.
    for (int i = 0; kTechCategoryOrder[i] != 0; ++i) {
        if (hasWorkstation && hasWorkstation[i])
            ++sliders;
    }
    // childIds beyond [0] would be the slider objects; we only need the count here.
    l.childCount = 1 + sliders;
    return l;
}

// ===========================================================================
// Wiring — WIRING (clicked id -> action).
// ===========================================================================

// SellPreview: clicking the price object with id 1210 enqueues a sell (op 90); 1155
// cancels.  Both end the loop.
bool BuildingDialog_DispatchSellPreview(const DialogLayout& l, const BuildingState& b,
                                        int clickedId, int /*clickedObj*/) {
    if (clickedId == kClickOK) {
        g_sink->Sell(b.handle, kCmdSell, l.displayWorth);
        return true;
    }
    if (clickedId == kClickCancel)
        return true;
    return false;
}

// ConfirmSell: id 1210 AND the child object hit -> Sell(op 90).
bool BuildingDialog_DispatchConfirmSell(const DialogLayout& l, const BuildingState& b,
                                        int clickedId, int clickedObj) {
    if (clickedId == kClickOK && l.childCount > 0 && clickedObj == l.childIds[0]) {
        g_sink->Sell(b.handle, kCmdSell, l.displayWorth);
        return true;
    }
    if (clickedId == kClickCancel)
        return true;
    return false;
}

// SellLand: id 1210 -> Sell(op 90) (+ reset tag 33); 1155 cancels.
bool BuildingDialog_DispatchSellLand(const DialogLayout& l, const BuildingState& b,
                                     int clickedId, int /*clickedObj*/) {
    if (l.childCount == 0) return true; // requirements failed; dialog not shown
    if (clickedId == kClickOK) {
        g_sink->Sell(b.handle, kCmdSell, l.displayWorth);
        return true;
    }
    if (clickedId == kClickCancel)
        return true;
    return false;
}

// Renovate: clicking child [0]/[1]/[2] confirms a renovate (label "renovieren");
// child [3] cancels.  gilde.exe 0x54a908: the loop tests dword_75BF38 == -1 (no widget
// hit -> keep looping) and otherwise compares dword_62D22C against the four child ids.
// There is NO 1155 branch here (unlike SellPreview/ConfirmSell/SellLand/ExtinguishFire);
// the cancel is purely child[3] plus the right-click global (dword_672230), so we model
// clicks via clickedObj only and treat clickedId == -1 (no hit) as "keep looping".
bool BuildingDialog_DispatchRenovate(const DialogLayout& l, const BuildingState& b,
                                     int clickedId, int clickedObj) {
    if (l.childCount == 0) return true;       // nothing to renovate (messagebox 4)
    if (clickedId == -1) return false;        // dword_75BF38 == -1 -> keep looping
    if (clickedObj == l.childIds[3]) return true; // child[3] (v38) cancel button
    if (clickedObj == l.childIds[0] || clickedObj == l.childIds[1] ||
        clickedObj == l.childIds[2]) {
        g_sink->Renovate(b.handle, l.displayWorth, kActionRenovate);
        return true;
    }
    return false;
}

// ExtinguishFire: clicking child [0]/[1]/[2] dispatches ExtinguishFire(level 1/2/4).
bool BuildingDialog_DispatchExtinguishFire(const DialogLayout& l, const BuildingState& b,
                                           int clickedId, int clickedObj) {
    if (clickedId == kClickCancel) return true;
    if (clickedId != kClickOK) return false;
    int level = 0;
    if (clickedObj == l.childIds[0]) level = 1;
    else if (clickedObj == l.childIds[1]) level = 2;
    else if (clickedObj == l.childIds[2]) level = 4;
    else return false;
    g_sink->ExtinguishFire(b.handle, level);
    return true;
}

} // namespace guild::gui
