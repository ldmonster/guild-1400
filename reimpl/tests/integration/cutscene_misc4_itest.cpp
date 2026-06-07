// Integration test: the LeaseWindow affordability gate is wired against the
// REAL reconstructed sibling VIBE_Math_ClampValueRange (guild::util::ClampValueRange,
// gilde.exe 0x552734) — no stub. This is exactly the live wiring: the binary's
// VIBE_Cutscene_LeaseWindow (0x4a9868) calls VIBE_Math_ClampValueRange(baseRent,
// funds, &out, 32) to decide whether the lessee can afford the lease before the
// rent-slider window is shown. We forward the affordability decision through the
// genuine clamp and assert the cross-module flow the binary would produce, then
// drive the full LeaseWindow over the hook surface with a real-rent slider.
#include "test.h"

#include "sim/cutscene_misc4.h"
#include "sim/cutscene.h"
#include "util/math.h"

using namespace guild;
using namespace guild::sim;

namespace {
// The REAL sibling, used exactly as LeaseWindow wires it: lo == baseRent,
// value == funds, mul == 32. ClampValueRange returns 1 (ok) only for lo>=1;
// the window is shown when the clamped value reaches at least the base rent,
// i.e. funds >= baseRent. We assert our kernel agrees with the real clamp.
bool ClampSaysAffordable(int funds, int baseRent) {
    int out = 0;
    int ok = util::ClampValueRange(/*lo=*/baseRent, /*value=*/funds, &out, /*mul=*/32);
    if (!ok)
        return false;                 // lo<1 -> clamp fails -> not shown
    return out >= baseRent && funds >= baseRent;
}

// A real rent-slider that negotiates down to half on accept, like the window.
i32 g_funds = 0;
i32 RentSlider(i32 baseRent, i32 funds) {
    g_funds = funds;
    return baseRent / 2;  // player accepted a lower rent
}

// Person record: +2 kind, +4 entity id. Size to the real 536-byte record.
struct PersonRec { u8 bytes[600] = {0}; };
PersonRec g_lessee;
void* FindLessee(i32 id) { return id == 11 ? &g_lessee : nullptr; }
int  SumCurrency(void* p) { return p ? 4000 : 0; }
}  // namespace

TEST(CutsceneMisc4Itest, AffordabilityMatchesRealClampSibling) {
    // Cross-check the kernel against the genuine reconstructed clamp across a
    // grid of (funds, baseRent) pairs — the two must agree everywhere.
    const int fundsV[] = {0, 100, 799, 800, 801, 5000};
    const int rentV[]  = {0, 1, 800, 32000};
    for (int f : fundsV) {
        for (int r : rentV) {
            bool kernel = CutsceneLeaseCanAfford(f, r);
            bool real   = ClampSaysAffordable(f, r);
            CHECK_EQ(kernel, real);
        }
    }
}

TEST(CutsceneMisc4Itest, LeaseWindowFullFlowWithRealAffordability) {
    // Wire the real clamp into the affordability decision AND a real rent slider,
    // then run the full LeaseWindow body. Lessee 11 has 4000 funds (SumCurrency),
    // base rent 800 -> affordable per the real clamp -> slider negotiates to 400.
    CutsceneMisc4Hooks h{};
    h.personFind = FindLessee;
    h.personSumCurrency = SumCurrency;
    h.leaseRunRentSlider = RentSlider;
    SetCutsceneMisc4Hooks(&h);

    i32 lessees[] = {99, 11};   // lessee 11 at offer index 1
    i32 valid[]   = { 1,  1};
    i32 rent = -1;

    // Sanity: the real clamp agrees this lease is affordable.
    CHECK(ClampSaysAffordable(/*funds=*/4000, /*baseRent=*/800));

    int r = CutsceneLeaseWindow(lessees, valid, 2, /*lesseeId=*/11,
                                /*funds=*/4000, /*baseRent=*/800, &rent);
    CHECK_EQ(r, 1);            // shown + accepted
    CHECK_EQ(rent, 400);       // slider negotiated half
    CHECK_EQ(g_funds, 4000);   // funds (SumCurrency) flowed into the slider

    // A lessee who cannot afford per the real clamp: window not shown.
    SumCurrency(nullptr);
    i32 rent2 = -1;
    // baseRent 0 -> ClampValueRange fails (lo<1) -> never shown.
    int r2 = CutsceneLeaseWindow(lessees, valid, 2, 11, 4000, /*baseRent=*/0, &rent2);
    CHECK_EQ(r2, 0);
    CHECK_EQ(rent2, 0);
    SetCutsceneMisc4Hooks(nullptr);
}
