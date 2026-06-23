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

    // --- upper bound (v37) ---
    //   v7 = wealth * flt_624A3C ; ConvertX ; if ((int)v7 >= 1600) v37 = ConvertX(...)
    //   else v37 = 1600
    double hiProd = static_cast<double>(wealth) * kGiftWealthMaxFraction;
    int hi = Gift_ConvertX(hiProd);
    r.sliderMax = (hi >= kGiftMaxFloor) ? hi : kGiftMaxFloor;

    // --- lower endpoint (v38) ---
    //   v31 = wealth * flt_624A40
    //   v8  = (held <= v31) ? held : (wealth * flt_624A40)     // == min(held, cap)
    //   v36 = v8 ; ConvertX ; if ((int)v36 >= 3200) { recompute, v38 = ConvertX;
    //                                                  if (v38 <= 0) return 0 }
    //         else v38 = 3200
    double cap = static_cast<double>(wealth) * kGiftWealthCapFraction;
    double v8 = (static_cast<double>(held) <= cap) ? static_cast<double>(held) : cap;
    int v36 = Gift_ConvertX(v8);
    if (v36 >= kGiftMinFloor) {
        // The original re-evaluates min(held, cap) identically and re-converts.
        double cap2 = static_cast<double>(wealth) * kGiftWealthCapFraction;
        double v18 = (static_cast<double>(held) <= cap2) ? static_cast<double>(held)
                                                         : cap2;
        int v38 = Gift_ConvertX(v18);
        r.sliderMin = v38;
        if (v38 <= 0)
            r.aborted = true; // dialog returns 0
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
