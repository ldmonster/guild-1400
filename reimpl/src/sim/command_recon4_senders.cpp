#include "sim/command_recon4_senders.h"

#include <cstring>

#include <vector>

#include "sim/building_type.h"   // BuildingType_GroupFromCode (0x58a4c8)
#include "sim/combat_drivers.h"  // FindFirstFreeFormationSlot / kFormOp* (0x48980c cores)
#include "sim/gametime.h"        // GameTimeAdvance            (0x583150)
#include "sim/types.h"           // GameTime (qword_13CE852 record shape)
#include "util/math_random.h"    // util::RandomModulo         (0x58b89c)

namespace guild::sim {

namespace {

void* DefPersonQueryBegin(i32, i32, i32, i32) { return nullptr; }
void* DefPersonFindRecordById(i32) { return nullptr; }
void* DefMapViewPanel(i32, const void*, const char*) { return nullptr; }
void* DefOfficeOverview(const void*, const char*, i32) { return nullptr; }
i16   DefRecTypeWord(void*) { return -1; }
i32   DefRecEntityId(void*) { return 0; }
u8    DefRecStatusKind(void*) { return 0; }
u8    DefTblKind(u16) { return 0; }
i32   DefTblId(u16) { return 0; }
i32   DefCmdToId(void*) { return 0; }
void  DefEmit(const Recon4EmitEvent*) {}

// --- wave-2 inert defaults --------------------------------------------------
const u8* DefFindSlotByProt(u8, i16) {
    // Inert stand-in for the live slot table (dword_13C3B50); the original
    // would fault on a true miss, but every live caller guarantees a hit (see
    // 0x481f5f / 0x584700). A zero slot degrades the deltas to the rec's raw
    // values, keeping the math well-defined headless.
    static const u8 kZeroSlot[0x80] = {};
    return kZeroSlot;
}
i32   DefEnqueuePacket(const u8*) { return -1; } // dword_764CF0 latch path of 0x49388c
u8    DefPersonStatByte(void*, u32) { return 0; }
i32   DefPersonSlotId(i32) { return 0; }
i8    DefPersonSlotBuildingCode(i32) { return 0; }
u8    DefSeasonByte6477A1() { return 0; }
void* DefPersonIterNext() { return nullptr; }
u16   DefLocalPlayerTypeWord() { return 0; }
u16   DefLocalPlayerIndex() { return 0; }
void* DefResolveOwnerOrParentB(void*) { return nullptr; }
u16   DefTblRowTypeWord(u16) { return 0xFFFF; }
void* DefTblRowOwnerRec(u16) { return nullptr; }
void  DefGameTimeNow(void* out14) { std::memset(out14, 0, 14); }
i32   DefQueueRequest39(const u8*) { return 0; }
// --- wave-2 part-3 inert defaults (CombatSetUnitFormationMode leaves) -------
void* DefPlayerSlotUnit(i32) { return nullptr; }
u16   DefUnitProtWord(void*) { return 0; }
i32   DefUnitStock24(void*) { return 0; }
void  DefResetObjectHighlights(void*) {}
float DefAdjustStockAndNotify(u16, i32) { return -1.0f; } // 0x57d780 miss path

const Recon4SenderHooks kDefaults = {
    &DefPersonQueryBegin, &DefPersonFindRecordById, &DefMapViewPanel,
    &DefOfficeOverview, &DefRecTypeWord, &DefRecEntityId, &DefRecStatusKind,
    &DefTblKind, &DefTblId, &DefCmdToId, &DefEmit,
    // wave-2 (positional, matching the struct order):
    &DefFindSlotByProt, &DefEnqueuePacket, &DefPersonStatByte, &DefPersonSlotId,
    &DefPersonSlotBuildingCode, &DefSeasonByte6477A1, &DefPersonIterNext,
    &DefLocalPlayerTypeWord, &DefLocalPlayerIndex, &DefResolveOwnerOrParentB,
    &DefTblRowTypeWord, &DefTblRowOwnerRec, &DefGameTimeNow, &DefQueueRequest39,
    // wave-2 part 3 (0x48980c leaves):
    &DefPlayerSlotUnit, &DefUnitProtWord, &DefUnitStock24,
    &DefResetObjectHighlights, &DefAdjustStockAndNotify,
};

Recon4SenderHooks g_hooks = kDefaults;

inline void emit(const Recon4EmitEvent& ev) { g_hooks.emit(&ev); }

} // namespace

void SetRecon4SenderHooks(const Recon4SenderHooks* h) {
    if (!h) { g_hooks = kDefaults; return; }
    g_hooks = *h;
    if (!g_hooks.personQueryBegin)     g_hooks.personQueryBegin = kDefaults.personQueryBegin;
    if (!g_hooks.personFindRecordById) g_hooks.personFindRecordById = kDefaults.personFindRecordById;
    if (!g_hooks.mapViewPanel)         g_hooks.mapViewPanel = kDefaults.mapViewPanel;
    if (!g_hooks.officeOverview)       g_hooks.officeOverview = kDefaults.officeOverview;
    if (!g_hooks.recTypeWord)          g_hooks.recTypeWord = kDefaults.recTypeWord;
    if (!g_hooks.recEntityId)          g_hooks.recEntityId = kDefaults.recEntityId;
    if (!g_hooks.recStatusKind)        g_hooks.recStatusKind = kDefaults.recStatusKind;
    if (!g_hooks.tblKind)              g_hooks.tblKind = kDefaults.tblKind;
    if (!g_hooks.tblId)                g_hooks.tblId = kDefaults.tblId;
    if (!g_hooks.cmdToId)              g_hooks.cmdToId = kDefaults.cmdToId;
    if (!g_hooks.emit)                 g_hooks.emit = kDefaults.emit;
    // wave-2 leaves:
    if (!g_hooks.findSlotByProt)             g_hooks.findSlotByProt = kDefaults.findSlotByProt;
    if (!g_hooks.enqueuePacket)              g_hooks.enqueuePacket = kDefaults.enqueuePacket;
    if (!g_hooks.personStatByte)             g_hooks.personStatByte = kDefaults.personStatByte;
    if (!g_hooks.personSlotId)               g_hooks.personSlotId = kDefaults.personSlotId;
    if (!g_hooks.personSlotBuildingCode)     g_hooks.personSlotBuildingCode = kDefaults.personSlotBuildingCode;
    if (!g_hooks.seasonByte6477A1)           g_hooks.seasonByte6477A1 = kDefaults.seasonByte6477A1;
    if (!g_hooks.personIterNext)             g_hooks.personIterNext = kDefaults.personIterNext;
    if (!g_hooks.localPlayerTypeWord)        g_hooks.localPlayerTypeWord = kDefaults.localPlayerTypeWord;
    if (!g_hooks.localPlayerIndex)           g_hooks.localPlayerIndex = kDefaults.localPlayerIndex;
    if (!g_hooks.resolveOwnerOrParentB)      g_hooks.resolveOwnerOrParentB = kDefaults.resolveOwnerOrParentB;
    if (!g_hooks.tblRowTypeWord)             g_hooks.tblRowTypeWord = kDefaults.tblRowTypeWord;
    if (!g_hooks.tblRowOwnerRec)             g_hooks.tblRowOwnerRec = kDefaults.tblRowOwnerRec;
    if (!g_hooks.gameTimeNow)                g_hooks.gameTimeNow = kDefaults.gameTimeNow;
    if (!g_hooks.queueRequest39)             g_hooks.queueRequest39 = kDefaults.queueRequest39;
    // wave-2 part-3 leaves (0x48980c):
    if (!g_hooks.playerSlotUnit)             g_hooks.playerSlotUnit = kDefaults.playerSlotUnit;
    if (!g_hooks.unitProtWord)               g_hooks.unitProtWord = kDefaults.unitProtWord;
    if (!g_hooks.unitStock24)                g_hooks.unitStock24 = kDefaults.unitStock24;
    if (!g_hooks.resetObjectHighlights)      g_hooks.resetObjectHighlights = kDefaults.resetObjectHighlights;
    if (!g_hooks.adjustStockAndNotify)       g_hooks.adjustStockAndNotify = kDefaults.adjustStockAndNotify;
}
const Recon4SenderHooks& GetRecon4SenderHooks() { return g_hooks; }

// gilde.exe 0x493a08 — VIBE_Command_RetZero
int CommandRetZero() { return 0; }

// ===========================================================================
// 0x5675ac — SendEntityActionA. Disease/contagion notify. Two arms keyed on the
// command status byte *(cmd+2): ==6 picks a target via MapView panel (msg 151);
// else via PersonQueryBegin(cmd,1,1,params[1]). Both arms: require a resolved
// record with a nonzero typeWord (+39); then emit a SlotReset28 block carrying
// cmd 118 / submsg 375 with the resolved entity id (*(rec+1)), a BeginDeltaPacket
// keyed on *params / *(*params+2), an AppendDeltaField(4,1, rec+1, *params+37),
// QueueRequestState22; finally if the target's table kind (byte_12CE912) is 6 or
// 7 (carried/diseased), SendEntityMessage(tblId, ... 1418). Returns 1 on success.
// ===========================================================================
int SendEntityActionA(u8* cmd, i32* params) {
    void* tgt;
    if (cmd[2] == 6) {
        tgt = g_hooks.mapViewPanel(1, nullptr, nullptr); // msg 151
        if (!tgt || g_hooks.recTypeWord(tgt) == 0) return 0;
    } else {
        tgt = g_hooks.personQueryBegin((i32)(intptr_t)cmd, 1, 1, params[1]);
        if (!tgt || g_hooks.recTypeWord(tgt) == 0) return 0;
    }

    i32 cmdTo = g_hooks.cmdToId(cmd);
    i32 entId = g_hooks.recEntityId(tgt);

    // SlotReset28 packet header: command byte 118, submsg 375, value = entId.
    emit({Recon4Emit::SlotReset28, /*cmd*/118, /*arg*/(cmd[2] == 6 ? -1 : -1), /*entId*/entId, /*submsg*/375, cmdTo});
    // BeginDeltaPacket(*params, *(*params+2)).
    emit({Recon4Emit::BeginDeltaPacket, params[0], 0});
    // AppendDeltaField(4, 1, &(rec+1)=entId, slotOff = (*params word)+37-dword_11AA474).
    emit({Recon4Emit::AppendDeltaField, /*width*/4, /*count*/1, /*valueSrc*/entId, /*slotOff*/37});
    emit({Recon4Emit::QueueRequestState22});

    // 0x567745 / 0x567792: indexed by the FULL u16 typeWord (*(u16*)(rec+39)),
    // not a u8 — byte_12CE912[536*w] / dword_12CE914[134*w].
    u16 w = (u16)g_hooks.recTypeWord(tgt);
    u8 k = g_hooks.tblKind(w);
    if (k == 6 || k == 7) {
        emit({Recon4Emit::SendEntityMessage, /*id*/g_hooks.tblId(w), -1, 0, 1418, 0});
    }
    return 1;
}

// ===========================================================================
// 0x567944 — SendEntityActionB. Like A but emits TWO SlotReset28 blocks
// (cmd 118 then cmd 119) and an extra QueueRequestArgs25(entId,90,32,2,0). The
// msg is 152; carried-message id is 3255. Arm split on *(cmd+2)==6 (MapView) vs
// PersonQueryBegin(src,1,1,params[1]).
// ===========================================================================
int SendEntityActionB(u8* cmd, i32* params, i32 src) {
    void* tgt;
    if (cmd[2] == 6) {
        tgt = g_hooks.mapViewPanel(1, nullptr, nullptr); // msg 152
        if (!tgt) return 0;
    } else {
        tgt = g_hooks.personQueryBegin(src, 1, 1, params[1]);
        if (!tgt) return 0;
    }

    i32 cmdTo = g_hooks.cmdToId(cmd);
    i32 entId = g_hooks.recEntityId(tgt);

    // First block: cmd 118, submsg 376.
    emit({Recon4Emit::SlotReset28, 118, (cmd[2] == 6 ? 10 : -1), entId, 376, cmdTo});
    // Second block: cmd 119, sub 1.
    emit({Recon4Emit::SlotReset28, 119, (cmd[2] == 6 ? 30464 : 1), entId, 0, cmdTo});
    emit({Recon4Emit::BeginDeltaPacket, params[0], 0});
    emit({Recon4Emit::AppendDeltaField, 4, 1, entId, 37});
    emit({Recon4Emit::QueueRequestState22});
    emit({Recon4Emit::QueueRequestArgs25, entId, 90, 32, 2, 0});

    i16 w16 = g_hooks.recTypeWord(tgt);
    if ((u16)w16 != 0xFFFF) {
        u8 k = g_hooks.tblKind((u16)w16);
        if (k == 6 || k == 7) {
            emit({Recon4Emit::SendEntityMessage, g_hooks.tblId((u16)w16), -1, 0, 1418, 0});
        }
    }
    return 1;
}

// ===========================================================================
// 0x567e24 — SendEntityActionC. Office-overview action. Arm on *(cmd+2)==6 uses
// Amt_RunOfficeOverviewWindow (msg 153); else PersonFindRecordById(*(params+4)).
// Emits one SlotReset28 (cmd -126, submsg fields 2/3/2) with the resolved entity
// id (*(rec+4)/+1). If the chosen record's status kind (+2) is 6 or 7, fires
// SendEntityMessage(recId, ... 1418) with carried-message id 3261.
// ===========================================================================
int SendEntityActionC(u8* cmd, i32* params) {
    void* tgt; i32 entId; u8 statusKind;
    if (cmd[2] == 6) {
        tgt = g_hooks.officeOverview(nullptr, nullptr, 0); // msg 153
        if (!tgt) return 0;
        entId = g_hooks.cmdToId(tgt);            // *(v4+4)
        statusKind = g_hooks.recStatusKind(cmd); // *(v8+2) — the command record
    } else {
        tgt = g_hooks.personFindRecordById(params[1]); // *(params+4)
        if (!tgt) return 0;
        entId = g_hooks.recEntityId(tgt);        // *(rec+1)
        statusKind = g_hooks.recStatusKind(cmd);
    }

    // SlotReset28 header: cmd byte -126 (0x82), submsg bytes 2/3/2.
    emit({Recon4Emit::SlotReset28, /*cmd*/(i32)(i8)-126, /*f1*/2, entId, /*f2*/3, /*f3*/2});

    if (statusKind == 6 || statusKind == 7) {
        emit({Recon4Emit::SendEntityMessage, entId, -1, 0, 1418, 0}); // msg 3261
    }
    return 1;
}

// ===========================================================================
// 0x56801c — SendEntityActionD. Like C: arm on *(cmd+2)==6 -> office overview
// (msg 153); else PersonFindRecordById(*(params+4)). The ==6 arm emits a
// SlotReset28 (cmd -126, fields 2/0/4) with entity id (*(rec+4)); the else arm
// emits NO packet — it only fires the carried SendEntityMessage (msg 3259) when
// the record status kind (+2) is 6 or 7.
// ===========================================================================
int SendEntityActionD(u8* cmd, i32* params, i32 aux) {
    (void)aux;
    if (cmd[2] == 6) {
        void* tgt = g_hooks.officeOverview(nullptr, nullptr, 0); // msg 153
        if (!tgt) return 0;
        i32 entId = g_hooks.cmdToId(tgt); // *(v5+4)
        u8 statusKind = g_hooks.recStatusKind(cmd);
        emit({Recon4Emit::SlotReset28, (i32)(i8)-126, 2, entId, 0 /*f2*/, 4 /*f3*/});
        if (statusKind == 6 || statusKind == 7) {
            emit({Recon4Emit::SendEntityMessage, entId, -1, 0, 1418, 0}); // msg 3259
        }
        return 1;
    } else {
        void* tgt = g_hooks.personFindRecordById(params[1]);
        if (!tgt) return 0;
        u8 statusKind = g_hooks.recStatusKind(tgt); // *(rec+2)
        if (statusKind == 6 || statusKind == 7) {
            emit({Recon4Emit::SendEntityMessage, g_hooks.recEntityId(tgt), -1, 0, 1418, 0});
        }
        return 1;
    }
}

// ===========================================================================
// 0x568278 — SendMapEntityAction. Resolves a target (MapView msg 152 when
// *(cmd+2)==6, else PersonQueryBegin(src,1,1,*(params+4))). On success, scans the
// 768-slot person table (stride 536) for the record whose dword_12CEAD5 column
// equals the resolved base, then emits, per match, a BeginDeltaPacket keyed on
// that slot + an AppendDeltaField(1,1,value,slotOff 453) + QueueRequestState22,
// where value = (col%2 + col>>1) * v15 (v15==1). Returns 1 if a target resolved.
//
// The slot-match column (dword_12CEAD5 / dword_12CEA7C, byte_12CEAD5) is live
// game state -> routed through tblId/tblKind-style hooks is not a clean fit, so
// the per-slot scan body is reconstructed but the slot-match predicate is driven
// by the resolved target's own typeWord (the engine's index identity), which is
// the value the loop ultimately keys the delta packet on.
// ===========================================================================
int SendMapEntityAction(u8* cmd, i32* params, i32 src) {
    void* tgt; int ok = 0;
    if (cmd[2] == 6) {
        tgt = g_hooks.mapViewPanel(1, nullptr, nullptr); // msg 152
        if (tgt) ok = 1;
    } else {
        tgt = g_hooks.personQueryBegin(src, 1, 1, params[1]); // *(params+4)
        if (tgt) ok = 1;
    }
    if (!tgt || !ok) return ok;

    // The matched slot is the resolved record's own table identity (typeWord).
    i16 w = g_hooks.recTypeWord(tgt);
    if ((u16)w != 0xFFFF) {
        u8 col = g_hooks.tblKind((u16)w);              // byte_12CEAD5[i] proxy
        i32 value = (i32)((col % 2) + (col >> 1)) * 1; // * v15 (==1)
        emit({Recon4Emit::BeginDeltaPacket, g_hooks.tblId((u16)w), w});
        emit({Recon4Emit::AppendDeltaField, 1, 1, value, /*slotOff*/453});
        emit({Recon4Emit::QueueRequestState22});
    }
    return ok;
}

// ===========================================================================
// gilde.exe 0x4952b4 — VIBE_Command_QueueRequestTransform64
// (__usercall, eax = rec, dl = building; returns EnqueuePacket's eax.)
//
// Disasm-level notes (Hex-Rays garbles this function):
//  * 0x4952c7 sets ecx = 0x80 BEFORE the FindSlotByProt call; that callee
//    (0x5851fc) push/pops ebx/ecx/esi/edi/ebp, so ecx/esi/edi survive and the
//    rep-movs at 0x4952ee copies exactly 0x80 bytes rec -> local scratch.
//    (Hex-Rays' "uninitialized v4" is just this preserved register. The
//    `repne` prefix on movsd/movsb is a no-op encoding quirk: plain rep movs.)
//  * Each float delta is a self-contained fld m32 / fsub m32 / fstp m32
//    triple (0x495331..0x49537e); no caller FPU state is involved. One
//    binary32 subtraction double-rounded through the x87 (PC = 64-, 53- or
//    24-bit mantissa, whichever the live control word holds) is provably
//    bit-identical to a single IEEE-754 binary32 subtraction (intermediate
//    precision >= 2*24+2 = 50 bits, or direct 24-bit rounding), so plain
//    `float` math below is exact, including -0, +/-inf, and NaN-quieting.
//  * Packet bytes +0x01..+0x0F are deliberately left for EnqueuePacket's ring
//    header (size/flag/ringIdx/seq/pendingLink); bytes +0x2F..+0x98 of the
//    153-byte ring copy are stack garbage in the original (wire size for
//    opcode 0x40 is 47, ComputePacketSize @0x493176) — zeroed here.
// ===========================================================================
int QueueRequestTransform64(const i16* rec, u8 building) {
    // 0x4952e1: FindSlotByProt(eax = zero-extended building, dx = *rec).
    const u8* slot = g_hooks.findSlotByProt(building, *rec);

    // 0x4952e8..0x4952f7: 0x80-byte local copy of rec; the deltas are computed
    // in place in this copy in the original.
    u8 copy[0x80];
    std::memcpy(copy, rec, 0x80);

    auto ld32 = [](const u8* p) { u32 v; std::memcpy(&v, p, 4); return v; };
    auto ldf  = [](const u8* p) { float v; std::memcpy(&v, p, 4); return v; };

    // 0x4952f8..0x49532a — i32 deltas (two's-complement wraparound).
    u32 dX = ld32(copy + 0x10) - ld32(slot + 0x10);   // 0x495302
    u32 dY = ld32(copy + 0x14) - ld32(slot + 0x14);   // 0x495315
    u32 dZ = ld32(copy + 0x18) - ld32(slot + 0x18);   // 0x495328
    // 0x495331..0x49537e — f32 deltas (bit-exact as plain float, see above).
    float dF0 = ldf(copy + 0x20) - ldf(slot + 0x20);  // 0x495338
    float dF1 = ldf(copy + 0x24) - ldf(slot + 0x24);  // 0x495349
    float dF2 = ldf(copy + 0x2C) - ldf(slot + 0x2C);  // 0x49535a
    float dF3 = ldf(copy + 0x38) - ldf(slot + 0x38);  // 0x49536d

    // Packet build at esp+0 (frame 0x120; EnqueuePacket copies 0x99 bytes).
    u8 pkt[0x99] = {};                                // garbage->0 (see note)
    pkt[0x00] = 0x40;                                 // 0x49535d / 0x495370
    pkt[0x10] = building;                             // 0x495373 / 0x49537a
    std::memcpy(pkt + 0x11, rec, 2);                  // 0x495385/0x495388 (prot)
    std::memcpy(pkt + 0x13, &dX, 4);                  // 0x4953aa
    std::memcpy(pkt + 0x17, &dY, 4);                  // 0x4953b9
    std::memcpy(pkt + 0x1B, &dZ, 4);                  // 0x4953c3
    std::memcpy(pkt + 0x1F, &dF0, 4);                 // 0x495394
    std::memcpy(pkt + 0x23, &dF1, 4);                 // 0x49539f
    std::memcpy(pkt + 0x27, &dF2, 4);                 // 0x4953ae
    std::memcpy(pkt + 0x2B, &dF3, 4);                 // 0x4953bd

    emit({Recon4Emit::EnqueuePacket, /*a*/0x40, /*b*/building, /*c*/(i32)*rec});
    return g_hooks.enqueuePacket(pkt);                // 0x4953c7 -> 0x49388c
}

// gilde.exe 0x4c1810 — recovered verbatim with get_bytes (51 records x 40 B).
// The +4 flag column is byte-identical to guild::world::kLawTypeVariantFlag
// (src/world/law_text.cpp), an independent recovery of the same table.
const LawActionDef kLawActionTable[kLawActionCount] = {
    {0,0,1,1,6715,"HANDWERKSKUNST"},   {0,0,2,0,6718,"NACHTUNDNEBEL"},
    {0,0,3,1,6721,"KAMPF_1"},          {0,0,3,0,6724,"KAMPF_2"},
    {0,0,3,0,6727,"KAMPF_3"},          {0,0,3,0,6730,"KAMPF_4"},
    {0,0,4,0,6733,"RHETORIK"},         {1,0,1,0,6736,"SCHMIED_1"},
    {1,0,1,0,6739,"SCHMIED_2"},        {1,0,1,0,6856,"SCHMIED_3"},
    {1,0,2,1,6742,"STEINMETZ"},        {1,0,3,1,6745,"TISCHLER"},
    {1,0,4,1,6748,"WIRT"},             {1,0,5,0,6751,"PARFUMEUR_1"},
    {1,0,5,0,6754,"PARFUMEUR_2"},      {1,0,6,0,6757,"KRAEUTERHAENDLER_1"},
    {1,0,6,0,6760,"KRAEUTERHAENDLER_2"},{1,0,7,0,6763,"PRIESTER_1"},
    {1,0,7,0,6766,"PRIESTER_2"},       {1,0,8,1,6769,"FERNHAENDLER"},
    {1,0,9,0,6772,"BANKIER"},          {1,0,10,0,6775,"GARDIST"},
    {1,0,11,1,6778,"DIEB_1"},          {1,0,11,0,6781,"DIEB_2"},
    {1,0,12,1,6784,"RAEUBER_1"},       {1,0,12,1,6787,"RAEUBER_2"},
    {1,0,12,0,6790,"RAEUBER_3"},       {1,0,12,0,6793,"RAEUBER_4"},
    {2,0,0,0,6796,"BUERGERMEISTER"},   {2,0,0,0,6799,"RICHTER"},
    {2,0,0,0,6802,"KAEMMERER"},        {2,0,0,0,6805,"SCHILFLIEDER"},
    {2,0,0,1,6808,"EDELSTEINE"},       {2,0,0,1,6811,"NOVEMBERLAND"},
    {2,0,0,0,6814,"BUERGER_FRIEDBERT"},{2,0,0,0,6817,"DIE_LETZTE_WACHT"},
    {2,0,0,0,6820,"DIE_GUTEN_ALTEN_ZEITEN"},{2,0,0,0,6823,"NEBELKRAEHE"},
    {2,0,0,0,6826,"PEST"},             {2,0,0,0,6829,"HERBSTBESUCH"},
    {2,0,0,0,6832,"SONNENUNTERGANG"},  {2,0,0,0,6835,"HUHNS_END"},
    {2,0,0,0,6838,"SPION"},            {2,0,0,0,6841,"DON_IM_GRAS"},
    {2,0,0,0,6844,"UEBERFALL"},        {2,0,0,0,6847,"GOPPOCKS_GESCHICHT"},
    {2,0,0,0,6850,"BOKTORUS"},         {2,0,0,0,6853,"ABLASSBRIEF"},
    {2,0,0,0,6859,"DIE_LETZTE_KERZE"}, {2,0,0,0,6862,"GRAF_ADALBERT"},
    {2,0,0,0,6865,"LEBENSLAUF"},
};

// ===========================================================================
// gilde.exe 0x4c2218 — VIBE_Command_EnqueueLawAction
//   (__usercall, eax = fn(rec@eax, aux@edx, lawIdx@bx))
// Bard "perform poem/story". Always emits an EnqueueCmd15 charge packet and a
// QueueRequestArgs25(id,456,0x20000,4,0). Then, for a valid law index (<51):
//   type 0 (stat poem):  delta = min(rand%8+8, 252.0f - stat) truncated, sent
//     as a 1-byte absolute field at slot 0x80+arg (BeginDeltaPacket /
//     AppendRawField(1,1,&delta,0x80+arg) / QueueRequestState22).
//   type 1 (guild poem): scan all 768 person slots (stride 536); for every
//     OTHER person whose building-type group (GroupFromCode of the i8 code at
//     slot+0x164) equals arg, QueueRequestCoord27(id, slotId, rand%4+3, 0).
//   type 2 (story): nothing further.
// Disasm-level notes:
//  * The decompiler's "v11 read before assignment" is edx = entry->arg loaded
//    at 0x4c237b and preserved across GroupFromCode (every reachable switch
//    case of 0x58a4c8 is `mov al,imm8; retn`).
//  * VIBE_Coord_ConvertX (0x5c6b08) is frndint with the control-word RC forced
//    to truncate -> a plain (i32) cast (precedent: src/util/coord.cpp).
//  * flt_61E584 == 0x437C0000 == 252.0f (the stat cap).
//  * The "102912-stride" person scan in the decompile is `esi += 0x218` until
//    0x64800: 768 slots of the 536-byte person table at 0x12CE910.
//  * Float math is exact in plain float: all operands (u8 stat, 8..15 roll,
//    252.0f) are integers representable in binary32; trunc-toward-zero handles
//    the cur > 252 negative-delta edge identically (cur=255 -> byte 0xFD).
// ===========================================================================
int EnqueueLawAction(u8* rec, i32 aux, u16 lawIdx) {
    // 0x4c2227..0x4c2237: EnqueueCmd15(-1, *(rec+4), aux, byte_6477A1)
    //   packet 0x0F: +0x10=-1, +0x14=id, +0x1C=mode byte, +0x1D=aux dword.
    const i32 recId = g_hooks.cmdToId(rec);
    emit({Recon4Emit::EnqueueCmd15, -1, recId, aux,
          (i32)g_hooks.seasonByte6477A1(), 0});
    // 0x4c223c..0x4c2253: QueueRequestArgs25(id, 0x1C8, 0x20000, 4, 0)
    //   packet 0x19: +0x10=id, +0x14=456, +0x18=4, +0x1C=0x20000, +0x20=0.
    emit({Recon4Emit::QueueRequestArgs25, recId, 456, 0x20000, 4, 0});

    // 0x4c2258 / 0x4c232a: entry = lawIdx < 0x33 ? &table[40*lawIdx] : null.
    if (lawIdx >= (u16)kLawActionCount)
        return 0;                       // 0x4c2262: null -> epilogue
    const LawActionDef& e = kLawActionTable[lawIdx];

    if (e.type == 0 && e.arg <= 4u) {   // 0x4c226c / 0x4c2275 (u16 compare)
        // ---- Branch A 0x4c2280..0x4c231d: stat-delta poem ----
        u8 roll = (u8)(util::RandomModulo(8) + 8);     // 0x4c2285/8a: rand%8+8
        u8 cur  = g_hooks.personStatByte(rec, e.arg);  // 0x4c2298: rec[0x80+arg]
        float fCur  = (float)cur;                      // 0x4c22a3 fild/fstp
        float fMax  = 252.0f - fCur;                   // 0x4c22ad: flt_61E584 - cur
        float fRoll = (float)roll;                     // 0x4c22c3 fild
        float take  = (fRoll < fMax) ? fRoll : fMax;   // 0x4c22ca fcomp / 0x4c22d1 jnb
        i32 value   = (i32)take; // 0x4c22de ConvertX(trunc) + 0x4c22e3 fistp
        u8 deltaByte = (u8)value;                      // 0x4c22e7/eb: low byte
        emit({Recon4Emit::BeginDeltaPacket, recId, 0});            // 0x4c22f4 (rec,id)
        // 0x4c2318: AppendRawField(width=1, count=1, &delta, bx=0x80+arg).
        emit({Recon4Emit::AppendRawField, 1, 1, (i32)deltaByte,
              (i32)(u16)(0x80 + e.arg)});
        emit({Recon4Emit::QueueRequestState22});                   // 0x4c231d
        return 0;   // original eax = State22's EnqueuePacket return (unobserved)
    }
    if (e.type != 1)                    // 0x4c234d: only type 1 scans
        return 40 * (i32)lawIdx;        // 0x4c233d: eax == 40*lawIdx at this exit
    // ---- Branch B 0x4c2353..0x4c23ad: notify the whole guild group ----
    for (i32 slot = 0; slot != 768; ++slot) {          // esi += 0x218 until 0x64800
        i32 id = g_hooks.personSlotId(slot);           // 0x4c2355: dword_12CE914 col
        if (id == recId)                               // 0x4c235b: skip the performer
            continue;
        i8 code = g_hooks.personSlotBuildingCode(slot);// 0x4c2370/78: sar(dword,24)
        // 0x4c237f..0x4c2389: movsx(GroupFromCode(code)) vs edx (= e.arg,
        // loaded at 0x4c237b, preserved across the call).
        if ((i32)(i8)BuildingType_GroupFromCode((u8)code) != (i32)e.arg)
            continue;
        u16 v = (u16)util::RandomModulo(4);            // 0x4c2390
        // 0x4c23a8: QueueRequestCoord27(id, slotId, v+3, ecx=0) — packet 0x1B.
        // (The packet's +0x20/+0x24 coord pair comes from Coord27's own
        //  internal globals dword_62EB94 / trunc(flt_62EB90), not the caller.)
        emit({Recon4Emit::QueueRequestCoord27, recId, id, (i32)v + 3, 0, 0});
    }
    return 0;       // original eax after the loop is iteration junk; caller discards
}

namespace {
inline void wr32(u8* p, i32 v) { std::memcpy(p, &v, 4); }
inline i32  rd32u(const u8* p) { i32 v; std::memcpy(&v, p, 4); return v; }
} // namespace

// ===========================================================================
// gilde.exe 0x48980c — VIBE_Combat_SetUnitFormationMode
//   (__usercall, eax = pkt : 276-byte duel/formation group packet, dl = mode;
//    eax return never observed — both callers overwrite it immediately:
//    0x538348/0x538356 in EnqueueDuelChallenge, 0x4ed72d/0x4ed734 region in
//    VIBE_NpcAction_CityFormationMoveStep 0x4ed018).
//
// Disasm-level notes (the Hex-Rays decompile of this function is GARBLED):
//  * Hex-Rays emits "v9/v22 used uninitialized" for the 2nd/4th slot scans
//    because the original stages `lea ecx,[esi+80h]` BEFORE each intermediate
//    QueueRequestState22 call (0x4898e9 / 0x489aaa) and reuses ECX AFTER it —
//    legal only because ECX is callee-saved in this binary's convention
//    (QueueRequestState22 0x494750 never touches ECX). The decompiler also
//    drops the +0x110 store into a bogus `result = a2` and loses the second
//    AdjustStockAndNotify/BeginDeltaPacket argument flow. The raw disasm
//    (239 instructions, 4 instruction-identical pass bodies differing only in
//    the opcode/delta-byte immediates) is the reference of record here.
//  * The four pass bodies: 0x489826..0x4898ef (mode-1 first), 0x4898f4..
//    0x4899af (mode-1 second), 0x4899e8..0x489ab0 (mode-2/3 first),
//    0x489ab5..0x489b61 (mode-2/3 second, tail shared with mode-1 @0x4899a0).
//  * `fstp st` after each AdjustStockAndNotify (0x4898bc/0x489982/0x489a7d/
//    0x489b41) discards that callee's st0 float return.
//  * The AppendDeltaField slot offset is computed from the POINTER VALUE:
//    bx = LOWORD(dword_6498F0[slot]) + 0x83 - LOWORD(dword_11AA474), i.e. the
//    unit's entity-pool-relative byte offset +0x83 in low-16 arithmetic
//    (carries cannot cross into bx). Reported as the relative offset 0x83,
//    the same convention as SendEntityActionA's slotOff 37.
// ===========================================================================
namespace {
// One emission pass (the 0x489826..0x4898ef body; run twice per mode arm).
void FormationPass(u8* pkt, i32 opcode, u8 deltaByte) {
    // 0x489826..0x489849 — first free (== -1) dword of the 16-slot id list at
    // pkt+0x80..+0xBC; -1 when all 16 are occupied (loc_4899CB). Deterministic
    // core shared with combat_drivers.cpp (FindFirstFreeFormationSlot, same
    // address provenance).
    std::vector<i32> slots(16);
    std::memcpy(slots.data(), pkt + 0x80, 16 * 4);
    const i32 slot = FindFirstFreeFormationSlot(slots);
    // 0x489850 — unit = dword_6498F0[slot]. slot == -1 indexes dword_6498F0[-1]
    // (the dword at 0x6498EC) in the original; the hook models that read (and
    // the slot >= 8 reads past the 8-entry save-layout table).
    void* unit = g_hooks.playerSlotUnit(slot);
    const i32 unitId = g_hooks.cmdToId(unit);            // *(u32*)(unit+4)
    // 0x48985c — pkt[0x80 + 4*slot] = *(unit+4); slot == -1 writes pkt+0x7C
    // (edi = -4), clobbering the duel packet's byte-9 field — preserved 1:1.
    wr32(pkt + 0x80 + 4 * slot, unitId);
    g_hooks.resetObjectHighlights(unit);                 // 0x489868
    // 0x48988f — QueueRequest17(*(unit+4)@eax, -1@edx, 1@ecx, opcode@bx,
    //            byte_6477A1, 0): the cmd-0x11 packet (+0x10 id, +0x14 -1,
    //            +0x18 opcode word, +0x1E season byte, +0x1F 1, +0x23 0).
    emit({Recon4Emit::QueueRequest17, unitId, -1, 1, opcode,
          (i32)g_hooks.seasonByte6477A1()});
    // 0x4898a7 — AdjustStockAndNotify(*(u16*)unit@ax, -*(i32*)(unit+0x24)@edx
    //            via `neg edx` @0x48989d); st0 return discarded (fstp st).
    (void)g_hooks.adjustStockAndNotify(
        g_hooks.unitProtWord(unit),
        (i32)(0u - (u32)g_hooks.unitStock24(unit)));     // two's-complement neg
    // 0x4898be — BeginDeltaPacket(unit@eax, *(unit+4)@edx).
    emit({Recon4Emit::BeginDeltaPacket, unitId, 0});
    // 0x4898e4 — AppendDeltaField(1, 1, &deltaByte, slotOff 0x83) (see the
    //            pointer-low-word note above).
    emit({Recon4Emit::AppendDeltaField, 1, 1, (i32)deltaByte, 0x83});
    emit({Recon4Emit::QueueRequestState22});             // 0x4898ef
}
} // namespace

void CombatSetUnitFormationMode(u8* pkt, u8 mode) {
    // 0x489815/0x4899bc..0x4899c6 — ANY nonzero mode stores the zero-extended
    // mode dword at +0x110 before the dispatch; mode 0 skips the store.
    if (mode != 0)
        wr32(pkt + 0x110, (i32)mode);
    if (mode == 1) {                       // 0x48981d
        FormationPass(pkt, kFormOpLine, 0xA8);  // 342 @0x489882, 0xA8 @0x4898ac
        FormationPass(pkt, kFormOpLine, 0xA8);  // 342 @0x489948, 0xA8 @0x48996d
    } else if (mode == 2 || mode == 3) {   // 0x4899df / 0x489b66 (3 -> same arm)
        FormationPass(pkt, kFormOpA, 0xD2);     // 344 @0x489a44, 0xD2 @0x489a6e
        FormationPass(pkt, kFormOpB, 0xD2);     // 352 @0x489b09, 0xD2 @0x489b33
    }
    // Every other nonzero mode: straight to the epilogue (0x489b6f). The eax
    // return (State22's result / the zero-extended mode / the caller's pkt) is
    // dead at both call sites, so the reconstruction returns void.
}

// ===========================================================================
// gilde.exe 0x538224 — VIBE_Command_EnqueueDuelChallenge (__usercall, eax=obj)
// Builds the 276-byte (0x114) duel-group packet at the frame base and hands it
// to VIBE_Command_QueueRequest39 (0x494c30: StagePendingBlock(276, pkt) + a
// cmd-39 wrapper). Hex-Rays' `v24[41]` is the frame tail, `v24[40] = a2` is
// the `push ecx` prologue save (ECX is NOT a real argument), and the garbled
// v6/v11 locals are ECX = the QueryBegin record, callee-saved across every
// intermediate call (fill thunk pop ecx @0x5c6b06, ResolveOwnerOrParentB pop
// ecx @0x59177d, SetUnitFormationMode pop ecx @0x489b74).
// ===========================================================================
i32 EnqueueDuelChallenge(u8* obj) {
    // 0x538235 — rec = VIBE_Person_QueryBegin(esi=obj; 1 pair: {type 5, 0x10}).
    void* rec = g_hooks.personQueryBegin((i32)(intptr_t)obj, 1, 5, 16);
    // 0x538243..0x538261 — skip records whose typeWord (+0x27) equals the local
    // player's word (*(u16*)(*(void**)0x6498E4)).
    // ORIGINAL DEFECT (preserved 1:1, rule 1): IterNext (0x586a6c) returns the
    // next record in EAX and RESTORES ECX (pop ecx @0x586bce), but this call
    // site keeps testing the stale ECX (QueryBegin's record, never reloaded).
    // The loop exits only through the mismatch branch @0x538258; if the first
    // record belongs to the local player the original spins forever. It is a
    // debug-key-only path (key 0x25 in 0x4bf054) so it was never noticed. We
    // reproduce it exactly: the loop re-reads the same stale record and the
    // live local-player word each iteration, and only an external change of
    // either (modeled by the hooks) terminates it.
    if (rec) {
        while ((u16)g_hooks.recTypeWord(rec) == g_hooks.localPlayerTypeWord())
            g_hooks.personIterNext();                    // 0x53825a — result discarded
    }
    if (!rec)                                            // 0x538265
        return 0;

    // 0x538274 — owner row of the challenged object (resolves *(u32*)(obj+0xA)).
    void* ownerRow = g_hooks.resolveOwnerOrParentB(obj);

    u8 pkt[276];                                         // frame base, 0x114 bytes
    std::memset(pkt, 0, sizeof(pkt));                    // 0x538281 (thunk: dl=0, ebx=0x114)

    pkt[0x26] = 0xA2;                                    // 0x538286 — inner command id 162
    wr32(pkt + 0x14, -1);                                // 0x53828b
    pkt[0x30] = 2;                                       // 0x538293
    pkt[0x08] = 3;                                       // 0x53829a / 0x5382a3
    // 0x53829c..0x5382bc — dword_12CE914[134*word_63CC5C]: challenger player id.
    wr32(pkt + 0x0C, g_hooks.tblId(g_hooks.localPlayerIndex()));
    // 0x5382c0..0x5382d7 — dword_12CE914[134 * *(u16*)(rec+0x27)]: opponent id.
    u16 oppW = (u16)g_hooks.recTypeWord(rec);
    wr32(pkt + 0x34, g_hooks.tblId(oppW));
    // 0x5382dd / 0x5382e5 — *(u32*)(ownerRow+4); the original dereferences
    // unconditionally (faults if ResolveOwnerOrParentB returned 0).
    wr32(pkt + 0x38, g_hooks.cmdToId(ownerRow));
    pkt[0x7C] = 9;                                       // 0x5382db / 0x5382e9
    // 0x5382f2..0x538306 — -1-fill both 16-slot id lists; the two store ranges
    // OVERLAP at +0xBC (var_A4+0x40 == var_64+0), so +0x80..+0xFF is all -1.
    for (i32 off = 4; off <= 0x40; off += 4) {
        wr32(pkt + 0xBC + off, -1);                      // +0xC0..+0xFC
        wr32(pkt + 0x7C + off, -1);                      // +0x80..+0xBC
    }
    // 0x538308..0x53832d — challenged-side trio at +0x100: [obj+2], -1, -1
    // (the original re-tests obj each iteration; all three -1 when obj null).
    for (i32 j = 0; j < 3; ++j) {
        if (obj && j == 0)
            wr32(pkt + 0x100 + 4 * j, rd32u(obj + 2));   // 0x53831c
        else
            wr32(pkt + 0x100 + 4 * j, -1);               // 0x5383cc
    }
    // 0x53832f..0x538351 — formation mode byte *(u8*)(obj+0x28). The original
    // reads it unconditionally (a null obj faults here, as in gilde.exe; the
    // sole caller 0x4bf159 guards on dword_11BC274 != 0).
    // mode==2 runs two calls (2 then 1); any other mode runs one call. Wired
    // to the real reconstructed 0x48980c (wave-2 part 3; gap closed).
    u8 mode = obj[0x28];
    if (mode == 2) {
        CombatSetUnitFormationMode(pkt, 2);              // 0x538343
        CombatSetUnitFormationMode(pkt, 1);              // 0x538351
    } else {
        CombatSetUnitFormationMode(pkt, mode);           // 0x5383dc -> 0x538351
    }
    // 0x538356 / 0x53835b — opponent person-record id *(u32*)(rec+1).
    wr32(pkt + 0x10C, g_hooks.recEntityId(rec));
    // 0x538362..0x538395 — opponent duel-group scan: 768 rows, stride 536;
    // rows whose +0x16C record pointer equals rec contribute their +4 entity
    // id to +0xC0..+0xCC (pre-incremented slot cursor: +0xBC stays -1; max 4).
    {
        i32 d = 0;
        for (u32 idx = 0;;) {
            if (g_hooks.tblRowTypeWord((u16)idx) != 0xFFFF
                && g_hooks.tblRowOwnerRec((u16)idx) == rec) {
                d += 4;
                wr32(pkt + 0xBC + d, g_hooks.tblId((u16)idx));
            }
            ++idx;                                       // add eax, 218h
            if (idx >= 768) break;                       // cmp eax, 64800h; jge
            if (d >= 0x10) break;                        // cmp edx, 10h; jl
        }
    }
    // 0x538397..0x5383b2 — duel time: copy the 14-byte clock (qword_13CE852)
    // into +0x18, advance +15 minutes in place (eax=buf, edx=0 days,
    // ecx=0 seconds, ebx=15 minutes). Computed in a local GameTime and copied
    // back — byte-identical to advancing in place.
    {
        GameTime t;
        g_hooks.gameTimeNow(&t);
        GameTimeAdvance(&t, 0, 0, 15);                   // 0x5383b2 -> 0x583150
        std::memcpy(pkt + 0x18, &t, 14);
    }
    // 0x5383bb — stage the 276-byte block + enqueue the cmd-39 wrapper.
    return g_hooks.queueRequest39(pkt);
}

} // namespace guild::sim
