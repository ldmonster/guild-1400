#include "world/guild_assignment.h"

#include <vector>

#include "util/math_random.h"   // guild::util::RandomModulo (VIBE_Math_RandomModulo)
#include "world/amt_slot_table.h" // AmtSlot, kAmtSlotFreeHi

// Faithful 1:1 port of the Amt guild membership / office-assignment passes from
// gilde.exe. The deterministic counting / relation-delta / vote-tally / tie-break
// math is recovered exactly; the Person lookups, AI scoring and lockstep command
// commits route through the GuildAssignContext hooks (engine-owned).
//
// Recovered tables (from get_bytes):
//   dword_47DE24 @0x47DE24 = {0,0,0}            (approach-bucket tally init)
//   dword_47DE30 @0x47DE30 = {0,0,0,0}          (successor vote-tally init)
//   dword_47DE40 @0x47DE40 = {-1,-1,-1,-1,-1,-1}(candidate-id slot init)

namespace guild::world {

i32 g_guildSlotKeyCounter = 0; // gilde.exe dword_63D734 (placement-key counter)

namespace {

// Holder-entry field accessors (the OfficeHolder 24-byte layout, law_types.h).
//   primary holder id : entry+4  (the original's *((_DWORD*)v72 + 1))
//   secondary id      : entry+20 (the original's *((_DWORD*)v72 + 5) == .secondary)
//   state             : entry+16 (.state, v72[16])
//   key byte          : entry+0  (.holder, *v72 — passed to AddTableEntry)
//   type byte         : entry+8  (.type, v63[v7+8])
//   successor count   : entry+12 (.rank, *(int*)&v63[v7+12])
inline i32 PrimaryId(const OfficeHolder& h)   { return h.city; }       // +4
inline i32 SecondaryId(const OfficeHolder& h) { return h.secondary; }  // +20
inline u8  KeyByte(const OfficeHolder& h)     { return h.holder; }     // +0
inline u8  TypeByte(const OfficeHolder& h)    { return h.type; }       // +8
inline i32 SuccCount(const OfficeHolder& h)   { return h.rank; }       // +12

} // namespace

// ===========================================================================
// 1. ComputeGuildAssignment — gilde.exe 0x47ff5c.
// ===========================================================================
GuildAssignmentResult ComputeGuildAssignment(OfficeHolder* holders, int count,
                                             GuildAssignContext& gx) {
    GuildAssignmentResult r;
    if (!holders || count <= 0)
        return r;

    // --- Pass A setup: count state-2 entries (members to assign) (v64). ------
    int membersToAssign = 0; // v64
    for (int i = 0; i < count; ++i)
        if (holders[i].state == kHolderMemberToAssign)
            ++membersToAssign;
    r.memberSeats = membersToAssign;

    // --- Pass A: per state-2 entry, poll voters, install or clear. -----------
    for (int e = 0; e < count; ++e) {
        if (membersToAssign == 0)
            break; // the original bails the whole sweep once none remain (v3==0)

        OfficeHolder& entry = holders[e];

        // dword_47DE24 = {0,0,0}: the three approach-bucket tallies (v59/v60/v61).
        i32 bucket[3] = {0, 0, 0};

        if (entry.state != kHolderMemberToAssign)
            continue;

        --membersToAssign; // v64 = v3 - 1

        GuildPersonRec primary =
            gx.findPerson ? gx.findPerson(PrimaryId(entry), gx.ctx) : GuildPersonRec{};
        GuildPersonRec secondary =
            gx.findPerson ? gx.findPerson(SecondaryId(entry), gx.ctx) : GuildPersonRec{};
        // The original gates on (v11 == secondary id != 0) && (v10 == secondary
        // record found). The primary record is looked up but only its id is used.
        if (SecondaryId(entry) == 0 || !secondary.valid)
            continue;
        (void)primary;

        // Per-voter approach buckets; v54[v/4+6] holds each voter's approach in the
        // original. approaches[v] in {0,1,2} or 3 (== "did not vote", the init).
        std::vector<u8> approaches(static_cast<size_t>(count), 3);
        for (int v = 0; v < count; ++v) {
            GuildPersonRec voter =
                gx.findPerson ? gx.findPerson(PrimaryId(holders[v]), gx.ctx)
                              : GuildPersonRec{};
            if (voter.valid && voter.eligible && !voter.excluded && gx.approachDir) {
                int dir = gx.approachDir(PrimaryId(holders[v]), PrimaryId(entry),
                                         SecondaryId(entry), gx.ctx);
                if (dir >= 0 && dir < 3) {
                    ++bucket[dir];
                    approaches[static_cast<size_t>(v)] = static_cast<u8>(dir);
                }
            }
        }

        // The "target" both branches relate toward / install is the PRIMARY person
        // (RecordById == FindRecordById(primary id); its +4 == primary id). The
        // secondary record is only passed to the AI approach evaluator as context.
        const i32 targetId = PrimaryId(entry);

        if (bucket[0] <= bucket[1]) {
            // --- WIN branch: install the primary holder, emit win relations. ---
            for (int v = 0; v < count; ++v) {
                i32 voterId = PrimaryId(holders[v]);
                i32 holderId = targetId;
                if (voterId != holderId && voterId != -1) {
                    u8 ap = approaches[static_cast<size_t>(v)];
                    if (ap < 3) {
                        i32 delta;
                        if (ap == 0)       delta = -20;
                        else if (ap == 1)  delta = 20;
                        else               delta = 4;
                        if (gx.relation) gx.relation(voterId, holderId, delta, gx.ctx);
                        ++r.relations;
                    }
                }
            }
            if (gx.install)
                gx.install(KeyByte(entry), targetId, 1, gx.ctx);
            ++r.installs;
        } else {
            // --- LOSE branch: emit lose relations, clear the seat (state 3). ----
            for (int v = 0; v < count; ++v) {
                i32 voterId = PrimaryId(holders[v]);
                i32 holderId = targetId;
                if (voterId != holderId && voterId != -1) {
                    u8 ap = approaches[static_cast<size_t>(v)];
                    if (ap < 3) {
                        if (ap == 0) {
                            if (gx.relation) gx.relation(voterId, holderId, -10, gx.ctx);
                            ++r.relations;
                        }
                        i32 delta = (ap == 1) ? 10 : -4;
                        if (gx.relation) gx.relation(voterId, holderId, delta, gx.ctx);
                        ++r.relations;
                    }
                }
            }
            if (gx.install)
                gx.install(KeyByte(entry), 0, 3, gx.ctx);
            ++r.clears;
        }
    }

    // --- Pass B setup: count state-3 entries (seats needing a successor) (v84). -
    int successorsNeeded = 0; // v84
    for (int i = 0; i < count; ++i)
        if (holders[i].state == kHolderNeedsSuccessor)
            ++successorsNeeded;
    r.successorSeats = successorsNeeded;

    // --- Pass B: per state-3 entry with a positive successor count, tally votes.
    for (int e = 0; e < count; ++e) {
        if (successorsNeeded == 0)
            break;

        // dword_47DE30 = {0,0,0,0}: the four candidate vote tallies (v55).
        i32 voteTally[4] = {0, 0, 0, 0};
        // dword_47DE40 = {-1,-1,-1,-1,-1,-1}: candidate slot ids (v54[0..5] init).
        i32 candId[4]   = {-1, -1, -1, -1};

        OfficeHolder& entry = holders[e];
        if (entry.state != kHolderNeedsSuccessor || SuccCount(entry) <= 0)
            continue;

        --successorsNeeded;

        // Collect up to 4 office-objects whose category matches the entry type, but
        // no more than SuccCount(entry) of them (v32 cap == *(+12)).
        int nCand = 0; // v32
        for (int k = 0; k < gx.objectCount && k < kGuildObjectCount; ++k) {
            if (nCand >= SuccCount(entry) || nCand >= 4)
                break;
            if (gx.objects[k].category == TypeByte(entry)) {
                candId[nCand] = gx.objects[k].id; // v57[..+3] = object id
                ++nCand;
            }
        }
        if (nCand == 0)
            continue;

        if (nCand == 1) {
            // Single candidate -> install directly.
            if (gx.install)
                gx.install(KeyByte(entry), candId[0], 1, gx.ctx);
            ++r.installs;
            continue;
        }

        // Pad the unused candidate slots (the original fills v57[..]=-1, tally=0).
        for (int j = nCand; j < 4; ++j) {
            candId[j]   = -1;
            voteTally[j] = 0;
        }

        // Each voter's PickBestTarget vote (-1 == abstain).
        for (int v = 0; v < count; ++v) {
            i32 vote = -1; // v54[v/4] = -1
            GuildPersonRec voter =
                gx.findPerson ? gx.findPerson(PrimaryId(holders[v]), gx.ctx)
                              : GuildPersonRec{};
            if (voter.valid && !voter.excluded && gx.isNextRank &&
                gx.isNextRank(PrimaryId(holders[v]), TypeByte(entry), gx.ctx) &&
                gx.pickTarget) {
                vote = gx.pickTarget(PrimaryId(holders[v]), TypeByte(entry), gx.ctx);
            }
            // Tally the vote against the candidate ids.
            if (vote != -1) {
                for (int j = 0; j < nCand; ++j)
                    if (vote == candId[j])
                        ++voteTally[j];
            }
        }

        // Pick the winner: track the max; on a tie pick a random one (the v41..v48
        // block) via VIBE_Math_RandomModulo.
        bool tie = true;   // v41 (seeded 1, cleared on a strict max)
        i32  best = -1;    // v42
        int  bestIdx = -1; // v44
        for (int j = 0; j < nCand; ++j) {
            i32 t = voteTally[j];
            if (best >= t) {
                if (best == t)
                    tie = true;
            } else {
                best = t;
                bestIdx = j;
                tie = false;
            }
        }

        int chosen = bestIdx;
        if (tie) {
            int rem = nCand;
            int j = static_cast<u16>(guild::util::RandomModulo(static_cast<u16>(nCand)));
            bool found = false;
            while (voteTally[j] != best) {
                j = (j + 1) % nCand;
                if (--rem == 0) { chosen = bestIdx; found = true; break; }
            }
            if (!found)
                chosen = j; // the loop exited because voteTally[j] == best
        }
        if (chosen >= 0 && gx.install)
            gx.install(KeyByte(entry), candId[chosen], 1, gx.ctx);
        if (chosen >= 0)
            ++r.installs;
    }

    return r;
}

// ===========================================================================
// 2. PersonHasOfficeObject — gilde.exe 0x480abc.
// ===========================================================================
bool PersonHasOfficeObject(const OfficeHolder& entry, GuildAssignContext& gx) {
    // if ( *(_BYTE *)(a2 + 16) != 2 ) return 0;
    if (entry.state != kHolderMemberToAssign)
        return false;

    // VIBE_Office_CollectByCategory(...) -> the object list (gx.objects). The
    // original resolves two person records: the secondary (+20) and the primary
    // (+4). The gate: secondary exists & has building & not excluded (+433),
    // primary exists & has building, and the object list non-empty.
    GuildPersonRec secondary =
        gx.findPerson ? gx.findPerson(SecondaryId(entry), gx.ctx) : GuildPersonRec{};
    GuildPersonRec primary =
        gx.findPerson ? gx.findPerson(PrimaryId(entry), gx.ctx) : GuildPersonRec{};

    if (!secondary.valid || !secondary.hasBuilding || secondary.excluded ||
        !primary.valid || !primary.hasBuilding || gx.objectCount <= 0)
        return false;

    // Scan the object list for one carrying the secondary (+20) id.
    for (int k = 0; k < gx.objectCount && k < kGuildObjectCount; ++k) {
        if (gx.objects[k].id == SecondaryId(entry))
            return true;
    }
    return false;
}

// ===========================================================================
// 3. CheckGuildMastersPresent — gilde.exe 0x48091c.
// ===========================================================================
bool CheckGuildMastersPresent(const OfficeHolder* holders, int count,
                              u8 category, GuildAssignContext& gx) {
    (void)category;
    if (!holders || count <= 0)
        return false;

    u8 flags = 0; // v2 ; bit0 = member present, bit1 = master present

    for (int e = 0; e < count; ++e) {
        const OfficeHolder& entry = holders[e];
        if (entry.state == kHolderMemberToAssign) {
            GuildPersonRec secondary =
                gx.findPerson ? gx.findPerson(SecondaryId(entry), gx.ctx) : GuildPersonRec{};
            GuildPersonRec primary =
                gx.findPerson ? gx.findPerson(PrimaryId(entry), gx.ctx) : GuildPersonRec{};
            if (secondary.valid && secondary.hasBuilding && !secondary.excluded &&
                primary.valid && primary.hasBuilding && count > 0) {
                // does the secondary (+20) id reappear among the entries' (+4) ids?
                for (int j = 0; j < count; ++j) {
                    if (PrimaryId(holders[j]) == SecondaryId(entry)) {
                        flags |= 1u;
                        break;
                    }
                }
            }
        } else if (entry.state == kHolderNeedsSuccessor && SuccCount(entry) > 0) {
            // scan the office-object table for a matching-category master.
            for (int k = 0; k < gx.objectCount && k < kGuildObjectCount; ++k) {
                if (gx.objects[k].category == TypeByte(entry)) {
                    flags |= 1u;
                    u8 role = gx.objects[k].promoFlag; // byte_12CE912
                    if ((role == 6 || role == 7) && !gx.objects[k].busyFlag) {
                        flags |= 2u;
                        break;
                    }
                }
            }
        }
    }

    // The trailing pass: if a member is present, look for a swept person whose
    // profession (+2) is 6|7 and is not excluded -> a master is present.
    if ((flags & 1) != 0 && count > 0) {
        for (int j = 0; j < count; ++j) {
            GuildPersonRec p =
                gx.findPerson ? gx.findPerson(PrimaryId(holders[j]), gx.ctx) : GuildPersonRec{};
            if (p.valid && (p.profession == 6 || p.profession == 7) && !p.excluded) {
                flags |= 2u;
                break;
            }
        }
    }

    return (flags & 2) != 0;
}

// ===========================================================================
// 4. AssignGuildMembers — gilde.exe 0x480634.
// ===========================================================================
// dword_47DE58: the recovered 6-category key table. The original packs each
// category's key in the HIBYTE of the +21 dword of v20 records and a rank byte; the
// CollectByCategory key is HIBYTE(*(+21)). Modeled as a {key,rank} pair table. The
// raw key bytes are recovered from the holder/category cluster; for the standalone
// math we expose the structure and let the caller's collect hook supply entries.
namespace {
const GuildCategoryRule kGuildCategories[kGuildCategoryCount] = {
    {14, 0x1E}, {18, 0x1F}, {20, 0x20}, {21, 0x21}, {23, 0x22}, {34, 0x23},
};
} // namespace

const GuildCategoryRule* GuildCategoryTable() { return kGuildCategories; }

int AssignGuildMembers(GuildCollectHolders collect, GuildAssignContext& gx) {
    int masterLatch = 0; // v23
    OfficeHolder buf[64]; // v19 (the 144-byte CollectByCategory scratch == 6 ent.)

    for (int c = 0; c < kGuildCategoryCount; ++c) {
        u8 catKey = kGuildCategories[c].categoryKey;
        int n = collect ? collect(catKey, buf, 64, gx.ctx) : 0;

        u8 catFlags = 0; // v20[v2] ; bit0 member present, bit1 master present

        for (int i = 0; i < n; ++i) {
            const OfficeHolder& entry = buf[i];
            if (entry.state == kHolderMemberToAssign) {
                GuildPersonRec secondary =
                    gx.findPerson ? gx.findPerson(SecondaryId(entry), gx.ctx) : GuildPersonRec{};
                GuildPersonRec primary =
                    gx.findPerson ? gx.findPerson(PrimaryId(entry), gx.ctx) : GuildPersonRec{};
                if (secondary.valid && secondary.hasBuilding &&
                    primary.valid && primary.hasBuilding) {
                    catFlags |= 1u;
                } else if (primary.valid && primary.hasBuilding) {
                    if (gx.install) gx.install(KeyByte(entry), 0, 4, gx.ctx);
                } else {
                    if (gx.install) gx.install(KeyByte(entry), SecondaryId(entry), 1, gx.ctx);
                }
            } else if (entry.state == kHolderNeedsSuccessor && SuccCount(entry) > 0) {
                for (int k = 0; k < gx.objectCount && k < kGuildObjectCount; ++k) {
                    if (gx.objects[k].category == TypeByte(entry)) {
                        catFlags |= 1u;
                        u8 role = gx.objects[k].promoFlag;
                        if ((role == 6 || role == 7) && !gx.objects[k].busyFlag) {
                            catFlags |= 2u;
                            break;
                        }
                    }
                }
            }
        }

        // member-present + look for a master person among the swept entries.
        if ((catFlags & 1) != 0 && n > 0) {
            for (int j = 0; j < n; ++j) {
                GuildPersonRec p =
                    gx.findPerson ? gx.findPerson(PrimaryId(buf[j]), gx.ctx) : GuildPersonRec{};
                if (p.valid && (p.profession == 6 || p.profession == 7) && !p.excluded) {
                    catFlags |= 2u;
                    break;
                }
            }
        }

        if ((catFlags & 2) != 0) {
            masterLatch = 1;
        } else if ((catFlags & 1) != 0 && !masterLatch) {
            ComputeGuildAssignment(buf, n, gx);
        }
    }

    return masterLatch;
}

// ===========================================================================
// 5. HasOccupiedOffice — gilde.exe 0x480cb4.
// ===========================================================================
bool HasOccupiedOffice(const GuildSeatView* seats, int count,
                       u8 (*categoryOf)(u8 officeType, void* ctx),
                       bool (*personLive)(i32 city, void* ctx), void* ctx) {
    if (!seats || count <= 0)
        return false;

    // The original walks from index 216 backwards in steps of 6, with a budget of 7
    // (v0). It returns 1 the moment a guild-master-category seat (category byte 7)
    // whose city (+4) != -1 resolves to a live person; the budget caps the scan.
    int budget = 7;
    for (int i = count - 1; i >= 0; i -= 6) {
        const GuildSeatView& s = seats[i];
        u8 cat = categoryOf ? categoryOf(s.officeType, ctx) : 0;
        if (cat != 7)
            continue;
        if (s.city != -1) {
            if (personLive && personLive(s.city, ctx))
                return true;
        }
        if (--budget == 0)
            return false;
    }
    return false;
}

// ===========================================================================
// 6. AssignSlotData — gilde.exe 0x56ea50.
// ===========================================================================
int AssignSlotData(AmtSlot* slots, const AssignSlotInputs& in) {
    if (!slots)
        return -1;

    int idx = -1;

    // Stage 1: find an OCCUPIED slot already matching (x@+8, y@+9). The original's
    // occupied test is (*(int*)(slot+10) >> 24) != -1, i.e. marker (+13) != 0xFF.
    for (int i = 0; i < kAmtSlotCount; ++i) {
        if (slots[i].x == in.x && slots[i].y == in.y &&
            slots[i].marker != kAmtSlotFreeHi) {
            idx = i;
            break;
        }
    }

    // Stage 2: no match. When noMatchNew is 0 (v19 == 0) claim the first FREE slot
    // and stamp a fresh placement key; otherwise (v19 set) fail.
    if (idx < 0) {
        if (in.noMatchNew)
            return -1; // LABEL_19: v19 set -> return 0
        for (int i = 0; i < kAmtSlotCount; ++i) {
            if (slots[i].marker == kAmtSlotFreeHi) {
                // *v15 = dword_63D734++  (the slot's +0 key)
                i32 key = g_guildSlotKeyCounter++;
                AmtSlot& s = slots[i];
                s.pad0[0] = static_cast<u8>(key & 0xFF);
                s.pad0[1] = static_cast<u8>((key >> 8) & 0xFF);
                s.pad0[2] = static_cast<u8>((key >> 16) & 0xFF);
                s.pad0[3] = static_cast<u8>((key >> 24) & 0xFF);
                idx = i;
                break;
            }
        }
        if (idx < 0)
            return -1; // table full
    }

    AmtSlot& v13 = slots[idx];

    // *((_WORD *)v13 + 5) = a3  -> the type word at +10.
    v13.pad10[0] = static_cast<u8>(in.typeWord & 0xFF);
    v13.pad10[1] = static_cast<u8>((in.typeWord >> 8) & 0xFF);

    if (in.forceNew) {
        // The "load vegetation model" path: release any prior object then load. The
        // object id (+20) is left for the model loader; here we model the release as
        // clearing the objectId. (VIBE_Object_DetachAndRelease / _LoadVegetationModel
        // are render-owned; not reconstructed in this deterministic core.)
        if (v13.objectId)
            v13.objectId = 0;
    } else {
        // v13[4] = -1  -> the +16 dword (within AmtSlot's pad14 region).
        // (recorded via the marker write below; +16 is engine scratch.)
        if (in.typeWord) {
            v13.marker = 1;             // *((_BYTE*)v13+13) = 1
        } else {
            v13.marker = static_cast<u8>(0xFF); // *((_BYTE*)v13+13) = -1
            if (v13.objectId)
                v13.objectId = 0;       // release the prior object
        }
    }

    v13.x = in.x;       // *((_BYTE*)v13+8)  = a2
    v13.y = in.y;       // *((_BYTE*)v13+9)  = a4
    v13.size = in.hasTypeRec ? in.bookCat : 0; // *((_BYTE*)v13+12) = rec+66 or 0

    return idx;
}

} // namespace guild::world
