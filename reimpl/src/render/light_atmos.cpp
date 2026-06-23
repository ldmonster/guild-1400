#include "render/light_atmos.h"

#include "render/light.h"   // kLumaGreen/Red/Blue, kShadeMax (exact flt_628C* bits)

#include <cstring>
#include <vector>

// =============================================================================
// guild::render — ATMOS LIGHTING-TABLE REBUILD implementation. See the header
// for the recovered chain, the scene-walk stand-in and the named gaps. Every
// arithmetic line carries its decompile line; the (int) casts are the engine's
// truncate-toward-zero FPU stores.
// =============================================================================
namespace guild::render {

namespace {

LightAtmosGlobals g_globals;            // the flt_64A0xx / dword_64A0xx block
LightAtmosHooks   g_hooks;              // scene-coupled leaves (default inert)
std::vector<LightAtmosObject*> g_registry;  // scene-graph membership stand-in

// Original byte offsets inside the 80-byte Vertex record (geometry_types.h
// keeps the engine layout; static_assert(sizeof(Vertex) == 80) holds).
constexpr int kAccumR = 48;   // float [12]  seeded from flt_64A074
constexpr int kAccumG = 52;   // float [13]  seeded from flt_64A078
constexpr int kAccumB = 56;   // float [14]  seeded from flt_64A07C
constexpr int kPackLo = 64;   // dword +64 (color0 / flag / lightIdx / +67)
constexpr int kPackHi = 68;   // dword +68 (shade bytes B/G/R at 68/69/70)

inline u8* VB(Vertex& v) { return reinterpret_cast<u8*>(&v); }
inline const u8* VB(const Vertex& v) { return reinterpret_cast<const u8*>(&v); }

inline void PutF(Vertex& v, int off, float f) { std::memcpy(VB(v) + off, &f, 4); }
inline float GetF(const Vertex& v, int off) {
    float f;
    std::memcpy(&f, VB(v) + off, 4);
    return f;
}

// The publish copy: `v[16] = v[17]` (0x5c840e / 0x5c851c) — dword +64 = dword
// +68 (the original moved the raw 4 bytes; lightIdx (+66) = shade byte (+70)).
inline void PublishShadeDword(Vertex& v) {
    std::memcpy(VB(v) + kPackLo, VB(v) + kPackHi, 4);
}

} // namespace

LightAtmosGlobals& LightAtmos() { return g_globals; }

void SetLightAtmosHooks(const LightAtmosHooks& h) { g_hooks = h; }
const LightAtmosHooks& GetLightAtmosHooks() { return g_hooks; }

// gilde.exe 0x5c8964 — VIBE_Light_ResetGlobalState.
void LightAtmosResetGlobalState() {
    g_globals.onscreenBudget  = 0;   // dword_64A05C = 0
    g_globals.onscreenSpent   = 0;   // dword_64A060 = 0
    g_globals.offscreenSpent  = 0;   // dword_64A058 = 0
    g_globals.rebuildSerial   = 0;   // dword_64A064 = 0
    g_globals.offscreenBudget = 0;   // dword_64A054 = 0
}

// gilde.exe 0x5b3900 @0x5b3982/0x5b398c — per-frame budget counter reset.
void LightAtmosBeginUniverseFrame() {
    g_globals.offscreenSpent = 0;    // dword_64A058 = 0
    g_globals.onscreenSpent  = 0;    // dword_64A060 = 0
}

// gilde.exe 0x5b3bbc @0x5b3c19 — frame-end invalidation clear.
void LightAtmosEndUniverseFrame() {
    g_globals.invalidated = 0;       // byte_64A068 = 0
}

// gilde.exe 0x5b85e4 @0x5b86ff..0x5b87b7 — the global ambient store.
void LightAtmosStoreAmbient(const SkyAmbient& a) {
    g_globals.ambientR    = a.r;     // flt_64A074
    g_globals.ambientG    = a.g;     // flt_64A078
    g_globals.ambientB    = a.b;     // flt_64A07C
    g_globals.ambientLuma = a.luma;  // flt_64A070
}

void LightAtmosResetAll() {
    g_globals = LightAtmosGlobals{};
    g_hooks   = LightAtmosHooks{};
    g_registry.clear();
}

// ---------------------------------------------------------------------------
// Vertex accumulator access.
// ---------------------------------------------------------------------------
void VertexLightAccumSet(Vertex& v, float r, float g, float b) {
    PutF(v, kAccumR, r);             // v4[12] = flt_64A074  (0x5c829d)
    PutF(v, kAccumG, g);             // v4[13] = flt_64A078  (0x5c8297)
    PutF(v, kAccumB, b);             // v4[14] = flt_64A07C  (0x5c829a)
}

void VertexLightAccumGet(const Vertex& v, float out[3]) {
    out[0] = GetF(v, kAccumR);
    out[1] = GetF(v, kAccumG);
    out[2] = GetF(v, kAccumB);
}

void VertexLightAccumAdd(Vertex& v, float r, float g, float b) {
    // VIBE_Light_ApplyToCachedVertices accumulate: v29[12]+=, [13]+=, [14]+=.
    PutF(v, kAccumR, GetF(v, kAccumR) + r);
    PutF(v, kAccumG, GetF(v, kAccumG) + g);
    PutF(v, kAccumB, GetF(v, kAccumB) + b);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5c7da4 — VIBE_Light_RemoveCacheEntry. List head *(obj+488)+408;
// node key at +4, next at +8; frees via VIBE_Memory_FreeDebug (delete here).
// ---------------------------------------------------------------------------
char LightAtmosRemoveCacheEntry(LightAtmosObject& o, const void* key) {
    // (the original also returns 1 for a null object / null +488 record)
    if (!key) {                                   // a2 == 0: free the whole list
        LightCacheNode* v7 = o.cacheList;         // *(v6+408)
        while (v7) {                              // do { Free(v7); } while (next)
            LightCacheNode* nxt = v7->next;       // *(v7+8)
            delete v7;                            // VIBE_Memory_FreeDebug
            v7 = nxt;
        }
        o.cacheList = nullptr;                    // *(... +408) = 0
        return 1;
    }
    LightCacheNode* v10 = o.cacheList;            // *(v6+408)
    if (!v10)
        return 1;
    LightCacheNode* v12 = o.cacheList;
    if (key == v12->key) {                        // a2 == *(v12+4): head arm
        o.cacheList = v12->next;                  // *(v11+408) = *(v12+8)
        delete v12;
        return 1;
    }
    // scan arm: find the node whose NEXT matches the key.
    LightCacheNode* a3 = nullptr;
    while (v12) {
        a3 = v12->next;                           // a3 = *(v12+8)
        if (a3 && key == a3->key)
            break;
        v12 = v12->next;
    }
    if (!v12 || !a3 || key != a3->key)
        return 1;                                 // missing key: no-op
    v12->next = a3->next;                         // *(v12+8) = *(a3+8)
    delete a3;
    return 1;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5c8218 — VIBE_Light_BuildObjectCache (the lighting-table rebuild).
// ---------------------------------------------------------------------------
char LightAtmosBuildObjectCache(LightAtmosObject& o, int frameStamp) {
    // if (!*(a1+460)) *(a1+460) = SelectLodFrame(a1); null frame -> return 0.
    MeshGeometry* v2 = o.geom;
    if (!v2)
        return 0;
    if ((o.flags530 & 2) != 0)                    // hidden object: no rebuild
        return 0;

    // v30 = 255 (dead store in the original; kept as a comment only).
    ++g_globals.walkDepth;                        // ++dword_64A050
    if (g_hooks.prepareObjectCache)               // VIBE_Light_PrepareObjectCache
        g_hooks.prepareObjectCache(o, g_hooks.prepareCtx);
    g_globals.rebuildScratch = 0.0f;              // flt_64A06C store (0x5c826b;
                                                  //  zeroed pre-call, re-zeroed at the tail)

    // --- SEED: every entry's accumulator = the global ambient (0x5c827c..a8) --
    {
        const float v6 = g_globals.ambientB;      // fld flt_64A07C
        const float v7 = g_globals.ambientG;      // fld flt_64A078
        const float v8 = g_globals.ambientR;      // fld flt_64A074
        Vertex* v4 = v2->vertices;
        for (int v5 = 0; v5 < v2->vertexCount; ++v5, ++v4) {
            PutF(*v4, kAccumG, v7);               // v4[13] = v7
            PutF(*v4, kAccumB, v6);               // v4[14] = v6
            PutF(*v4, kAccumR, v8);               // v4[12] = v8
        }
    }

    // --- affecting-light collect + accumulate (0x5c82b2..0x5c833b) ----------
    // ctx = { obj, count, buffer, collectFlag }; collectFlag (+12) =
    // (u8)(4 * *(obj+528)) >> 7 (bit5 of flags528). Default hook absent ==
    // no lights collected (v25 == 0): the walk + alloc + apply are skipped.
    const u8 v27 = (u8)((u8)(4u * o.flags528) >> 7);
    if (g_hooks.accumulateLights)
        g_hooks.accumulateLights(o, v27, g_hooks.lightsCtx);

    // --- REDUCE the accumulators to the shade bytes -------------------------
    if (g_globals.colorMode) {                    // byte_649D70 != 0: colour branch
        Vertex* v11 = v2->vertices;
        for (int i = 0; i < v2->vertexCount; ++i, ++v11) {
            float v33 = GetF(*v11, kAccumR);      // v11[12]
            float v29 = GetF(*v11, kAccumG);      // v11[13]
            float v34 = GetF(*v11, kAccumB);      // v11[14]
            // v13 = (v33 <= v29) ? v29 : v33 ; v14 = (v13 <= v34) ? v34 : v13
            float v13 = (v33 <= v29) ? v29 : v33;
            float v14 = (v13 <= v34) ? v34 : v13;
            // if (bits(v14) > 1132396544) — the int compare on the float bit
            // pattern (0x437EFFFF == 254.999985f); scale to <= 255.
            i32 bits;
            std::memcpy(&bits, &v14, 4);
            if (bits > 1132396544) {
                const float v15 = kShadeMax / v14;  // flt_628C94 / v31
                v33 = v33 * v15;
                v29 = v29 * v15;
                v34 = v15 * v34;
            }
            VB(*v11)[70] = (u8)(int)v33;          // +70 = R   (0x5c83e6)
            VB(*v11)[69] = (u8)(int)v29;          // +69 = G   (0x5c83f7)
            VB(*v11)[68] = (u8)(int)v34;          // +68 = B   (0x5c8408)
            PublishShadeDword(*v11);              // v11[16] = v11[17]
        }
    } else {                                      // luma branch (byte_649D70 == 0)
        Vertex* v20 = v2->vertices;
        for (int j = 0; j < v2->vertexCount; ++j, ++v20) {
            const float v33 = GetF(*v20, kAccumR);  // v20[12]
            const float v29 = GetF(*v20, kAccumG);  // v20[13]
            const float v34 = GetF(*v20, kAccumB);  // v20[14]
            // v22 = g*0.58999997 + r*0.30000001 + b*0.10999999 (flt_628C88/8C/90)
            const float v22 = v29 * kLumaGreen + v33 * kLumaRed + v34 * kLumaBlue;
            const int vi = (int)v22;              // truncate toward zero
            u8 v23;
            if ((u32)vi >= 0xFFu)                 // unsigned compare: negatives
                v23 = 0xFF;                       // saturate too (LOBYTE = -1)
            else
                v23 = (u8)vi;
            VB(*v20)[70] = v23;                   // +70 = the luma shade
            PublishShadeDword(*v20);              // v20[16] = v20[17]
        }
    }

    // --- per-poly material pass (0x5c842a) -----------------------------------
    LightAtmosApplyVertexShading(o);

    // --- tail bookkeeping (0x5c844f..0x5c8481) -------------------------------
    // (the WalkAndInvoke(obj+520, RecomputeForObject, 4) child pass: named gap —
    //  children relight via their own registry entry; see the header.)
    o.flags528 |= 4;                              // *(a1+528) |= 4 (cache built)
    g_globals.rebuildScratch = 0.0f;              // flt_64A06C = 0.0
    o.lastBuildFrame = frameStamp;                // *(a1+68) = dword_62EB38
    o.cacheSerial = g_globals.rebuildSerial;      // *(*(a1+492)+2300) = dword_64A064
    --g_globals.walkDepth;                        // dword_64A050 = v18 - 1
    return 1;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5c7f04 — VIBE_Light_ApplyVertexShading.
// ---------------------------------------------------------------------------
char LightAtmosApplyVertexShading(LightAtmosObject& o) {
    if (!o.geom)
        return 0;
    const u8 v2 = o.flags530;                     // *(a1+530)
    const bool v21 = (v2 & 1) == 0;
    const bool v20 = (i8)o.flags529 >= 0;         // *(a1+529) sign bit clear
    if ((v2 & 2) != 0)                            // hidden: skip the whole pass
        return 0;

    constexpr float kInv256 = 1.0f / 256.0f;      // flt_628C70 (0x3B800000)
    Polygon* v3 = o.geom->polygons;               // a2[1], 40-byte stride
    for (int v16 = 0; v16 < o.geom->polyCount; ++v16, ++v3) {
        const LightAtmosPolyMaterial* v5 =        // *(v3+20) material record
            g_hooks.polyMaterial
                ? g_hooks.polyMaterial(o, *v3, v16, g_hooks.materialCtx)
                : nullptr;
        if (!v5)
            continue;                             // null material: untouched
        if (v21) {
            // *(v3+38) = (flags & 0xFB) | (4 * ((u8)(8 * mat[104]) >> 7))
            const u8 v6 = (u8)(v3->flags38 & 0xFB);
            const u8 v7 = (u8)(4u * ((u8)(8u * v5->flagByte104) >> 7));
            v3->flags38 = (u8)(v7 | v6);
        }
        const u32 a1 = v20 ? v5->lightWord108     // *(v5+108)
                           : o.frameLightWord376; // a2[94] (frame +376)
        const u16 v18 = (u16)a1;
        Vertex* vs[3] = {v3->v0, v3->v1, v3->v2}; // the 3 vertex pointers (+0/4/8)
        if ((a1 & 0x10000) != 0) {
            // per-vertex recompute from the accumulator triple (+48/52/56):
            //   v17 = (r*0.30 + g*0.59 + b*0.11 + HIBYTE(v18)) * (1/256) * lo
            const float v13 = (float)(u8)v18;     // LOBYTE scale
            const float hi  = (float)(u8)(v18 >> 8);  // HIBYTE(v18)
            for (int k = 0; k < 3; ++k) {
                Vertex* v9 = vs[k];
                if (!v9) continue;
                const float v17 = (GetF(*v9, 48) * kLumaRed     // flt_628C64
                                   + GetF(*v9, 52) * kLumaGreen // flt_628C68
                                   + GetF(*v9, 56) * kLumaBlue  // flt_628C6C
                                   + hi)
                                  * kInv256 * v13;
                const float v14 = (kShadeMax >= v17) ? v17 : 255.0f; // flt_628C74
                const u8 b = (u8)(int)v14;        // HIBYTE(v19) = (int)v14
                u32 v19 = (u32)b | ((u32)b << 8) | ((u32)b << 16) | ((u32)b << 24);
                std::memcpy(VB(*v9) + 64, &v19, 4);  // *(v9+64) = v19
                std::memcpy(VB(*v9) + 68, &v19, 4);  // *(v9+68) = v19
            }
        } else if ((u8)a1 != 0xFF) {
            for (int k = 0; k < 3; ++k) {
                Vertex* v11 = vs[k];
                if (!v11) continue;
                VB(*v11)[67] = (u8)a1;            // *(v11+67) = a1
                VB(*v11)[71] = (u8)a1;            // *(v11+71) = a1
            }
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Registry (the scene-graph membership stand-in).
// ---------------------------------------------------------------------------
void LightAtmosRegisterObject(LightAtmosObject* o) {
    if (!o) return;
    for (LightAtmosObject* p : g_registry)
        if (p == o) return;                       // idempotent
    g_registry.push_back(o);
}

void LightAtmosUnregisterObject(LightAtmosObject* o) {
    for (std::size_t i = 0; i < g_registry.size(); ++i) {
        if (g_registry[i] == o) {
            g_registry.erase(g_registry.begin() + (long)i);
            return;
        }
    }
}

int LightAtmosRegisteredCount() { return (int)g_registry.size(); }

// ---------------------------------------------------------------------------
// gilde.exe 0x5c886c — VIBE_Light_RefreshAllObjects(force).
// ---------------------------------------------------------------------------
u8 LightAtmosRefreshAllObjects(u8 force, int frameStamp) {
    ++g_globals.rebuildSerial;                    // ++dword_64A064 (ALWAYS)
    ++g_globals.walkDepth;                        // ++dword_64A050
    u8 result = force;
    if (force || g_globals.relightAlways) {       // (result || byte_649D54)
        if (force <= 1) {
            // WalkAndInvoke(root, 0, RemoveCacheEntry, 2, 0): invalidate walk.
            for (LightAtmosObject* o : g_registry)
                result = LightAtmosRemoveCacheEntry(*o, nullptr);
            g_globals.invalidated = 1;            // byte_64A068 = 1
        } else {
            // TraverseTree(root, 0, BuildObjectCache, 64): eager rebuild walk.
            for (LightAtmosObject* o : g_registry)
                result = (u8)LightAtmosBuildObjectCache(*o, frameStamp);
        }
    }
    if (g_hooks.floor) {                          // if (dword_64A028)
        if (g_hooks.floorBuildTilePolys)          // VIBE_Floor_BuildTilePolys
            result = g_hooks.floorBuildTilePolys(g_hooks.floor);
    }
    --g_globals.walkDepth;                        // --dword_64A050
    return result;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5add1c — the three lazy-rebuild trigger arms.
// ---------------------------------------------------------------------------
char LightAtmosEnsureNodeLit(LightAtmosObject& o, bool freshFrame,
                             bool onScreen, int frameStamp) {
    if (o.type533 != 4 || o.isFloorNode)          // *(a1+533) == 4, a1 != floor
        return 0;
    if (freshFrame) {
        // @0x5adfc3: (dword_64A064 > cacheSerial || byte_64A068) -> rebuild.
        if (g_globals.rebuildSerial > o.cacheSerial || g_globals.invalidated)
            return LightAtmosBuildObjectCache(o, frameStamp);
        return 0;
    }
    if (onScreen) {
        // @0x5adf07: serial stale && dword_64A05C > dword_64A060.
        if (o.cacheSerial < g_globals.rebuildSerial
            && g_globals.onscreenBudget > g_globals.onscreenSpent) {
            g_globals.onscreenSpent +=            // dword_64A060 += *(v3+8)
                (u32)(o.geom ? o.geom->vertexCount : 0);
            return LightAtmosBuildObjectCache(o, frameStamp);
        }
        return 0;
    }
    // @0x5ae25f (culled arm): serial stale && dword_64A054 > dword_64A058.
    if (g_globals.rebuildSerial <= o.cacheSerial
        || g_globals.offscreenBudget <= g_globals.offscreenSpent)
        return 0;
    g_globals.offscreenSpent +=                   // dword_64A058 += *(v3+8)
        (u32)(o.geom ? o.geom->vertexCount : 0);
    o.flags528 |= 4;                              // *(a1+528) |= 4 (0x5ae279)
    return LightAtmosBuildObjectCache(o, frameStamp);
}

} // namespace guild::render
