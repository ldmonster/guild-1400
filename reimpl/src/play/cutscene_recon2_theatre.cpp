// guild::play — VIBE_Theatre_RunEventMenu reconstruction (deterministic core).
// gilde.exe 0x536a30. The form/render/command side effects are inert; the event
// record assembly + participant gathering + date-offset math are translated 1:1.

#include "play/cutscene_recon2_theatre.h"

namespace guild::play {

// gilde.exe 0x536e7e-0x536ea4 :
//   do {
//     role = v19[+2];                          // *((BYTE*)v19+2)  : role byte
//     if (role==6||role==7||role==5||role==4) {
//        v21 += 4; id = *((DWORD*)v19+1); ++count; rec[v21] = id;
//     }
//     v19 += 268;  // 268 __int16 == 536 bytes == one record
//     ++v2;
//   } while (v21 < 32 && v2 < 768);
std::vector<i32> Theatre_GatherTenancyParticipants(const std::vector<u8>& roleBytes,
                                                   const std::vector<i32>& idCol,
                                                   int& count) {
    std::vector<i32> ids;
    count = 0;
    int v21 = 0;     // byte offset into the record tail (caps at 32)
    const int recCount = static_cast<int>(roleBytes.size());
    for (int rec = 0; rec < 768 && rec < recCount; ++rec) {
        const u8 role = roleBytes[rec];
        if (role == 6 || role == 7 || role == 5 || role == 4) {
            v21 += 4;
            ++count;
            ids.push_back(rec < static_cast<int>(idCol.size()) ? idCol[rec] : 0);
        }
        if (!(v21 < 32))   // post-condition mirrors do/while: stop once v21 >= 32
            break;
    }
    return ids;
}

// gilde.exe 0x537275-0x5372f7 :
//   v30 = 1; v29 = 4;
//   do {
//     id16 = word_12CE910[268*rec];                   // COMPARE key (id word)
//     if (id16 != -1 && byte_12CEA76[536*rec] && localMaster != id16) {
//        v29 += 4; ++v30; rec_tail[v29] = dword_12CE914[134*rec];  // STORE master dword
//     }
//     rec += 536;  (i.e. ++recIndex)
//   } while (recIndex < 768 && v29 < 8);
//
// The binary compares the id-word column but STORES the dword_12CE914 master column
// (two distinct tables) — `masterCol` carries the stored value.
std::vector<i32> Theatre_GatherDuelParticipants(const std::vector<i32>& idCol,
                                                const std::vector<u8>& roleBytes,
                                                const std::vector<i32>& masterCol,
                                                i32 localMaster, int& count) {
    std::vector<i32> ids;
    count = 1;            // v30 starts at 1
    int v29 = 4;          // starts at 4 (room reserved before the tail)
    const int recCount = static_cast<int>(idCol.size());
    for (int rec = 0; rec < 768 && rec < recCount; ++rec) {
        const i32 id16 = idCol[rec];                                    // word_12CE910
        const u8 role = rec < static_cast<int>(roleBytes.size()) ? roleBytes[rec] : 0;
        if (id16 != -1 && role != 0 && localMaster != id16) {
            v29 += 4;
            ++count;
            // stored value is dword_12CE914[134*rec], bounded; 0 past the column end.
            ids.push_back(rec < static_cast<int>(masterCol.size()) ? masterCol[rec] : 0);
        }
        if (!(v29 < 8))   // do/while bound
            break;
    }
    return ids;
}

// 4-arg compatibility form: compare and store from the same column (callers where
// the id-word and master-dword columns coincide).
std::vector<i32> Theatre_GatherDuelParticipants(const std::vector<i32>& idCol,
                                                const std::vector<u8>& roleBytes,
                                                i32 localMaster, int& count) {
    return Theatre_GatherDuelParticipants(idCol, roleBytes, idCol, localMaster, count);
}

// gilde.exe 0x536c44-0x536c8c style scan: walk records until the role byte matches,
// then return that record's dword; if none match before the table end, return the
// index-0 dword (the original's LABEL fallthrough reads dword_12CE914[idx] with the
// idx left at its last value — for the "found at 0" fast path idx==0).
i32 Theatre_FindByRole(const std::vector<u8>& roleBytes, const std::vector<i32>& dwords,
                       u8 role, i32 fallback) {
    if (!roleBytes.empty() && roleBytes[0] == role)
        return dwords.empty() ? fallback : dwords[0];
    const int recCount = static_cast<int>(roleBytes.size());
    for (int rec = 1; rec < 768 && rec < recCount; ++rec) {
        if (roleBytes[rec] == role)
            return rec < static_cast<int>(dwords.size()) ? dwords[rec] : fallback;
    }
    return fallback;
}

// gilde.exe 0x536fde / 0x5371a7 / 0x5371d3 etc. : disaster date offset.
//   days   = 30 * rand(2);
//   months = rand(4)        if clockMonthWord <  0x17
//   months = rand(4) + 7    if clockMonthWord >= 0x17
void Theatre_ComputeDisasterOffset(const TheatreHooks& h, int clockMonthWord,
                                   i32& outDays, i32& outMonths) {
    const u32 r2 = h.randMod ? h.randMod(2) : 0;
    outDays = 30 * static_cast<i32>(r2);
    const u32 r4 = h.randMod ? h.randMod(4) : 0;
    outMonths = static_cast<i32>(r4);
    if (static_cast<unsigned>(clockMonthWord) >= 0x17u)
        outMonths += 7;
}

bool Theatre_BuildCutsceneRecord(TheatreEvent ev, i32 localMaster, i32 masterDword,
                                 i32 partnerDword, const TheatreHooks& h,
                                 TheatreCutsceneRecord& out) {
    return Theatre_BuildCutsceneRecord(ev, localMaster, masterDword, partnerDword, h,
                                       out, /*weddingPartnerMaster*/ masterDword);
}

bool Theatre_BuildCutsceneRecord(TheatreEvent ev, i32 localMaster, i32 masterDword,
                                 i32 partnerDword, const TheatreHooks& h,
                                 TheatreCutsceneRecord& out,
                                 i32 weddingPartnerMaster) {
    (void)localMaster;
    out = TheatreCutsceneRecord{};
    out.master = masterDword;     // v36 = dword_12CE914[134*localMaster]
    out.master2 = masterDword;    // v43 = v36
    out.subKind = 32;             // v41[0] = 32 (default)
    out.targetId = -1;            // v37 = -1 (default)

    switch (ev) {
    case TheatreEvent::Execution:        // 0x536be8 path (v60), code 5
        out.kind = kTheatreCodeExecution;   // v35 = 5
        out.count = 1;                       // LOBYTE(v42) = 1
        out.extra = 0;                       // LOBYTE(v47) = rand(5) (role pick)
        out.extra = h.randMod ? static_cast<i32>(h.randMod(5)) : 0;
        // master2/3/4 filled from role 13/15/14 lookups by the caller via
        // Theatre_FindByRole; left as master here.
        return true;
    case TheatreEvent::Wedding:          // 0x536ceb path (ChildObjectId), code 6
        out.kind = kTheatreCodeWedding;
        out.count = 2;                       // LOBYTE(v42) = 2
        out.targetId = partnerDword;         // v37 = *(Begin+1) (queried partner)
        out.master2 = weddingPartnerMaster;  // v44 = dword_12CE914[134*localMaster+402]
        return true;
    case TheatreEvent::Bankruptcy:       // 0x536d4f path (v63), code 9
        out.kind = kTheatreCodeBankruptcy;
        out.count = 1;                       // LOBYTE(v42) = 1
        out.targetId = -1;
        return true;
    case TheatreEvent::Death:            // 0x536da6 path (v61), code 8
        out.kind = kTheatreCodeDeath;
        out.count = 1;                       // LOBYTE(v42) = 1
        out.targetId = -1;
        return true;
    case TheatreEvent::Birth:            // 0x536df5 path (v64), code 7
        out.kind = kTheatreCodeBirth;
        out.count = 2;                       // LOBYTE(v42) = 2
        out.targetId = -1;
        out.master2 = masterDword;           // v43 = v44 = v18
        out.master3 = masterDword;
        return true;
    case TheatreEvent::Tenancy: {        // 0x536e70 path (v68), code 10
        out.kind = kTheatreCodeTenancy;
        out.subKind = 34;                    // v41[0] = 34
        out.targetId = -1;
        out.amount = 16000;                  // v48 = 16000
        out.extra = partnerDword;            // v47 = *(v24+1) (queried tenant)
        // participant gathering + count assigned by caller via GatherTenancy.
        return true;
    }
    case TheatreEvent::Duel:             // 0x53722e path (v62), code 4
        out.kind = kTheatreCodeDuel;
        out.subKind = 34;                    // v41[0] = 34
        out.count = 2;                       // LOBYTE(v42) = 2
        out.targetId = -1;
        out.extra = 0;                       // LOBYTE(v47) = 0
        // duel participants gathered by caller via GatherDuel; advance = 30 days.
        return true;
    case TheatreEvent::Plague:
    case TheatreEvent::Fire:
    case TheatreEvent::Storm:
    default:
        return false;   // disaster path — use Theatre_BuildDisasterRecord
    }
}

bool Theatre_BuildDisasterRecord(TheatreEvent ev, i32 srcMaster, int clockMonthWord,
                                 const TheatreHooks& h, TheatreDisasterRecord& out) {
    out = TheatreDisasterRecord{};
    out.srcId = srcMaster;     // v51 = *(dword_6498E4+4)
    out.target = -1;           // v52 = -1
    switch (ev) {
    case TheatreEvent::Plague:        // 0x536f6e path (v65), code 89
        out.kind = kTheatreCodePlague;          // v50 = 89
        out.subKind = 2;                        // v56[0] = 2
        out.v57 = 3 * static_cast<i32>(h.randMod ? h.randMod(3) : 0);  // 3*rand(3)
        Theatre_ComputeDisasterOffset(h, clockMonthWord, out.advanceDays, out.advanceMonths);
        return true;
    case TheatreEvent::Fire:          // 0x537074 path (v66), code 78
        out.kind = kTheatreCodeFire;            // v50 = 78
        out.subKind = 1;                        // v56[0] = 1
        out.v57 = -1;                           // v57 = -1
        out.v58 = 2000;                         // v58 = 2000
        Theatre_ComputeDisasterOffset(h, clockMonthWord, out.advanceDays, out.advanceMonths);
        return true;
    case TheatreEvent::Storm:         // 0x537157 path (v12), code 80
        out.kind = kTheatreCodeStorm;           // v50 = 80
        out.subKind = 2;                        // v56[0] = 2
        out.v57 = -1;                           // v57 = -1
        out.v58 = 1000;                         // v58 = 1000
        Theatre_ComputeDisasterOffset(h, clockMonthWord, out.advanceDays, out.advanceMonths);
        return true;
    default:
        return false;
    }
}

} // namespace guild::play
