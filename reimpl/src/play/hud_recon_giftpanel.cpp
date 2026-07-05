// hud_recon_giftpanel.cpp — gilde.exe 0x55d03c VIBE_InfoPanel_RunGiftDialog.
// 1:1 of the slider-range clamp math + the confirm value/gate.
#include "hud_recon_giftpanel.h"

namespace guild {
namespace play {

namespace {
GiftEmitHook g_emit = nullptr;
void*        g_emitCtx = nullptr;
} // namespace

void Gift_SetEmitHook(GiftEmitHook hook, void* ctx) {
    g_emit = hook;
    g_emitCtx = ctx;
}
void Gift_Emit(i32 amount) {
    if (g_emit)
        g_emit(amount, g_emitCtx);
}

GiftSliderRange Gift_ComputeSliderRange(int wealth, int held) {
    GiftSliderRange r{};
    r.aborted = false;

    // --- upper bound (v37) — gilde.exe 0x55d07d..0x55d096 ---
    //   fild wealth ; fmul ds:flt_624A3C ; call ConvertX ; fistp (RC=trunc).
    //   The product NEVER leaves the x87 stack: a 31-bit int x 24-bit float
    //   product is EXACT in the 80-bit register, so the truncation acts on the
    //   exact product (long double models this; a double product can round up
    //   across an integer).  >= 1600 re-runs the identical computation
    //   (0x55d2aa..0x55d2e4) — provably the same value.
    int hi = Gift_ConvertX(static_cast<long double>(wealth) *
                           static_cast<long double>(kGiftWealthMaxFraction));
    r.sliderMax = (hi >= kGiftMaxFloor) ? hi : kGiftMaxFloor;

    // --- lower endpoint (v38) — gilde.exe 0x55d0a4..0x55d16c ---
    //   The cap is SPILLED TO A 4-BYTE FLOAT before the compare
    //   (fstp dword @0x55d0fe), the compare is fild held / fcomp dword
    //   (@0x55d10a..0x55d10e), and the selected min value is spilled to a
    //   float AGAIN (fstp dword @0x55d14f; held path jumps into the same
    //   fstp via 0x55d313->0x55d14f) before ConvertX/fistp.
    const float capF = static_cast<float>(
        static_cast<long double>(wealth) *
        static_cast<long double>(kGiftWealthCapFraction));
    const float v36 = (static_cast<long double>(held) <=
                       static_cast<long double>(capF))
                          ? static_cast<float>(held)   // (float)held spill
                          : capF;                      // recomputed spill == capF
    int lo = Gift_ConvertX(v36);
    if (lo >= kGiftMinFloor) {
        // 0x55d31c..0x55d3c8 — identical recompute (float spills @0x55d36e /
        // 0x55d3bb), so v38 == lo exactly.
        r.sliderMin = lo;
        if (lo <= 0)
            r.aborted = true; // 0x55d3cc..0x55d3d7 — dialog returns 0
    } else {
        r.sliderMin = kGiftMinFloor;
    }
    return r;
}

GiftConfirmResult Gift_EvaluateConfirm(const GiftConfirmInput& in) {
    GiftConfirmResult out{};
    out.amount = 0;
    out.emit = false;

    // v41 set -> the dialog renders the "no amount" variant (string 0x1B2A) and never
    // builds the amount widget; the confirm transfer path is unreachable.
    if (in.recipientIsPlayerControlled)
        return out;

    // amt = MultiplyByRate(GetDataPtr(amountWidget), byte_6477A1)
    out.amount = Gift_MultiplyByRate(in.sliderValue, in.ratePct);
    // if (CheckResourceAmount(amt, byte_6477A1)) -> emit
    out.emit = in.resourceAvailable;
    return out;
}

} // namespace play
} // namespace guild
