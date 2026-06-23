// See wire_ai.h. Binds the faithfully-reconstructed engine leaves into the live
// AiRecon5Hooks bridge consumed by the recon5/recon2 AI decision math.
#include "sim/wire_ai.h"

#include "crt/rand.h"            // guild::crt::RandNext         (0x5cb8bc)
#include "world/trade_player.h"  // MoneyConvertToDisplayCoord   (0x58f14c)
#include "world/amt.h"           // AmtMoneyMultiplyByRate       (0x58f19c)

namespace guild::sim {

namespace {

// VIBE_Util_RandNext (0x5cb8bc). The hook field is `i32(*)()`; the reconstruction
// lives in guild::crt as `int RandNext()`. Trampoline to keep namespaces explicit
// and the function-pointer type exact.
i32 WireRandNext() {
    return static_cast<i32>(guild::crt::RandNext());
}

// VIBE_Money_ConvertToDisplayCoord (0x58f14c): amount / rateTable[...] + 0.5,
// truncated. guild::world::MoneyConvertToDisplayCoord(i32 amount, u8 city) is the
// faithful form over the injected currency/rate tables (rate 1 == identity until
// the session installs the tables). Shape matches the hook's (i32 value, u8 mode).
i32 WireMoneyToDisplay(i32 value, u8 mode) {
    return guild::world::MoneyConvertToDisplayCoord(value, mode);
}

// VIBE_Money_MultiplyByRate (0x58f19c): amount * rateTable[...]. guild::world::
// AmtMoneyMultiplyByRate(i32 amount, u8 currencyId) is the faithful form over the
// same per-currency rate table (identity until AmtSetRateHook installs it).
i32 WireMoneyMulByRate(i32 value, u8 mode) {
    return guild::world::AmtMoneyMultiplyByRate(value, mode);
}

// Process-lifetime real bridge. Bindable leaves only; the rest stay null so the
// scorers keep the inert default for couplings whose faithful form is unreachable
// here (personTotalWealth/selectMoodColor/ratingCurveA — see wire_ai.h).
AiRecon5Hooks g_realAiHooks{};
bool          g_installed = false;

} // namespace

void InstallRealAiWiring() {
    if (g_installed)
        return;
    g_realAiHooks.randNext       = &WireRandNext;        // 0x5cb8bc
    g_realAiHooks.moneyToDisplay = &WireMoneyToDisplay;  // 0x58f14c
    g_realAiHooks.moneyMulByRate = &WireMoneyMulByRate;  // 0x58f19c
    // personTotalWealth (0x591f7c), selectMoodColor (0x4664d8),
    // ratingCurveA (0x58a6e8) intentionally left null -> inert default.
    g_installed = true;
}

const AiRecon5Hooks& GetRealAiRecon5Hooks() {
    return g_realAiHooks;
}

} // namespace guild::sim
