// =============================================================================
// guild::play — REAL NPC DAILY-SCHEDULE / ACTION-DISPATCH BRIDGE implementation.
// See wire_npc_actions.h. Additive: routes sim::NpcDailyHooks (the inert per-turn
// NPC daily-routine director leaves) at the REAL reconstructed leaves — the live
// g_persons parallel columns, sim::Building_IsProductionKind, and the real
// Command_* packet builders. Wired ONLY through the public sim::SetNpcDailyHooks
// setter; no owned file (wiring.cpp / frameloop.cpp / turn_driver.cpp / a hook
// table) is edited.
// =============================================================================
#include "play/wire_npc_actions.h"

#include "sim/npc_daily.h"        // NpcDailyHooks, DailyPersonRow, SetNpcDailyHooks
#include "sim/entity.h"           // g_persons, BuildingFindById, kPersonCapacity
#include "sim/types.h"            // Person, ObjectRec, PersonField offsets
#include "sim/building_type.h"    // Building_IsProductionKind
#include "sim/command_apply7.h"   // RequestBuildOp77, QueueRequestString47
#include "sim/command_apply9.h"   // RequestChrMoveToUniverse
#include "sim/command_builders.h" // QueueRequestNamedObject53
#include "sim/command_codec.h"    // QueueRequestArgs25

#include <cstring>

namespace guild::play {

namespace {

// -- process-static state -----------------------------------------------------
bool                              g_installed = false;
const NpcActionsTargetProvider*   g_provider  = nullptr;
NpcActionsTallies                 g_tally{};

// Bridge-owned live command queue the real emitters build into (the engine's
// global request ring; here a self-owned instance so tests can inspect it). The
// real builders enqueue into the send ring, so the queue is Init()'d once (free
// list / ACK table set up) on first use, matching VIBE_Command_QueueInitAndSync.
sim::CommandQueue& Queue() {
    static sim::CommandQueue q;
    static bool inited = false;
    if (!inited) { q.Init(); q.set_standalone(false); inited = true; }
    return q;
}

// ---------------------------------------------------------------------------
// Raw-column reads into the live g_persons record (sim/entity.h). The director
// reads these EXACT byte offsets off word_12CE910[i] (see npc_daily.h column map):
//   +356 activeA, +357 activeB, +364 homeBld, +368 workBld, +388 destBld,
//   +456 turnBits, +4 id. We read/write them with the same unaligned-byte
//   semantics the original used.
// ---------------------------------------------------------------------------
inline guild::u8* PersonBytes(int i) {
    return reinterpret_cast<guild::u8*>(&sim::g_persons[i]);
}
template <typename T>
inline T ReadCol(int i, int off) {
    T v;
    std::memcpy(&v, PersonBytes(i) + off, sizeof(T));
    return v;
}
template <typename T>
inline void WriteCol(int i, int off, T v) {
    std::memcpy(PersonBytes(i) + off, &v, sizeof(T));
}

// ===========================================================================
// REAL leaves: live g_persons column sweep.
// ===========================================================================

// personCount(): the live person-array bound (capacity-bounded sweep, like the
// engine's 768-slot loop). Slots whose marker is the free sentinel (-1) are
// reported not-valid by personRow below, so the director skips them.
int RealPersonCount() { return sim::kPersonCapacity; }

// personRow(i): the parallel-column snapshot read straight off g_persons[i].
guild::sim::DailyPersonRow RealPersonRow(int i) {
    guild::sim::DailyPersonRow r{};
    ++g_tally.rowsRead;
    if (i < 0 || i >= sim::kPersonCapacity) { r.valid = false; return r; }
    const guild::i16 marker = ReadCol<guild::i16>(i, 0x00);
    if (marker == -1) { r.valid = false; return r; }   // free slot
    r.valid    = true;
    r.activeA  = ReadCol<guild::u8>(i, 356);   // byte_12CEA74 (+356)
    r.activeB  = ReadCol<guild::u8>(i, 357);   // byte_12CEA75 (+357)
    r.homeBld  = ReadCol<guild::i32>(i, 364);  // dword_12CEA7C (+364)
    r.workBld  = ReadCol<guild::i32>(i, 368);  // dword_12CEA80 (+368)
    r.destBld  = ReadCol<guild::i32>(i, 388);  // dword_12CEA94 (+388)
    r.turnBits = ReadCol<guild::u32>(i, 456);  // dword_12CEAD8 (+456)
    r.personId = ReadCol<guild::i32>(i, 4);    // dword_12CE914 (+4)
    return r;
}

// setTurnBits(i, bits): write the per-turn dispatch bitfield back into the +456
// column of g_persons[i] — the director's in-line per-person state mutation
// (folded by play::HashFullWorld).
void RealSetTurnBits(int i, guild::u32 bits) {
    if (i < 0 || i >= sim::kPersonCapacity) return;
    WriteCol<guild::u32>(i, 456, bits);
    ++g_tally.turnBitsWrites;
}

// ===========================================================================
// REAL leaf: production-building probe.
//   homeIsProduction(i): resolve the home building by id (BuildingFindById) and
//   feed its type byte (ObjectRec +0 alive/type) to Building_IsProductionKind
//   (kind in {11,12,13,16,28}). The original called VIBE_Building_IsProductionType
//   on the home-building pointer; we resolve the pointer from the id column.
// ===========================================================================
bool RealHomeIsProduction(int i) {
    ++g_tally.prodProbes;
    if (i < 0 || i >= sim::kPersonCapacity) return false;
    const guild::i32 homeId = ReadCol<guild::i32>(i, 364);
    if (homeId == 0) return false;
    sim::ObjectRec* b = sim::BuildingFindById(homeId);
    if (!b) return false;
    return sim::Building_IsProductionKind(b->alive);   // type byte @+0
}

// ===========================================================================
// REAL command emitters: build the director's real network packets onto the
// bridge-owned queue (the engine's global request ring).
// ===========================================================================
void EmitOp77(guild::i32 personId) {
    sim::RequestBuildOp77(Queue(), personId);                       // opcode 77
    ++g_tally.commandsBuilt;
}
void EmitChrMove(guild::i32 personId, guild::i32 universe, guild::i32 obj,
                 const char* tag) {
    // gilde.exe 0x494dd8 — RequestChrMoveToUniverse(a1,a2,name,a4): a1=person,
    // a2=universe, name=tag ("dummy_EINGANG"/"dummy_TUER"), a4=obj.
    sim::RequestChrMoveToUniverse(Queue(), personId, universe, tag, obj);
    ++g_tally.commandsBuilt;
}
void EmitString47(guild::i32 personId, guild::i32 universe, guild::i32 obj) {
    // opcode 47: a1=person, a2=universe, name=null (no string), a4=obj.
    sim::QueueRequestString47(Queue(), personId, universe, /*name=*/nullptr, obj);
    ++g_tally.commandsBuilt;
}
void EmitNamedObject53(guild::i32 personId, guild::i32 universe, int /*a*/,
                       guild::i32 obj, int flag, const char* name) {
    // opcode 53: a1=person, a2=universe, obj-name string=null (director passes 0),
    // a4=obj, a5=flag, name="Go to work"/"Go to wirtshaus"/"Go home".
    sim::QueueRequestNamedObject53(Queue(), personId, universe, /*obj=*/nullptr,
                                   obj, static_cast<guild::i8>(flag), name);
    ++g_tally.commandsBuilt;
}
void EmitArgs25(guild::i32 personId, guild::i32 fieldOffset, int a, int b,
                int value) {
    // opcode 25: a1=person, a2=fieldOffset(+456 col), a3=a, a4=b, a5=value.
    sim::QueueRequestArgs25(Queue(), personId, fieldOffset, a, b, value);
    ++g_tally.commandsBuilt;
}

// ===========================================================================
// Render/entity-coupled searches — routed through the deterministic provider
// (FINDINGS in the header: no standalone reconstructed leaf to point at).
// With no provider installed they report "nothing found"/safe defaults.
// ===========================================================================
int  PvFindTarget(int i, guild::i32* u, guild::i32* o) {
    return (g_provider && g_provider->findTarget) ? g_provider->findTarget(i, u, o) : 0;
}
bool PvDestDoorIds(int i, guild::i32* a, guild::i32* b) {
    return (g_provider && g_provider->destDoorIds) ? g_provider->destDoorIds(i, a, b) : false;
}
bool PvHomeHasMesh(int i) {
    return (g_provider && g_provider->homeHasMesh) ? g_provider->homeHasMesh(i) : false;
}
guild::u8 PvOwnerKind(int i) {
    return (g_provider && g_provider->ownerKind) ? g_provider->ownerKind(i) : 0;
}
guild::u8 PvAiClass(int i) {
    return (g_provider && g_provider->aiPlayerClass) ? g_provider->aiPlayerClass(i) : 0;
}
bool PvWorkDistOk(int i) {
    return (g_provider && g_provider->workDistanceOk) ? g_provider->workDistanceOk(i) : false;
}
bool PvCharBudgetOk() {
    return (g_provider && g_provider->characterBudgetOk) ? g_provider->characterBudgetOk() : false;
}
guild::i32 PvCurrency(int i) {
    return (g_provider && g_provider->currencyHeld) ? g_provider->currencyHeld(i) : 0;
}
int PvPickTavern(int i, guild::i32* u, guild::i32* o) {
    return (g_provider && g_provider->pickTavern) ? g_provider->pickTavern(i, u, o) : 0;
}
int PvCandidateCount() {
    return (g_provider && g_provider->candidateCount) ? g_provider->candidateCount() : 0;
}

// The wired NpcDailyHooks table. Real leaves for the column sweep / writeback /
// production probe / command builds; provider-routed for the render-coupled
// searches; freeHandlerEntry left null (free path returns 0 — unchanged).
const guild::sim::NpcDailyHooks g_hooks = {
    /* personCount           */ RealPersonCount,
    /* personRow             */ RealPersonRow,
    /* setTurnBits           */ RealSetTurnBits,
    /* findCarryTarget       */ PvFindTarget,
    /* findInteractionTarget */ PvFindTarget,
    /* destDoorIds           */ PvDestDoorIds,
    /* homeIsProduction      */ RealHomeIsProduction,
    /* homeHasMesh           */ PvHomeHasMesh,
    /* ownerKind             */ PvOwnerKind,
    /* aiPlayerClass         */ PvAiClass,
    /* workDistanceOk        */ PvWorkDistOk,
    /* characterBudgetOk     */ PvCharBudgetOk,
    /* currencyHeld          */ PvCurrency,
    /* pickTavern            */ PvPickTavern,
    /* candidateCount        */ PvCandidateCount,
    /* requestBuildOp77      */ EmitOp77,
    /* requestChrMoveToUniv  */ EmitChrMove,
    /* queueRequestString47  */ EmitString47,
    /* queueRequestNamedObj53*/ EmitNamedObject53,
    /* queueRequestArgs25    */ EmitArgs25,
    /* freeHandlerEntry      */ nullptr,
};

} // namespace

// ---------------------------------------------------------------------------
void InstallRealNpcActions() {
    guild::sim::SetNpcDailyHooks(&g_hooks);   // public setter — no owned-file edit
    g_installed = true;
}
void UninstallRealNpcActions() {
    guild::sim::SetNpcDailyHooks(nullptr);     // restore inert default (no persons)
    g_installed = false;
}
bool RealNpcActionsInstalled() { return g_installed; }

void SetNpcActionsTargetProvider(const NpcActionsTargetProvider* provider) {
    g_provider = provider;
}

sim::CommandQueue& NpcActionsQueue() { return Queue(); }

void ResetNpcActionsQueue() {
    Queue().Init();   // reset ring/lists/ACK table to the initial empty state
}

const NpcActionsTallies& GetNpcActionsTallies() { return g_tally; }
void ResetNpcActionsTallies() { g_tally = NpcActionsTallies{}; }

} // namespace guild::play
