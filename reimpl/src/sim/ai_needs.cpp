// ai_needs.cpp — 1:1 reconstruction of VIBE_AiNeeds_BuildScoreTable (gilde.exe
// 0x4764e8) and the DFN overlay of VIBE_AiMethod_LoadDataFile's read path.
// See ai_needs.h for full scope/provenance.
#include "sim/ai_needs.h"

#include <cstring> // std::memset, std::memcpy

namespace guild::sim {

// ---------------------------------------------------------------------------
// The fixed 60-method definition table — gilde.exe code literals at
// 0x4764fd..0x4783e2, one block per VIBE_AiMethod_RegisterFromIni call. Columns:
//   id      : the var_AC immediate (NOT strictly sequential — see ids 12/13/60)
//   name    : the code-literal section/record name (copied into record +1)
//   scorer  : +36 score evaluator fn VA (var_88)
//   execA   : +40 eval/can-action fn VA (var_84)
//   execB   : +44 apply/perform fn VA (var_80)
//   flag    : +144 category/eligibility word (var_1C)
// All fn VAs are original gilde.exe addresses (opaque dispatch tokens). Symbol
// names are in the trailing comments for readability.
// ---------------------------------------------------------------------------
const std::array<AiNeedsMethodDef, kAiNeedsMethodCount> kAiNeedsMethodDefs = {{
    // id, name,                            scorer,   execA,    execB,    flag
    { 1, "DummyUnversehrtheit",         0x4796b0, 0x469de0, 0x469df0, 0x001}, // RelWeighted/EvalReturnBoolNotArmed/StubReturn1
    { 2, "DummyBeruf",                  0x4796b0, 0x469df4, 0x469e04, 0x001}, // .../EvalReturnTwo/StubReturn2
    { 3, "DummyVergnuegen",             0x4796b0, 0x469e08, 0x469e18, 0x001}, // .../EvalReturnThree/StubReturn3
    { 4, "Bildung",                     0x4796b0, 0x469e1c, 0x46a488, 0x001}, // .../EvalGoToDrink/ApplyDrinkAction
    { 5, "Bildung (Beruf)",             0x4796b0, 0x46a4d8, 0x46abd8, 0x001}, // .../EvalGoToEat/ApplyEatAction
    { 6, "DummyRechtschaffenheit",      0x4796b0, 0x469d9c, 0x469db4, 0x001}, // .../EvalReturnSix/StubReturn6
    { 7, "DummySicherheit",             0x4796b0, 0x469db8, 0x469dc8, 0x001}, // .../EvalReturnSeven/StubReturn7
    { 8, "DummyFortpflanzung",          0x4796b0, 0x469dcc, 0x469ddc, 0x001}, // .../EvalReturnEight/StubReturn8
    { 9, "Bestechung",                  0x46ac24, 0x46af90, 0x46b544, 0x019}, // EvalMoveToBuilding/EvalEquipWeapon/EvalSendMessage
    {10, "Amtsbewerbung",               0x46b654, 0x46b6dc, 0x46b8a8, 0x01b}, // ComputeApproachOffset/EvalDuelChallenge/EvalApplyForCandidacy
    {11, "Objekt kaufen",               0x4796b0, 0x46b8c8, 0x46bcc0, 0x001}, // RelWeighted/EvalGiveGift/RequestSellObject
    {13, "Objekt kaufen (AP)",          0x4796b0, 0x469da8, 0x46b8b0, 0x001}, // .../StubReturnZero/EvalRejectStub
    {14, "Raum kaufen",                 0x4796b0, 0x46be8c, 0x46bff0, 0x0ff}, // .../EvalBuyObject/Trade_RequestSellObjekt
    {15, "Repair Building",             0x4796b0, 0x46c0c0, 0x46c30c, 0x0ff}, // .../FindNearestTarget/PerformRenovate
    {16, "Wohnsitz kaufen",             0x4796b0, 0x46c3d0, 0x46c97c, 0x003}, // .../EvalEnterBuilding/Building_EvalBuyBuilding
    {17, "Wohnsitz bauen",              0x4796b0, 0x46c9d4, 0x46cfc4, 0x003}, // .../EvalLeaveBuilding/PerformBuildingUpgrade
    {18, "Wohnsitz upgraden (Credits)", 0x4796b0, 0x46d0bc, 0x46b8b0, 0x003}, // .../EvalAssignPatrol/EvalRejectStub
    {19, "Betrieb bauen",               0x4796b0, 0x46d1a8, 0x46d978, 0x003}, // .../EvalBuildingAction/PerformBuildingUpgradeOnObject
    {20, "Betrieb upgraden (AP)",       0x4796b0, 0x469da8, 0x46b8b0, 0x003}, // .../StubReturnZero/EvalRejectStub
    {21, "Betrieb upgraden (Credits)",  0x4796b0, 0x46dad8, 0x46b8b0, 0x003}, // .../EvalAssignDestination/EvalRejectStub
    {22, "Medicus ausfsuchen",          0x4796b0, 0x46dbf0, 0x46dc3c, 0x03b}, // .../EvalEnterTavern/EvalEnterTavernDirect
    {23, "Sabotieren",                  0x46dc80, 0x46dcf8, 0x46e1ec, 0x1c3}, // ComputeMoveTargetA/EvalPickTarget/PerformSabotage
    {24, "Verpruegeln",                 0x46e2c4, 0x46e33c, 0x46e7b4, 0x1c9}, // ComputeMoveTargetB/EvalAttackTarget/PerformBeating
    {25, "Buergerrecht",                0x4796b0, 0x46e8a0, 0x46e91c, 0x01b}, // .../EvalUseDoor/PerformBroadcastSummon
    {26, "ApsSparen",                   0x4796b0, 0x46eb10, 0x46ebfc, 0x001}, // .../EvalRestSlotFree/EvalRestStub
    {27, "UseObject (Military)",        0x4796b0, 0x46ec00, 0x46ee50, 0x003}, // .../EvalSelectWorker/OrientToActionTarget27
    {28, "UseObject (Ansehen)",         0x4796b0, 0x46ef04, 0x46f1fc, 0x019}, // .../EvalChooseGesture/OrientToActionTarget28
    {29, "UseObject (APS)",             0x4796b0, 0x46f2a4, 0x46f4d0, 0x039}, // .../EvalChooseTalkAction/OrientToActionTarget29
    {30, "UseObject (Bildung)",         0x4796b0, 0x46f578, 0x46f758, 0x03b}, // .../EvalChooseFlirtAction/OrientToActionTarget30
    {31, "UseObject (Unversehrtheit)",  0x4796b0, 0x46f7fc, 0x46f9b0, 0x03f}, // .../EvalChooseDrinkAction/OrientToActionTarget31
    {32, "UseObject (Dynastie)",        0x4796b0, 0x46fa54, 0x46fe58, 0x017}, // .../EvalChooseGroupAction/OrientToActionTarget32
    {33, "UseObject (Gemeinheit)",      0x4796b0, 0x46ff00, 0x470510, 0x0c9}, // .../EvalChooseSocialGesture/OrientQuadToTarget33
    {34, "UseObject (Sicherheit)",      0x4796b0, 0x470620, 0x4708f0, 0x039}, // .../EvalChooseInsultAction/OrientToActionTarget34
    {35, "Use Parfuem",                 0x470998, 0x470a18, 0x470a30, 0x0ff}, // ComputeMoveTargetSocial/EvalActionCode35/OrientQuadToTarget35
    {36, "Use Trank",                   0x470b68, 0x470be8, 0x470c00, 0x0ff}, // ComputeMoveTargetSocialAlt/EvalActionCode36/AimTurretToward
    {37, "Spionage",                    0x4796b0, 0x470d38, 0x470ea4, 0x069}, // .../EvaluateShoot/PerformSpionage
    {12, "Objekt Kaufen (Markt)",       0x4796b0, 0x470f24, 0x471000, 0x0ff}, // .../EvaluateThrow/RequestSellObjekt
    {38, "Ablass Kaufen",               0x4796b0, 0x4710c4, 0x471380, 0x039}, // .../EvaluateThrowAtRival/EvaluateUseItemOnTarget
    {39, "UseAmt",                      0x4796b0, 0x47156c, 0x4715d8, 0x0ff}, // .../EvaluateIdleStand/AiIntrigue_EvalActionLabel
    {40, "Drohen",                      0x4715e8, 0x471650, 0x471840, 0x05d}, // EvaluateMoveTo/EvaluateEnterBuilding/PerformEnterBuilding
    {41, "Pamphlete",                   0x471850, 0x4718d4, 0x471ae0, 0x05d}, // EvaluateMoveGuarded/EvaluateUseFront/AiPlayer_EvalPamphlet
    {42, "Anschwaerzen",                0x4796b0, 0x471b10, 0x471cb4, 0x039}, // .../EvaluateUseBack/PerformUseBack
    {43, "Beleidigen",                  0x471ce4, 0x471d68, 0x471dfc, 0x0c9}, // EvaluateMoveToSecondary/EvaluateOpenDoorLarge/PerformOpenDoorLarge
    {44, "Erpressen",                   0x471e0c, 0x471e90, 0x471f24, 0x0c9}, // EvaluateMoveToTertiary/EvaluateOpenDoorSmall/PerformOpenDoorSmall
    {45, "UsePrivs",                    0x4796b0, 0x471f34, 0x471fa0, 0x0ff}, // .../EvaluateCloseDoor/AiIntrigue_EvalSlanderLabel
    {46, "Buergermeister",              0x471fb0, 0x472068, 0x4725c0, 0x039}, // EvaluateApproachMarket/EvaluateGuardPatrol/BuildWorkerQuarters
    {47, "Baumeister",                  0x4796b0, 0x4730cc, 0x473448, 0x03b}, // .../EvaluateBribeJailed/BuildWell
    {48, "Buergermeister upgrade",      0x4796b0, 0x472720, 0x46b8b0, 0x039}, // .../EvaluateHirePersonnel/EvalRejectStub
    {49, "Buergermeister Rathaus",      0x4796b0, 0x47285c, 0x472b90, 0x039}, // .../EvaluateRecruitWorker/UpgradeTownHall
    {60, "Buergermeister Kerker",       0x4796b0, 0x472c8c, 0x472fd0, 0x039}, // .../EvaluateRecruitFromBuilding/UpgradeDungeon
    {50, "Dunkle Ecke",                 0x4735f8, 0x473700, 0x4737cc, 0x069}, // EvaluateApproachShop/EvaluateShopInteract/PerformShopTransaction
    {51, "Kredit Nehmen",               0x4796b0, 0x4737fc, 0x473e00, 0x007}, // .../EvaluateCombatTarget/GrantAiCredit
    {52, "Change Profession",           0x473f8c, 0x474070, 0x4742ec, 0x007}, // EvaluateApproachTavern/EvaluateAssignProfession/PerformDrinkTavern
    {53, "Stammtisch",                  0x4796b0, 0x474340, 0x4746f8, 0x019}, // .../EvaluateSocializeGroup/TavernJoinLeave
    {54, "Zunft Preismanipulation",     0x4796b0, 0x4747dc, 0x474c18, 0x003}, // .../EvaluateCourtTrial/PerformVerdict
    {55, "Zunft Aufruhr",               0x4796b0, 0x474c4c, 0x474da0, 0x043}, // .../EvaluateArrest/PerformArrest
    {56, "Zunft Level 3",               0x4796b0, 0x475638, 0x4756c4, 0x04b}, // .../NpcTarget_EvalCombatOrMoveAction/PerformPickupCarry
    {57, "Upgrade Zunft",               0x475700, 0x4757e8, 0x475c74, 0x01b}, // EvalGuildhallTarget/PlanGuildhallUpgrade/HandleGuildhallTrigger
    {58, "Berufsausbildung",            0x475dd0, 0x475f48, 0x476140, 0x01f}, // EvalBestPersonTarget/PlanPersonInteraction/HandleObjectType23
    {59, "Studium",                     0x4796b0, 0x4761a0, 0x476440, 0x013}, // .../EvalNeedFulfillTarget/HandleObjectType14
}};

namespace {

// Fill `entry`'s id/name/fixed fields from `def` (the zeroed-record + field writes
// at the head of each build block: 0x4764fd.. memset, then the field stores).
void WriteFixedFields(AiNeedsCatalogEntry& entry, const AiNeedsMethodDef& def) {
    entry = AiNeedsCatalogEntry{};                 // memset(record, 0, 0x94)
    entry.id = def.id;                             // record +0
    std::memset(entry.name, 0, sizeof(entry.name));
    if (def.name) {
        // Copy NUL-terminated, bounded by the 32-byte name field (matches the
        // 2-byte-per-iteration copy loop, which stops at the NUL).
        std::size_t n = 0;
        while (def.name[n] && n < sizeof(entry.name) - 1) {
            entry.name[n] = def.name[n];
            ++n;
        }
        entry.name[n] = '\0';
    }
    entry.scorer = def.scorer;                     // record +36 (var_88)
    entry.execA  = def.execA;                      // record +40 (var_84)
    entry.execB  = def.execB;                      // record +44 (var_80)
    entry.flag   = def.flag;                        // record +144 (var_1C)
}

// Run RegisterFromIni's desire-slot fill on `entry` (reuses the registry kernel).
// Projects the catalog entry's id/name/slots into an AiMethodRecord, runs the
// fill, and copies the slots back. Returns RegisterFromIni's commit flag.
int FillDesireSlots(AiNeedsCatalogEntry& entry, bool haveIni, const IniSource& ini) {
    AiMethodRecord rec{};
    rec.id = entry.id;
    std::memcpy(rec.name, entry.name, sizeof(rec.name));
    // Carry any pre-existing slot values (the binary's record was zeroed, so these
    // start at {-1,0}; AiMethodRecord defaults already give attr=-1/change=0).
    int ok = AiMethod_RegisterFromIni(rec, haveIni, ini, nullptr);
    if (ok) {
        for (int i = 0; i < kAiMethodDesireSlots; ++i) {
            entry.shortSlots[i] = rec.shortSlots[i];
            entry.prevSlots[i]  = rec.prevSlots[i];
            entry.longSlots[i]  = rec.longSlots[i];
        }
    }
    return ok;
}

const IniSource kNullIniSource{}; // all-null: no keys present (shipped no-INI path)

} // namespace

// ---------------------------------------------------------------------------
// gilde.exe 0x4764e8 — VIBE_AiNeeds_BuildScoreTable.
// ---------------------------------------------------------------------------
int AiNeeds_BuildScoreTable(bool haveIni, const AiNeedsIniBinder& binder,
                            AiNeedsCatalog& out) {
    out = AiNeedsCatalog{}; // catalog starts zeroed (slot 0 stays the unused slot)

    for (int i = 0; i < kAiNeedsMethodCount; ++i) {
        const AiNeedsMethodDef& def = kAiNeedsMethodDefs[i];

        // The binary builds the record on the stack, then RegisterFromIni commits
        // it to catalog[148*id]. We build directly into the catalog slot.
        if (def.id >= kAiNeedsRecordCount)
            return 0; // id-gate guard (never hit by the genuine table)
        AiNeedsCatalogEntry& entry = out[def.id];

        WriteFixedFields(entry, def);

        // Bind the per-method INI source (section == method name) for the fill.
        IniSource ini = kNullIniSource;
        bool useIni = haveIni;
        if (haveIni && binder.bind) {
            if (!binder.bind(def, ini, binder.ctx))
                useIni = false; // binder declined -> treat as no keys present
        } else {
            useIni = false;
        }

        // RegisterFromIni: fill desire slots + commit. Abort the whole build on a
        // 0 return (the binary's `test eax,eax; jz return-0`).
        if (!FillDesireSlots(entry, useIni, useIni ? ini : kNullIniSource))
            return 0;
    }
    return 1; // all 60 committed (0x4783ef: mov eax, 1)
}

int AiNeeds_BuildScoreTable(AiNeedsCatalog& out) {
    AiNeedsIniBinder none{};
    return AiNeeds_BuildScoreTable(false, none, out);
}

// ---------------------------------------------------------------------------
// DFN overlay — VIBE_AiMethod_LoadDataFile read path (0x468a40).
// Per-record on-disk layout (73 bytes), in field-schedule order:
//   id(1) name(32) [shortAttr(1) shortChange(4)]x4 [longAttr(1) longChange(4)]x4
// then MemMove(+80, +48, 32): the prev vector mirrors the 4 short slots.
// ---------------------------------------------------------------------------
namespace {
f32 ReadF32LE(const u8* p) {
    u32 bits = static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
               (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
    f32 v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}
} // namespace

int AiNeeds_OverlayFromDfn(const u8* dfn, std::size_t dfnLen, AiNeedsCatalog& out) {
    if (!dfn)
        return 0;
    int n = 0;
    for (int r = 0; r < kAiNeedsRecordCount; ++r) {
        std::size_t base = static_cast<std::size_t>(r) * kAiNeedsDfnRecordBytes;
        if (base + kAiNeedsDfnRecordBytes > dfnLen)
            break; // first short read stops the load (the binary's failed ReadStream)

        const u8* p = dfn + base;
        AiNeedsCatalogEntry& e = out[r];
        e.id = p[0];
        std::memcpy(e.name, p + 1, kAiNeedsNameMax); // 32 bytes (may be NUL-padded)
        e.name[kAiNeedsNameMax - 1] = '\0';

        const u8* q = p + 33;
        for (int s = 0; s < kAiMethodDesireSlots; ++s) {
            e.shortSlots[s].attrIndex = static_cast<i8>(q[0]);
            e.shortSlots[s].change    = ReadF32LE(q + 1);
            q += 5;
        }
        for (int s = 0; s < kAiMethodDesireSlots; ++s) {
            e.longSlots[s].attrIndex = static_cast<i8>(q[0]);
            e.longSlots[s].change    = ReadF32LE(q + 1);
            q += 5;
        }
        // MemMove(+80, +48, 32): prev vector mirrors the 4 short slots.
        for (int s = 0; s < kAiMethodDesireSlots; ++s)
            e.prevSlots[s] = e.shortSlots[s];
        ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x468a40 — VIBE_AiMethod_LoadDataFile (shipped read path).
// ---------------------------------------------------------------------------
int AiNeeds_LoadDataFile(const u8* decompressedDfn, std::size_t dfnLen,
                         AiNeedsCatalog& out) {
    // Step 2: VIBE_AiNeeds_BuildScoreTable(0) — fixed fields, no INI.
    if (!AiNeeds_BuildScoreTable(out))
        return 0;                                  // 0x468d33: jz -> CloseStream, ret 0
    // Step 3: overlay all 61 records (the field schedule + prev-mirror).
    int overlaid = AiNeeds_OverlayFromDfn(decompressedDfn, dfnLen, out);
    // Step 4: the binary's loop returns 1 only after all 61 records read (0x468cde).
    return (overlaid == kAiNeedsRecordCount) ? 1 : 0;
}

} // namespace guild::sim
