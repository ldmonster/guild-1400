#pragma once
// Building "Bauplatz" (build-plot) placement + gate-handler slice for the Guild
// simulation (gilde.exe). MODULE: buildings, prefix VIBE_Building_* (namespace
// guild::sim). Fifth companion to building.{h,cpp} / building2..4 /
// building_*.cpp — translates the still-deferred build-plot finder family (the
// scene-graph walk that collects free "bk_"/"vg_" plots, filters ones blocked by
// a nearby person, finds the nearest plot to a point, and resolves a plot name /
// position) and the gate-handler family (the He_* gate state-machine handlers,
// their reset / flag-sync steps and the register/deselect/stub leaves).
//
// Translated functions (gilde.exe addr):
//   VIBE_Building_CollectFreeBauplatzCandidate   0x50c7b0
//   VIBE_Building_FilterBlockedBauplatze         0x50c8ec
//   VIBE_Building_ForEachBauplatzReserve         0x50cac4
//   VIBE_Building_ReserveBauplatzForActiveChar   0x50ccbc
//   VIBE_Building_FindNearestBauplatzName        0x50ccfc
//   VIBE_Building_GetGebaeudeBauplatzPos         0x50cd94
//   VIBE_Building_FindNearestPlotByDistance      0x50cf24
//   VIBE_Building_ResetGateState                 0x4f704c
//   VIBE_Building_RequestGateFlagSync            0x4f70a0
//   VIBE_Building_RegisterGateHandlers           0x4f72a0
//   VIBE_Building_DeselectThunk                  0x4f740c
//   VIBE_Building_HandlerStub                    0x4f73e8
//   VIBE_Building_GateCallbackStub               0x4f710c
//   VIBE_Building_EmptyCallbackStub              0x4f73ec
//
// REAL reconstructed siblings wired in (NOT mocked) — these are the leaves the
// originals call and they all already exist in the library:
//   util::StrCmpNoCaseN          (string_ops.cpp, 0x5e0db0)  — plot-name match
//   util::StrncmpN               (string_ops.cpp, 0x5e9ee0)  — "bk_" prefix test
//   util::VectorWithinTolerance  (math.cpp,       0x5caa4c)  — 100-unit overlap
//   util::PointThroughBoneChain  (transform.cpp,  0x5c8b38)  — world position
//   sim::GameTimeAdvance         (gametime.cpp,   0x583150)  — gate scheduling
//   sim::Building3_LookupTypeName(building3.cpp,  0x50c738)  — type-name copy
//
// The scene-graph walk, the Person query iterator, the He_* handler registry,
// the Universe/Light render thunks, the Command net channel, the MissionReq
// evaluator and the few render-global tables are NOT reconstructed; each is
// routed through an installable Building5Hooks struct with INERT default
// implementations defined in building5.cpp. Tests install their own hooks.
//
// Building records are the live object records ("char*"/"u8*" in the originals);
// only the fields actually read/written are documented per-function.

#include <cstdint>

#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered constants.
//   aBk_1 = "bk_\0vg_\0" — the two plot name prefixes (3 chars each).
//   dbl_621450 = 100.0 — the nearest-plot distance cutoff.
// ---------------------------------------------------------------------------
extern const char kPlotPrefixBk[4];   // "bk_"
extern const char kPlotPrefixVg[4];   // "vg_"
constexpr double kNearestPlotCutoff = 100.0;
constexpr float  kPlotOverlapTol = 100.0f;

// ---------------------------------------------------------------------------
// A scene-graph node walked by the Bauplatz finders, modelled as a base pointer.
// The finders treat each node as a byte blob and read only:
//   +97  (dword)  person/sub-object link used for the world-position transform
//   +496 (dword)  next-sibling link (FindActiveWorkSlot chain)
//   +504 (float*) the node's frame pointer (PointThroughBoneChain input)
//   +508 (dword)  child object pointer (StrncmpN("bk_") chain)
// We expose them as raw byte addresses, matching the originals.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Cross-module hooks (default inert).
// ---------------------------------------------------------------------------
struct Building5Hooks {
    virtual ~Building5Hooks() = default;

    // gilde.exe 0x5ac738 — VIBE_SceneGraph_WalkAndInvoke(root, ctx, fn, recordSize,
    // arg): walk the scene list invoking `fn` per node. The finders pass
    // CollectFreeBauplatzCandidate / a per-node callback. We model the walk as
    // "iterate the nodes I supply and invoke the callback". Default: invoke `fn`
    // for each node returned by NextSceneNode() until null. Returns the engine
    // status (0 inert).
    virtual std::int32_t SceneGraphWalkAndInvoke(
        void* root, std::int32_t ctx,
        std::int32_t (*fn)(const std::uint8_t* node, void* arg),
        int recordSize, void* arg);

    // Node source consulted by the default SceneGraphWalkAndInvoke. `index` runs
    // 0,1,2,... until this returns null. Inert: null (empty scene).
    virtual const std::uint8_t* SceneNode(int index) { (void)index; return nullptr; }

    // gilde.exe 0x586c20 / 0x586a6c — VIBE_Person_QueryBegin(node, 1, 6) /
    // VIBE_Person_IterNext: iterate the persons attached to `node`. Inert: null.
    virtual const std::uint8_t* PersonQueryBegin(const std::uint8_t* node,
                                                 int a, int b) {
        (void)node; (void)a; (void)b; return nullptr;
    }
    virtual const std::uint8_t* PersonIterNext() { return nullptr; }

    // gilde.exe 0x5b43f0 — VIBE_Universe_RestoreObjectStates(node, flag): mark a
    // reserved plot node. Inert: 0.
    virtual std::int32_t UniverseRestoreObjectStates(const std::uint8_t* node,
                                                     std::uint8_t flag) {
        (void)node; (void)flag; return 0;
    }
    // gilde.exe 0x5b4a24 — VIBE_Universe_SwitchActiveSlot(a,b,node,slot): swap the
    // active universe slot around the position lookup. Inert: no-op.
    virtual void UniverseSwitchActiveSlot(int a, int b, const std::uint8_t* node,
                                          std::int32_t slot) {
        (void)a; (void)b; (void)node; (void)slot;
    }
    // gilde.exe 0x5c6af0 — VIBE_Light_SetGrayColorThunk(a,b,arg): render tint
    // thunk used to clear / mark plots. Inert: no-op.
    virtual void LightSetGrayColorThunk(int a, int b, void* arg) {
        (void)a; (void)b; (void)arg;
    }

    // gilde.exe handler registry — VIBE_He_RegisterHandlerByType(type, alloc, run):
    // register a gate event handler. Returns non-zero on FAILURE (the original
    // bails out of RegisterGateHandlers on the first failure). Inert: 0 (success).
    virtual std::int32_t HeRegisterHandlerByType(std::uint32_t type,
                                                 void* allocFn, void* runFn) {
        (void)type; (void)allocFn; (void)runFn; return 0;
    }
    // gilde.exe 0x4c6144 — VIBE_He_FreeHandlerEntry(a,b,c): free a gate handler.
    virtual void HeFreeHandlerEntry(std::int32_t a, std::int32_t b,
                                    std::int32_t c) {
        (void)a; (void)b; (void)c;
    }

    // gilde.exe 0x583150 — VIBE_GameTime_Advance(rec, addDays, addSeconds,
    // addMinutes): advance a 14-byte packed time record. Routed so the itest can
    // forward the REAL sim::GameTimeAdvance. Inert: no-op, returns 0.
    virtual std::int32_t GameTimeAdvance(std::uint8_t* timeRec, int addDays,
                                         int addSeconds, int addMinutes) {
        (void)timeRec; (void)addDays; (void)addSeconds; (void)addMinutes;
        return 0;
    }

    // gilde.exe 0x5398c4 — VIBE_MissionReq_Evaluate(req, arg): is a gate mission
    // requirement satisfied? Inert: 0 (not satisfied).
    virtual std::int32_t MissionReqEvaluate(const std::uint8_t* req,
                                            std::int32_t arg) {
        (void)req; (void)arg; return 0;
    }
    // gilde.exe 0x494ab4 / 0x4939d4 — VIBE_Command_QueueRequestFlagBlob32 /
    // _GetPacketStatusById: gate flag net channel. Inert: 0.
    virtual std::int32_t CommandQueueRequestFlagBlob32(int n, const void* blob) {
        (void)n; (void)blob; return 0;
    }
    virtual std::int32_t CommandGetPacketStatusById(std::int32_t id) {
        (void)id; return 0;
    }
};
void SetBuilding5Hooks(Building5Hooks* hooks);
Building5Hooks* Building5HooksGet();

// ===========================================================================
// Bauplatz (build-plot) finders.
// ===========================================================================
// gilde.exe 0x50c7b0 — scene-walk callback: if `node` is a free plot matching the
// wanted prefix (`wantName`, or "bk_"/"vg_" when null) and no nearby person sits
// on it, append it to the result frame `g_bauplatzResult` (count `g_bauplatzCount`)
// set up by FilterBlockedBauplatze. Always returns 1 (continue the walk).
std::int32_t Building_CollectFreeBauplatzCandidate(const std::uint8_t* node,
                                                   const char* wantName);

// gilde.exe 0x50c8ec — collect every free plot (via the scene walk) into
// `resultFrame[0..]`, then drop any whose frame overlaps (within 100 units) a
// person's position. Returns the surviving plot count. `resultFrame` is a
// DWORD array of node pointers (the original stores raw pointers).
std::int32_t Building_FilterBlockedBauplatze(std::int32_t a1,
                                             const std::uint8_t** resultFrame,
                                             int frameCapacity);

// gilde.exe 0x50cac4 — walk the scene collecting reserve plots into a local frame,
// then restore each plot's object state (gating on the `reserve` flag and a
// per-difficulty cap). Returns the engine status of the last restore.
std::int32_t Building_ForEachBauplatzReserve(std::int32_t reserve,
                                             std::int32_t walkCtx);

// gilde.exe 0x50cf24 — return the plot node nearest to point `p` (xyz) within the
// 100-unit cutoff, scanning the 256-entry plot pointer array `plots`. null = none.
const std::uint8_t* Building_FindNearestPlotByDistance(const float* p,
                                                       const std::uint8_t** plots);

// ===========================================================================
// Gate handlers.
// ===========================================================================
// gilde.exe 0x4f704c — copy the gate's time block (+68..+80 -> +82..+94) and the
// appointment mirror (+82.. -> +96..), clear +112, set +172 to -1, and schedule
// the next tick +10 min. `rec` is the building record base. Returns the hour.
std::int32_t Building_ResetGateState(std::uint8_t* rec);

// gilde.exe 0x4f70a0 — drive the gate flag-sync state machine for building `rec`.
// Routes the mission/command/time leaves through hooks; with inert hooks it takes
// the "no matching flag / not satisfied" path and schedules +30 / +10 minutes.
void Building_RequestGateFlagSync(std::uint8_t* rec);

// gilde.exe 0x4f72a0 — register the twelve gate event handlers. Returns 1 if any
// registration failed (the original's early-out), 0 if all succeeded.
std::int32_t Building_RegisterGateHandlers();

// gilde.exe 0x4f740c — deselect render thunk (clears the selection tint).
void Building_DeselectThunk();
// gilde.exe 0x4f73e8 — handler stub: delegates to the empty callback. Returns 0.
std::int32_t Building_HandlerStub();
// gilde.exe 0x4f710c / 0x4f73ec — empty gate callbacks (no-ops).
void Building_GateCallbackStub();
void Building_EmptyCallbackStub();

}  // namespace guild::sim
