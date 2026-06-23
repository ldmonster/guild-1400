// =============================================================================
// guild::render — texraster_recon2: texture-cache teardown cluster. 1:1 from the
// Hex-Rays decompile (see texraster_recon2_texcache.h). Memory free + LUT/filter
// builders + per-entry release are boundary callees routed through hooks.
// =============================================================================
#include "render/texraster_recon2_texcache.h"
#include <cstring>

namespace guild::render {

namespace {
TexCacheHooks g_hooks;
TexCacheState g_state;

// dword_62EB38 — the global frame stamp the dispose path snapshots. Modelled as
// a local mirror that the caller can seed; defaults to 0.
u32 g_frameStamp62EB38 = 0;

// Built-in default for releaseEntry: zero the slot's +64 ref dword so the
// `while (slot[16] > 0)` loops terminate after one pass (one-shot drain). The
// real engine's ReleaseEntry decrements/frees; this preserves loop semantics.
void DefaultReleaseEntry(u8* slot) {
    if (slot) *reinterpret_cast<i32*>(slot + 64) = 0;
}
}

void SetTexCacheHooks(const TexCacheHooks& h) {
    g_hooks = h;
    if (!g_hooks.releaseEntry) g_hooks.releaseEntry = &DefaultReleaseEntry;
}
const TexCacheHooks& GetTexCacheHooks() { return g_hooks; }
TexCacheState& TexCache() { return g_state; }

static void EnsureDefaults() {
    if (!g_hooks.releaseEntry) g_hooks.releaseEntry = &DefaultReleaseEntry;
}

// gilde.exe 0x5b9444 — VIBE_TextureCache_Free
void TextureCache_Free()
{
    EnsureDefaults();
    if (g_state.tileCount && g_state.tileBase) {     // if (dword_64A034)
        u32 v2 = 0;
        int v3 = 0;
        do {                                          /*0x5b949d*/
            // a1 = *(u8**)(v3 + dword_64A040)   slot data ptr at slot+0
            u8* slotBase = g_state.tileBase + v3;
            u8* a1 = *reinterpret_cast<u8**>(slotBase);
            if (a1) {
                if (*a1 == 42) {                      // if (*a1 == 42)
                    // drain sub-entries: while ((int)slot[16] > 0) ReleaseEntry(slot)
                    int v4 = v3;
                    while (true) {
                        u8*  sub  = *reinterpret_cast<u8**>(g_state.tileBase + v4);
                        i32* ref  = reinterpret_cast<i32*>(sub + 64); // [+64] == [16]
                        if (*ref <= 0) break;
                        g_hooks.releaseEntry(sub);
                    }
                }
                *reinterpret_cast<u8**>(slotBase) = nullptr;  // *(slot) = 0
                slotBase[64] = 0;                              // *(slot+64) = 0
            }
            ++v2;
            v3 += 68;                                  // v3 += 68
        } while (v2 < g_state.tileCount);
    }
    g_state.tileCount = 0;                             // dword_64A034 = 0
    if (g_hooks.freeDebug) g_hooks.freeDebug(g_state.tileBase);
    g_state.tileBase = nullptr;                        // dword_64A040 = result
}

// gilde.exe 0x5ba428 — VIBE_TextureCache_Shutdown
void TextureCache_Shutdown()
{
    TextureCache_Free();
    g_state.tileStamp = 0;                             // dword_64A038 = v3 (=0)
}

// gilde.exe 0x5ba37c — VIBE_TextureCache_Setup(a1@eax, mipFilterLevel@edx,
//                                              mode@cl, blur@bl)
void TextureCache_Setup(u32 mipFilterLevel, u8 mode, u8 blur)
{
    if (g_hooks.buildChannelLut) g_hooks.buildChannelLut();  // BuildChannelLUT
    if (g_hooks.cacheInit) g_hooks.cacheInit();              // VIBE_TextureCache_Init(a1)
    if (g_hooks.setMipFilterLevel) g_hooks.setMipFilterLevel(mipFilterLevel);

    // Identity-ish texture matrix: edx=0.0f, ebx=1.0f (3F800000h).
    //   C0=0 C1=0 C2=1 C3=1 C4=0 C5=1 C6=0 C7=0 C8=1 C9=0 C10=1 C11=1
    const float Z = 0.0f, O = 1.0f;
    g_state.texMatrix[0]  = Z; g_state.texMatrix[1]  = Z;
    g_state.texMatrix[2]  = O; g_state.texMatrix[3]  = O;
    g_state.texMatrix[4]  = Z; g_state.texMatrix[5]  = O;
    g_state.texMatrix[6]  = Z; g_state.texMatrix[7]  = Z;
    g_state.texMatrix[8]  = O; g_state.texMatrix[9]  = Z;
    g_state.texMatrix[10] = O; g_state.texMatrix[11] = O;

    g_state.filterBlur = blur;                          // byte_64A02C = a4 (bl)
    g_state.filterMode = mode;                          // byte_64A02D = a3 (cl)

    if (g_hooks.computeFilterWeights) g_hooks.computeFilterWeights();
}

// gilde.exe 0x5d9b78 — VIBE_TextureCache_DisposeAll
u32 TextureCache_DisposeAll()
{
    EnsureDefaults();
    // Drain each 128-byte texture record: while ((int)rec[16] > 0) ReleaseEntry.
    {
        u8* rec = g_state.texRecBase;
        for (u32 i = 0; i < g_state.texRecCount; ++i, rec += 128) {   // v1 += 32 dwords
            while (rec && *reinterpret_cast<i32*>(rec + 64) > 0)      // rec[16] == +64
                g_hooks.releaseEntry(rec);
        }
    }
    // memset the whole record array to zero (the original's unrolled memset).
    if (g_state.texRecBase)
        std::memset(g_state.texRecBase, 0, (size_t)g_state.texRecCount * 128);

    g_state.frameStamp = 0;                             // dword_1406A74 = 0

    // Drain + zero each 776-byte mip block; free its [+? = *(blk+? )] payload.
    if (g_state.mipCount && g_state.mipBase) {
        int v9 = 0;
        u32 idx = 0;
        do {                                            /*0x5d9c18*/
            void** payload = reinterpret_cast<void**>(g_state.mipBase + v9);
            if (g_hooks.freeDebug) g_hooks.freeDebug(*payload);
            *payload = nullptr;                          // *(blk) = 0
            v9 += 776;
            ++idx;
        } while (idx < g_state.mipCount);                 // 0x5d9c03 inc edx; cmp edx,dword_1406A60; jb (all N entries)
    }
    if (g_state.mipBase)
        std::memset(g_state.mipBase, 0, (size_t)g_state.mipCount * 776);

    g_state.mipCount    = 0;                             // dword_1406A60 = 0
    g_state.frameStamp  = 0;                             // dword_1406A74 = 0
    g_state.uvScrollHi  = 0;                             // dword_14069DC = 0
    g_state.uvScrollLo  = 0;                             // dword_14069D8 = 0
    g_state.uvScrollV   = 0;                             // dword_14069D4 = 0
    g_state.uvScrollF   = 0.0f;                          // flt_14069D0 = 0.0
    g_state.nextAnimId  = 0;                             // dword_1406A58 = 0
    g_state.frameStamp  = g_frameStamp62EB38;            // dword_1406A6C = dword_62EB38
    g_state.captureFlag = 0;                             // dword_64A1F8 = 0 (defaultMip)
    g_state.defaultMip  = 0;
    return g_frameStamp62EB38;                           // return dword_62EB38
}

// gilde.exe 0x5d9c98 — VIBE_TextureCache_Shutdown_d9c98 (full shutdown: frees arrays)
void TextureCache_ShutdownFull()
{
    EnsureDefaults();
    // Drain each texture record.
    {
        u8* rec = g_state.texRecBase;
        for (u32 i = 0; i < g_state.texRecCount; ++i, rec += 128) {
            while (rec && *reinterpret_cast<i32*>(rec + 64) > 0)
                g_hooks.releaseEntry(rec);
        }
    }
    if (g_hooks.freeDebug) g_hooks.freeDebug(g_state.texRecBase);   // free record array
    g_state.texRecBase = nullptr;                                   // dword_1406A84 = 0

    // Free each mip block payload.
    if (g_state.mipCount && g_state.mipBase) {
        int v5 = 0;
        u32 v4 = 0;
        do {                                            /*0x5d9d17*/
            void** payload = reinterpret_cast<void**>(g_state.mipBase + v5);
            if (g_hooks.freeDebug) g_hooks.freeDebug(*payload);
            *payload = nullptr;
            ++v4;
            v5 += 776;
        } while (v4 < g_state.mipCount);
    }
    g_state.basePathSet = 0;                             // dword_1406A54 = 0
    if (g_state.mipCap) {                                // if (dword_1406A64)
        if (g_hooks.freeDebug) g_hooks.freeDebug(g_state.mipBase);  // free mip array
        g_state.mipBase = nullptr;                       // dword_1406A68 = v8 (=0)
    }
    g_state.mipCount    = 0;                             // dword_1406A60 = 0
    g_state.frameStamp  = 0;                             // dword_1406A74 = 0
    g_state.captureFlag = 0;                             // dword_64A1FC = 0
    if (g_hooks.surfaceCacheFreeAll) g_hooks.surfaceCacheFreeAll();
}

// Test/wiring helper: seed the global frame stamp (dword_62EB38) the dispose
// path snapshots. Not part of the original; the original reads a live global.
void TexCache_SetFrameStamp62EB38(u32 v) { g_frameStamp62EB38 = v; }

} // namespace guild::render
