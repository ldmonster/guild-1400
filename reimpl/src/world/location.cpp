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

// gilde.exe 0x51defc — VIBE_Building_EnterAndDispatch contact-loop selector.
// The original reads the building object's 16-bit type code (`v31 = *v21`, the
// `mov ax,[esi]` at 0x51e267) and runs a binary-search switch (0x51e26a..) that
// picks the contact loop. The exact object-type codes for the loops modeled by
// LocationKind here (verified against the decompile, ==/range tests):
//   84  (0x54)        -> VIBE_Location_ThiefGuildContactLoop   (0x525070)
//   229 (0xE5) / 230 (0xE6) -> VIBE_Location_ChurchContactLoop (0x5239c0)
//   247 (0xF7)        -> VIBE_Location_ProductionContactLoop   (0x52388c)
//   288 (0x120)       -> VIBE_ContactMenu_Tavern               (0x518c50)
// Every other code routes to a different loop / the default LABEL_30 spin; the
// kinds those open are not represented by this small enum, so they map to Idle.
// (There is NO "residence" loop in the dispatcher — Residence is unreachable.)
LocationKind ClassifyLocationKind(int objectType) {
    switch (objectType) {
        case 84:
            return LocationKind::ThiefGuild;
        case 229:
        case 230:
            return LocationKind::Church;
        case 247:
            return LocationKind::Production;
        case 288:
            return LocationKind::Tavern;
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
