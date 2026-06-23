// ===========================================================================
// MeisterAi economy TRADE planners — pure decision-logic reconstruction.
// gilde.exe 0x4614d0 / 0x46329c / 0x463c4c. See header for the full provenance,
// the recovered scratch-table layouts, and the DEFERRED engine-coupled plumbing.
// Every kernel below is a byte-faithful translation of the Hex-Rays decompile of
// the corresponding decision; the cart-routing / command-queue / logging tail is
// surfaced through the snapshot inputs (it is not pure plan math).
// ===========================================================================
#include "sim/meister_economy_recon_planners.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// gilde.exe 0x4614d0 — TradeGeneral kernels.
// ---------------------------------------------------------------------------

// Top item pre-pass:
//   v5 = word_B5448C[..];
//   if ( (v5 & 6) == 0 && (v5 & 8) != 0 )
//   {
//       SlotCapacity = VIBE_Inventory_GetSlotCapacity(cart);
//       v7 = dword_B54478[..];                 // stock
//       if ( SlotCapacity / 3 < v7 )
//           dword_B54468[..] = v7;             // plan = stock
//   }
int GeneralStoragePrePass(int bits, int stock, int slotCap) {
    if ((bits & 6) != 0)
        return 0;
    if ((bits & 8) == 0)
        return 0;
    if (slotCap / 3 < stock)
        return stock;
    return 0;
}

// Profit-margin sell (odd-hour block):
//   v10 = word_B564BC[..];
//   if ( (v10 & 6) == 0 )
//   {
//       if ( (v10 & 8) == 0 || slotCap/3 < stock )
//       {
//           v11 = ComputeMarketPrice(typeId, 100);          // sellPrice
//           ratio = v11 / LookupCachedMarketPrice(typeId);   // buyPrice
//           if ( ratio < 1.0f && RandomFloatScaled()*1.25 >= ratio )
//               plan = stock;                                // dump
//       }
//   }
int GeneralProfitSell(int bits, int stock, float sellPrice, float buyPrice,
                      double roll, int slotCap) {
    if ((bits & 6) != 0)
        return 0;
    // the (bits & 8) gate: when set, also require slotCap/3 < stock.
    if ((bits & 8) != 0 && !(slotCap / 3 < stock))
        return 0;
    if (buyPrice == 0.0f)
        return 0;
    // The binary computes the ratio in float (v155) and compares the bit pattern of
    // 1.0f (0x3F800000): ratio < 1.0. Then compares the float ratio against the
    // scaled roll. Reproduce with float ratio to preserve rounding edge cases.
    float ratio = sellPrice / buyPrice;
    if (ratio < 1.0f && roll * static_cast<double>(kProfitSellSlope) >= static_cast<double>(ratio))
        return stock;
    return 0;
}

// Emergency sell:
//   v21 = LookupCachedMarketPrice(typeId); price = CoordTrunc(v21);   // v151
//   v22 = 32000 / price; if ( v22 < 1 ) v22 = 1;  v23 = v22;          // pre-clamp
//   if ( v22 >= stock ) v22 = stock;                                   // clamp
//   plan = v22;
//   proceeds += v23 * price;                                           // UN-clamped qty
// The outer loop only enters while running proceeds < 32000; gate (bits & 6)==0.
int GeneralEmergencySell(int bits, int stock, int price, int* proceeds) {
    if ((bits & 6) != 0)
        return 0;
    if (proceeds && *proceeds >= kEmergencyTarget)
        return 0;
    if (price <= 0)
        return 0;
    int qty = kEmergencyTarget / price; // 32000 / price
    if (qty < 1)
        qty = 1;
    int raw = qty;          // v23 drives the proceeds running sum (pre-clamp)
    if (qty >= stock)
        qty = stock;
    if (proceeds)
        *proceeds += raw * price;
    return qty;
}

// Overstock sell:
//   if ( (word_B564BC[..] & 6) == 0 )
//   {
//       v26 = Inventory_GetSlotCapacity(cart);
//       v27 = dword_B564A4[..];                  // stock
//       if ( 3 * v26 / 4 < v27 )
//       {
//           v28 = v27 / 4; if ( v27/4 < 5 ) v28 = 5;
//           if ( v28 >= v27 ) v28 = v27;          // clamp to stock
//           plan = v28;
//       }
//   }
int GeneralOverstockSell(int bits, int stock, int slotCap) {
    if ((bits & 6) != 0)
        return 0;
    if (!(3 * slotCap / 4 < stock))
        return 0;
    int qty = stock / 4;
    if (qty < kOverstockMin)
        qty = kOverstockMin;
    if (qty >= stock)
        qty = stock;
    return qty;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x46329c / 0x463c4c — remote-purchase kernels.
// ---------------------------------------------------------------------------

// Cart-trip-count clamp:
//   VIBE_Character_CountActiveByTurn();
//   v6 = flt_641DAC * 0.006;  v82 = CoordTrunc(v6);   // raw
//   if ( raw >= 5 ) n = raw; else n = 5;
//   if ( n > 16 ) n = 16;
int RemoteCartTripCount(double activeByTurn) {
    int raw = CoordTrunc(activeByTurn * kCartTripScale);
    int n = (raw >= kCartTripMin) ? raw : kCartTripMin;
    if (n > kCartTripMax)
        n = kCartTripMax;
    return n;
}

// The signed (9*t)/8 idiom the binary emits:
//   (9*t - ( ((9*t)>>31 << 3) + 8*((9*t)>>31) )) >> 3
// For non-negative 9*t the sign-shift is 0 and this is (9*t)>>3. For negative 9*t
// it adds the 8*sign correction so the >>3 truncates toward zero, i.e. it is the
// exact two's-complement expansion of the C expression (9*t)/8. We compute it via
// the literal idiom on 32-bit ints to preserve any overflow wraparound.
int NineEighthsTrunc(int t) {
    // Reproduce the decompiler idiom literally:
    //   p = 9*t;  sign = p >> 31;  cf = __CFSHL__(sign, 3);
    //   r = (p - (cf + 8*sign)) >> 3
    // sign is 0 (p>=0) or -1 (p<0). __CFSHL__(0xFFFFFFFF, 3) carries out 1, so cf=1
    // when sign==-1 else 0. Then correction = cf + 8*sign = (sign<0 ? 1-8 : 0) = -7
    // for negatives, 0 for non-negatives, and (p - (-7)) >> 3 == (p+7) >> 3, the
    // textbook truncate-toward-zero signed /8. Verified == (9*t)/8 for all 32-bit t.
    i32 p = static_cast<i32>(static_cast<u32>(9u) * static_cast<u32>(t)); // 9*t (wraps)
    i32 sign = p >> 31;                  // arithmetic shift: 0 or -1
    i32 cf = (sign != 0) ? 1 : 0;        // __CFSHL__(sign, 3)
    i32 correction = cf + 8 * sign;      // -7 for p<0, else 0
    return (p - correction) >> 3;
}

// Restock build-up / build-down decision:
//   v30 = target (the accumulated weighted demand v76/v77[...+184]);
//   v29 = current count (GameObject_CountAtLocation);
//   if ( v29 < v30 )      magnitude = CoordTrunc( ((9*v30)/8) * 1.03 );  // build UP
//   else if ( v29 > 2*v30) magnitude = CoordTrunc( (v29 - (9*v30)/8) * 1.03 ); // DOWN
// Returns signed: + = up, - = down, 0 = none.
int RemoteRestockDelta(int count, int target) {
    int nineEighths = NineEighthsTrunc(target);
    if (count < target) {
        return CoordTrunc(static_cast<double>(nineEighths) * kRestockPriceScale);
    }
    if (count > 2 * target) {
        int delta = count - nineEighths;
        return -CoordTrunc(static_cast<double>(delta) * kRestockPriceScale);
    }
    return 0;
}

// Per-source planned-purchase clamp:
//   SlotCapacity = Inventory_GetSlotCapacity(targetStorage);
//   if ( SlotCapacity >= plannedQty ) qty = plannedQty; else qty = SlotCapacity;
int RemotePlanQty(int slotCapacity, int plannedQty) {
    return (slotCapacity >= plannedQty) ? plannedQty : slotCapacity;
}

// ---------------------------------------------------------------------------
// Snapshot-driven orchestration.
// ---------------------------------------------------------------------------

int RunTradeGeneral(GeneralPlayer& player,
                    std::vector<GeneralWorkstation>& stations,
                    const std::function<double()>& roll,
                    std::vector<TradeDecision>& out) {
    int emitted = 0;

    // The odd-hour profit / emergency block runs once per odd hour, guarded by the
    // +437&2 latch (here sellDoneFlag). The original sets the latch after the block;
    // we mirror "only when oddHour && !sellDoneFlag".
    if (player.oddHour && !player.sellDoneFlag) {
        // (1) profit-margin sell over the workstation table.
        for (GeneralWorkstation& w : stations) {
            double r = roll ? roll() : 0.0;
            int qty = GeneralProfitSell(w.bits, w.stock, w.sellPrice, w.buyPrice, r,
                                        player.slotCap);
            if (qty != 0) {
                w.plannedQty = qty;
                out.push_back(TradeDecision{TradeDecisionKind::ProfitSell, w.typeId, qty});
                ++emitted;
            }
        }

        // (2) emergency sell when cash-strapped (funds<3200 OR held<8000), until the
        // running projected proceeds reach 32000.
        if (player.funds < kEmergencyFundsFloor || player.heldCurrency < kEmergencyHeldFloor) {
            // base proceeds = min(held, funds) — the original v17 selection.
            int proceeds = (player.heldCurrency <= player.funds) ? player.heldCurrency
                                                                 : player.funds;
            if (proceeds < kEmergencyTarget) {
                for (GeneralWorkstation& w : stations) {
                    if (proceeds >= kEmergencyTarget)
                        break;
                    // emergency uses the cached market price as the unit price.
                    int price = CoordTrunc(static_cast<double>(w.buyPrice));
                    int qty = GeneralEmergencySell(w.bits, w.plannedQty ? w.plannedQty : w.stock,
                                                   price, &proceeds);
                    if (qty != 0) {
                        w.plannedQty = qty;
                        out.push_back(TradeDecision{TradeDecisionKind::EmergencySell,
                                                    w.typeId, qty});
                        ++emitted;
                    }
                }
            }
        }
    }

    // (3) overstock sell — always runs.
    for (GeneralWorkstation& w : stations) {
        int qty = GeneralOverstockSell(w.bits, w.stock, player.slotCap);
        if (qty != 0) {
            w.plannedQty = qty;
            out.push_back(TradeDecision{TradeDecisionKind::OverstockSell, w.typeId, qty});
            ++emitted;
        }
    }
    return emitted;
}

int RunRemotePurchase(int slotCapacity, int currentCount, int target,
                      double cartActiveByTurn,
                      const std::vector<RemoteSource>& sources,
                      std::vector<TradeDecision>& out) {
    int emitted = 0;

    // Trip-count clamp (the original sets a "too many trips" flag from it; we keep
    // the computation faithful even though the flag itself is plumbing).
    (void)RemoteCartTripCount(cartActiveByTurn);

    // Per-source planned purchase quantities.
    for (const RemoteSource& s : sources) {
        int qty = RemotePlanQty(slotCapacity, s.plannedQty);
        if (qty != 0) {
            out.push_back(TradeDecision{TradeDecisionKind::RemotePlanSell, s.typeId, qty});
            ++emitted;
        }
    }

    // The market restock-balance command (build up / down), computed once.
    int delta = RemoteRestockDelta(currentCount, target);
    if (delta > 0) {
        out.push_back(TradeDecision{TradeDecisionKind::RemoteRestockUp, 0, delta});
        ++emitted;
    } else if (delta < 0) {
        out.push_back(TradeDecision{TradeDecisionKind::RemoteRestockDown, 0, delta});
        ++emitted;
    }
    return emitted;
}

} // namespace guild::sim
