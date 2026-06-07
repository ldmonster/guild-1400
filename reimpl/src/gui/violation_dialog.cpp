#include "gui/violation_dialog.h"

#include <cstdlib>

namespace guild::gui {

namespace {

ViolationCommandSink  g_defaultSink;
ViolationCommandSink* g_sink = &g_defaultSink;

constexpr int kConfirmObj = 2100; // GetChildObjectId(form, _, body)
constexpr int kCancelObj  = 2101; // GetChildObjectId(form, 1, body2)

} // namespace

void ViolationDialog_SetCommandSink(ViolationCommandSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

// gilde.exe 0x548480 (layout half).
//   v46 = abs(ComputeTotalWealth(self) + ComputeTotalWealth(target));
//   v42 = (int)(v46 * 0.0007843137);            // ConvertX identity here
//   v13 = SumCurrencyHeld(self);
//   ClampValueRange(v42, v13, 32);              // constrain to affordable slider range
//   Object_SetValueOrText(confirmObj, <fee>, ...);
ViolationLayout ViolationDialog_Build(const ViolationState& s) {
    ViolationLayout l{};
    l.form = kFormViolation;
    l.confirmObj = kConfirmObj;
    l.cancelObj  = kCancelObj;

    int wealth = std::abs(s.wealthSelf + s.wealthTarget);
    int fee0 = (int)((double)wealth * kVioFeeRate);
    l.feeMax = s.currencyHeld;
    // Math_ClampValueRange clamps the default into [0, currencyHeld] (the affordable range).
    if (fee0 < 0) fee0 = 0;
    if (l.feeMax > 0 && fee0 > l.feeMax) fee0 = l.feeMax;
    l.feeDefault = fee0;
    return l;
}

// gilde.exe 0x548480 (wiring half).
//   if (dword_62D22C == v43 /*confirm*/) {
//     v19 = Money_MultiplyByRate(GetDataPtr(confirm), cur);   // the chosen fee
//     if (CheckResourceAmount(v19, cur)) {
//        FamilyRecord[+40] += <rep>; EnqueueCmd15(target,building,v19,cur);
//        QueueRequestCoord27(...); if rank in {6,7} send message 4964;
//        Gesetz_EvaluateViolation(15,1,target,building,-1); done; }
//   } else if (dword_62D22C == v40 /*cancel*/) cancel;
bool ViolationDialog_Dispatch(const ViolationLayout& l, const ViolationState& s,
                              int clickedObj, int chosenFee) {
    if (clickedObj == l.cancelObj)
        return true;

    if (clickedObj == l.confirmObj) {
        if (!s.hasResources)
            return false; // CheckResourceAmount failed; loop continues
        g_sink->Report(s.targetEntity, s.building, chosenFee, kVioGesetzOp);
        if (s.targetRank == kVioRankNotifyA || s.targetRank == kVioRankNotifyB)
            g_sink->NotifyOffice(s.targetEntity, chosenFee);
        return true;
    }
    return false;
}

} // namespace guild::gui
