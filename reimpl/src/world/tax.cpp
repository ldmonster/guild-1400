#include "world/tax.h"

namespace guild::world {

// gilde.exe 0x57aa88 (inner arithmetic).
//   v19 = (float)lawThreshold; v18 = v19 * 0.01f;
//   v20 = (float)accountValue; v21 = v18 * v20;
//   v22 = (v21 <= 0.0) ? 0.0 : v21;  v24 = (int)v22;
// All multiplies are single-precision in the original (float locals); we keep
// that to stay bit-faithful to the truncated integer result.
i32 TaxComputeTradeIncome(i32 lawThreshold, i32 accountValue) {
    float v19 = static_cast<float>(lawThreshold);
    float v18 = v19 * kTaxRateScale;
    float v20 = static_cast<float>(accountValue);
    float v21 = v18 * v20;
    float v22 = (v21 <= 0.0f) ? 0.0f : v21;
    return static_cast<i32>(v22);
}

// Weak default for the command-queue hook: in an isolated/single-player build
// there is no network layer, so the default just reports "not committed". The
// engine (or a test) overrides this by linking its own definition; here we keep
// a definition so the logic module links standalone. Tests that need to observe
// commits provide their own via the function pointer below.
namespace {
bool (*g_taxCommitHook)(i32, i32, i32) = nullptr;
}

void TaxSetCommitHook(bool (*hook)(i32, i32, i32)) { g_taxCommitHook = hook; }

bool TaxCommitToCommandQueue(i32 payerObjectId, i32 payerId, i32 amount) {
    if (g_taxCommitHook)
        return g_taxCommitHook(payerObjectId, payerId, amount);
    return false;
}

// gilde.exe 0x57aa88 — VIBE_Tax_CollectTradeIncome.
//   if (activeFinanceLaws >= 6) return 0;            // law slot full
//   amount = TaxComputeTradeIncome(...);
//   if (accountValue > 0) {
//       if (flags&1) queue command;
//       if (flags&2) cityAccumulator += amount;
//   }
int TaxCollectTradeIncome(i32 lawThreshold, int activeFinanceLaws,
                          i32 accountValue, i32 payerObjectId, i32 payerId,
                          int flags, TradeTaxResult* out, i32* cityAccumulator) {
    if (out) { out->amount = 0; out->committed = false; out->addedToCity = false; }

    if (activeFinanceLaws >= 6)
        return 0;

    i32 amount = TaxComputeTradeIncome(lawThreshold, accountValue);
    if (out)
        out->amount = amount;

    if (accountValue > 0) {
        if (flags & 1) {
            bool ok = TaxCommitToCommandQueue(payerObjectId, payerId, amount);
            if (out) out->committed = ok;
        }
        if (flags & 2) {
            if (cityAccumulator)
                *cityAccumulator += amount;
            if (out) out->addedToCity = true;
        }
    }
    return 1;
}

} // namespace guild::world
