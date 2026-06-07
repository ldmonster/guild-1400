#include "world/location.h"

namespace guild::world {

// gilde.exe — the StatusText_Register loop body. Each offered item gets the next
// sequential slot id (>=1); a non-offered item gets 0 (it never matches a click).
ContactRegistration ContactRegisterMenu(const std::vector<ContactMenuItem>& items) {
    ContactRegistration reg;
    reg.slotIds.resize(items.size(), 0);
    int next = 1;
    for (std::size_t i = 0; i < items.size(); ++i) {
        bool offered = !items[i].gate || items[i].gate();
        reg.slotIds[i] = offered ? next++ : 0;
    }
    return reg;
}

// gilde.exe contact-loop dispatch: first registered item whose slot id matches
// the clicked id wins (the else-if chain), in registration order. clickedSlot==0
// (dword_631720 unset) -> nothing.
int ContactDispatch(const std::vector<ContactMenuItem>& items,
                    const ContactRegistration& reg, int clickedSlot) {
    if (clickedSlot == 0)
        return -1;
    for (std::size_t i = 0; i < items.size() && i < reg.slotIds.size(); ++i) {
        if (reg.slotIds[i] != 0 && reg.slotIds[i] == clickedSlot)
            return items[i].target;
    }
    return -1;
}

// gilde.exe PanelDispatcher title switch (the `switch (v11)` on *object) reused
// as the contact-router classifier. Object-type bytes:
//   8/14 -> dwelling/residence, 6 -> market/church-adjacent, 22 -> guild,
//   11/12/13 -> office, 2/7 -> special, 19/4/16 -> production, 10 -> contor.
LocationKind ClassifyLocationKind(int objectType) {
    switch (objectType) {
        case 19:
        case 4:
        case 16:
        case 10:
            return LocationKind::Production;
        case 6:
            return LocationKind::Church;
        case 22:
            return LocationKind::ThiefGuild;
        case 2:
        case 7:
            return LocationKind::Tavern;
        case 8:
        case 14:
            return LocationKind::Residence;
        default:
            return LocationKind::Idle;
    }
}

// gilde.exe 0x52388c — VIBE_Location_ProductionContactLoop registration order.
//   ob_PERSONALBUCH, contact_LAGER, contact_PRODUKTION_SCHREIBEN,
//   contact_TRANSPORT, ob_MEISTERBRIEF  (all gated by shop-open 0x200).
// NOTE: the original registers in source order produkt/lager/transport/personal/
// meister but dispatches personal/lager/produkt/transport/meister; the slot ids
// follow registration order. We register in registration order so slot ids and
// dispatch targets line up exactly.
std::vector<ContactMenuItem> ProductionContactMenu(bool shopOpen) {
    auto gate = [shopOpen]() { return shopOpen; };
    return {
        {"contact_PRODUKTION_SCHREIBEN", (int)ProductionTarget::ProductionWindow, gate},
        {"contact_LAGER",                (int)ProductionTarget::Storage,          gate},
        {"contact_TRANSPORT",            (int)ProductionTarget::Transport,        gate},
        {"ob_PERSONALBUCH",              (int)ProductionTarget::StaffBook,        gate},
        {"ob_MEISTERBRIEF",              (int)ProductionTarget::MasterCertificate,gate},
    };
}

// gilde.exe 0x525070 — VIBE_Location_ThiefGuildContactLoop registration order.
//   (shop 0x200): EINBRUCH, GEBAEUDE_AUSSPIONIEREN, [rank>=2] ANGRIFF,
//                 TASCHENDIEBSTAHL, ob_INFORMATIONSPERGAMENT
//   (back-room 0x400): ob_PERSONALBUCH, ob_MEISTERBRIEF
std::vector<ContactMenuItem> ThiefGuildContactMenu(bool shopOpen, bool backRoom,
                                                   int thiefRank) {
    auto shop = [shopOpen]() { return shopOpen; };
    auto back = [backRoom]() { return backRoom; };
    auto attackGate = [shopOpen, thiefRank]() { return shopOpen && thiefRank >= 2; };
    return {
        {"contact_EINBRUCH",             (int)ThiefGuildTarget::Burglary,          shop},
        {"contact_GEBAEUDE_AUSSPIONIEREN",(int)ThiefGuildTarget::SpyBuilding,      shop},
        {"contact_ANGRIFF",              (int)ThiefGuildTarget::Attack,            attackGate},
        {"contact_TASCHENDIEBSTAHL",     (int)ThiefGuildTarget::Pickpocket,        shop},
        {"ob_INFORMATIONSPERGAMENT",     (int)ThiefGuildTarget::Information,       shop},
        {"ob_PERSONALBUCH",              (int)ThiefGuildTarget::StaffBook,         back},
        {"ob_MEISTERBRIEF",              (int)ThiefGuildTarget::MasterCertificate, back},
    };
}

} // namespace guild::world
