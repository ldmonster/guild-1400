#include "world/exchange_loop.h"

namespace guild::world {

// gilde.exe 0x51ce1c init loop (16 slots, 7 dwords each, id=-1 / count=0).
void ExchangeInitSlotTables(std::vector<ExchangeSlot>& give,
                            std::vector<ExchangeSlot>& take) {
    give.assign(16, ExchangeSlot{});
    take.assign(16, ExchangeSlot{});
    // ExchangeSlot's defaults already give {id=-1, count=0}; the explicit
    // assignment mirrors the original's loop that stamps both columns.
    for (int k = 0; k < 16; ++k) {
        give[k].id = -1; give[k].count = 0;
        take[k].id = -1; take[k].count = 0;
    }
}

// gilde.exe 0x51ce1c currency seed (cities 1..3).
int ExchangeSeedCityCurrencies(const std::vector<ExchangeCityRow>& cities,
                               std::vector<ExchangeSlot>& take) {
    int seeded = 0;
    for (int c = 1; c < 4; ++c) {
        if (static_cast<size_t>(c) >= cities.size())
            break;
        if (cities[c].active) {
            int slot = c - 1;
            if (static_cast<size_t>(slot) < take.size()) {
                take[slot].currency = cities[c].currencyId;
                take[slot].flag = static_cast<u8>(c);
                ++seeded;
            }
        }
    }
    return seeded;
}

// gilde.exe 0x51ce1c non-empty count (id != -1).
int ExchangeCountActiveSlots(const std::vector<ExchangeSlot>& slots) {
    int n = 0;
    for (const auto& s : slots)
        if (s.id != -1)
            ++n;
    return n;
}

bool ExchangeListOverflows(int activeCount) {
    return activeCount > 4;
}

// gilde.exe 0x51ce1c courier-trigger predicate.
bool ExchangeShouldShowCourier(bool shipmentPending, int selectedSlot,
                               int populatedSlot) {
    return !shipmentPending && selectedSlot != -1 && populatedSlot != -1;
}

// gilde.exe 0x51ce1c button dispatch.
ExchangeAction ExchangeDispatchButton(i32 widgetCode, i32 clickedObject,
                                      const ExchangeWidgets& w) {
    if (widgetCode == 1210) {
        if (clickedObject == w.feesButton)
            return ExchangeAction::ShowFees;
        if (clickedObject == w.exchangeButton)
            return ExchangeAction::ShowExchange;
        return ExchangeAction::None;
    }
    if (widgetCode == 1211 || widgetCode == 1212) {
        if (clickedObject == w.giveUp)   return ExchangeAction::ScrollGiveUp;
        if (clickedObject == w.giveDn)   return ExchangeAction::ScrollGiveDn;
        if (clickedObject == w.takeUp)   return ExchangeAction::ScrollTakeUp;
        if (clickedObject == w.takeDn)   return ExchangeAction::ScrollTakeDn;
    }
    return ExchangeAction::None;
}

// gilde.exe 0x51da04 — VIBE_Bank_RunContactDispatchLoop (contact router).
//   Meister/Vermögen contacts only exist when (word_631758 & 0x200).
//   The Exchange/Credit pair routes by home-city:
//     home   -> RunGoodsExchangeLoop / ShowLenderDialog
//     foreign-> ShowGoodsExchangeDialog / ShowTakeLoanDialog
BankContact BankRouteContact(const BankContactInput& in) {
    using Clicked = BankContactInput::Clicked;
    switch (in.clicked) {
        case Clicked::Meister:
            return in.isGuildMaster ? BankContact::MasterCertificate
                                    : BankContact::None;
        case Clicked::Vermoegen:
            return in.isGuildMaster ? BankContact::AssetOverview
                                    : BankContact::None;
        case Clicked::Exchange:
            return in.inHomeCity ? BankContact::GoodsExchangeLoop
                                 : BankContact::GoodsExchangeDialog;
        case Clicked::Credit:
            return in.inHomeCity ? BankContact::LenderDialog
                                 : BankContact::TakeLoanDialog;
        case Clicked::None:
        default:
            return BankContact::None;
    }
}

} // namespace guild::world
