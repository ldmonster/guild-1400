#include "sim/debugcmd2.h"

namespace guild::sim {

// ===========================================================================
// Per-handler scale/base double constants, recovered from the gilde.exe data
// section (.rdata @0x6254xx). The originals load each as an 8-byte double; the
// "base" is the additive term applied to the RandomModulo roll, the "scale" is
// the fraction-of-wealth multiplier.
//
//   IfNotState14A 0x5713a4: base dbl_625424=2.0    scale dbl_62542C=0.01
//   IfNotState14B 0x5716a0: base 1.0(imm)          scale dbl_625444=0.01
//   ScaledB       0x571898: base dbl_625454=2.0    scale dbl_62545C=0.01
//   ScaledC       0x571990: base 1.0(imm)          scale dbl_625464=0.01
//   ScaledD       0x571bb4: base 1.0(imm)          scale dbl_62547C=0.01
//   ScaledE       0x571c98: base dbl_625484=2.0    scale dbl_62548C=0.01
//   ScaledF       0x571d7c: base 1.0(imm)          scale dbl_625494=0.0125
//   ScaledG       0x571e60: base dbl_62549C=2.0    scale dbl_6254A4=1/120
//   IfNotState14C 0x571f44: base 1.0(imm)          scale dbl_6254AC=0.01
//   ScaledH       0x572038: base dbl_6254B4=2.0    scale dbl_6254BC=0.01
//   ScaledI       0x572120: base 1.0(imm)          scale dbl_6254C4=0.01
//   CheckType     0x572204: base 1.0(imm)          scale dbl_6254CC=0.01
//   ScaledJ       0x572308: base 1.0(imm)          scale dbl_6254D4=0.01
//   ScaledK       0x572538: base 1.0(imm)          scale dbl_6254E4=0.01
//   ScaledL       0x57261c: base 1.0(imm)          scale dbl_6254EC=0.01
//   ScaledM       0x572700: base 1.0(imm)          scale dbl_6254F4=0.01
// ===========================================================================
namespace {

constexpr double kBase1   = 1.0;
constexpr double kBase2   = 2.0;        // dbl_625424/625454/625484/62549C/6254B4
constexpr double kScale01 = 0.01;       // most handlers
constexpr double kScaleF  = 0.0125;     // dbl_625494 (ScaledF)
constexpr double kScaleG  = 1.0 / 120.0;// dbl_6254A4 == 0.008333333333333333 (ScaledG)

// QueueRequest16 argument order: the originals emit either (personId, -1, ...)
// or (-1, personId, ...). The amount/market tail is identical.
enum class Q16Order { PersonFirst, MinusOneFirst };

// The shared tail every handler runs after the gate passes. `extraRollMod`>0
// consumes a second RandomModulo (the original feeds it to the GUI text-render
// only; we still advance the RNG to stay bit-faithful). Returns 0 (handled).
//
// NOTE: the original always rolls the MAIN modulo first, computes wealth/gold,
// THEN (where present) rolls the secondary modulo, THEN renders text / emits.
i32 RunScaledHandler(i32 personId, const DebugCmdPerson& p,
                     u16 rollMod, double base, double scale,
                     Q16Order order, u16 extraRollMod = 0) {
    const auto& h = GetDebugCmdHooks();

    int roll = DebugCmdRandomModulo(rollMod);
    i32 gold = DebugCmdScaledGold(p.wealth, roll, base, scale);

    if (extraRollMod) {
        // Secondary roll (RandomModulo): observable only as RNG advance + the
        // message argument; we consume it to preserve generator state.
        (void)DebugCmdRandomModulo(extraRollMod);
    }

    // Text_RenderFormattedMessage(buf, rec.id-derived textId, ...) — GUI leaf.
    // QueueRequest16(a1, a2, gold, market) — opcode-16 money command.
    if (h.queueRequest16) {
        if (order == Q16Order::PersonFirst)
            h.queueRequest16(personId, -1, gold, h.market);
        else
            h.queueRequest16(-1, personId, gold, h.market);
    }
    // He_SendEntityMessage(rec.id, rec.id, ...) — GUI/voice leaf.
    if (h.sendEntityMessage)
        h.sendEntityMessage(p.id, p.id, 3881);
    return kDbgHandled;
}

} // namespace

// ===========================================================================
// "Scaled" family. No eligibility gate beyond the person lookup.
// ===========================================================================

// gilde.exe 0x571898 — VIBE_DebugCmd_SpawnEntityScaledB (index 5).
//   roll=Math_RandomModulo(4u); gold=wealth*(roll+2.0)*0.01; second roll
//   Math_RandomModulo(6u) (+3, message arg); QueueRequest16(id,-1,...).
i32 DebugCmdSpawnEntityScaledB(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    return RunScaledHandler(personId, p, 4, kBase2, kScale01,
                            Q16Order::PersonFirst, /*extraRoll*/ 6);
}

// gilde.exe 0x571990 — VIBE_DebugCmd_SpawnEntityScaledC (index 6).
//   roll=Math_RandomModulo(3u); gold=wealth*(roll+1.0)*0.01; QR16(id,-1,...).
i32 DebugCmdSpawnEntityScaledC(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    return RunScaledHandler(personId, p, 3, kBase1, kScale01,
                            Q16Order::PersonFirst);
}

// gilde.exe 0x571bb4 — VIBE_DebugCmd_SpawnEntityScaledD (index 8).
//   roll=Math_RandomModulo(3u); gold=wealth*(roll+1.0)*0.01; QR16(id,-1,...).
i32 DebugCmdSpawnEntityScaledD(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    return RunScaledHandler(personId, p, 3, kBase1, kScale01,
                            Q16Order::PersonFirst);
}

// gilde.exe 0x571c98 — VIBE_DebugCmd_SpawnEntityScaledE (index 9).
//   roll=Math_RandomModulo(3u); gold=wealth*(roll+2.0)*0.01; QR16(id,-1,...).
i32 DebugCmdSpawnEntityScaledE(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    return RunScaledHandler(personId, p, 3, kBase2, kScale01,
                            Q16Order::PersonFirst);
}

// gilde.exe 0x571d7c — VIBE_DebugCmd_SpawnEntityScaledF (index 10).
//   roll=Math_RandomModulo(2u); gold=wealth*(roll+1.0)*0.0125; QR16(id,-1,...).
i32 DebugCmdSpawnEntityScaledF(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    return RunScaledHandler(personId, p, 2, kBase1, kScaleF,
                            Q16Order::PersonFirst);
}

// gilde.exe 0x571e60 — VIBE_DebugCmd_SpawnEntityScaledG (index 11).
//   roll=Math_RandomModulo(3u); gold=wealth*(roll+2.0)*(1/120); QR16(id,-1,...).
i32 DebugCmdSpawnEntityScaledG(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    return RunScaledHandler(personId, p, 3, kBase2, kScaleG,
                            Q16Order::PersonFirst);
}

// gilde.exe 0x572038 — VIBE_DebugCmd_SpawnEntityScaledH (index 13).
//   roll=Math_RandomModulo(3u); gold=wealth*(roll+2.0)*0.01; QR16(-1,id,...).
i32 DebugCmdSpawnEntityScaledH(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    return RunScaledHandler(personId, p, 3, kBase2, kScale01,
                            Q16Order::MinusOneFirst);
}

// gilde.exe 0x572120 — VIBE_DebugCmd_SpawnEntityScaledI (index 14).
//   roll=Math_RandomModulo(2u); gold=wealth*(roll+1.0)*0.01; QR16(-1,id,...).
i32 DebugCmdSpawnEntityScaledI(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    return RunScaledHandler(personId, p, 2, kBase1, kScale01,
                            Q16Order::MinusOneFirst);
}

// gilde.exe 0x572308 — VIBE_DebugCmd_SpawnEntityScaledJ (index 16).
//   gate: if (!rec[+358] && !rec[+361]) return 1024;
//   roll=Math_RandomModulo(4u); gold=wealth*(roll+1.0)*0.01; QR16(-1,id,...).
i32 DebugCmdSpawnEntityScaledJ(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    if (p.officeRank == 0 && p.officeAlt == 0)              // both zero -> 1024
        return kDbgIneligible;
    return RunScaledHandler(personId, p, 4, kBase1, kScale01,
                            Q16Order::MinusOneFirst);
}

// gilde.exe 0x572538 — VIBE_DebugCmd_SpawnEntityScaledK (index 18).
//   roll=Math_RandomModulo(2u); gold=wealth*(roll+1.0)*0.01; QR16(-1,id,...).
i32 DebugCmdSpawnEntityScaledK(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    return RunScaledHandler(personId, p, 2, kBase1, kScale01,
                            Q16Order::MinusOneFirst);
}

// gilde.exe 0x57261c — VIBE_DebugCmd_SpawnEntityScaledL (index 19).
//   roll=Math_RandomModulo(2u); gold=wealth*(roll+1.0)*0.01; QR16(-1,id,...).
i32 DebugCmdSpawnEntityScaledL(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    return RunScaledHandler(personId, p, 2, kBase1, kScale01,
                            Q16Order::MinusOneFirst);
}

// gilde.exe 0x572700 — VIBE_DebugCmd_SpawnEntityScaledM (index 20).
//   roll=Math_RandomModulo(3u); gold=wealth*(roll+1.0)*0.01; second roll
//   Math_RandomModulo(0xFu) (+2, message arg); QR16(-1,id,...).
i32 DebugCmdSpawnEntityScaledM(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    return RunScaledHandler(personId, p, 3, kBase1, kScale01,
                            Q16Order::MinusOneFirst, /*extraRoll*/ 15);
}

// ===========================================================================
// "IfNotState14" family. Gate: rec->officeRank(+358) == 14 -> 1024.
// ===========================================================================

// gilde.exe 0x5713a4 — VIBE_DebugCmd_SpawnEntityIfNotState14A (index 1).
//   if (rec[+358]==14) return 1024;
//   roll=Math_RandomModulo(3u); gold=wealth*(roll+2.0)*0.01; QR16(id,-1,...).
i32 DebugCmdSpawnEntityIfNotState14A(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    if (p.officeRank == 14) return kDbgIneligible;
    return RunScaledHandler(personId, p, 3, kBase2, kScale01,
                            Q16Order::PersonFirst);
}

// gilde.exe 0x5716a0 — VIBE_DebugCmd_SpawnEntityIfNotState14B (index 3).
//   if (rec[+358]==14) return 1024;
//   roll=Math_RandomModulo(3u); gold=wealth*(roll+1.0)*0.01; QR16(id,-1,...).
i32 DebugCmdSpawnEntityIfNotState14B(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    if (p.officeRank == 14) return kDbgIneligible;
    return RunScaledHandler(personId, p, 3, kBase1, kScale01,
                            Q16Order::PersonFirst);
}

// gilde.exe 0x571f44 — VIBE_DebugCmd_SpawnEntityIfNotState14C (index 12).
//   if (rec[+358]==14) return 1024;
//   roll=Math_RandomModulo(3u); gold=wealth*(roll+1.0)*0.01; QR16(-1,id,...).
i32 DebugCmdSpawnEntityIfNotState14C(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    if (p.officeRank == 14) return kDbgIneligible;
    return RunScaledHandler(personId, p, 3, kBase1, kScale01,
                            Q16Order::MinusOneFirst);
}

// ===========================================================================
// "CheckType" — gate: BuildingType_GroupFromCode(rec->buildType) == 7 -> 1024.
// gilde.exe 0x572204 — VIBE_DebugCmd_SpawnEntityCheckType (index 15).
//   roll=Math_RandomModulo(4u); gold=wealth*(roll+1.0)*0.01; QR16(-1,id,...).
// ===========================================================================
i32 DebugCmdSpawnEntityCheckType(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    if (p.buildType == 7) return kDbgIneligible;           // group == 7 gate
    return RunScaledHandler(personId, p, 4, kBase1, kScale01,
                            Q16Order::MinusOneFirst);
}

// ===========================================================================
// gilde.exe 0x57477c — VIBE_DebugCmd_RetFail1024 (index 40). return 1024.
// ===========================================================================
i32 DebugCmdRetFail1024() { return kDbgIneligible; }

// ===========================================================================
// funcs_5766CB index -> translated handler (this file's slice). debugcmd.cpp's
// DebugCmdNpcTableEntry owns indices 0/4/32/33; the indices below are added
// here. Returns nullptr for anything outside this file's set.
// ===========================================================================
i32 (*DebugCmd2NpcTableEntry(int npcActionIndex))(i32 personId) {
    switch (npcActionIndex) {
        case 1:  return DebugCmdSpawnEntityIfNotState14A; // 0x5713a4
        case 3:  return DebugCmdSpawnEntityIfNotState14B; // 0x5716a0
        case 5:  return DebugCmdSpawnEntityScaledB;       // 0x571898
        case 6:  return DebugCmdSpawnEntityScaledC;       // 0x571990
        case 8:  return DebugCmdSpawnEntityScaledD;       // 0x571bb4
        case 9:  return DebugCmdSpawnEntityScaledE;       // 0x571c98
        case 10: return DebugCmdSpawnEntityScaledF;       // 0x571d7c
        case 11: return DebugCmdSpawnEntityScaledG;       // 0x571e60
        case 12: return DebugCmdSpawnEntityIfNotState14C; // 0x571f44
        case 13: return DebugCmdSpawnEntityScaledH;       // 0x572038
        case 14: return DebugCmdSpawnEntityScaledI;       // 0x572120
        case 15: return DebugCmdSpawnEntityCheckType;     // 0x572204
        case 16: return DebugCmdSpawnEntityScaledJ;       // 0x572308
        case 18: return DebugCmdSpawnEntityScaledK;       // 0x572538
        case 19: return DebugCmdSpawnEntityScaledL;       // 0x57261c
        case 20: return DebugCmdSpawnEntityScaledM;       // 0x572700
        default: return nullptr;
    }
}

// ===========================================================================
// DEFERRED (touch raw global arrays / unbuilt subsystems; not cleanly stubbable
// while staying 1:1):
//   0x571498 SpawnEntityByOfficeCategory        Office_CollectByCategory + raw
//                                               word_12CE910 person-array scan
//   0x571a74 SpawnEntityFromHandlerList         Person_QueryBegin/IterNext
//   0x572400 SpawnEntityNearNearest             ObjectSearch_FindNearestEntity
//   0x572b24 SpawnEntityWithCoordReq            Person_QueryBegin + Coord27 loop
//   0x572d18 SpawnEntityCheckTypeWithCoord      raw byte_12CE912 array scan
//   0x5727f8 SpawnAtPaletteRangeObjectA    ObjectSearch_FindByPaletteRange +
//   0x572904 SpawnAtPaletteRangeObjectB    word_12CE910 + Coord27
//   0x572a14 SpawnAtPaletteRangeObjectC
//   0x572ed8 SpawnAtPaletteRangeObjectD
//   0x572fe8 SpawnAtPaletteRangeObjectE
//   0x5730f8 SpawnAtPaletteRangeObjectF
//   0x573204 SpawnFromOwnedListA           Person_QueryBegin + Building_* +
//   0x57340c SpawnFromOwnedListB           raw arrays
//   0x573618 SpawnFromOwnedListC
//   0x573930 QueueStateRequestPerHandler        He_FindFirstHandlerByFilter +
//   0x573bd8 QueueQuadEntityRequestPerHandler    delta-packet command builders
//   0x5740ec QueueScaledRequestPerHandler        (He handler iteration)
//   0x573ebc BroadcastMsgToHandlersA       He handler-list broadcast loops
//   0x574388 BroadcastMsgToHandlersB
//   0x57458c BroadcastMsgToHandlersC
//   0x574784 BroadcastMsgToHandlersD
//   0x5749f8 BroadcastMsgToHandlersE
//   0x574c3c SpawnAndResetSlotFromList          owned-list + slot reset
//   0x574e88 SendMsgPerOwnedItem                owned-item iteration
//   0x57502c ResetSlotsFromOwnedList            owned-list + slot reset
//   0x575274 RequestBuildOpPerOwnedItem         owned-item iteration
// ===========================================================================

} // namespace guild::sim
