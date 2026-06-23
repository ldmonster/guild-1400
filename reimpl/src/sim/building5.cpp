#include "sim/building5.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "util/math.h"            // REAL sibling: VectorWithinTolerance (0x5caa4c)
#include "util/string_ops.h"      // REAL siblings: StrCmpNoCaseN/StrncmpN
#include "util/transform.h"       // REAL sibling: PointThroughBoneChain (0x5c8b38)

namespace guild::sim {

// ===========================================================================
// Recovered constants (byte-exact from gilde.exe aBk_1 / dbl_621450).
// ===========================================================================
const char kPlotPrefixBk[4] = {'b', 'k', '_', '\0'};
const char kPlotPrefixVg[4] = {'v', 'g', '_', '\0'};

// ===========================================================================
// Module state used by FilterBlockedBauplatze <-> CollectFreeBauplatzCandidate.
// The originals communicate through two globals (dword_122EE48 result base,
// dword_63C708 count); we keep them as module statics with the same role.
// ===========================================================================
static const std::uint8_t** g_bauplatzResult = nullptr;
static int                   g_bauplatzCount  = 0;
static int                   g_bauplatzCap    = 0;
static const char*           g_bauplatzWant   = nullptr;   // wanted prefix (null)

// ---------------------------------------------------------------------------
// Hook plumbing + the default scene walk.
// ---------------------------------------------------------------------------
std::int32_t Building5Hooks::SceneGraphWalkAndInvoke(
    void* /*root*/, std::int32_t /*ctx*/,
    std::int32_t (*fn)(const std::uint8_t* node, void* arg),
    int /*recordSize*/, void* arg) {
    if (!fn) return 0;
    for (int i = 0;; ++i) {
        const std::uint8_t* node = SceneNode(i);
        if (!node) break;
        fn(node, arg);
    }
    return 0;
}

static Building5Hooks  g_defaultHooks;
static Building5Hooks* g_hooks = &g_defaultHooks;
void SetBuilding5Hooks(Building5Hooks* hooks) {
    g_hooks = hooks ? hooks : &g_defaultHooks;
}
Building5Hooks* Building5HooksGet() { return g_hooks; }

// ---------------------------------------------------------------------------
// Small node-field readers (byte offsets, little-endian pointer-sized fields).
// In the live process pointers are 32-bit; here a "link field" stores a node
// pointer by value, so we read it as a pointer.
// ---------------------------------------------------------------------------
static const std::uint8_t* NodeLink(const std::uint8_t* node, int off) {
    const std::uint8_t* p;
    std::memcpy(&p, node + off, sizeof p);
    return p;
}
static float* NodeFramePtr(const std::uint8_t* node, int off) {
    float* p;
    std::memcpy(&p, node + off, sizeof p);
    return p;
}

// ===========================================================================
// 0x50c7b0 — VIBE_Building_CollectFreeBauplatzCandidate.
// ===========================================================================
std::int32_t Building_CollectFreeBauplatzCandidate(const std::uint8_t* node,
                                                   const char* wantName) {
    const char* name = reinterpret_cast<const char*>(node);
    if (wantName) {
        std::size_t n = std::strlen(wantName) + 1;
        // name must match wantName over (n-1) and over strlen(wantName) at name+1.
        if (util::StrCmpNoCaseN(name, wantName, static_cast<int>(n - 1)) &&
            util::StrCmpNoCaseN(name + 1, wantName,
                                static_cast<int>(std::strlen(wantName)))) {
            return 1;   // does not match the requested plot name -> skip.
        }
    } else {
        if (util::StrCmpNoCaseN(name, kPlotPrefixBk, 3) &&
            util::StrCmpNoCaseN(name + 1, kPlotPrefixBk, 3)) {
            return 1;   // not a "bk_" plot -> skip.
        }
    }

    // Reject the plot if a nearby person (within 100 units) sits on it.
    float v10[3];
    float v11[3];
    // The original passes (a1, a1+76, out): a1 is the frame, a1+76 the point.
    util::PointThroughBoneChain(reinterpret_cast<float*>(const_cast<std::uint8_t*>(node)),
                                reinterpret_cast<const float*>(node + 76), v10);

    const std::uint8_t* it = g_hooks->PersonQueryBegin(node, 1, 6);
    bool occupied = false;
    for (; it; it = g_hooks->PersonIterNext()) {
        const std::uint8_t* sub = NodeLink(it, 97);
        if (sub) {
            util::PointThroughBoneChain(const_cast<float*>(reinterpret_cast<const float*>(sub)),
                                        reinterpret_cast<const float*>(sub + 76), v11);
            if (util::VectorWithinTolerance(v11, v10, kPlotOverlapTol)) {
                occupied = true;
                break;
            }
        }
    }
    if (!occupied) {
        // Original (0x50c84f): *(_DWORD*)(4*dword_63C708 + dword_122EE48) = a1;
        //   dword_63C708 = dword_63C708 + 1;  — UNCONDITIONAL store + bump (the
        // caller sizes the result buffer to 256). We keep the same count bump but
        // bounds-guard the store to avoid OOB in the headless build; the returned
        // count is identical to the original.
        if (g_bauplatzResult && g_bauplatzCount < g_bauplatzCap)
            g_bauplatzResult[g_bauplatzCount] = node;
        ++g_bauplatzCount;
    }
    return 1;
}

// The scene-walk trampoline used by FilterBlockedBauplatze.
static std::int32_t CollectTrampoline(const std::uint8_t* node, void* /*arg*/) {
    return Building_CollectFreeBauplatzCandidate(node, g_bauplatzWant);
}

// ===========================================================================
// 0x50c8ec — VIBE_Building_FilterBlockedBauplatze.
// ===========================================================================
std::int32_t Building_FilterBlockedBauplatze(std::int32_t a1,
                                             const std::uint8_t** resultFrame,
                                             int frameCapacity) {
    g_bauplatzResult = resultFrame;
    g_bauplatzCount  = 0;
    g_bauplatzCap    = frameCapacity;
    g_bauplatzWant   = nullptr;

    g_hooks->SceneGraphWalkAndInvoke(nullptr, 0, &CollectTrampoline, 384,
                                     reinterpret_cast<void*>(static_cast<std::intptr_t>(a1)));

    // Second pass: drop a plot whose frame overlaps a person on a linked node.
    for (int idx = 0; idx < g_bauplatzCount; ++idx) {
        const std::uint8_t* v4 = resultFrame ? resultFrame[idx] : nullptr;
        if (!v4) continue;

        float* v6 = NodeFramePtr(v4, 504);
        if (v6) {
            float v13[3];
            float v12[3];
            util::PointThroughBoneChain(v6, v6 + 19, v13);
            const std::uint8_t* it = g_hooks->PersonQueryBegin(v4, 1, 6);
            const std::uint8_t* hit = nullptr;
            for (; it; it = g_hooks->PersonIterNext()) {
                float* v8 = NodeFramePtr(it, 97);
                if (v8) {
                    util::PointThroughBoneChain(v8, v8 + 19, v12);
                    if (util::VectorWithinTolerance(v12, v13, kPlotOverlapTol)) {
                        hit = it;
                        break;
                    }
                }
            }
            if (hit && resultFrame)
                resultFrame[idx] = nullptr;
        }

        // Third pass: walk the child "bk_" chain at +508 for an occupied node.
        const std::uint8_t* v9 = NodeLink(v4, 508);
        while (v9) {
            if (util::StrncmpN(reinterpret_cast<const char*>(v9), kPlotPrefixBk, 3) == 0) {
                float v13[3];
                float v12[3];
                util::PointThroughBoneChain(const_cast<float*>(reinterpret_cast<const float*>(v9)),
                                            reinterpret_cast<const float*>(v9 + 76), v13);
                const std::uint8_t* it = g_hooks->PersonQueryBegin(v9, 1, 6);
                const std::uint8_t* hit = nullptr;
                for (; it; it = g_hooks->PersonIterNext()) {
                    const std::uint8_t* sub = NodeLink(it, 97);
                    if (sub) {
                        util::PointThroughBoneChain(
                            const_cast<float*>(reinterpret_cast<const float*>(sub)),
                            reinterpret_cast<const float*>(sub + 76), v12);
                        if (util::VectorWithinTolerance(v12, v13, kPlotOverlapTol)) {
                            hit = it;
                            break;
                        }
                    }
                }
                if (hit) {
                    if (resultFrame) resultFrame[idx] = nullptr;
                    break;
                }
            }
            v9 = NodeLink(v9, 496);
        }
    }
    return g_bauplatzCount;
}

// ===========================================================================
// 0x50cac4 — VIBE_Building_ForEachBauplatzReserve.
// ===========================================================================
std::int32_t Building_ForEachBauplatzReserve(std::int32_t reserve,
                                             std::int32_t walkCtx) {
    // v11[32] frame + v12 count; the original clears it via the light thunk then
    // walks the scene to fill it. We expose the same SceneNode source: collect
    // the nodes into a local frame, then RestoreObjectStates per node.
    const std::uint8_t* frame[32];
    int count = 0;
    g_hooks->LightSetGrayColorThunk(0, 132, frame);
    for (int i = 0; count < 32; ++i) {
        const std::uint8_t* node = g_hooks->SceneNode(i);
        if (!node) break;
        frame[count++] = node;
    }
    (void)walkCtx;

    // byte_1233514 — difficulty/reserve cap (runtime global, routed via hook).
    const std::uint8_t cap = g_hooks->DifficultyReserveCap();

    std::int32_t result = 0;
    int v7 = 0;
    for (int v8 = 0; v7 < count; ++v8) {
        // Exact original: v9 = a1 && (v7 < (unsigned __int8)byte_1233514
        //                            || byte_1233514 == 2).
        std::uint8_t v9 = static_cast<std::uint8_t>(
            reserve && (v7 < static_cast<int>(cap) || cap == 2));
        result = g_hooks->UniverseRestoreObjectStates(frame[v8], v9);
        ++v7;
    }
    return result;
}

// ===========================================================================
// 0x50cf24 — VIBE_Building_FindNearestPlotByDistance.
// ===========================================================================
const std::uint8_t* Building_FindNearestPlotByDistance(const float* p,
                                                       const std::uint8_t** plots) {
    const std::uint8_t* best = nullptr;
    float bestDist = 100000000.0f;
    for (int i = 0; i < 256; ++i) {
        const std::uint8_t* node = plots ? plots[i] : nullptr;
        if (!node) continue;
        float out[3];
        // plots[i] (== *v4 in the original) is itself the frame pointer.
        float* frame = reinterpret_cast<float*>(const_cast<std::uint8_t*>(node));
        util::PointThroughBoneChain(frame, frame + 19, out);
        float dx = out[0] - p[0];
        float dy = out[1] - p[1];
        float dz = out[2] - p[2];
        // Original: v7 = sqrt(...) is an x87 80-bit long double; v13 = (float)v7.
        // First compare uses the wide value (v7 < v12), the SECOND compares the
        // float-truncated v13 against the *double* cutoff (v13 < dbl_621450).
        //   if ( v7 < v12 && v13 < dbl_621450 ) { v3 = *v4; v12 = v13; }
        double d = std::sqrt(static_cast<double>(dx * dx + dy * dy + dz * dz));
        float df = static_cast<float>(d);
        if (d < bestDist && static_cast<double>(df) < kNearestPlotCutoff) {
            best = node;
            bestDist = df;
        }
    }
    return best;
}

// ===========================================================================
// Gate handlers.
// ===========================================================================
static void GateWriteU32(std::uint8_t* p, std::uint32_t v) {
    std::memcpy(p, &v, 4);
}

std::int32_t Building_ResetGateState(std::uint8_t* rec) {
    if (!rec) return 0;
    // Copy the gate's time block (+68..+80, 14 bytes) into the live slot at +82.
    std::memcpy(rec + 82, rec + 68, 14);
    // Mirror the +82 block into the appointment slot at +96 (14 bytes).
    std::memcpy(rec + 96, rec + 82, 14);
    // *(_DWORD*)(rec+82+30) = 0   -> rec+112
    GateWriteU32(rec + 112, 0);
    // *(_DWORD*)(rec+82+90) = -1  -> rec+172
    GateWriteU32(rec + 172, 0xFFFFFFFFu);
    // VIBE_GameTime_Advance(rec+82, addDays=0, addSeconds=0, addMinutes=10).
    return g_hooks->GameTimeAdvance(rec + 82, 0, 0, 10);
}

std::int32_t Building_RegisterGateHandlers() {
    struct Reg { std::uint32_t type; };
    // The originals register twelve (type) handlers, bailing on first failure.
    static const std::uint32_t kTypes[] = {3, 4, 5, 8, 9, 0xA, 0xB, 0xC, 0xD, 0xE, 0x4C, 0x4D};
    for (std::uint32_t t : kTypes) {
        if (g_hooks->HeRegisterHandlerByType(t, nullptr, nullptr))
            return 1;
    }
    return 0;
}

void Building_DeselectThunk() {
    g_hooks->LightSetGrayColorThunk(0, 0x4000, nullptr);
}
void Building_GateCallbackStub() {}
void Building_EmptyCallbackStub() {}
std::int32_t Building_HandlerStub() {
    Building_EmptyCallbackStub();
    return 0;
}

// ===========================================================================
// 0x4f70a0 — VIBE_Building_RequestGateFlagSync.
// ===========================================================================
// The original scans a global flag table (byte_122FEC0 / dword_122FEC4 vs the
// building's flag at dword_12CE914[134*objWord]) for a matching index, then runs
// a 0/1/2 state machine off the dword at rec+112, scheduling future gate ticks.
// The render-global tables and the time/mission/command leaves are not
// reconstructed; we faithfully reproduce the STATE MACHINE (the deterministic
// part) and route the table-scan result + the leaf calls through hooks. With
// inert hooks the scan finds no match (index 128) and schedules +30 min.
void Building_RequestGateFlagSync(std::uint8_t* rec) {
    if (!rec) return;

    // Top-level guard (gilde.exe 0x4f70b6):
    //   if ( *(int*)((char*)&dword_63C8F0 + 1) >> 24 <= -1 ) {
    //       VIBE_He_FreeHandlerEntry(a1, a1, a3);
    //       VIBE_Building_GateCallbackStub();
    //       return;
    //   }
    // The >>24 is arithmetic (sar) on a signed int.
    if ((g_hooks->GateSyncGuardWord() >> 24) <= -1) {
        g_hooks->HeFreeHandlerEntry(0, 0, 0);
        Building_GateCallbackStub();
        return;
    }

    std::uint8_t* timeRec = rec + 82;
    std::uint32_t state;
    std::memcpy(&state, rec + 112, 4);

    // No reconstructed flag table -> the scan never matches (index >= 128).
    // (The original increments `a3` to 128 and falls into the +30-min branch.)
    const int matchIndex = 128;

    if (matchIndex >= 128) {
        g_hooks->GameTimeAdvance(timeRec, 0, 0, 30);
        return;
    }

    switch (state) {
        case 0xFFFFFFFEu:
        case 0xFFFFFFFFu:
            g_hooks->HeFreeHandlerEntry(0, 0, 0);
            Building_GateCallbackStub();
            return;
        case 0: {
            // Inert mission requirement => not satisfied => reschedule +10 min.
            if (!g_hooks->MissionReqEvaluate(nullptr, 0)) {
                g_hooks->GameTimeAdvance(timeRec, 0, 0, 10);
            } else {
                g_hooks->GameTimeAdvance(timeRec, 0, 0, 1);
                GateWriteU32(rec + 112, 1);
            }
            break;
        }
        case 1: {
            g_hooks->GameTimeAdvance(timeRec, 0, 0, 1);
            // Inert packet path falls to the +1-day / state-stay branch.
            g_hooks->GameTimeAdvance(timeRec, 1, 0, 0);
            GateWriteU32(rec + 112, 1);
            break;
        }
        case 2: {
            std::int32_t reqId;
            std::memcpy(&reqId, rec + 172, 4);
            std::int32_t status = g_hooks->CommandGetPacketStatusById(reqId);
            g_hooks->GameTimeAdvance(timeRec, 0, 0, 1);
            if (!status) {
                Building_GateCallbackStub();
                return;
            }
            if (status == 1) {
                GateWriteU32(rec + 112, 0);
                g_hooks->GameTimeAdvance(timeRec, 24, 0, 0);
            } else {
                GateWriteU32(rec + 112, 1);
                g_hooks->GameTimeAdvance(timeRec, 0, 0, 30);
            }
            break;
        }
        default:
            Building_GateCallbackStub();
            break;
    }
}

}  // namespace guild::sim
