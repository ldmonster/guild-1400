#include "world/office_assign.h"

#include <cstring>

#include "world/office.h"

// Faithful 1:1 port of the VIBE_Office_* holder-mutation rules (gilde.exe
// 0x47e1b8..0x47ee44). The originals address the holder table through parallel
// named globals at fixed 24-byte-record offsets:
//   byte_B59848[24*i]   holder char id (+0)      -> g_officeHolders[i].holder
//   dword_B5984C[6*i]   city/owner id  (+4)      -> g_officeHolders[i].city
//   byte_B59850[24*i]   office type    (+8)      -> g_officeHolders[i].type
//   dword_B59854[6*i]   rank-level     (+12)     -> g_officeHolders[i].rank
//   byte_B59858[24*i]   state          (+16)     -> g_officeHolders[i].state
//   dword_B5985C[6*i]   secondary id   (+20)     -> g_officeHolders[i].secondary
// Those become field access on g_officeHolders[i]. The candidacy/transfer scans
// keep the exact 30-entry (720-byte) vs 37-entry (888-byte) bounds per function.

namespace guild::world {

// ===========================================================================
// Command / notify hooks (mock lockstep queue).
// ===========================================================================
namespace {
constexpr int kCmdLogCap = 16;
OfficeCommand g_cmdLog[kCmdLogCap];
int g_cmdLogCount = 0;

void DefaultCommandHook(const OfficeCommand& cmd, void*) {
    if (g_cmdLogCount < kCmdLogCap)
        g_cmdLog[g_cmdLogCount++] = cmd;
}
OfficeCommandHook g_cmdHook = &DefaultCommandHook;
void* g_cmdCtx = nullptr;

constexpr int kNotifyLogCap = 16;
OfficeNotify g_notifyLog[kNotifyLogCap];
int g_notifyLogCount = 0;

void DefaultNotifyHook(const OfficeNotify& n, void*) {
    if (g_notifyLogCount < kNotifyLogCap)
        g_notifyLog[g_notifyLogCount++] = n;
}
OfficeNotifyHook g_notifyHook = &DefaultNotifyHook;
void* g_notifyCtx = nullptr;

void EmitCommand(const OfficeCommand& cmd) { g_cmdHook(cmd, g_cmdCtx); }
void EmitNotify(const OfficeNotify& n) { g_notifyHook(n, g_notifyCtx); }
} // namespace

void OfficeSetCommandHook(OfficeCommandHook hook, void* ctx) {
    g_cmdHook = hook ? hook : &DefaultCommandHook;
    g_cmdCtx = hook ? ctx : nullptr;
}
void OfficeCommandLogReset() {
    g_cmdHook = &DefaultCommandHook;
    g_cmdCtx = nullptr;
    g_cmdLogCount = 0;
}
const OfficeCommand* OfficeCommandLog(int* outCount) {
    if (outCount) *outCount = g_cmdLogCount;
    return g_cmdLog;
}

void OfficeSetNotifyHook(OfficeNotifyHook hook, void* ctx) {
    g_notifyHook = hook ? hook : &DefaultNotifyHook;
    g_notifyCtx = hook ? ctx : nullptr;
}
void OfficeNotifyLogReset() {
    g_notifyHook = &DefaultNotifyHook;
    g_notifyCtx = nullptr;
    g_notifyLogCount = 0;
}
const OfficeNotify* OfficeNotifyLog(int* outCount) {
    if (outCount) *outCount = g_notifyLogCount;
    return g_notifyLog;
}

// ===========================================================================
// Shared slot scans (the exact "find first slot whose +0 holder == key" loops).
// ===========================================================================
namespace {
// 30-entry / 720-byte bound (the candidacy rules). Returns index, or >=30 fail.
int FindSlotByHolder720(u8 key) {
    int idx = 0;
    if (g_officeHolders[0].holder != key) {
        int byteOff = 0;
        do {
            byteOff += kOfficeHolderStride;
            ++idx;
        } while (byteOff < 720 && g_officeHolders[idx].holder != key);
    }
    return idx;
}

// 37-entry / 888-byte bound (transfer / add-entry). Returns index, or >=37 fail.
int FindSlotByHolder888(u8 key) {
    int idx = 0;
    if (g_officeHolders[0].holder != key) {
        int byteOff = 0;
        do {
            byteOff += kOfficeHolderStride;
            ++idx;
        } while (byteOff < 888 && g_officeHolders[idx].holder != key);
    }
    return idx;
}
} // namespace

// ===========================================================================
// gilde.exe 0x47e4e0 — VIBE_Office_AssignToCandidate.
// ===========================================================================
int OfficeAssignToCandidate(const AssignRequest& req, OfficePersonStore& ps) {
    OfficePersonRec* rec = ps.Find(req.candidateId);
    // !RecordById || *RecordById == -1 || *(rec+360)
    if (!rec || !rec->valid || rec->office360)
        return 0;

    int i = FindSlotByHolder720(req.keyA);     // v3
    if (i >= 30)
        return 0;
    if (req.officeType != g_officeHolders[i].type) // v2[6] != byte_B59850[24*v3]
        return 0;

    if (req.keyB == 0xFF) {
        // Single-slot path.
        OfficeHolder& a = g_officeHolders[i];
        if (a.city != -1 || a.state != 3 || a.rank >= 4)
            return 0;
        rec->office360 = req.officeType; // *(v13+360) = v2[6]
        ++a.rank;                        // ++dword_B59854[6*v3]
        return 1;
    }

    // Two-slot path: find the keyB slot.
    int j = FindSlotByHolder720(req.keyB);     // v5
    if (j >= 30)
        return 0;
    u8 typeI = g_officeHolders[i].type;        // v7
    u8 typeJ = g_officeHolders[j].type;        // v8
    if (typeI != typeJ)
        return 0;
    // v9 = &dword_62EC8E[4*v8 - v7] + 2 : the def record for type (typeI==typeJ);
    // (unsigned __int8)*v9 == record id byte; v9[1] == flag dword. The original
    // requires id == officeType and flag != 0.
    if (OfficeDefId(typeJ) != req.officeType)
        return 0;
    if (!OfficeDefFlag(typeJ))
        return 0;

    OfficeHolder& a = g_officeHolders[i];
    OfficeHolder& b = g_officeHolders[j];
    if (a.city != -1) return 0;
    if (a.state != 3) return 0;
    if (a.rank >= 4)  return 0;
    if (b.city != -1 || a.state != 3 || b.rank >= 4) return 0;

    rec->office360 = req.officeType;
    ++a.rank;
    ++b.rank;
    return 1;
}

// ===========================================================================
// gilde.exe 0x47e1b8 — VIBE_Office_ApplyForCandidacy.
// ===========================================================================
int OfficeApplyForCandidacy(OfficePersonRec& applicant, u8 officeType) {
    if (applicant.office360)             // *(a1+360)
        return -1;

    // First scan: find a slot whose type (+8) == officeType (30-entry bound).
    int v4 = 0;
    int i;
    for (i = 0; i < 720; i += kOfficeHolderStride) {
        if (g_officeHolders[v4].type == officeType)
            break;
        ++v4;
    }
    if (v4 >= 30)
        return -1;

    // v7 = def flag for the matched slot's type.
    i32 v7 = OfficeDefFlag(g_officeHolders[v4].type);

    u8 holderA = 0xFF; // v19
    u8 holderB = 0xFF; // v20
    i32 applicantId = -1; // v18

    if (v7) {
        // Two-holder office: try to claim up to two open slots.
        bool haveFirst = false; // v22 / v8
        bool emit = false;      // v8 (any open found)
        OfficeHolder& s0 = g_officeHolders[v4];
        if (s0.city == -1 && s0.state == 3 && s0.rank < 4) {
            emit = true;
            haveFirst = true;
            applicantId = applicant.ownerId;
            holderA = s0.holder;
        }
        // Advance to the next same-type slot.
        int v4b = v4 + 1;
        if (v4b < 30) {
            int v11 = kOfficeHolderStride * v4b;
            do {
                if (g_officeHolders[v4b].type == officeType)
                    break;
                v11 += kOfficeHolderStride;
                ++v4b;
            } while (v11 < 720);
        }
        if (v4b < 30) {
            OfficeHolder& s1 = g_officeHolders[v4b];
            if (s1.city == -1 && s1.state == 3 && s1.rank < 4) {
                if (!haveFirst) {
                    applicantId = applicant.ownerId;
                    holderA = s1.holder;
                    // goto LABEL_29 (emit single)
                    OfficeCommand cmd{68, applicantId, -1, holderA, 0xFF,
                                      officeType, 0};
                    EmitCommand(cmd);
                    return 1;
                }
                holderB = s1.holder;
            }
        }
        if (!emit)
            goto single_path; // !v8 -> fall to LABEL_20

        // LABEL_29: emit the candidacy command (one or two holders).
        {
            OfficeCommand cmd{68, applicantId, -1, holderA, holderB,
                              officeType, 0};
            EmitCommand(cmd);
        }
        return 1;
    }

single_path:
    {
        OfficeHolder& s = g_officeHolders[v4];
        if (s.city != -1 || s.state != 3 || s.rank >= 4)
            return -1;
        OfficeCommand cmd{68, applicant.ownerId, -1, s.holder, 0xFF,
                          officeType, 0};
        EmitCommand(cmd);
        return 1;
    }
}

// ===========================================================================
// gilde.exe 0x47e750 — VIBE_Office_AddTableEntry.
// ===========================================================================
int OfficeAddTableEntry(u8 holderKey, const OfficePersonRec* primary,
                        u8 officeType, const OfficePersonRec* secondary,
                        u8 state) {
    i32 idA = primary   ? primary->ownerId   : -1; // v5 = a2 ? *(a2+4) : -1
    i32 idB = secondary ? secondary->ownerId : -1; // v6 = a4 ? *(a4+4) : -1

    int idx = FindSlotByHolder888(holderKey);
    if (idx >= 37)
        return -1;

    // Pack (v10 holderKey, v11 idA, v12 officeType, v13 state, v14 idB) and emit.
    OfficeCommand cmd{69, idA, idB, holderKey, 0xFF, officeType, state};
    EmitCommand(cmd);
    return 0; // VIBE_Command_RequestBuildOp69 result (committed)
}

// gilde.exe 0x47e6e4 — VIBE_Office_AddEntryForCharacter.
int OfficeAddEntryForCharacter(i32 cityId, const OfficePersonRec* successor,
                               OfficePersonStore& ps) {
    // GetHolderEntryByCity(cityId) fills a 24-byte entry; v5[0] == holder char id.
    OfficeHolder entry;
    OfficePersonRec* cityRec = ps.Find(cityId);
    // GetHolderEntryByCity gates on the city's person record being valid + +358.
    if (!cityRec || !cityRec->valid || !cityRec->office358)
        return -1;
    // Find the slot whose city (+4) == cityId.
    bool found = false;
    for (int i = 0; i < kOfficeDefCount; ++i) {
        if (g_officeHolders[i].city == cityId) { entry = g_officeHolders[i]; found = true; break; }
    }
    if (!found || !successor)
        return -1;
    return OfficeAddTableEntry(entry.holder, cityRec, /*officeType*/2, successor, 1);
}

// gilde.exe 0x47e72c — VIBE_Office_AddEntryIfValid.
int OfficeAddEntryIfValid(const OfficePersonRec* rec, u8 officeType, u8 state) {
    if (!rec)
        return -1;
    return OfficeAddTableEntry(officeType, rec, 1, nullptr, state);
}

// ===========================================================================
// gilde.exe 0x47e870 — VIBE_Office_TransferHoldership.
// ===========================================================================
int OfficeTransferHoldership(const TransferRequest& req, OfficePersonStore& ps) {
    // v17 = the seated person at +7 (office fields updated; stored as secondary).
    OfficePersonRec* v17 = nullptr;
    if (req.seatedId != -1) {
        v17 = ps.Find(req.seatedId);
        if (!v17)
            return 0;
    }
    // v18 = the +1 person's id (the new slot owner). -1 when +1 is -1.
    i32 v18 = -1;
    if (req.primaryId != -1) {
        OfficePersonRec* primaryRec = ps.Find(req.primaryId);
        if (!primaryRec)
            return 0;
        v18 = primaryRec->ownerId;
    }

    int slot = FindSlotByHolder888(req.holderKey); // v3
    if (slot >= 37)
        return 0;

    OfficeHolder& s = g_officeHolders[slot];

    // The current occupant (the slot's city/owner) loses the seat if it differs
    // from the new primary (a1+1) id.
    OfficePersonRec* occupant = ps.Find(s.city);
    if (occupant && occupant->ownerId != req.primaryId) {
        u8 v9 = s.type;                   // byte_B59850[v5]
        if (occupant->office358 == v9)
            occupant->office358 = 0;
        else if (occupant->office361 == v9)
            occupant->office361 = 0;
    }

    // Update the seated person v17 (the original keys on edx != 0).
    if (v17) {
        u8 slotType = s.type;             // byte_B59850[24*v8]
        if (slotType > 0x1B) {
            // High office: drives the +361 secondary field, clearing stale slots.
            if (v17->office361) {
                for (int k = 0; k < kOfficeDefCount; ++k) {
                    if (g_officeHolders[k].type == v17->office361 &&
                        g_officeHolders[k].city == v17->ownerId)
                        g_officeHolders[k].city = -1;
                }
            }
            v17->office361 = slotType;
        } else {
            // Normal office: clear the seated person's prior primary seat (the slot
            // whose owner == v18) if it currently holds an office (+358 set).
            if (v17->office358) {
                for (int k = 0; k < kOfficeDefCount; ++k) {
                    if (g_officeHolders[k].city == v18) {
                        g_officeHolders[k].city      = -1;
                        g_officeHolders[k].secondary = -1;
                        g_officeHolders[k].state     = 4;
                        g_officeHolders[k].rank      = 0;
                        break; // LABEL_17 then stops
                    }
                }
            }
            // Rank bookkeeping via the def book-cat compare (byte_62EC92).
            if (OfficeDefBookCat(slotType) >= OfficeDefBookCat(v17->office358))
                v17->office359 = slotType; // +359
            v17->office358 = slotType;     // +358
            if (v17->office360 == v17->office358)
                v17->office360 = 0;        // +360 candidacy cleared
            // If the office def flag is set, decrement a partner slot's rank.
            if (OfficeDefFlag(slotType)) {
                for (int k = 0; k < kOfficeDefCount; ++k) {
                    if (g_officeHolders[k].type == slotType && k != slot) {
                        if (g_officeHolders[k].state == 3) {
                            int v12 = g_officeHolders[k].rank;
                            if (v12) {
                                g_officeHolders[k].rank = v12 - 1;
                                if (v12 == 1)
                                    g_officeHolders[k].state = 4;
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    // LABEL_30: install into the slot.
    s.city      = v18;                     // dword_B5984C[v13] = v18 (the +1 owner)
    s.secondary = req.seatedId;            // dword_B5985C[v13] = *(a1+7)
    s.state     = req.state;               // byte_B59858 = v14
    if (req.state == 3)
        s.rank = 0;

    // Notify on the master-visible path (v17 && occupant).
    if (v17 && occupant) {
        OfficeNotify n{OfficeNotify::Transfer, v17->ownerId, occupant->ownerId, 0};
        EmitNotify(n);
    }
    return 1;
}

// ===========================================================================
// gilde.exe 0x47ec64 — VIBE_Office_SwapHolders.
// ===========================================================================
int OfficeSwapHolders(const SwapRequest& req, OfficePersonStore& ps) {
    OfficePersonRec* a = ps.Find(req.personAId); // v13
    if (!a)
        return 0;
    if (a->money < req.cost)                      // *(rec+404) < cost
        return 0;

    // Slot A: first slot whose holder char-id (+0) == keyA (30-entry bound).
    int slotA = -1;
    for (int i = 0; i < 30; ++i) {
        if (g_officeHolders[i].holder == req.keyA) { slotA = i; break; }
    }
    if (slotA < 0)
        return 0;

    // Person seated in slot A == the slot's city/owner id.
    OfficePersonRec* pA = ps.Find(g_officeHolders[slotA].city); // v12
    if (!pA)
        return 0;

    // Slot B: first slot whose holder char-id (+0) == keyB.
    int slotB = -1;
    for (int i = 0; i < 30; ++i) {
        if (g_officeHolders[i].holder == req.keyB) { slotB = i; break; }
    }
    if (slotB < 0)
        return 0;

    OfficePersonRec* pB = ps.Find(g_officeHolders[slotB].city); // v9
    if (!pB)
        return 0;

    OfficeNotify n{OfficeNotify::Swap, a->ownerId, pA->ownerId, pB->ownerId};
    EmitNotify(n);

    // Swap the seated persons' office-type fields (+358), taking the OTHER slot's
    // type byte (v5[8] == slotB type for pA; v4[8] == slotA type for pB).
    pA->office358 = g_officeHolders[slotB].type;
    pB->office358 = g_officeHolders[slotA].type;
    // Swap the two slots' city/owner ids (+4).
    i32 tmp = g_officeHolders[slotA].city;
    g_officeHolders[slotA].city = g_officeHolders[slotB].city;
    g_officeHolders[slotB].city = tmp;
    // Charge personA the cost.
    a->money -= req.cost;
    return 1;
}

// ===========================================================================
// gilde.exe 0x47ed68 — VIBE_Office_ReleaseCharacterHoldings.
// ===========================================================================
int OfficeReleaseCharacterHoldings(OfficePersonRec& person, bool vacantStateBig) {
    int changed = 0;                       // v2 (bit0 == changed)
    u8 vacantState = vacantStateBig ? 4 : 3; // v8 + 3
    i32 id = person.ownerId;               // *(a1+4)

    // Scan slots 0..36 (loop bound v3 != 222, step 6 -> 37 records).
    for (int i = 0; i < kOfficeDefCount; ++i) {
        OfficeHolder& s = g_officeHolders[i];
        if (s.city == id) {
            s.city      = -1;
            s.secondary = -1;
            s.state     = vacantState;
            changed |= 1;
            s.rank      = 0;
        } else if (s.secondary == id) {
            s.secondary = -1;
            s.state     = 1;
        }
    }
    // Decrement the candidacy slot's rank (the +360 office type).
    if (person.office360) {
        for (int i = 0; i < kOfficeDefCount; ++i) {
            if (g_officeHolders[i].type == person.office360) {
                int v6 = g_officeHolders[i].rank;
                if (v6 > 0) {
                    changed |= 1;
                    g_officeHolders[i].rank = v6 - 1;
                }
            }
        }
    }
    person.office358 = 0;
    person.office361 = 0;
    person.office360 = 0;
    return changed;
}

// ===========================================================================
// gilde.exe 0x47ee44 — VIBE_Office_ClearCharacterHoldings.
// ===========================================================================
int OfficeClearCharacterHoldings(OfficePersonRec& person, bool full,
                                 bool vacantStateBig) {
    u8 vacantState = vacantStateBig ? 4 : 3;
    i32 id = person.ownerId;

    for (int i = 0; i < kOfficeDefCount; ++i) {
        OfficeHolder& s = g_officeHolders[i];
        if (s.city == id && full) {
            s.city      = -1;
            s.secondary = -1;
            s.state     = vacantState;
            s.rank      = 0;
        } else if (s.secondary == id) {
            s.secondary = -1;
            s.state     = 1;
        }
    }
    if (person.office360) {
        for (int i = 0; i < kOfficeDefCount; ++i) {
            if (g_officeHolders[i].type == person.office360) {
                int v5 = g_officeHolders[i].rank;
                if (v5 > 0)
                    g_officeHolders[i].rank = v5 - 1;
            }
        }
    }
    if (full) {
        person.office358 = 0;
        person.office361 = 0;
    }
    person.office360 = 0;
    return 222 * 4; // the original returns result*4 with result==222
}

// ===========================================================================
// gilde.exe 0x47e7d8 — VIBE_Office_CheckPrerequisitesMet.
// ===========================================================================
int OfficeCheckPrerequisitesMet(u8 holderKey, u8 officeType, i32 idA, i32 idB,
                                OfficePersonStore& ps) {
    if (officeType == 0xFF)                 // *(a1+6) == 0xFF
        return 1;
    int idx = FindSlotByHolder888(holderKey);
    if (idx >= 37)
        return 0;
    if (g_officeHolders[idx].state != 1)    // byte_B59858[24*v2] != 1
        return 0;
    if (g_officeHolders[idx].secondary != -1) // dword_B5985C[6*v2] != -1
        return 0;
    // Both referenced persons must resolve (or be -1).
    if (idA != -1 && !ps.Find(idA))
        return 0;
    if (idB == -1)
        return 1;
    return ps.Find(idB) ? 1 : 0;
}

} // namespace guild::world
