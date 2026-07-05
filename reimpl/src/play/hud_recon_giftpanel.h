#pragma once
// ===========================================================================
// hud_recon_giftpanel — gilde.exe 0x55d03c  VIBE_InfoPanel_RunGiftDialog
// ===========================================================================
// In-game "give a gift" info-panel dialog (misc\geschenck). The player picks a money
// amount via a slider whose range is clamped to a fraction of the giver's wealth, and
// on confirm an opcode-15 money transfer + a +0x1C8 delta field are emitted.
//
// PURE VALUE/FORMULA logic reconstructed 1:1 here — the slider min/max derivation:
//
//   wealth   = VIBE_Person_ComputeTotalWealth(giver, recipientFlag)
//   held     = VIBE_Person_SumCurrencyHeld(giver)
//
//   // upper bound (v37): wealth * (1/200), but never below 1600
//   hi = ConvertX(wealth * flt_624A3C)            // flt_624A3C = 0.005
//   sliderMax = (hi >= 1600) ? hi : 1600
//
//   // lower bound (v38): min(held, wealth*0.05), but never below 3200
//   capF = (float)(wealth * flt_624A40)   // fstp dword @0x55d0fe — the cap is
//                                         // spilled to a 4-BYTE FLOAT
//   v36  = (float)((held <= capF) ? held : wealth * flt_624A40)
//                                         // fstp dword @0x55d14f (the held path
//                                         // jumps into the same spill @0x55d313)
//   lo   = ConvertX(v36)                  // fistp of the FLOAT value
//   sliderMin = (lo >= 3200) ? lo : 3200
//   if (sliderMin <= 0) -> dialog aborts (returns 0) when the >=3200 branch is taken
//
// VIBE_Coord_ConvertX (0x5c6b08) arms the x87 control word for RC=truncate; the
// fistp that follows truncates toward zero (out-of-range/NaN stores the x87
// integer indefinite 0x80000000).  The UPPER product stays on the x87 stack
// (exact 80-bit intermediate — fild/fmul/fistp @0x55d07d..0x55d08c, no spill);
// the LOWER endpoint is float-spilled before truncation as shown above.
//
// The confirm action (slider value -> MultiplyByRate -> CheckResourceAmount ->
// EnqueueCmd15 + delta field 0x1C8) is exposed as a pure decision + inert hook.
//
// Additive: new file, namespace guild::play.

#include "guild/common/types.h"

namespace guild {
namespace play {

using namespace guild;

// flt_624A3C = 0x3ba3d70a = 0.004999999888241291  (≈ 1/200)
// flt_624A40 = 0x3d4ccccd = 0.05000000074505806   (≈ 1/20)
inline constexpr float kGiftWealthMaxFraction = 0.004999999888241291f; // flt_624A3C
inline constexpr float kGiftWealthCapFraction = 0.05000000074505806f;  // flt_624A40
inline constexpr int   kGiftMaxFloor = 1600; // v37 lower limit
inline constexpr int   kGiftMinFloor = 3200; // v38 lower limit

struct GiftSliderRange {
    int  sliderMax;   // v37 — clamped upper bound
    int  sliderMin;   // v38 — clamped "lower" bound (the second money endpoint)
    bool aborted;     // true when the >=3200 branch produced v38<=0 (return 0)
};

// gilde.exe 0x55d03c — compute the gift slider money range from wealth + held cash.
// `wealth` = ComputeTotalWealth, `held` = SumCurrencyHeld (both already integers).
GiftSliderRange Gift_ComputeSliderRange(int wealth, int held);

// The gift path's ConvertX/fistp pair: truncate toward zero (RC=11), with the
// x87 "integer indefinite" 0x80000000 for NaN / out-of-range values, exactly as
// fistp stores it.  long double models the 80-bit st register the value sits in.
inline int Gift_ConvertX(long double v) {
    if (!(v > -2147483649.0L) || v >= 2147483648.0L)   // NaN, <= -2^31-1, >= 2^31
        return static_cast<int>(0x80000000u);          // fistp integer indefinite
    return static_cast<int>(static_cast<long long>(v)); // trunc toward zero
}

// ---------------------------------------------------------------------------
// Confirm decision. The original, on the confirm button:
//   amt = MultiplyByRate(GetDataPtr(amountWidget), byte_6477A1)   // slider value
//   if (CheckResourceAmount(amt, currency)) emit opcode-15 transfer + delta 0x1C8
// We reproduce the value math (MultiplyByRate == value*rate/100) and the gate; the
// command emit is an inert hook.
// ---------------------------------------------------------------------------
struct GiftConfirmInput {
    bool   recipientIsPlayerControlled; // v41 = (*(a1+456) & 0x20)!=0 -> no amount widget
    int    sliderValue;                 // GetDataPtr(amount widget)
    u8     ratePct;                     // byte_6477A1 currency unit
    bool   resourceAvailable;           // CheckResourceAmount() result
};

struct GiftConfirmResult {
    bool emit;        // true -> the opcode-15 transfer should be queued
    i32  amount;      // MultiplyByRate(sliderValue, ratePct)
};

// gilde.exe 0x55d22c.. — confirm-branch value+gate.
GiftConfirmResult Gift_EvaluateConfirm(const GiftConfirmInput& in);

// gilde.exe 0x58f19c — VIBE_Money_MultiplyByRate(amount, ratePct) = amount*ratePct/100.
// (Faithful copy of the formula reconstructed in src/app/session_init.cpp; kept local
//  to keep this unit self-contained — same integer 64-bit intermediate.)
inline i32 Gift_MultiplyByRate(i32 amount, u8 ratePct) {
    return static_cast<i32>(static_cast<i64>(amount) * ratePct / 100);
}

// Inert command hook for the confirm transfer.
using GiftEmitHook = void (*)(i32 amount, void* ctx);
void Gift_SetEmitHook(GiftEmitHook hook, void* ctx);
void Gift_Emit(i32 amount); // routes to the hook if any

} // namespace play
} // namespace guild
