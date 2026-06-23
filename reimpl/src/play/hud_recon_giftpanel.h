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
//   cap = min(held, wealth * flt_624A40)          // flt_624A40 = 0.05
//   lo  = ConvertX(cap)
//   sliderMin = (lo >= 3200) ? lo : 3200
//   if (sliderMin <= 0) -> dialog aborts (returns 0) when the >=3200 branch is taken
//
// VIBE_Coord_ConvertX (0x5c6b08) reads/writes st-register scratch; in the gift path
// it is an identity passthrough of the FP value already in (int) form (the result is
// taken straight from the (int)cast of the multiplied wealth). We model ConvertX as
// identity over the integer-truncated product, which is what the decompile's
// `v37 = (int)v17` / `v38 = (int)v35` paths read.
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

// ConvertX passthrough used by the gift path (identity over the int-truncated FP).
inline int Gift_ConvertX(double v) { return static_cast<int>(v); }

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
