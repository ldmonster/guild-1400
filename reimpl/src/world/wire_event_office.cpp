// See wire_event_office.h. Binds the reconstructed mission-requirement and
// interaction-RNG leaves into their live hook bridges.
#include "world/wire_event_office.h"

#include "world/mission_recon3_evaluate.h"   // MissionReq3Hooks / MissionReq3GetHooks
#include "world/mission_requirement.h"        // MissionReqCheck* / AccumulateTimer / Count*
#include "sim/interaction4.h"                 // Interaction4Hooks / g_i4Hooks
#include "util/math_random.h"                 // util::RandomModulo (0x58b89c)

namespace guild::world {

namespace {

// ---------------------------------------------------------------------------
// Thin signature adapter for case 43 (CheckCumulativeStats).  The dispatcher
// hook is checkCumulativeStats(person, row); the reconstructed native leaf
// VIBE_MissionReq_CheckCumulativeStats (0x539534) is a pure row-driven scan
// that does not read the person record at all, so dropping the person arg is
// behaviour-identical.
bool AdaptCheckCumulativeStats(const u8* /*person*/, const ReqTableRow* row) {
    return MissionReqCheckCumulativeStats(row);
}

// case 47 fills *out and reads out->average; the native counter's i32 return
// (the float bit-pattern of the average) is redundant with out->average, so the
// void hook drops it.  Behaviour-identical.
void AdaptCountGuildMembers(u8 state, MemberCount* out) {
    MissionReqCountGuildMembers(state, out);
}

} // namespace

void InstallRealEventOfficeWiring() {
    // --- MissionReq3 dispatcher (0x5398c4) leaves ---------------------------
    // Only the fields whose reconstructed signatures match exactly (plus the
    // one row-only adapter) are bound; the rest stay null = inert default.
    MissionReq3Hooks& m = MissionReq3GetHooks();
    m.checkStatThreshold    = &MissionReqCheckStatThreshold;    // 0x539160
    m.checkOwnPersonRatio   = &MissionReqCheckOwnPersonRatio;   // 0x539380
    m.checkMultiStat        = &MissionReqCheckMultiStat;        // 0x5393ec
    m.checkStatCombo        = &MissionReqCheckStatCombo;        // 0x53945c
    m.checkCumulativeStats  = &AdaptCheckCumulativeStats;       // 0x539534 (adapter)
    m.accumulateTimer       = &MissionReqAccumulateTimer;       // 0x539c44
    m.countGuildMembers     = &AdaptCountGuildMembers;          // 0x539da0 (void adapter)

    // --- Interaction4 RNG leaf (determinism-critical) -----------------------
    // VIBE_Math_RandomModulo @0x58b89c — every RNG-driven interaction decision
    // (FindNearestThresholdSeed, tavern/robber dispatch tie-breaks) draws
    // through this.  Inert default returns 0; bind the real generator.
    sim::g_i4Hooks.randomModulo = &util::RandomModulo;
}

} // namespace guild::world
