#pragma once
// Location contact-dispatch rules — the per-building/per-NPC interaction-menu
// state machines (gilde.exe VIBE_Location_*ContactLoop). Every contact loop is
// the SAME small FSM, recovered here as a reusable dispatcher; the per-menu
// dialog bodies (church donation, thief burglary, ...) are deeply GUI-coupled
// and DEFERRED (listed in the report). What is recovered byte-for-byte:
//
//   The contact-loop control flow (gilde.exe pattern, e.g.
//     VIBE_Location_ChurchContactLoop        0x5239c0
//     VIBE_Location_ProductionContactLoop    0x52388c
//     VIBE_Location_ThiefGuildContactLoop    0x525070
//     VIBE_Location_ChurchGuildContactLoop   0x521608
//     VIBE_Location_ChurchConfessionContactLoop 0x522d70
//     VIBE_Location_IdleContactLoop          0x524058):
//
//     VIBE_StatusText_ResetEntries();
//     while ( VIBE_GameLogic_RunFrameLoop(425983, ..) ) {
//        // (re)register menu items each frame, gated by panel flags / NPC state:
//        slot_i = VIBE_StatusText_Register(menuString_i, len, icon_i);
//        if ( dword_631720 ) {                  // a menu item was clicked
//            if      ( dword_631720 == slot_0 ) dispatch_0();
//            else if ( dword_631720 == slot_1 ) dispatch_1();
//            ...
//        }
//     }
//
//   The gates are bits of word_631758 ("shop open" 0x200, "back room" 0x400) and
//   per-NPC predicates (foreign city, guild eligibility, thief rank >= 2, ...).
//   The clicked-item id lives in dword_631720; dispatch resolves to the menu item
//   whose registered slot id matches it. THIS is the "contact dispatch resolves
//   the expected target" rule.
#include <functional>
#include <string>
#include <vector>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Panel-state flag bits (gilde.exe word_631758). Menu registration is gated by
// these: the front shop (0x200) and the back room (0x400) are the two recovered
// bits used across the contact loops.
// ===========================================================================
constexpr u16 kPanelShopOpen = 0x200; // word_631758 & 0x200
constexpr u16 kPanelBackRoom = 0x400; // word_631758 & 0x400

// One menu item a contact loop can register. `gate` decides whether the item is
// offered this frame (mirrors the per-item `if` guarding the StatusText_Register
// call). `target` is an opaque dispatch id (the dialog/start function the
// original would call); the FSM returns it when the item is clicked.
struct ContactMenuItem {
    std::string menuString;          // contact_*/ob_* localization key
    int         target = 0;          // dispatch target id (which dialog runs)
    std::function<bool()> gate;      // offered this frame? (default: always)
};

// Registration result for one frame: the slot id assigned to each *offered*
// item, in registration order. The originals get these from StatusText_Register;
// here we assign sequential slot ids starting at 1 (0 == "no item").
struct ContactRegistration {
    std::vector<int> slotIds;   // slotIds[i] == 0 when item i was NOT offered
};

// gilde.exe VIBE_StatusText_Register loop body — register every offered item and
// return the slot ids. `panelFlags` is word_631758 (so gates can test it via a
// captured reference). The registration order matches the original's source
// order, which is what determines slot-id assignment.
ContactRegistration ContactRegisterMenu(const std::vector<ContactMenuItem>& items);

// gilde.exe contact-loop dispatch step. Given the registration and the clicked
// slot id (dword_631720; 0 == nothing clicked), resolve the dispatch target:
// returns the `target` of the item whose slot id equals `clickedSlot`, or -1 if
// nothing matches. This is the exact "first match wins, in registration order"
// rule the loops implement with their else-if chain.
int ContactDispatch(const std::vector<ContactMenuItem>& items,
                    const ContactRegistration& reg, int clickedSlot);

// ===========================================================================
// City contact dispatch (the City/Location entry point that decides WHICH
// contact loop a building/NPC opens). gilde.exe routes by building object type
// (*object == kind) and panel flags. This recovers the routing table the world
// uses to pick a loop. The kinds are the object-type bytes the dispatcher and
// the trade panel switch on (see VIBE_TradeTransport_PanelDispatcher's title
// switch: 8/14 dwelling, 6 market, 22 guild, 11/12/13 office, 2/7 special,
// 19/4/16 production, 10 contor/market-stall).
// ===========================================================================
enum class LocationKind {
    Production = 0,   // craft/production building -> ProductionContactLoop
    Church,           // -> ChurchContactLoop
    ThiefGuild,       // -> ThiefGuildContactLoop
    Tavern,           // -> Tavern* loops
    Residence,        // -> Residence* loops
    Idle,             // no interactive contact -> IdleContactLoop
};

// gilde.exe contact-loop title/kind classifier (the `switch (v11)` on *object in
// the PanelDispatcher, reused by the City contact router). Maps an object-type
// byte to a contact-loop kind. Unknown types -> Idle.
LocationKind ClassifyLocationKind(int objectType);

// ===========================================================================
// Concrete contact menus recovered from the named loops, as data. Building these
// lets a test drive the generic FSM with the exact items/order the originals
// register, and verify a click resolves to the right dialog target.
// ===========================================================================

// Dispatch targets for the Production contact loop (gilde.exe 0x52388c order).
enum class ProductionTarget {
    StaffBook = 1,        // ob_PERSONALBUCH       -> Personnel_RunStaffBook
    Storage,              // contact_LAGER         -> StorageDialog_Options
    ProductionWindow,     // contact_PRODUKTION_*  -> TradePanel_BuildProductionWindow
    Transport,            // contact_TRANSPORT     -> TradeTransport_OpenPanelMode1
    MasterCertificate,    // ob_MEISTERBRIEF       -> Meister_RunMasterCertificateDialog
};
// Items in the original's registration order (all gated by shop-open 0x200).
std::vector<ContactMenuItem> ProductionContactMenu(bool shopOpen);

// Dispatch targets for the Thieves' Guild contact loop (gilde.exe 0x525070).
enum class ThiefGuildTarget {
    Burglary = 1,         // contact_EINBRUCH        (shop 0x200)
    SpyBuilding,          // contact_GEBAEUDE_AUS..  (shop 0x200)
    Attack,               // contact_ANGRIFF         (shop 0x200, rank>=2)
    Pickpocket,           // contact_TASCHENDIEBSTAHL(shop 0x200)
    Information,          // ob_INFORMATIONSPERGAMENT(shop 0x200)
    StaffBook,            // ob_PERSONALBUCH         (back-room 0x400)
    MasterCertificate,    // ob_MEISTERBRIEF         (back-room 0x400)
};
// `thiefRank` is the NPC's +583 byte; Attack is only offered when rank >= 2.
std::vector<ContactMenuItem> ThiefGuildContactMenu(bool shopOpen, bool backRoom,
                                                   int thiefRank);

} // namespace guild::world
