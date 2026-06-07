// ===========================================================================
// cutscene_misc5.cpp — VIBE_Scene_* scene activation / world-sync slice.
// 1:1 translations of the deterministic scene-sync bodies that back the
// cutscene scene loaders. See cutscene_misc5.h for the recovered-offset map.
// ===========================================================================
#include "sim/cutscene_misc5.h"

#include <cstdio>
#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Hook surface — inert defaults (all leaves no-op; name-matches fail; the
// scene load fails so loaders take the not-loaded path deterministically).
// ---------------------------------------------------------------------------
namespace {
const SceneSyncHooks kInertHooks = []{
    SceneSyncHooks h{};
    return h;
}();

const SceneSyncHooks* g_hooks = &kInertHooks;
}  // namespace

void SetSceneSyncHooks(const SceneSyncHooks* hooks) {
    g_hooks = hooks ? hooks : &kInertHooks;
}
const SceneSyncHooks& GetSceneSyncHooks() { return *g_hooks; }

static const SceneSyncHooks& H() { return *g_hooks; }

// ===========================================================================
// Deterministic kernels.
// ===========================================================================

// 0x5e9020 — VIBE_Scene_RetZero.
char SceneRetZero() {
    return 0;  // 0x5e9022
}

// 0x503678 — VIBE_Scene_CollectMatchingObject.
//   if (StrCmpNoCase(list, list+513)) { v6 = (*list)++; list[v6+1] = obj; }
//   return *list < 512;
bool SceneCollectMatchingObject(i32* list, int candidateId, bool matched) {
    if (!list) return false;
    if (matched) {
        int idx = list[0]++;        // v6 = (*v4)++
        list[idx + 1] = candidateId; // v4[v6+1] = a1
    }
    return list[0] < 512;           // *a2 < 512
}

// 0x504774 — VIBE_Scene_CollectTorchObject.
//   if (StrCmpNoCase(ctx, "ub_FACKEL_")) {
//       v6 = *(ctx+128); *(ctx+128) = v6+1; *(ctx + 4*v6) = obj; }
//   return *(ctx+128) < 32;
// list[32] holds the count; list[0..31] the ids.
bool SceneCollectTorchObject(i32* list, int candidateId, bool matched) {
    if (!list) return false;
    if (matched) {
        int count = list[32];       // *(v4+128)
        list[32] = count + 1;
        list[count] = candidateId;  // *(v4 + 4*v6)
    }
    return list[32] < 32;           // *(a2+128) < 32
}

// 0x5048c4 — VIBE_Scene_FlagBuildingGate.
//   if (StrncmpN(name, "gb_", 3) || StrCmpNoCase-fail) return 1;
//   v4 = *(+530) & 0xF3; *(+530) = v4; *(+530) = v4 | 4; return 1;
char SceneFlagBuildingGate(u8* gateByte, bool nameStartsGb, bool matched) {
    // The original: `if (Strncmp(name,"gb_",3) || namematch) return 1;` — i.e.
    // the body runs ONLY when the name starts with "gb_" AND the inner match
    // succeeds. (Strncmp returns 0 on equal -> the "gb_" branch is taken.)
    if (!nameStartsGb || !matched)
        return 1;  // 0x5048db
    if (gateByte) {
        u8 v4 = *gateByte & 0xF3;   // clear bits 2,3
        *gateByte = v4;
        *gateByte = v4 | 4;         // set bit 2
    }
    return 1;  // 0x5048de
}

// 0x505b3c — VIBE_Scene_FlagGateObject.
//   result = StrCmpNoCase(obj, "gate", ...);
//   if (result) { v5 = *(+529); *(+536)=1; *(+529) = v5 & 0xFE; }
//   else        { *(+536) = 0; }
//   return result;
int SceneFlagGateObject(bool matched, u8* loByte, i32* flag536) {
    if (matched) {
        if (flag536) *flag536 = 1;          // *(+536) = 1
        if (loByte)  *loByte = *loByte & 0xFE; // clear bit 0
        return 1;
    }
    if (flag536) *flag536 = 0;              // *(+536) = 0
    return 0;
}

// 0x504ce0 — VIBE_Scene_SyncMeisterBuildings classification core.
//   v3 = anchorB (class 12, sub-state 1); v4 = anchorA (class 12, sub-state 0).
//   The first scan stops once BOTH anchors are found (v5 == 3). A second scan
//   collects all class-11 records into the dword_6498EC list.
int SceneClassifyMeisterRecords(const u8* kinds, const u8* subState, int count,
                                int* anchorA, int* anchorB,
                                int* outB, int outBCap) {
    int aA = -1;   // v4 (class 12 / sub 0)
    int aB = -1;   // v3 (class 12 / sub 1)
    int found = 0; // v5 bitmask: 1 -> A found, 2 -> B found

    // First scan: pick the two anchors; stop early when both are found.
    for (int i = 0; i < count && found != 3; ++i) {
        if (kinds[i] == kMeisterClassA) {       // byte_12CE912[..] == 12
            u8 v7 = subState ? subState[i] : 0; // HIBYTE(dword_12CE919[..])
            if (v7 == 0) {
                if ((found & 1) == 0) { aA = i; found |= 1; }
            } else if (v7 == 1) {
                if ((found & 2) == 0) { aB = i; found |= 2; }
            }
            // any other sub-state value: ignored (verbatim — only 0/1 branch).
        }
    }
    // The binary's first scan only LATCHES the first match for each (because it
    // ORs the bit and the `if (v7==1) { if matches } ` guards prevent overwrite);
    // we mirror that with the (found & bit) guards above.

    // Second scan: collect every class-11 record id index.
    int nB = 0;
    for (int i = 0; i < count; ++i) {
        if (kinds[i] == kMeisterClassB) {       // byte_12CE912[..] == 11
            if (outB && nB < outBCap)
                outB[nB] = i;
            ++nB;
        }
    }

    if (anchorA) *anchorA = aA;
    if (anchorB) *anchorB = aB;
    return nB;
}

// 0x502198 — VIBE_Scene_ComputeProductionTickRate cost/rate core.
int SceneComputeProductionTickRate(const int* prices, const u16* counts,
                                   const bool* valid, int slotCount,
                                   bool isSalonType) {
    // sum the per-product cost (price * count) over up-to-4 valid slots.
    int sum = 0;             // v8
    int n = slotCount;
    if (n > 4) n = 4;        // do { ... } while (v9 < 4 && slot present)
    for (int i = 0; i < n; ++i) {
        if (valid[i]) {      // *(v10+46) != 0xFFFF
            float v21 = static_cast<float>(counts[i]);          // (float)*(v10+38)
            float v11 = static_cast<float>(prices[i]) * v21;    // MarketPrice * count
            sum += static_cast<int>(v11);                       // v8 += (int)v11
        }
    }
    if (sum == 0)
        return 0;            // the original logs "costs are zero" and bails.

    int v19;
    if (isSalonType)         // *v5 == 21
        v19 = static_cast<int>(kProductionBase / static_cast<double>(sum));
    else
        v19 = 16000 / sum;

    return (v19 <= 1) ? 1 : v19; // v12 = (v19 <= 1) ? 1 : v19
}

// ===========================================================================
// Full flow drivers.
// ===========================================================================

// 0x500218 — VIBE_Scene_LoadStadtScene.
char SceneLoadStadtScene(const char* name) {
    char buf[264];
    std::snprintf(buf, sizeof(buf), "scenes/*stadt_%s.ed3", name ? name : "");
    int result = H().loadFromStream ? H().loadFromStream(buf) : 0;
    if (result) {
        // SceneGraph_TraverseTree(root, 0, Object_InitParticleEmitters, 6).
        if (H().traverseTree) H().traverseTree(6);
        return 1;  // 0x500262
    }
    return static_cast<char>(result);  // 0x500249 (0)
}

// 0x5023b8 — VIBE_Scene_SyncBuildingEntrance.
int SceneSyncBuildingEntrance(u8 typeByte, i32 objId, u8 rate) {
    // VIBE_Building_MapTypeToCategory(*a1) is called for side effects only here.
    if (H().mapTypeToCategory) (void)H().mapTypeToCategory(typeByte);

    int feeProduct = -1;  // the entrance-fee product type queued
    if (typeByte == kBuildingTypeBauplatz) {
        // QueueRequest17(objId, -1, 1, 310, rate, 0)
        if (H().queueRequest17) H().queueRequest17(objId, -1, 1, 310);
        feeProduct = 310;
        // EnqueueCmd15(linkedObj, -1, MultiplyByRate(500, rate), rate)
        int amt = H().multiplyByRate ? H().multiplyByRate(500, rate) : 500;
        if (H().enqueueCmd15) H().enqueueCmd15(objId, amt);
    } else if (typeByte != kBuildingTypeStore0 &&
               typeByte != kBuildingTypeStore1 &&
               typeByte != kBuildingTypeStore2) {
        // QueueRequest17(objId, -1, 1, 308, rate, 0)
        if (H().queueRequest17) H().queueRequest17(objId, -1, 1, 308);
        feeProduct = 308;
    }
    return feeProduct;
}

// 0x504910 — VIBE_Scene_RefreshBuildingEffects.
int SceneRefreshBuildingEffects(const int* personData, int personCount, int a1) {
    int spawns = 0;
    // for (person : Person_QueryBegin(...)) { if (+97) { clear+set visual; smoke } }
    for (int i = 0; i < personCount; ++i) {
        int data = personData[i];
        if (data) {
            // (visual byte +530 &= 0xF3; |= 4 — modeled inside updateVisualState's
            // caller in the binary; here the smoke spawn is the observable leaf.)
            if (H().spawnChimneySmoke) H().spawnChimneySmoke(data, a1);
            ++spawns;
        }
    }
    // SceneGraph_WalkAndInvoke(root, FlagBuildingGate, 64) — gate pass.
    if (H().traverseTree) H().traverseTree(64);
    // SceneGraph_TraverseTree(root, Light_ApplyTorchEffects, 192) — torch pass.
    if (H().traverseTree) H().traverseTree(192);
    if (H().refreshAllLights) H().refreshAllLights();
    if (H().reserveBauplatz) H().reserveBauplatz(a1);
    if (H().buildTerrainMesh) H().buildTerrainMesh();
    return spawns;
}

}  // namespace guild::sim
