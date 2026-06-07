#include "gui/contact_loops.h"

namespace guild::gui {

// Reuse the status-text leaves + page-flag bits + ContactGate from contact_menu.cpp (same
// cluster): StatusText_Register / StatusText_ResetEntries / kFormFlagPage200/400.  The
// modal frame loop (VIBE_GameLogic_RunFrameLoop), the localized-label text engine and the
// 3D-overlay updater (VIBE_Hud_UpdateSelectedObjectContact) live in io/sim/render and are
// out of scope here; what this module owns is each loop's entry set + id->action wiring.

namespace {

ContactLoopSink  g_defaultSink;
ContactLoopSink* g_sink = &g_defaultSink;

ContactLoopGate  g_defaultGate;
ContactLoopGate* g_gate = &g_defaultGate;

} // namespace

void ContactLoops_SetCommandSink(ContactLoopSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}
void ContactLoops_SetGate(ContactLoopGate* gate) {
    g_gate = gate ? gate : &g_defaultGate;
}

// ===========================================================================
// VIBE_Hud_RunErzAbbauContactLoop (0x511af4) — ore search + mine.
//   if (word_631758 & 0x200) {
//     search = Register("contact_SUCHEN_ERZ", 22, ...);
//     mine   = Register("contact_ABBAUEN",   10, ...);
//   }
//   if (clicked) {
//     if (clicked==search) OpenProductionWindowA;
//     else if (clicked==mine) OpenProductionWindowB;
//   }
// ===========================================================================
ErzAbbauIds Hud_BuildErzAbbau(int pageFlags) {
    ErzAbbauIds e{};
    if (pageFlags & kFormFlagPage200) {
        e.search = StatusText_Register("contact_SUCHEN_ERZ", 22, nullptr);
        e.mine   = StatusText_Register("contact_ABBAUEN", 10, nullptr);
    }
    return e;
}
bool Hud_DispatchErzAbbau(int clicked, const ErzAbbauIds& e) {
    if (!clicked) return false;
    if (clicked == e.search) { g_sink->OpenProductionWindowA(clicked); return true; }
    if (clicked == e.mine)   { g_sink->OpenProductionWindowB(clicked); return true; }
    return false;
}

// ===========================================================================
// VIBE_Hud_RunProductionContactDispatch (0x511b74) — metal-production hub.
//   if (word_631758 & 0x200) {
//     production = Register("contact_PRODUKTION_METALL", 19, ...);   // v2
//     transport  = Register("contact_TRANSPORT", 21, ...);           // v9
//     storage    = Register("contact_LAGER", 14, ...);               // v12
//     feast      = Register("contact_GELAGE", 22, ...);              // v10
//     if (QueryFind(.., 53)) targetA = Register("ob_STROHPUPPE", 23, ...);  // v4
//     if (QueryFind(.., 52)) targetB = Register("ob_ZIELSCHEIBE", 23, ...); // v11
//   }
//   dispatch order: transport, storage, production, feast, (targetB||targetA).
// ===========================================================================
MetalProdIds Hud_BuildProductionMetal(int pageFlags) {
    MetalProdIds e{};
    if (pageFlags & kFormFlagPage200) {
        e.production = StatusText_Register("contact_PRODUKTION_METALL", 19, nullptr);
        e.transport  = StatusText_Register("contact_TRANSPORT", 21, nullptr);
        e.storage    = StatusText_Register("contact_LAGER", 14, nullptr);
        e.feast      = StatusText_Register("contact_GELAGE", 22, nullptr);
        if (g_gate->HasObject(53)) // QueryFind(.., 53) -> straw dummy
            e.targetA = StatusText_Register("ob_STROHPUPPE", 23, nullptr);
        if (g_gate->HasObject(52)) // QueryFind(.., 52) -> target board
            e.targetB = StatusText_Register("ob_ZIELSCHEIBE", 23, nullptr);
    }
    return e;
}
bool Hud_DispatchProductionMetal(int clicked, const MetalProdIds& e) {
    if (!clicked) return false;
    if (clicked == e.transport)  { g_sink->OpenTransport(clicked); return true; }
    if (clicked == e.storage)    { g_sink->OpenStorage(clicked); return true; }
    if (clicked == e.production) { g_sink->OpenProductionWindowC(clicked); return true; }
    if (clicked == e.feast)      { g_sink->RunFeast(clicked); return true; }
    if ((e.targetB && clicked == e.targetB) || (e.targetA && clicked == e.targetA)) {
        g_sink->RunTraining(clicked); return true;
    }
    return false;
}

// ===========================================================================
// VIBE_ContactMenu_RunEmptyLoop (0x514a34) — registers nothing.
//   ResetEntries(); do RunFrameLoop(...); while (...);  (no Register calls)
// ===========================================================================
void ContactMenu_BuildEmpty() {
    // No entries are registered; the original only spins the frame loop.
}

// ===========================================================================
// VIBE_ContactMenu_RobberHideout (0x513274) — thieves'-guild hideout (richest).
//   page 0x200 registers (in order):
//     equip=AUSRUESTEN(13) ambush=AUF_LAUER_LEGEN(21) attack=ANGRIFF(23)
//     [targetA=ob_STROHPUPPE(23) if QueryFind 53] [targetB=ob_ZIELSCHEIBE(23) if 52]
//     extort=SCHUTZGELD_ERPRESSEN(23) spyBuild=GEBAEUDE_AUSSPIONIEREN(23)
//     raid=RAUBUEBERFALL(23) regen=REGENERATION(23)
//   page 0x400 registers: storage=LAGER(14) feast=GELAGE(14) transport=TRANSPORT(21)
//   dispatch order: storage, feast, transport, equip, (targetB||targetA||regen),
//     ambush(guarded burglary), attack(ShowBar), extort(CheckAndShow), spyBuild(Bribery),
//     raid(RaidConfirm); then a trailing "byte_67225C==19 -> RaidConfirm".
// ===========================================================================
RobberHideoutIds ContactMenu_BuildRobberHideout(int pageFlags) {
    RobberHideoutIds e{};
    if (pageFlags & kFormFlagPage200) {
        e.equip    = StatusText_Register("contact_AUSRUESTEN", 13, nullptr);
        e.ambush   = StatusText_Register("contact_AUF_LAUER_LEGEN", 21, nullptr);
        e.attack   = StatusText_Register("contact_ANGRIFF", 23, nullptr);
        if (g_gate->HasObject(53))
            e.targetA = StatusText_Register("ob_STROHPUPPE", 23, nullptr);
        if (g_gate->HasObject(52))
            e.targetB = StatusText_Register("ob_ZIELSCHEIBE", 23, nullptr);
        e.extort   = StatusText_Register("contact_SCHUTZGELD_ERPRESSEN", 23, nullptr);
        e.spyBuild = StatusText_Register("contact_GEBAEUDE_AUSSPIONIEREN", 23, nullptr);
        e.raid     = StatusText_Register("contact_RAUBUEBERFALL", 23, nullptr);
        e.regen    = StatusText_Register("contact_REGENERATION", 23, nullptr);
    }
    if (pageFlags & kFormFlagPage400) {
        e.storage   = StatusText_Register("contact_LAGER", 14, nullptr);
        e.feast     = StatusText_Register("contact_GELAGE", 14, nullptr);
        e.transport = StatusText_Register("TRANSPORT", 21, nullptr);
    }
    return e;
}
bool ContactMenu_DispatchRobberHideout(int clicked, const RobberHideoutIds& e, int byteEvent) {
    bool handled = false;
    if (clicked) {
        if (clicked == e.storage)        { g_sink->OpenStorage(clicked); handled = true; }
        else if (clicked == e.feast)     { g_sink->RunFeast(clicked); handled = true; }
        else if (clicked == e.transport) { g_sink->OpenTransport(clicked); handled = true; }
        else if (clicked == e.equip)     { g_sink->RunThievesGuildEquipment(clicked); handled = true; }
        else if ((e.targetB && clicked == e.targetB) ||
                 (e.targetA && clicked == e.targetA) ||
                 (e.regen   && clicked == e.regen)) { g_sink->RunTraining(clicked); handled = true; }
        else if (clicked == e.ambush) {
            // if (!Dialog_CheckActiveCharFlag) { PlayerBar_Create(Burglary); ...; ClearAll(); }
            g_sink->ThievesGuildBurglary(); handled = true;
        }
        else if (clicked == e.attack)    { g_sink->RobberCampShowBar(clicked); handled = true; }
        else if (clicked == e.extort)    { g_sink->RobberCampCheckAndShow(); handled = true; }
        else if (clicked == e.spyBuild)  { g_sink->BriberyConfirm(); handled = true; }
        else if (clicked == e.raid)      { g_sink->RobberRaidConfirm(); handled = true; }
    }
    // Trailing per-frame trigger: byte_67225C == 19 fires a raid regardless of the click.
    if (byteEvent == 19) { g_sink->RobberRaidConfirm(); handled = true; }
    return handled;
}

// ===========================================================================
// VIBE_ContactMenu_SabotageActions (0x514d78).
//   Each frame registers (no page gate):
//     sabotage=SABOTAGE(23) beatUp=VERPRUEGELN(13) night=BEI_NACHT_UND_NEBEL(12)
//     bribe=BESTECHUNG(16)
//   dispatch: sabotage(Sabotage dialog), beatUp(office overview), night(ShowTalent 2),
//     bribe(PromptTargetSelect).
// ===========================================================================
SabotageIds ContactMenu_BuildSabotage() {
    SabotageIds e{};
    e.sabotage = StatusText_Register("contact_SABOTAGE", 23, nullptr);
    e.beatUp   = StatusText_Register("contact_VERPRUEGELN", 13, nullptr);
    e.night    = StatusText_Register("contact_BEI_NACHT_UND_NEBEL", 12, nullptr);
    e.bribe    = StatusText_Register("contact_BESTECHUNG", 16, nullptr);
    return e;
}
bool ContactMenu_DispatchSabotage(int clicked, const SabotageIds& e) {
    if (!clicked) return false;
    if (clicked == e.sabotage) { g_sink->RunSabotage(clicked); return true; }
    if (clicked == e.beatUp)   { g_sink->RunBeatUp(clicked); return true; }
    if (clicked == e.night)    { g_sink->ShowTalent(2); return true; }
    if (clicked == e.bribe)    { g_sink->PromptTargetSelect(clicked); return true; }
    return false;
}

// ===========================================================================
// VIBE_ContactMenu_GuildMasterActions (0x515ea4).
//   if (HandlerFlag(8)) { negotiate=VERHANDELN(12); craft=HANDWERKSKUNST(12); }
//   if (TutorialInactive) { proof=BEWEISBUCH(12); spy=SPIONAGE(23); exam=MEISTERPRUEFUNG(12); }
//   dispatch: negotiate(ShowTalent 0), craft(ShowTalent 1), proof(EvidenceBrowse),
//     exam(ResidenceMasterExam), spy(SpionageConfirm);
//   else (nothing clicked) if (dword_63C7C0 && byte_67225C==45) ResidenceMistress.
// ===========================================================================
GuildMasterIds ContactMenu_BuildGuildMaster() {
    GuildMasterIds e{};
    if (g_gate->HandlerFlag(8)) { // VIBE_Interaction_TestHandlerFlagWord(8)
        e.negotiate = StatusText_Register("contact_VERHANDELN", 12, nullptr);
        e.craft     = StatusText_Register("contact_HANDWERKSKUNST", 12, nullptr);
    }
    if (g_gate->TutorialInactive()) { // VIBE_Tutorial_IsInactive()
        e.proof = StatusText_Register("contact_BEWEISBUCH", 12, nullptr);
        e.spy   = StatusText_Register("contact_SPIONAGE", 23, nullptr);
        e.exam  = StatusText_Register("contact_MEISTERPRUEFUNG", 12, nullptr);
    }
    return e;
}
bool ContactMenu_DispatchGuildMaster(int clicked, const GuildMasterIds& e,
                                     bool mistressEnabled, int byteEvent) {
    if (clicked) {
        if (clicked == e.negotiate) { g_sink->ShowTalent(0); return true; }
        if (clicked == e.craft)     { g_sink->ShowTalent(1); return true; }
        if (clicked == e.proof)     { g_sink->EvidenceBrowse(clicked); return true; }
        if (clicked == e.exam)      { g_sink->ResidenceMasterExam(clicked); return true; }
        if (clicked == e.spy)       { g_sink->SpionageConfirm(); return true; }
        return false;
    }
    // else branch (nothing clicked this frame).
    if (mistressEnabled && byteEvent == 45) { g_sink->ResidenceMistress(clicked); return true; }
    return false;
}

// ===========================================================================
// VIBE_ContactMenu_Tavern (0x518c50).
//   each frame: Trade_RegisterEinkaufContact(self);
//     dice=WUERFELSPIEL(22)? (registered but its id discarded in the original: the dice
//       dispatch compares the *void* slot, see note) regulars=STAMMTISCH(22)
//       darkCorner=ob_DUNKLE_ECKE(22)
//   dispatch: dice(TavernCardGame), regulars(TavernStammtisch), darkCorner(TavernDarkCorner);
//   then if (dword_63C7C0): event 18 -> queue favour comment, event 23 -> dark-corner again.
// ===========================================================================
TavernIds ContactMenu_BuildTavern() {
    TavernIds e{};
    e.dice       = StatusText_Register("contact_WUERFELSPIEL", 22, nullptr);
    e.regulars   = StatusText_Register("contact_STAMMTISCH", 22, nullptr);
    e.darkCorner = StatusText_Register("ob_DUNKLE_ECKE", 22, nullptr);
    return e;
}
bool ContactMenu_DispatchTavern(int clicked, const TavernIds& e,
                                bool eventEnabled, int byteEvent) {
    bool handled = false;
    if (clicked) {
        if (clicked == e.dice)            { g_sink->TavernCardGame(clicked); handled = true; }
        else if (clicked == e.regulars)   { g_sink->TavernStammtisch(clicked); handled = true; }
        else if (clicked == e.darkCorner) { g_sink->TavernDarkCorner(clicked); handled = true; }
    }
    if (eventEnabled) { // dword_63C7C0
        if (byteEvent == 18)      { g_sink->TavernQueueComment(clicked); handled = true; }
        else if (byteEvent == 23) { g_sink->TavernDarkCorner(clicked); handled = true; }
    }
    return handled;
}

// ===========================================================================
// VIBE_WineCellar_RunContactLoop (0x519d74) — single entry.
//   wine = Register("contact_WEINSCHRANK", 22, ...);
//   if (clicked==wine) WineCellar_ShowBuyDialog.
// ===========================================================================
int WineCellar_BuildContact() {
    return StatusText_Register("contact_WEINSCHRANK", 22, nullptr);
}
bool WineCellar_DispatchContact(int clicked, int wineId) {
    if (!clicked) return false;
    if (clicked == wineId) { g_sink->WineCellarBuy(clicked); return true; }
    return false;
}

// ===========================================================================
// VIBE_CityTreasury_RunContactLoop (0x51f6a8) — single entry, gated.
//   if (byte_12CEA76[536*activeChar]) treasury = Register("ob_STADTKASSE", 22, ...); else 0;
//   if (clicked==treasury) GuildTreasury_ShowCashDialog.
// ===========================================================================
int CityTreasury_BuildContact() {
    if (g_gate->TreasuryActive())
        return StatusText_Register("ob_STADTKASSE", 22, nullptr);
    return 0;
}
bool CityTreasury_DispatchContact(int clicked, int treasuryId) {
    if (!clicked) return false;
    if (clicked == treasuryId) { g_sink->TreasuryCash(clicked); return true; }
    return false;
}

} // namespace guild::gui
