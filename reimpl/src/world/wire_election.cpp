// See wire_election.h. Binds the election / court-council / create bridges to
// their real reconstructed leaves. Glue only — no module logic lives here.
#include "world/wire_election.h"

#include "world/court_council2.h"     // CourtCouncilHooks / Set/GetCourtCouncilHooks
#include "world/guild_election.h"     // GuildSetElectionHooks
#include "world/election_candidacy.h" // CanvassSetHooks
#include "world/election.h"           // ElectionSetInstallHook
#include "world/guild_rank.h"         // GuildGetEligibility (VIBE_Amt_GetGuildEligibility)
#include "world/guild.h"              // GuildPlayer / GuildEligibility
#include "world/office_assign.h"      // OfficeAddTableEntry (VIBE_Office_AddTableEntry)

#include "sim/building_create2.h"     // ICreateHooks / SetCreateHooks
#include "sim/types.h"                // Person, kPersonStride

#include "util/math_random.h"         // util::RandomModulo (VIBE_Math_RandomModulo)
#include "mem/heap.h"                 // mem::Heap
#include "mem/memory_debug.h"         // mem::MemoryTracker (VIBE_Memory_AllocDebug)

#include <cstring>

namespace guild::world {

using guild::sim::Person;

namespace {

// =========================================================================
// CourtCouncilHooks leaves -> real reconstructed targets.
// =========================================================================

// VIBE_Amt_GetGuildEligibility (0x481bcc) on the accused. The original takes the
// live 589-byte Person record and reads rank@+361, flags@+459, money@+404; here
// we fold that exact field block into a GuildPlayer view and run the reconstructed
// rule (world::GuildGetEligibility). The court scorer wants 1 == eligible, which is
// exactly the kEligible code; every other code (not-guild / already-in / poor) is
// non-eligible -> 0, matching the original's `if (eligibility != 1) return 0`.
int WeGuildEligibility(Person* accused) {
    if (!accused)
        return 0;
    const u8* r = reinterpret_cast<const u8*>(accused);
    GuildPlayer p{};
    p.rank     = r[361];                                    // +361 held rank byte
    p.flags459 = r[459];                                    // +459 flags byte
    std::memcpy(&p.money, r + 404, sizeof p.money);         // +404 liquid money
    return GuildGetEligibility(p) == GuildEligibility::kEligible ? 1 : 0;
}

// VIBE_Math_RandomModulo(4) (0x58b89c) — the ambiguous-verdict tie-break. The
// shipping game wires this to util::RandomModulo(4) (the header documents exactly
// this); the inert default returned a deterministic 0.
int WeRandomMod4() {
    return util::RandomModulo(4);
}

// Process-lifetime wired CourtCouncilHooks table (the global hook ptr references
// this for the life of the run, like the engine's process-global hook block).
CourtCouncilHooks g_court{};

// =========================================================================
// Office-table install -> VIBE_Office_AddTableEntry (0x47e750).
// =========================================================================
// All four election install hooks (single-seat 0x481228, two-seat 0x480e4c,
// guild-master full) commit the winner via
//   VIBE_Office_AddTableEntry(officeSlot/holderKey, winnerObj, 1, 0, 255)
// and a clear via (holderKey, 0, 1, 0, 255). The reconstructed OfficeAddTableEntry
// runs the faithful 37-entry holder-slot scan + build-op-69 command pack; its
// terminal lockstep emit is office_assign's own OfficeCommandHook (default sink),
// so the slot-find + pack rule executes for real here. We synthesise the minimal
// OfficePersonRec view the original derives from FindRecordById(winnerObj): only
// the owner id + validity are read by the add-entry pack. personId <= 0 clears.
void WeOfficeInstall(int officeSlot, i32 personId, u8 state) {
    OfficePersonRec primary{};
    const OfficePersonRec* prim = nullptr;
    if (personId > 0) {
        primary.ownerId = personId;   // +4 id the pack writes as the slot owner
        primary.valid   = true;       // models a resolved FindRecordById
        prim = &primary;
    }
    // holderKey is the slot-table key (v17[0] / the seat key); officeType 1 and
    // state 255 are the install constants the originals pass.
    OfficeAddTableEntry(static_cast<u8>(officeSlot), prim, /*officeType=*/1,
                        /*secondary=*/nullptr, state);
}

// guild_election.h GuildInstallHook: (officeSlot, winnerId, ctx) install-only.
void WeGuildInstall(int officeSlot, i32 winnerId, void* /*ctx*/) {
    WeOfficeInstall(officeSlot, winnerId, /*state=*/255);
}

// election_candidacy.h CanvassInstallHook: (seatKey, personId, ctx). personId == 0
// -> clear the seat (the original passes id 0 for AddTableEntry(seat,0,1,0,255)).
void WeCanvassInstall(int seatKey, i32 personId, void* /*ctx*/) {
    WeOfficeInstall(seatKey, personId, /*state=*/255);
}

// election.h ElectionInstallHook: (officeSlot, winnerId, incumbentId, ctx). Only
// the winner install fires here (the original install gate is winner != incumbent,
// already decided upstream in ElectionCommit).
void WeElectionInstall(int officeSlot, i32 winnerId, i32 /*incumbentId*/,
                       void* /*ctx*/) {
    WeOfficeInstall(officeSlot, winnerId, /*state=*/255);
}

// =========================================================================
// ICreateHooks.AllocPlantMap -> VIBE_Memory_AllocDebug(0x600, "f3_gm:PlantMap").
// =========================================================================
// The original CreateGebaeude (prot 30, the farm) allocates the 0x600-byte plant
// map through the engine's process-global debug allocator. The reconstructed
// allocator (mem::MemoryTracker over mem::Heap) IS that allocator; back it with a
// process-lifetime heap+tracker (mirroring the original's process globals) so a
// farm-create receives a real guarded buffer the per-type init then fills.
class RealCreateHooks : public guild::sim::ICreateHooks {
public:
    u8* AllocPlantMap() override {
        if (!inited_) {
            tracker_.Init(/*capacity=*/256);   // VIBE_Memory_InitTracker
            inited_ = true;
        }
        // VIBE_Memory_AllocDebug(0x600, "f3_gm:PlantMap").
        return static_cast<u8*>(tracker_.AllocDebug(0x600, "f3_gm:PlantMap"));
    }
private:
    guild::mem::Heap          heap_{};
    guild::mem::MemoryTracker tracker_{heap_};
    bool                      inited_ = false;
};

RealCreateHooks g_createHooks{};

} // namespace

void InstallRealElectionWiring() {
    // --- CourtCouncilHooks (court_council2.h) --------------------------------
    // SEED-FROM-DEFAULTS: copy the module inert defaults, override only the two
    // fields with a clean reconstructed target. (Every field is null-checked by
    // court_council2.cpp, so seeding is for layer symmetry, not crash-safety.)
    g_court = GetCourtCouncilHooks();
    g_court.guildEligibility = &WeGuildEligibility;  // VIBE_Amt_GetGuildEligibility
    g_court.randomMod4       = &WeRandomMod4;         // VIBE_Math_RandomModulo(4)
    // buildingSlotCount / buildingSlot: the live 256-slot object/building array
    // read (object +0/+39, def +0/+583) — no clean reconstructed slot-view leaf
    // surfacing those four fields -> inert (default 0 slots).
    // aiNeedsComputeWeights / categoryRating: VIBE_AiNeeds_ComputeWeights (0x47936c)
    // category gate + its scratch float — AI scoring not reconstructed as a
    // standalone category leaf -> inert (default: no category participates / 1.0).
    // setBarValue: SetValueOrText widget write (GUI/render) -> inert (default 1).
    SetCourtCouncilHooks(&g_court);

    // --- election install hooks (x3) -----------------------------------------
    // All three install the winner via the reconstructed VIBE_Office_AddTableEntry
    // (0x47e750). The notify hooks (the office/person message broadcast TRANSPORT,
    // VIBE_He_SendEntityMessage) have no clean reconstructed message sink -> left
    // null (inert), so only the deterministic office-table install is wired.
    GuildSetElectionHooks(&WeGuildInstall, /*notify=*/nullptr, /*ctx=*/nullptr);
    CanvassSetHooks(&WeCanvassInstall, /*notify=*/nullptr, /*ctx=*/nullptr);
    ElectionSetInstallHook(&WeElectionInstall, /*ctx=*/nullptr);

    // --- ICreateHooks (building_create2.h) -----------------------------------
    // Bind the CreateGebaeude plant-map allocation leaf to the real reconstructed
    // debug allocator (VIBE_Memory_AllocDebug). REPLACEMENT object (a vtable), not
    // a per-field seed: ICreateHooks has the single AllocPlantMap virtual.
    guild::sim::SetCreateHooks(&g_createHooks);
}

} // namespace guild::world
