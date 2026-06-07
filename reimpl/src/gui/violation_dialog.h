#pragma once
// guild::gui — VIBE_ViolationDialog_ReportAtOffice @0x548480.
//
// The "report a person to the office / pay a bribe-fee to file a charge" parchment.  Modal:
//   form = "misc\perga_rolle";  VIBE_Form_CenterChildWindows;  SelectWindow(form, 1);
//   body = RenderRichString(4960, *target, currencyByte);  confirm child + a second child.
//   feeMin / feeMax are derived from the combined wealth of self + target:
//     wealth = |ComputeTotalWealth(self) + ComputeTotalWealth(target)|;
//     fee0   = wealth * 0.0007843137 -> ConvertX;            (flt_6241B4)
//     feeMax = SumCurrencyHeld(self) (what we can afford);
//     fee    = ClampValueRange(fee0, feeMax, 32);            (clamp to a slider range)
//   the fee is shown via an editable value object (Object_SetValueOrText) the player can
//   nudge; a second body line 4961 + law seals {2,15} finish the parchment.
//   modal loop:
//     dword_62D22C == confirmObj -> read the chosen fee from the value object, multiply by
//       the currency rate (Money_MultiplyByRate), CheckResourceAmount, then:
//         EnqueueCmd15(target, building, fee, currency);
//         a reputation tick (FamilyRecord[+40] += delta) and a QueueRequestCoord27 wait;
//         if target rank is 6/7 send an entity message (4964) to that office;
//         Gesetz_EvaluateViolation(15,1,target,building,-1);  done.
//     dword_62D22C == cancelObj  -> cancel.
//
// We recover the form name, slot, the two text ids, the fee-range math, the law seals, the
// command opcode (cmd15 / coord27), and the confirm/cancel wiring.  The frame loop, text
// engine, money formatters and command codec are forward-declared / mocked.

#include "gui/types.h"

namespace guild::gui {

inline constexpr int kVioClickOK     = 1210;
inline constexpr int kVioClickCancel = 1155;
inline constexpr int kVioLoopForm    = 423879;

inline constexpr const char* kFormViolation = "misc\\perga_rolle";

inline constexpr int kVioSlot       = 1;    // SelectWindow(form, 1)
inline constexpr int kTextVioBody   = 4960; // primary body (with the fee value object)
inline constexpr int kTextVioBody2  = 4961; // second body line
inline constexpr int kTextVioMsgTo  = 4964; // entity message sent to a rank-6/7 office

// Law-seal stamps for the violation parchment (BuildLawSeals v36=2, v37=15).
inline constexpr int kVioSeal0 = 2;
inline constexpr int kVioSeal1 = 15;

// Fee scaling float (flt_6241B4), byte-exact.
inline constexpr double kVioFeeRate = 0.0007843137136660516;
// The slider clamp window the fee is constrained to (Math_ClampValueRange ..., 32).
inline constexpr int kVioFeeClampStep = 32;

// Commands the confirm path enqueues.
inline constexpr int kVioCmd15    = 15; // EnqueueCmd15 (charge the fee)
inline constexpr int kVioCmdCoord = 27; // QueueRequestCoord27 (reputation effect)
inline constexpr int kVioGesetzOp = 15; // Gesetz_EvaluateViolation opcode

// The ranks that trigger the "office gets notified" message branch.
inline constexpr int kVioRankNotifyA = 6;
inline constexpr int kVioRankNotifyB = 7;

struct ViolationState {
    int targetEntity = 0;  // *((dword*)target+1) — the reported person's entity id
    int building     = 0;  // dword_12CE914[...] active office/building handle
    int wealthSelf   = 0;  // ComputeTotalWealth(self)
    int wealthTarget = 0;  // ComputeTotalWealth(target)
    int currencyHeld = 0;  // Person_SumCurrencyHeld(self) — the affordable maximum
    int targetRank   = 0;  // *((byte*)target+2) — rank (6/7 -> notify)
    bool hasResources = true; // Dialog_CheckResourceAmount(chosenFee)
};

struct ViolationLayout {
    const char* form = nullptr;
    int slot = kVioSlot;
    int bodyText = kTextVioBody;
    int confirmObj = -1;  // GetChildObjectId of the confirm / fee object
    int cancelObj  = -1;  // second child object (cancel)
    int feeDefault = 0;   // the pre-clamp default fee shown
    int feeMax     = 0;   // the clamp upper bound (= currencyHeld)
    int seal0 = kVioSeal0, seal1 = kVioSeal1;
};

struct ViolationCommandSink {
    virtual ~ViolationCommandSink() = default;
    // Charge the chosen fee (cmd15) and record the violation.
    virtual void Report(int /*target*/, int /*building*/, int /*fee*/,
                        int /*gesetzOp*/) {}
    // Notify the target's office when rank is 6/7.
    virtual void NotifyOffice(int /*target*/, int /*fee*/) {}
};
void ViolationDialog_SetCommandSink(ViolationCommandSink* sink);

// gilde.exe 0x548480 (layout half) — compute the fee range + child objects for a state.
//   feeDefault = wealth*0.0007843137; feeMax = currencyHeld; clamped at build time.
ViolationLayout ViolationDialog_Build(const ViolationState& s);

// gilde.exe 0x548480 (wiring half) — dispatch a click.  `chosenFee` is the value the
// player left in the fee object (defaults to feeDefault).  Confirm requires the confirm
// object hit AND CheckResourceAmount; rank 6/7 additionally notifies the office.
bool ViolationDialog_Dispatch(const ViolationLayout& l, const ViolationState& s,
                              int clickedObj, int chosenFee);

} // namespace guild::gui
