#include "test.h"

#include "gui/contact_loops.h"

using namespace guild::gui;

namespace {

// Records which verb the loop dispatched (the WIRING half).
struct LoopSink : ContactLoopSink {
    int prodA = 0, prodB = 0, prodC = 0;
    int transport = 0, storage = 0, feast = 0, training = 0;
    int equip = 0, raids = 0, bribery = 0, campBar = 0, campCheck = 0, burglary = 0;
    int sabotage = 0, beatUp = 0, talent = -1, promptTarget = 0;
    int evidence = 0, exam = 0, spy = 0, mistress = 0;
    int cardGame = 0, stammtisch = 0, darkCorner = 0, comment = 0;
    int wine = 0, treasury = 0;

    void OpenProductionWindowA(int o) override { prodA = o; }
    void OpenProductionWindowB(int o) override { prodB = o; }
    void OpenProductionWindowC(int o) override { prodC = o; }
    void OpenTransport(int o) override { transport = o; }
    void OpenStorage(int o) override { storage = o; }
    void RunFeast(int o) override { feast = o; }
    void RunTraining(int o) override { training = o; }
    void RunThievesGuildEquipment(int o) override { equip = o; }
    void RobberCampShowBar(int o) override { campBar = o; }
    void RobberCampCheckAndShow() override { ++campCheck; }
    void BriberyConfirm() override { ++bribery; }
    void RobberRaidConfirm() override { ++raids; }
    void ThievesGuildBurglary() override { ++burglary; }
    void RunSabotage(int o) override { sabotage = o; }
    void RunBeatUp(int o) override { beatUp = o; }
    void ShowTalent(int w) override { talent = w; }
    void PromptTargetSelect(int o) override { promptTarget = o; }
    void EvidenceBrowse(int o) override { evidence = o; }
    void ResidenceMasterExam(int o) override { exam = o; }
    void SpionageConfirm() override { ++spy; }
    void ResidenceMistress(int o) override { mistress = o + 1; } // o may be 0; +1 marks it fired
    void TavernCardGame(int o) override { cardGame = o; }
    void TavernStammtisch(int o) override { stammtisch = o; }
    void TavernDarkCorner(int o) override { darkCorner = o; }
    void TavernQueueComment(int) override { ++comment; }
    void WineCellarBuy(int o) override { wine = o; }
    void TreasuryCash(int o) override { treasury = o; }
};

// Gate that exposes both training targets + flips the GuildMaster / Treasury predicates.
struct LoopGate : ContactLoopGate {
    bool hasObjects = true;
    bool handlerFlag = true;
    bool tutInactive = true;
    bool treasury = true;
    bool HasObject(int) override { return hasObjects; }
    bool HandlerFlag(int) override { return handlerFlag; }
    bool TutorialInactive() override { return tutInactive; }
    bool TreasuryActive() override { return treasury; }
};

} // namespace

TEST(GuiContactLoops, ErzAbbau) {
    ResetContactMenu();
    LoopSink sink; ContactLoops_SetCommandSink(&sink);

    // Page off: nothing registered.
    ErzAbbauIds off = Hud_BuildErzAbbau(0);
    CHECK_EQ(off.search, 0);
    CHECK_EQ(off.mine, 0);

    ErzAbbauIds e = Hud_BuildErzAbbau(kFormFlagPage200);
    CHECK(e.search != 0);
    CHECK(e.mine != 0);
    CHECK(e.search != e.mine);

    CHECK(Hud_DispatchErzAbbau(e.search, e)); CHECK_EQ(sink.prodA, e.search);
    CHECK(Hud_DispatchErzAbbau(e.mine, e));   CHECK_EQ(sink.prodB, e.mine);
    CHECK(Hud_DispatchErzAbbau(0, e) == false);
    ContactLoops_SetCommandSink(nullptr);
}

TEST(GuiContactLoops, ProductionMetal) {
    ResetContactMenu();
    LoopSink sink; ContactLoops_SetCommandSink(&sink);
    LoopGate gate; gate.hasObjects = true; ContactLoops_SetGate(&gate);

    MetalProdIds e = Hud_BuildProductionMetal(kFormFlagPage200);
    CHECK(e.production && e.transport && e.storage && e.feast);
    CHECK(e.targetA && e.targetB);

    CHECK(Hud_DispatchProductionMetal(e.transport, e));  CHECK_EQ(sink.transport, e.transport);
    CHECK(Hud_DispatchProductionMetal(e.storage, e));    CHECK_EQ(sink.storage, e.storage);
    CHECK(Hud_DispatchProductionMetal(e.production, e));  CHECK_EQ(sink.prodC, e.production);
    CHECK(Hud_DispatchProductionMetal(e.feast, e));       CHECK_EQ(sink.feast, e.feast);
    CHECK(Hud_DispatchProductionMetal(e.targetA, e));     CHECK_EQ(sink.training, e.targetA);
    CHECK(Hud_DispatchProductionMetal(e.targetB, e));     CHECK_EQ(sink.training, e.targetB);

    // No training objects -> targets not registered.
    ResetContactMenu();
    gate.hasObjects = false;
    MetalProdIds e2 = Hud_BuildProductionMetal(kFormFlagPage200);
    CHECK_EQ(e2.targetA, 0);
    CHECK_EQ(e2.targetB, 0);
    ContactLoops_SetGate(nullptr);
    ContactLoops_SetCommandSink(nullptr);
}

TEST(GuiContactLoops, Empty) {
    ResetContactMenu();
    // The empty loop registers nothing; building it must not touch the table.
    ContactMenu_BuildEmpty();
    CHECK_EQ(g_statusEntries[0].handle, 0);
}

TEST(GuiContactLoops, RobberHideout) {
    ResetContactMenu();
    LoopSink sink; ContactLoops_SetCommandSink(&sink);
    LoopGate gate; gate.hasObjects = true; ContactLoops_SetGate(&gate);

    RobberHideoutIds e = ContactMenu_BuildRobberHideout(kFormFlagPage200 | kFormFlagPage400);
    CHECK(e.equip && e.ambush && e.attack && e.extort && e.spyBuild && e.raid && e.regen);
    CHECK(e.targetA && e.targetB);
    CHECK(e.storage && e.feast && e.transport);

    CHECK(ContactMenu_DispatchRobberHideout(e.storage, e, 0));   CHECK_EQ(sink.storage, e.storage);
    CHECK(ContactMenu_DispatchRobberHideout(e.feast, e, 0));     CHECK_EQ(sink.feast, e.feast);
    CHECK(ContactMenu_DispatchRobberHideout(e.transport, e, 0)); CHECK_EQ(sink.transport, e.transport);
    CHECK(ContactMenu_DispatchRobberHideout(e.equip, e, 0));     CHECK_EQ(sink.equip, e.equip);
    CHECK(ContactMenu_DispatchRobberHideout(e.regen, e, 0));     CHECK_EQ(sink.training, e.regen);
    CHECK(ContactMenu_DispatchRobberHideout(e.targetA, e, 0));   CHECK_EQ(sink.training, e.targetA);
    CHECK(ContactMenu_DispatchRobberHideout(e.attack, e, 0));    CHECK_EQ(sink.campBar, e.attack);

    int prevCheck = sink.campCheck;
    CHECK(ContactMenu_DispatchRobberHideout(e.extort, e, 0)); CHECK_EQ(sink.campCheck, prevCheck + 1);
    int prevBribe = sink.bribery;
    CHECK(ContactMenu_DispatchRobberHideout(e.spyBuild, e, 0)); CHECK_EQ(sink.bribery, prevBribe + 1);
    int prevBurg = sink.burglary;
    CHECK(ContactMenu_DispatchRobberHideout(e.ambush, e, 0)); CHECK_EQ(sink.burglary, prevBurg + 1);

    // Raid fires both on the explicit click and on the trailing byte-event 19.
    int prevRaid = sink.raids;
    CHECK(ContactMenu_DispatchRobberHideout(e.raid, e, 0)); CHECK_EQ(sink.raids, prevRaid + 1);
    prevRaid = sink.raids;
    CHECK(ContactMenu_DispatchRobberHideout(0, e, 19)); CHECK_EQ(sink.raids, prevRaid + 1);
    // Click + trailing event both fire: raid twice (click matches storage, event raids).
    prevRaid = sink.raids;
    CHECK(ContactMenu_DispatchRobberHideout(e.attack, e, 19)); CHECK_EQ(sink.raids, prevRaid + 1);

    ContactLoops_SetGate(nullptr);
    ContactLoops_SetCommandSink(nullptr);
}

TEST(GuiContactLoops, Sabotage) {
    ResetContactMenu();
    LoopSink sink; ContactLoops_SetCommandSink(&sink);

    SabotageIds e = ContactMenu_BuildSabotage();
    CHECK(e.sabotage && e.beatUp && e.night && e.bribe);

    CHECK(ContactMenu_DispatchSabotage(e.sabotage, e)); CHECK_EQ(sink.sabotage, e.sabotage);
    CHECK(ContactMenu_DispatchSabotage(e.beatUp, e));   CHECK_EQ(sink.beatUp, e.beatUp);
    CHECK(ContactMenu_DispatchSabotage(e.night, e));    CHECK_EQ(sink.talent, 2);
    CHECK(ContactMenu_DispatchSabotage(e.bribe, e));    CHECK_EQ(sink.promptTarget, e.bribe);
    CHECK(ContactMenu_DispatchSabotage(0, e) == false);
    ContactLoops_SetCommandSink(nullptr);
}

TEST(GuiContactLoops, GuildMaster) {
    ResetContactMenu();
    LoopSink sink; ContactLoops_SetCommandSink(&sink);
    LoopGate gate; ContactLoops_SetGate(&gate);

    GuildMasterIds e = ContactMenu_BuildGuildMaster();
    CHECK(e.negotiate && e.craft && e.proof && e.spy && e.exam);

    CHECK(ContactMenu_DispatchGuildMaster(e.negotiate, e, false, 0)); CHECK_EQ(sink.talent, 0);
    CHECK(ContactMenu_DispatchGuildMaster(e.craft, e, false, 0));     CHECK_EQ(sink.talent, 1);
    CHECK(ContactMenu_DispatchGuildMaster(e.proof, e, false, 0));     CHECK_EQ(sink.evidence, e.proof);
    CHECK(ContactMenu_DispatchGuildMaster(e.exam, e, false, 0));      CHECK_EQ(sink.exam, e.exam);
    int prevSpy = sink.spy;
    CHECK(ContactMenu_DispatchGuildMaster(e.spy, e, false, 0));       CHECK_EQ(sink.spy, prevSpy + 1);

    // Nothing clicked: mistress fires only when enabled and byteEvent==45.
    CHECK(ContactMenu_DispatchGuildMaster(0, e, false, 45) == false);
    CHECK(ContactMenu_DispatchGuildMaster(0, e, true, 44) == false);
    CHECK(ContactMenu_DispatchGuildMaster(0, e, true, 45)); CHECK(sink.mistress != 0);

    // Gate off: handler-flag group + tutorial group both suppressed.
    ResetContactMenu();
    gate.handlerFlag = false; gate.tutInactive = false;
    GuildMasterIds g2 = ContactMenu_BuildGuildMaster();
    CHECK_EQ(g2.negotiate, 0); CHECK_EQ(g2.craft, 0);
    CHECK_EQ(g2.proof, 0); CHECK_EQ(g2.spy, 0); CHECK_EQ(g2.exam, 0);

    ContactLoops_SetGate(nullptr);
    ContactLoops_SetCommandSink(nullptr);
}

TEST(GuiContactLoops, Tavern) {
    ResetContactMenu();
    LoopSink sink; ContactLoops_SetCommandSink(&sink);

    TavernIds e = ContactMenu_BuildTavern();
    CHECK(e.dice && e.regulars && e.darkCorner);

    CHECK(ContactMenu_DispatchTavern(e.dice, e, false, 0));       CHECK_EQ(sink.cardGame, e.dice);
    CHECK(ContactMenu_DispatchTavern(e.regulars, e, false, 0));   CHECK_EQ(sink.stammtisch, e.regulars);
    CHECK(ContactMenu_DispatchTavern(e.darkCorner, e, false, 0)); CHECK_EQ(sink.darkCorner, e.darkCorner);

    // Trailing event branch (dword_63C7C0): 18 -> comment, 23 -> dark corner again.
    // The event path passes the (null) selected handle, so dark-corner runs with clicked==0.
    int prevComment = sink.comment;
    CHECK(ContactMenu_DispatchTavern(0, e, true, 18)); CHECK_EQ(sink.comment, prevComment + 1);
    CHECK(ContactMenu_DispatchTavern(0, e, true, 23)); CHECK_EQ(sink.darkCorner, 0);
    CHECK(ContactMenu_DispatchTavern(0, e, false, 18) == false);
    ContactLoops_SetCommandSink(nullptr);
}

TEST(GuiContactLoops, WineCellarAndTreasury) {
    ResetContactMenu();
    LoopSink sink; ContactLoops_SetCommandSink(&sink);
    LoopGate gate; ContactLoops_SetGate(&gate);

    int wine = WineCellar_BuildContact();
    CHECK(wine != 0);
    CHECK(WineCellar_DispatchContact(wine, wine)); CHECK_EQ(sink.wine, wine);
    CHECK(WineCellar_DispatchContact(0, wine) == false);

    int treas = CityTreasury_BuildContact();
    CHECK(treas != 0);
    CHECK(CityTreasury_DispatchContact(treas, treas)); CHECK_EQ(sink.treasury, treas);

    // Gate off: treasury entry not registered.
    ResetContactMenu();
    gate.treasury = false;
    CHECK_EQ(CityTreasury_BuildContact(), 0);

    ContactLoops_SetGate(nullptr);
    ContactLoops_SetCommandSink(nullptr);
}
