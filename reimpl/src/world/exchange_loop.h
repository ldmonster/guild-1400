#pragma once
// ===========================================================================
// exchange_loop.{h,cpp} — the goods/money exchange ("Geldleihe\Wechsel") main
// loop logic core + the bank contact router (gilde.exe). MODULE: the remaining
// Exchange body (RunGoodsExchangeLoop) and Bank_RunContactDispatchLoop.
// ===========================================================================
//
//   * VIBE_Exchange_RunGoodsExchangeLoop 0x51ce1c — the contor goods-exchange
//     panel. The deterministic RULE CORE recovered here:
//       - the two 16-slot tables (give v34 / take v35), each 7 dwords/slot,
//         initialised to {id=-1, count=0};
//       - the per-CITY currency seed: for cities 1..3 that are active, the take
//         table gets that city's currency id + the city index as its flag;
//       - the non-empty slot COUNT for each table (drives the ">4 overflow"
//         scroll widgets);
//       - the COURIER-trigger predicate (a give slot is selected AND there is no
//         pending shipment AND a take slot is active);
//       - the button DISPATCH (1210 -> fees/exchange dialogs; 1211/1212 -> scroll
//         the overflow lists).
//     The form/window/drag-slot/text-render plumbing is GUI and is DEFERRED.
//
//   * VIBE_Bank_RunContactDispatchLoop 0x51da04 — the Geldleihe building's contact
//     router. Decides which sub-dialog a clicked contact opens, gated on the
//     guild-master flag (word_631758 & 0x200) and the home-city check
//     (*(obj+39) == word_63CC5C). Recovered as a pure routing decision.
#include <vector>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// One exchange slot, as the loop addresses it (7 dwords; only the id/count and
// the currency/flag columns are load-bearing for the rule core).
// ===========================================================================
struct ExchangeSlot {
    i32 id = -1;       // slot[0]  (-1 == empty)
    i32 count = 0;     // slot[3]  (quantity / non-empty marker)
    i32 currency = 0;  // slot[3]>>16 seeded value (take side) — the city currency
    u8  flag = 0;      // slot[4] low byte — the city index marker (give side seed)
};

// ===========================================================================
// gilde.exe 0x51ce1c init loop:
//   for (k=0; k<16; ++k) { give[k].id=-1; give[k].count=0;
//                          take[k].id=-1; take[k].count=0; }
// Returns the two zeroed 16-slot tables.
// ===========================================================================
void ExchangeInitSlotTables(std::vector<ExchangeSlot>& give,
                            std::vector<ExchangeSlot>& take);

// One city row the seed loop reads.
struct ExchangeCityRow {
    bool active = false; // byte_13CD6A0[756*c] != 0  (city name present)
    i32  currencyId = 0; // (cityRecord+82)>>16       (the city currency object id)
};

// ===========================================================================
// gilde.exe 0x51ce1c currency seed loop (cities 1..3):
//   for (c=1; c<4; ++c) if (cities[c].active) { take[c-1].currency = cities[c].id;
//                                               take[c-1].flag    = c; }
// `cities` is indexed by city id (index 0 unused for the seed; the loop visits
// 1,2,3). Writes the seeds into `take` (slots 0..2). Returns the number seeded.
// ===========================================================================
int ExchangeSeedCityCurrencies(const std::vector<ExchangeCityRow>& cities,
                               std::vector<ExchangeSlot>& take);

// ===========================================================================
// gilde.exe 0x51ce1c non-empty count (the v46 / v25 prefix counters): count the
// slots whose id (+0) != -1 across the 16-entry table.
// ===========================================================================
int ExchangeCountActiveSlots(const std::vector<ExchangeSlot>& slots);

// ">4 overflow": the original shows the scroll affordance + the "+N more" label
// only when the active count exceeds 4. (VIBE_AnimationState_Update renders the
// count into the label.)
bool ExchangeListOverflows(int activeCount);

// ===========================================================================
// gilde.exe 0x51ce1c courier-trigger predicate. Inside the frame loop the courier
// dialog is invoked when:
//   - there is NO pending shipment handler for the player (`!shipmentPending`),
//   - a take slot is currently selected (selectedSlot != -1),
//   - and a give slot is populated (populatedSlot != -1).
// Returns true when the courier dialog would be shown.
// ===========================================================================
bool ExchangeShouldShowCourier(bool shipmentPending, int selectedSlot,
                               int populatedSlot);

// ===========================================================================
// gilde.exe 0x51ce1c button dispatch. The frame loop reads the clicked widget id
// (dword_75BF38) + the clicked object (dword_62D22C). These are the actions the
// loop body resolves; the slot-layout / render branches are deferred.
// ===========================================================================
enum class ExchangeAction {
    None,
    ShowFees,      // 1210 + click == feesButton  -> VIBE_Exchange_ShowFeesDialog
    ShowExchange,  // 1210 + click == exchangeBtn -> VIBE_Exchange_ShowGoodsExchangeDialog
    ScrollGiveUp,  // 1211/1212 + click == giveUp   -> Window_Scroll(-80) on give list
    ScrollGiveDn,  // 1211/1212 + click == giveDn   -> Window_Scroll(+80)
    ScrollTakeUp,  // 1211/1212 + click == takeUp   -> Window_Scroll(+80) on take list
    ScrollTakeDn,  // 1211/1212 + click == takeDn   -> Window_Scroll(+80)
};
// Widget ids the loop captured for the dispatch (the v40/v44/v49..v52 locals).
struct ExchangeWidgets {
    i32 feesButton = 0;     // v40
    i32 exchangeButton = 0; // v44
    i32 giveUp = 0;         // v51
    i32 giveDn = 0;         // v47
    i32 takeUp = 0;         // v50
    i32 takeDn = 0;         // v49
};
ExchangeAction ExchangeDispatchButton(i32 widgetCode, i32 clickedObject,
                                      const ExchangeWidgets& w);

// ===========================================================================
// gilde.exe 0x51da04 — VIBE_Bank_RunContactDispatchLoop (contact router).
//
// The Geldleihe building registers up to four contacts and routes the clicked one
// to a sub-dialog. The Meisterbrief/Vermögen pair is only registered when the
// player is the guild master (word_631758 & 0x200). The Geldwechsel/Kredite pair
// routes differently depending on whether the panel object is in the player's
// HOME city (*(obj+39) == word_63CC5C): home -> the foreign goods-exchange loop +
// lender dialog; foreign -> the plain goods-exchange dialog + take-loan dialog.
// ===========================================================================
enum class BankContact {
    None,
    GoodsExchangeLoop,   // home: VIBE_Exchange_RunGoodsExchangeLoop
    GoodsExchangeDialog, // foreign: VIBE_Exchange_ShowGoodsExchangeDialog
    LenderDialog,        // home: VIBE_Credit_ShowLenderDialog
    TakeLoanDialog,      // foreign: VIBE_Credit_ShowTakeLoanDialog
    MasterCertificate,   // VIBE_Meister_RunMasterCertificateDialog (guild master)
    AssetOverview,       // VIBE_Credit_ShowAssetOverview (guild master)
};
struct BankContactInput {
    bool isGuildMaster = false; // (word_631758 & 0x200) != 0
    bool inHomeCity = false;    // *(obj+39) == word_63CC5C
    // Which contact was clicked (the original matches the click against the four
    // registered contact widget ids in this order):
    enum class Clicked { None, Exchange, Credit, Meister, Vermoegen } clicked =
        Clicked::None;
};
BankContact BankRouteContact(const BankContactInput& in);

} // namespace guild::world
