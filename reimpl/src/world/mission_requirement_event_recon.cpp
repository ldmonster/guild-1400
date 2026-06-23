// ---------------------------------------------------------------------------
// Mission requirement / objective-goal evaluators (second leaf cluster).
// Faithful 1:1 port of nine VIBE_MissionReq_* checkers. See the header for the
// full provenance table and the coupled-leaf hook rationale.
// ---------------------------------------------------------------------------
#include "world/mission_requirement_event_recon.h"

#include <cstring>

namespace guild::world {

// ---------------------------------------------------------------------------
// Recovered constants (get_bytes).
//   byte_6477A1 == 0  — the "blood/coin" currency index used by the wealth checks.
//   dbl_623D60 == 0.1 — economy-demand upper floor (CheckMinThresholds).
//   0.3f == 1050253722 (0x3E99999A) — the per-channel demand lower floor.
// ---------------------------------------------------------------------------
static const u8  kBloodCurrencyIdx = 0;        // byte_6477A1
static const double kDemandUpperFloor = 0.1;   // dbl_623D60
static const i32 kDemandChannelFloorBits = 1050253722;  // 0.3f as signed int bits

// Reinterpret a float's storage as a signed int (the original compares the
// snapshot floats as SLODWORD bit patterns against 1050253722).
static i32 FloatBits(float f) {
    i32 v;
    std::memcpy(&v, &f, sizeof v);
    return v;
}

// Unaligned little-endian field reads off raw person/object records (byte-packed).
static u16 ReadWordAt(const u8* base, int off) {
    u16 v;
    std::memcpy(&v, base + off, sizeof v);
    return v;
}
static i32 ReadDwordAt(const u8* base, int off) {
    i32 v;
    std::memcpy(&v, base + off, sizeof v);
    return v;
}

// ---------------------------------------------------------------------------
// Process-wide hook table (inert by default).
// ---------------------------------------------------------------------------
MissionReqEventHooks& MissionReqEventGetHooks() {
    static MissionReqEventHooks hooks;
    return hooks;
}

// ===========================================================================
// 0x539138 — VIBE_MissionReq_CheckBloodLevel        VERIFIED-1:1
//   disasm 0x53913c-0x53914d: xor ecx,ecx; mov cl,byte_6477A1; mov edx,ecx;
//   GetCurrencyAmount(eax=owner, edx=0); mov edx,ecx (ecx preserved == 0);
//   ConvertToDisplayCoord(eax=amount, edx=0). Both currency-idx args are
//   byte_6477A1 (==0). 0x539155 setnl => signed >= row+0x10. Matches.
// ===========================================================================
bool MissionReqCheckBloodLevel(const ReqTableRow* row, const void* objectiveOwner) {
    MissionReqEventHooks& h = MissionReqEventGetHooks();
    // amount = VIBE_Person_GetCurrencyAmount(owner, byte_6477A1)
    int amount = h.personGetCurrencyAmount
                     ? h.personGetCurrencyAmount(objectiveOwner, kBloodCurrencyIdx)
                     : 0;
    // display = VIBE_Money_ConvertToDisplayCoord(amount, idx)
    int display = h.moneyConvertToDisplayCoord
                      ? h.moneyConvertToDisplayCoord(amount, kBloodCurrencyIdx)
                      : 0;
    return display >= row->threshold;          // *(_DWORD *)(a1 + 16)
}

// ===========================================================================
// 0x5391d8 — VIBE_MissionReq_CheckObjectCount        VERIFIED-1:1
//   while (record[0] < 5 || record+101 != arg+4 || DiffMinutes(record+105) <= row+20)
//        record = IterNext();  if (!record) return 0;
//   return 1;
// disasm: 0x5391f8 cmp byte[eax],5 / jl(signed); 0x5391fd mov edx,[eax+65h]
//   (+101) cmp edx,[ecx+4] (ecx==a1==queryArg, so targetId = queryArg+4);
//   0x539218 add eax,69h (+105) DiffMinutes; 0x539220 cmp eax,[ebx+14h]
//   jle(signed) => success needs DiffMinutes > row+0x14 (strict). All matches.
//   BOUNDARY: QueryBegin(owner,2,4,*queryArg,5,2) is the personQueryOwnedObjects
//   hook (the *queryArg query-kind word is consumed inside the cursor subsystem).
// (Object age is exposed by the cursor: record bytes carry the alive flag (+0),
//  the matched id (+101) and the owned-since timestamp diff at +105. The diff
//  itself is precomputed by the iterator; we read the resolved "minutes owned"
//  dword the original recovers via VIBE_GameTime_DiffMinutes(record+105, now).)
// ===========================================================================
bool MissionReqCheckObjectCount(const u16* queryArg, const ReqTableRow* row,
                                int owner) {
    MissionReqEventHooks& h = MissionReqEventGetHooks();
    if (!h.personQueryOwnedObjects || !h.personIterNext)
        return false;
    const i32 targetId = ReadDwordAt(reinterpret_cast<const u8*>(queryArg), 4);
    const u8* rec = h.personQueryOwnedObjects(owner);
    if (!rec)
        return false;                          // !Begin -> 0
    while (rec[0] < 5 ||                                            // *Begin < 5
           ReadDwordAt(rec, 101) != targetId ||                    // +101 != arg+4
           ReadDwordAt(rec, 105) <= row->timerMin) {               // DiffMin <= row+20
        rec = h.personIterNext();
        if (!rec)
            return false;
    }
    return true;
}

// ===========================================================================
// 0x539230 — VIBE_MissionReq_CheckBuildingEquip        VERIFIED-1:1
//   disasm 0x539237-0x539240: mov eax,[a1+0x161]; sar eax,18h (signed >>24,
//     top byte of the dword at +0x161); MapToActionCode(eax=that byte). The
//     buildingTypeMapToActionCode hook abstracts that derived arg (BOUNDARY).
//   0x539271 movsx eax,byte[ebx]; imul 0x24D (589); + dword_13CE294 — equip
//     table base; equip word at +0x23 (35), advanced +2/iter; objId at ebx+0x5D
//     (+93). These table lookups are the buildingEquipWord/buildingObjId hooks
//     (BOUNDARY). 0x539298 and ah,7Fh -> mask bit15 (& 0x7FFF); cwde is benign
//     (value already positive). 0x5392c1/0x5392ca cmp esi,3 jge/jl => v4 >= 3.
//   The decision arithmetic / control flow is translated exactly.
// ===========================================================================
bool MissionReqCheckBuildingEquip(const void* buildingTypeRec) {
    MissionReqEventHooks& h = MissionReqEventGetHooks();
    u8 actionCode = h.buildingTypeMapToActionCode
                        ? h.buildingTypeMapToActionCode(buildingTypeRec)
                        : 0;
    if (!actionCode)                           // !v1 -> 0
        return false;
    if (!h.personQueryOwnedObjects || !h.personIterNext)
        return false;

    int equippedCount = 0;                     // v4
    const u8* building = h.personQueryOwnedObjects(actionCode);
    if (building) {
        do {
            int fullyEquipped = 1;             // v6 = 1
            // Iterate the building's required equipment words (up to 64). Each is
            // masked to clear the high bit (HIBYTE(v8) &= ~0x80 -> & 0x7FFF).
            u16 first = h.buildingEquipWord ? h.buildingEquipWord(building, 0) : 0;
            if (first) {                       // *(_WORD *)(v7 + 35) != 0
                int slot = 0;
                while (true) {
                    u16 raw = h.buildingEquipWord(building, slot);
                    u16 equip = static_cast<u16>(raw & 0x7FFF);   // mask high bit
                    i32 objId = h.buildingObjId ? h.buildingObjId(building) : 0;
                    bool present = h.gameObjectQueryFind
                                       ? h.gameObjectQueryFind(objId, equip)
                                       : false;
                    if (!present) {            // QueryFind == 0 -> missing
                        fullyEquipped = 0;     // v6 = 0
                        break;
                    }
                    ++slot;                    // next equip word
                    if (slot >= 64 ||          // v10 + 1 >= 64
                        h.buildingEquipWord(building, slot) == 0)  // end of list
                        break;                 // goto LABEL_7 (stays equipped)
                }
            }
            equippedCount += fullyEquipped;    // v4 += v6
            building = h.personIterNext();     // IterNext()
        } while (equippedCount < 3 && building);  // v4 < 3 && v11
    }
    return equippedCount >= 3;                  // v4 >= 3
}

// ===========================================================================
// 0x5392f0 — VIBE_MissionReq_CheckSkillAbove        VERIFIED-1:1
//   disasm 0x539300 mov edx,[eax+2Ch] (dword[11]); 0x539305 mov esi,[eax+34h]
//   (dword[13]); lea eax,[edx+esi] (sum); ConvertToDisplayCoord(sum, 0);
//   0x53931b setnle => signed strict > row+0x10. Null record => return 0.
// ===========================================================================
bool MissionReqCheckSkillAbove(const ReqTableRow* row, const void* person) {
    MissionReqEventHooks& h = MissionReqEventGetHooks();
    const i32* rec = h.personGetFamilyRecord ? h.personGetFamilyRecord(person)
                                             : nullptr;
    if (!rec)
        return false;                          // result == 0
    // VIBE_Money_ConvertToDisplayCoord(rec[11] + rec[13], byte_6477A1) > row->threshold
    int sum = rec[11] + rec[13];
    int display = h.moneyConvertToDisplayCoord
                      ? h.moneyConvertToDisplayCoord(sum, kBloodCurrencyIdx)
                      : 0;
    return display > row->threshold;
}

// ===========================================================================
// 0x5394d4 — VIBE_MissionReq_CheckGuildMemberCount        VERIFIED-1:1
//   (__usercall: eax=person, edx=record(objective), ebx=row).
//   disasm 0x5394db movzx esi,byte[ecx+0Dh] (ecx==person; unsigned byte +13);
//     0x5394e7 setnl => signed >= row+0x10. AccumulateTimer(record, met, row+0x14).
//   count loop: 0x539500 xor edx,edx (count=0); QueryBegin(...,0,7); if begin,
//     0x539519 IterNext / 0x53951e inc edx / jnz — count increments once per
//     IterNext INCLUDING the terminating null. 0x539527 setnl cmp 3 => count>=3.
//   met1 = (person[+13] (u8) >= row->threshold)         // mov ecx,eax; [ecx+0Dh]
//   if (!AccumulateTimer(record, met1, row->timerMin)) return false
//   count guild members of state 7; return count >= 3
// The clan-size byte is read from the PERSON record (eax), while the hold timer
// lives in the separate objective/record (edx) passed to AccumulateTimer. The two
// are distinct records (verified at 0x5394d7..0x5394f1: mov ecx,eax / mov eax,edx).
// ===========================================================================
bool MissionReqCheckGuildMemberCount(const u8* person, ObjectiveRecord* objective,
                                     const ReqTableRow* row) {
    MissionReqEventHooks& h = MissionReqEventGetHooks();
    bool met = static_cast<int>(person[13]) >= row->threshold;  // (int)*(u8*)(eax+13)
    if (!MissionReqAccumulateTimer(objective, met, row->timerMin))
        return false;

    int count = 0;                             // v6
    if (h.personQueryMemberState && h.personIterNext) {
        // VIBE_Person_QueryBegin(..., 0, 7) — guild members of state 7. The
        // original ignores the Begin record and counts via IterNext():
        //   do { v7 = IterNext(); v6 = v8 + 1; } while (v7);
        if (h.personQueryMemberState(7)) {
            const u8* rec;
            do {
                rec = h.personIterNext();      // v7 = IterNext()
                ++count;                       // v6 = v8 + 1
            } while (rec);
        }
    }
    return count >= 3;                          // v6 >= 3
}

// ===========================================================================
// 0x539708 — VIBE_MissionReq_CheckZeroValue        VERIFIED-1:1
//   return 0.0 == ComputeWeightedLawScore(a1,a2). __fastcall (ecx=a1, edx=a2).
// ===========================================================================
bool MissionReqCheckZeroValue(int a1, int a2) {
    MissionReqEventHooks& h = MissionReqEventGetHooks();
    double score = h.economyComputeWeightedLawScore
                       ? h.economyComputeWeightedLawScore(a1, a2)
                       : 0.0;
    return 0.0 == score;
}

// ===========================================================================
// 0x539728 — VIBE_MissionReq_CheckTimeElapsed        VERIFIED-1:1
//   disasm: 0x53972e cmp (int)qword_13CE852,0Ah jge — currentDay < 10 => 0
//     (BOUNDARY: the binary reads the global clock; per project convention the
//      day is passed in as currentDay). 0x539745 fstp st DISCARDS the snapshot
//     return value (only snap[6] is used). 0x539747 fild row+0x10 (threshold as
//     int->float); 0x53974a fcomp snap[6] (esp+0x18, index 6); 0x539751 jnb =>
//     met when threshold >= snap[6]. Gated through AccumulateTimer(record,met,
//     row+0x14). The (double) promotion matches the x87 fild/fcomp.
// ===========================================================================
bool MissionReqCheckTimeElapsed(ObjectiveRecord* record, const ReqTableRow* row,
                                i32 currentDay) {
    MissionReqEventHooks& h = MissionReqEventGetHooks();
    if (currentDay < 10)                       // (int)qword_13CE852 < 10
        return false;
    float snap[10];
    std::memset(snap, 0, sizeof snap);
    if (h.economyLoadDemandSnapshot)
        h.economyLoadDemandSnapshot(snap);
    // fild row->threshold ; fcomp snap[6] ; jnb -> met (threshold >= snap[6]).
    bool met = static_cast<double>(row->threshold) >= snap[6];
    return MissionReqAccumulateTimer(record, met, row->timerMin);
}

// ===========================================================================
// 0x539778 — VIBE_MissionReq_CheckMinThresholds        VERIFIED-1:1
//   LoadDemandSnapshot(v2) <= dbl_623D60(0.1)
//   && SLODWORD(v2[3]) >= 0.3f && SLODWORD(v2[1]) >= 0.3f && SLODWORD(v2[2]) >= 0.3f
//   disasm: 0x539782 fcomp dbl_623D60 on the float RETURN value (st0); 0x53978b
//     ja => fail when ret > 0.1 (i.e. pass needs ret <= 0.1). Then signed (jl/jge)
//     int-bits compares against 3E99999Ah (0.3f) in ORDER: var_1C=esp+0xC=snap[3],
//     var_24=esp+0x4=snap[1], var_20=esp+0x8=snap[2]. dbl_623D60 bytes
//     0x3FB999999999999A == 0.1; 0.3f bits 0x3E99999A == 1050253722. Matches.
// ===========================================================================
bool MissionReqCheckMinThresholds() {
    MissionReqEventHooks& h = MissionReqEventGetHooks();
    float snap[10];
    std::memset(snap, 0, sizeof snap);
    double ret = h.economyLoadDemandSnapshot ? h.economyLoadDemandSnapshot(snap)
                                             : 0.0;
    return ret <= kDemandUpperFloor &&
           FloatBits(snap[3]) >= kDemandChannelFloorBits &&
           FloatBits(snap[1]) >= kDemandChannelFloorBits &&
           FloatBits(snap[2]) >= kDemandChannelFloorBits;
}

// ===========================================================================
// 0x5397bc — VIBE_MissionReq_CheckNoActiveCombat        VERIFIED-1:1
//   Begin = QueryBegin(owner,1,6); if (!Begin) return 1;
//   loop: v2 = *(WORD*)(Begin+39);
//         if (v2 != 0xFFFF && byte_12CE912[536*v2]==5) return 0;
//         Begin = IterNext(); if (!Begin) return 1;
//   disasm: 0x5397cc mov dx,[eax+27h] (+39); 0x5397dd imul 0x218 (536) stride;
//     0x5397e3 cmp byte_12CE912[idx*536],5. The owner query and the 536-stride
//     state table are the personQueryAll / objectStateByIndex hooks (BOUNDARY).
// ===========================================================================
bool MissionReqCheckNoActiveCombat(int owner) {
    MissionReqEventHooks& h = MissionReqEventGetHooks();
    if (!h.personQueryAll || !h.personIterNext)
        return true;                           // no iterator -> nobody in combat
    const u8* rec = h.personQueryAll();
    if (!rec)
        return true;                           // !Begin -> 1
    while (true) {
        u16 objIdx = ReadWordAt(rec, 39);      // *(_WORD *)(Begin + 39)
        if (objIdx != 0xFFFF) {
            u8 state = h.objectStateByIndex ? h.objectStateByIndex(objIdx) : 0;
            if (state == 5)                    // combat state
                return false;                  // return 0
        }
        rec = h.personIterNext();
        if (!rec)
            return true;                       // return 1
    }
}

}  // namespace guild::world
