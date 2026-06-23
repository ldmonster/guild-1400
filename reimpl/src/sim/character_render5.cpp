// character_render5 — remaining deterministic Character leaves of gilde.exe.
// Faithful 1:1 ports; non-reconstructed renderer/anim/object/heightmap leaves go
// through CharRender5Hooks (inert defaults). See character_render5.h for the map.
#include "sim/character_render5.h"
#include "sim/types.h"   // kPersonStride (canonical person-record stride)

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Reused, already-reconstructed deterministic siblings (extern-declared, linked):
namespace guild::util { int RandomModulo(u16 n); }                 // 0x58b89c
namespace guild::render { int AnimStrCmp(const char* a, const char* b); } // 0x5d3f10

namespace guild::sim {

// ---------------------------------------------------------------------------
// Raw-record byte readers/writers (the engine reads these unaligned by offset).
// ---------------------------------------------------------------------------
namespace {
inline int   RdI32(const u8* p, int off) { int v; std::memcpy(&v, p + off, 4); return v; }
inline void  WrI32(u8* p, int off, int v) { std::memcpy(p + off, &v, 4); }
inline float RdF32(const u8* p, int off) { float v; std::memcpy(&v, p + off, 4); return v; }
inline void  WrF32(u8* p, int off, float v) { std::memcpy(p + off, &v, 4); }
inline u8    RdU8(const u8* p, int off) { return p[off]; }
inline void  WrU8(u8* p, int off, u8 v) { p[off] = v; }
// the engine's `*(int*)(a1+301) >> 24` reads the dword at +301 and arithmetic-
// shifts; that yields the signed byte at +304 ((p+301)[3] == p[304]).
inline int   ActiveNeedIndex(const u8* p) { return static_cast<signed char>(p[kPnActiveIndex + 3]); }
} // namespace

// ---------------------------------------------------------------------------
// Hooks (inert defaults). Defaults keep every function deterministic in isolation.
// ---------------------------------------------------------------------------
namespace {
const CharRender5Hooks* g_hooks = nullptr;

void  DefTouchMeshFrames(void*) {}
void  DefPruneAttachments(void*) {}
int   DefStrCmp(const char* a, const char* b) {
    // faithful to VIBE_Util_StrCmp: 0 == equal (the live default reuses AnimStrCmp).
    return render::AnimStrCmp(a, b);
}
void  DefSetupAttachCamera(void*, int) {}
void  DefScriptError(const char*) {}
void* DefFindByName(const char*) { return nullptr; }
void* DefObjectFindByHandle(void*, int, int, int, int) { return nullptr; }
void* DefResolveMesh(void*) { return nullptr; }
int   DefWorldToTileWithHeight(void*, const float*, int* t, float* h) {
    if (t) { t[0] = 0; t[1] = 0; }
    if (h) *h = 0.0f;
    return 1;
}
u8    DefTerrainCodeAt(void*, int, int) { return 1; }  // walkable (not 0, not 13)
int   DefTraceLineOfSight(void*, const float*, int toCol, int toRow,
                          int* oc, int* orr) {
    if (oc) *oc = toCol;
    if (orr) *orr = toRow;
    return 1;
}
void  DefFreeObjAnimData(void*) {}
void  DefLightUpdateDayCycle(void*) {}

const CharRender5Hooks g_default = {
    DefTouchMeshFrames, DefPruneAttachments, DefStrCmp, DefSetupAttachCamera,
    DefScriptError, DefFindByName, DefObjectFindByHandle, DefResolveMesh,
    DefWorldToTileWithHeight, DefTerrainCodeAt, DefTraceLineOfSight,
    DefFreeObjAnimData, DefLightUpdateDayCycle,
};
} // namespace

void SetCharRender5Hooks(const CharRender5Hooks* h) { g_hooks = h; }
const CharRender5Hooks& GetCharRender5Hooks() { return g_hooks ? *g_hooks : g_default; }

// FlushPendingMesh global state.
PendingMeshState g_pendingMesh = {};
void ResetCharRender5() { g_pendingMesh = PendingMeshState{}; }

// ===========================================================================
// 0x403474 — VIBE_Character_CheckQueueReady.
//   result = 1;
//   if (a1+112 || a1+128) {
//     v1 = a1+112; if (!v1) return 0;
//     v2 = *(v1+104);
//     v3 = (*(v2+340) == -1) ? *(v2+328) : *(v2+340);
//     v4 = a1+112;
//     if (v3 > *v4 && (*(v4+109) & 0x20) == 0) return 0;
//   }
//   return result;
// ===========================================================================
bool CheckQueueReady(const ChActor* actor) {
    if (!actor) return true;
    if (actor->actionSlot1 || actor->actionSlot2) {  // a1+112 || a1+128
        const ActionStream* s = actor->actionSlot1;  // v1 = a1+112
        if (!s)
            return false;                            // primary missing -> not ready
        const StreamDesc* d = static_cast<const StreamDesc*>(s->desc);  // *(v1+104)
        int v3 = (d->endFrame == -1) ? d->altEnd : d->endFrame;
        if (v3 > s->cursor && (s->flags & 0x20) == 0) // *(v4+109) & 0x20
            return false;
    }
    return true;
}

// ===========================================================================
// 0x4521cc — VIBE_Character_UpdateNeedsDecay.
// One need is "primary" (rate +292, decay +296, value +300); the rest are a 13-
// entry table at +144 (value), +136 (rate), +140 (decay), addressed 12*idx. Each
// updated value is clamped to [0,1000]. The dominant index is selected and, when
// it changes from +304, the primary is reset and the new dominant nudged by -50.
// Faithful 1:1.
// ===========================================================================
int UpdateNeedsDecay(u8* person, int seed) {
    if (!person) return seed;

    // gilde.exe 0x4521f2: v3 = value + rate - value*decay (x87 double accumulation).
    // gilde.exe 0x452217: clamp ceiling is dbl_619110 == 1000.0 (NOT the 0.002 eps),
    //   clamp floor 0.0; the stored value is written verbatim (no 1.0 clamp exists).
    double prim = static_cast<double>(RdF32(person, kPnPrimaryValue))
                + static_cast<double>(RdF32(person, kPnPrimaryRate))
                - static_cast<double>(RdF32(person, kPnPrimaryValue))
                      * static_cast<double>(RdF32(person, kPnPrimaryDecay));
    float v18 = static_cast<float>(prim);
    double v11;
    if (prim >= 0.0 && prim >= static_cast<double>(kNeedCeiling))  // dbl_619110 == 1000.0
        v11 = 1000.0;
    else
        v11 = (v18 >= 0.0f) ? static_cast<double>(v18) : 0.0;
    WrF32(person, kPnPrimaryValue, static_cast<float>(v11));       // *(a1+300) = v11

    int active = ActiveNeedIndex(person);
    float best = 0.0f;
    int   dominant = seed;

    for (int k = 0; k < kNeedCount; ++k) {
        int eoff = kNeedEntryStride * k;          // 12 * idx
        if (k != active) {
            int   phase = RdI32(person, kPnPhase);
            if (k == 1 || (k == 0 && phase > 6)) {
                WrI32(person, kPnNeedBase + eoff, 0);
            } else if (k == 2 && (RdI32(person, kPnBusyFlag) != 0 || RdU8(person, kPnKind) == 3)) {
                WrI32(person, kPnNeedBase + eoff, 0);
            } else if (k == 3) {
                // gilde.exe 0x452434: rate*scale + value - value*decay.
                double nv = static_cast<double>(RdF32(person, kPnRateBase + eoff))
                              * static_cast<double>(RdF32(person, kPnScale))
                          + static_cast<double>(RdF32(person, kPnNeedBase + eoff))
                          - static_cast<double>(RdF32(person, kPnNeedBase + eoff))
                                * static_cast<double>(RdF32(person, kPnDecayBase + eoff));
                float v17 = static_cast<float>(nv);
                double c;
                // gilde.exe 0x4524a6: if (v7 < 0.0 || v7 < 1000.0) clamp [0..]; else 1000.0
                if (nv < 0.0 || nv < static_cast<double>(kNeedCeiling))
                    c = (v17 >= 0.0f) ? static_cast<double>(v17) : 0.0;
                else
                    c = 1000.0;
                WrF32(person, kPnNeedBase + eoff, static_cast<float>(c));
            } else {
                // gilde.exe 0x4524f4: value + rate - value*decay.
                double nv = static_cast<double>(RdF32(person, kPnNeedBase + eoff))
                          + static_cast<double>(RdF32(person, kPnRateBase + eoff))
                          - static_cast<double>(RdF32(person, kPnNeedBase + eoff))
                                * static_cast<double>(RdF32(person, kPnDecayBase + eoff));
                float v8 = static_cast<float>(nv);
                double c;
                // gilde.exe 0x452564: if (v8 < 0.0 || v8 < 1000.0) clamp [0..]; else 1000.0
                if (nv < 0.0 || nv < static_cast<double>(kNeedCeiling))
                    c = (v8 >= 0.0f) ? static_cast<double>(v8) : 0.0;
                else
                    c = 1000.0;
                WrF32(person, kPnNeedBase + eoff, static_cast<float>(c));
            }
        }

        float val = RdF32(person, kPnNeedBase + eoff);
        if (k == active) {
            // gilde.exe 0x4522f3: active need weighted by (2.0 - storedPrimary*0.002),
            // using the just-written *(a1+300) (== v11) and dbl_619128/dbl_619118.
            double weighted = static_cast<double>(val)
                            * (static_cast<double>(kNeedTwo)
                               - static_cast<double>(RdF32(person, kPnPrimaryValue))
                                     * static_cast<double>(kNeedDecayEps));
            if (static_cast<double>(best) <= weighted) {
                dominant = k;
                best = static_cast<float>(weighted);
            }
        } else {
            // gilde.exe 0x452594: if (val >= (double)v16) ...
            if (static_cast<double>(val) >= static_cast<double>(best)) {
                dominant = k;
                best = val;
            }
        }
    }

    int result = dominant;
    if (dominant != active) {
        int aoff = kNeedEntryStride * active;
        // if active need value >= 1000.0f, nudge it by -50 (flt_619120).
        if (RdI32(person, kPnNeedBase + aoff) >= 1148846080) // 1000.0f bit pattern
            WrF32(person, kPnNeedBase + aoff, RdF32(person, kPnNeedBase + aoff) + kNeedAdjust);
        WrI32(person, kPnPrimaryValue, 0);
        WrU8(person, kPnLastDominant, static_cast<u8>(dominant));
        result = aoff;  // engine returns a1 + 12*active in this branch (a pointer)
    }
    return result;
}

// ===========================================================================
// 0x452190 — VIBE_Character_UpdateAllNeeds. Walk live persons (marker != -1).
// ===========================================================================
int UpdateAllNeeds(u8* persons, int personCount, int seed) {
    int result = 0;
    for (int i = 0; i < personCount; ++i) {
        u8* rec = persons + static_cast<std::size_t>(i) * kPersonStride;
        i16 marker; std::memcpy(&marker, rec, 2);
        if (marker == -1)
            continue;
        result = UpdateNeedsDecay(rec, seed);
    }
    return result;
}

// ===========================================================================
// 0x4b092c — VIBE_Character_CountByType. Walk the 3-entry owner id list; resolve
// each via PersonFindRecordById (the hook supplies the resolved role byte + the
// object-def want pair), then bucket into out2 (matches +559) / out3 (matches
// +560). Returns the count scanned (== triple) — the engine returns a1 at the end.
// ===========================================================================
int CountByType(const int* idTriple, int triple,
                u8 (*resolveRole)(int personId, CountByTypeDefs* defs),
                int* out2, int* out3) {
    if (out2) *out2 = 0;
    if (out3) *out3 = 0;
    if (!idTriple || !resolveRole) return 0;
    int scanned = 0;
    for (int i = 0; i < triple; ++i) {
        ++scanned;
        int id = idTriple[i];
        if (id == -1)
            continue;                       // *(HIDWORD+140) == -1 -> skip
        CountByTypeDefs defs = { 0, 0 };
        // resolveRole returns the person's matched role byte (>>24 of +354) and the
        // object-def +559/+560 wanted bytes; a 0xFF role means "not resolvable".
        u8 role = resolveRole(id, &defs);
        if (role == 0xFF)
            continue;
        if (role == defs.wantA) { if (out2) ++*out2; }
        else if (role == defs.wantB) { if (out3) ++*out3; }
    }
    return scanned;
}

// ===========================================================================
// 0x4b1e08 — VIBE_Character_GetIndex_Thunk -> GameTime_GetSeasonFromDay (day%4).
// ===========================================================================
int GetIndexThunk(int day) {
    return day % 4;   // VIBE_GameTime_GetSeasonFromDay 0x58339c
}

// ===========================================================================
// 0x43d8f0 — VIBE_Character_SetCameraViewMode.
// ===========================================================================
int SetCameraViewMode(int actorPtr0, void* actor, const char* viewName) {
    const CharRender5Hooks& h = GetCharRender5Hooks();
    if (!actorPtr0) {
        h.scriptError("MoveCharacterCamera(): Invalid character");
        return 1;
    }
    if (h.strCmp(viewName, "CLOSEUP") == 0)        { h.setupAttachCamera(actor, 0); return 0; }
    if (h.strCmp(viewName, "LEFT_SHOULDER") == 0)  { h.setupAttachCamera(actor, 1); return 0; }
    if (h.strCmp(viewName, "RIGHT_SHOULDER") == 0) { h.setupAttachCamera(actor, 2); return 0; }
    if (h.strCmp(viewName, "EGO") == 0)            { h.setupAttachCamera(actor, 3); return 0; }
    return 0;
}

// ===========================================================================
// 0x43d208 — VIBE_Character_CmdGetCharacterHandle.
// ===========================================================================
void* CmdGetCharacterHandle(const char* name) {
    const CharRender5Hooks& h = GetCharRender5Hooks();
    void* found = h.findByName(name);
    if (!found) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "GetCharacterHandle(): character '%s' not found",
                      name ? name : "");
        h.scriptError(buf);
    }
    return found;
}

// ===========================================================================
// 0x43de10 — VIBE_Character_CmdGetCharacterSubObjectHandle.
// ===========================================================================
void* CmdGetCharacterSubObjectHandle(int actorPtr0, void* meshBase, int name, int extra) {
    const CharRender5Hooks& h = GetCharRender5Hooks();
    if (actorPtr0)
        return h.objectFindByHandle(meshBase, 320, name, 0, extra);
    h.scriptError("GetCharacterSubObjectHandle(): invalid character");
    return nullptr;
}

// ===========================================================================
// 0x405894 — VIBE_Character_StopSample.
// ===========================================================================
void StopSample(ChActor* actor) {
    if (!actor) return;
    const CharRender5Hooks& h = GetCharRender5Hooks();
    if ((actor->flagsA & 0x10) != 0) {            // +140 & 0x10 (sample-active)
        if (actor->animMorph0) {                  // *(result+116) != 0
            MeshRec* m = actor->mesh;             // *(result+52)
            if (m && m->material == 0)            // *(*(result+52)+460) == 0
                h.touchMeshFrames(actor);
            if (actor->animMorph && actor->animMorph->attached) {  // *(*(v1+116)+104)
                // Engine: PruneExpiredAttachments(*(mesh+492) + 244) — the morph
                // attach slot rooted at the mesh's low-poly base (+492). We forward
                // the mesh so the hook can reach its attach slot.
                h.pruneAttachments(m);
            }
            actor->animMorph0 = 0;                // *(result+116) = 0
            actor->animMorph  = nullptr;
        }
        actor->flagsA = static_cast<u8>(actor->flagsA & ~0x10);  // clear 0x10
    }
}

// ===========================================================================
// 0x404650 — VIBE_Character_QueryTerrainType.
//   v4 = ResolveMesh(a1); if (!v4) return 0;
//   v5 = *(a1+52); world = {*(v5+76),*(v5+80),*(v5+84)};
//   if (!WorldToTileWithHeight(v4, &world, &col, &row, &h)) return 0;
//   code = *(char*)(24*(*(v4+32)*row + col) + *(v4+36)); -> via terrainCodeAt hook
//   if (!force) { if (!code || code==13) return code; }
//   ... floor pick + SetPosition (render leaf) ...
//   return code;
// We translate the data path 1:1; the floor-pick/SetPosition tail (pure render
// side effect on world XYZ) is a no-op in isolation (no hook needed: it does not
// affect the returned terrain code), matching the inert default behaviour.
// ===========================================================================
int QueryTerrainType(ChActor* actor, int force) {
    if (!actor) return 0;
    const CharRender5Hooks& h = GetCharRender5Hooks();
    void* map = h.resolveMesh(actor);
    if (!map) return 0;

    MeshRec* mesh = actor->mesh;                 // v5 = *(a1+52)
    float world[3] = {
        mesh ? mesh->worldPos[0] : 0.0f,         // *(v5+76)
        mesh ? mesh->worldPos[1] : 0.0f,         // *(v5+80)
        mesh ? mesh->worldPos[2] : 0.0f,         // *(v5+84)
    };
    int tile[2] = { 0, 0 };
    float height = 0.0f;
    if (!h.worldToTileWithHeight(map, world, tile, &height))
        return 0;

    int code = h.terrainCodeAt(map, tile[0], tile[1]);
    if (!force) {
        if (code == 0 || code == 13)
            return code;
    }
    // The floor-pick / SetPosition tail is a pure render side effect; it does not
    // change `code`. (Inert in isolation.)
    return code;
}

// ===========================================================================
// 0x404f6c — VIBE_Character_ComputeTargetTile.
//   v5 = active heightmap; if (!v5) return 0;
//   WorldToTileWithHeight(v5, src, &srcTile, &h);
//   tx = RandomModulo(10) - 4 + srcCol; ty = RandomModulo(10) - 4 + srcRow;
//   clamp tx,ty to [2, mapDim-2];
//   if (TraceLineOfSight(v5, src, tx, src, ty, &tx, &ty, 600, 0)) use traced
//   else use srcTile;
//   TileToWorld(v5, col, out, row); return 1;
// ===========================================================================
int ComputeTargetTile(void* activeMap, const float* srcWorld, float* outWorld) {
    if (!activeMap) return 0;
    const CharRender5Hooks& h = GetCharRender5Hooks();

    int srcTile[2] = { 0, 0 };
    float height = 0.0f;
    h.worldToTileWithHeight(activeMap, srcWorld, srcTile, &height);

    // mapDim is *(v5+32) in the engine; the floor clamp uses (mapDim - 2). With an
    // inert map (dim 0) the clamp degenerates to 2 exactly as the binary would with
    // dim 0 (min(2, x) then max(2, .) -> 2). We read it via the raw map pointer.
    const u8* m = reinterpret_cast<const u8*>(activeMap);
    int mapDim = RdI32(m, 32);

    int tx = static_cast<u16>(util::RandomModulo(10)) - 4 + srcTile[0];
    int ty = static_cast<u16>(util::RandomModulo(10)) - 4 + srcTile[1];
    int hi = mapDim - 2;
    // clamp tx to [2, mapDim-2]  (engine: min then max, exactly as below)
    if (hi < tx) tx = hi;
    if (tx < 2)  tx = 2;
    // clamp ty to [2, mapDim-2]
    if (hi < ty) ty = hi;
    if (ty < 2)  ty = 2;

    int outCol, outRow;
    int col, row;
    if (h.traceLineOfSight(activeMap, srcWorld, tx, ty, &outCol, &outRow)) {
        col = outCol; row = outRow;
    } else {
        col = srcTile[0]; row = srcTile[1];
    }
    // TileToWorld(v5, col, out, row): render leaf; with the inert map we just stamp
    // the chosen tile coords into outWorld (deterministic, exercises the path).
    if (outWorld) { outWorld[0] = static_cast<float>(col); outWorld[1] = height;
                    outWorld[2] = static_cast<float>(row); }
    return 1;
}

// ===========================================================================
// 0x426924 — VIBE_Character_FlushPendingMesh.
// ===========================================================================
void FlushPendingMesh() {
    const CharRender5Hooks& h = GetCharRender5Hooks();
    if (g_pendingMesh.pendingAnim) {
        g_pendingMesh.flushBusy = 1;
        if ((g_pendingMesh.pendingFlag & 0x20) != 0) {
            h.freeObjAnimData(g_pendingMesh.pendingAnim);
            g_pendingMesh.flushBusy = 0;
            g_pendingMesh.pendingAnim = nullptr;
        }
    }
    if (g_pendingMesh.pendingLight) {
        if (g_pendingMesh.activeScene == g_pendingMesh.pendingLightScene)
            h.lightUpdateDayCycle(g_pendingMesh.pendingLight);
    }
}

} // namespace guild::sim
