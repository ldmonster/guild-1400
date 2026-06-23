// =============================================================================
// guild::render — texraster_recon2: DDraw-surface management layer of the
// gilde.exe texture record. See texraster_recon2_texsurf.h for the full banner
// and the platform-boundary note. Every function is a 1:1 translation of its
// Hex-Rays decompile; only the DDraw/Render vtable calls are routed through the
// inert hooks (Rule 3).
// =============================================================================
#include "render/texraster_recon2_texsurf.h"

namespace guild::render {

namespace {
TexSurfHooks   g_hooks;
TexSurfGlobals g_glob;
}

void SetTexSurfHooks(const TexSurfHooks& h) { g_hooks = h; }
const TexSurfHooks& GetTexSurfHooks() { return g_hooks; }
TexSurfGlobals& TexSurf() { return g_glob; }

// gilde.exe 0x5d9660 — VIBE_Texture_RestoreSurface@<al>(a1@eax, a2@edx, a3@bl)
char Texture_RestoreSurface(TexSurfRecord& rec, u32 a2, char a3)
{
    // if ((flags & 8) != 0) v4 = 0; else v4 = dword_64A1FC;        /*0x5d966b*/
    u8 mip;
    if ((rec.flags & 8) != 0)
        mip = 0;
    else
        mip = g_glob.defaultMipFlag;

    // v5 = &record[+96] (frontSurface), v6 = &record[+100] (paletteSurface)
    // if ((flags & 4) != 0): CreateDynamicTexture(device, FMT_DYNAMIC, ..,0,0,mip)
    if ((rec.flags & 4) != 0) {
        if (g_hooks.createDynamicTexture)
            return g_hooks.createDynamicTexture(rec, /*fmt byte_14080C4*/1, mip != 0,
                                                /*a2*/0, /*a3*/0);
        return 0;
    }
    // else CreateDynamicTexture(device, FMT_PLAIN byte_14080A0, .., a2, a3, mip)
    if (g_hooks.createDynamicTexture)
        return g_hooks.createDynamicTexture(rec, /*fmt byte_14080A0*/0, mip != 0, a2, a3);
    return 0;
}

// gilde.exe 0x5d96c0 — VIBE_Texture_LoadFromCache@<al>(a1@eax, a2@edx, a3@ebx)
char Texture_LoadFromCache(TexSurfRecord& rec, u32 a2, const u8* a3)
{
    // if (!dword_1406A5C) return RestoreSurface(...);              /*0x5d96d1*/
    if (!g_glob.freeCacheCount)
        return Texture_RestoreSurface(rec, a2, (char)(a3 ? a3[0] : 0));

    // v5 = dword_64A204 (cache base); v6 = 0
    // if (!dword_1406A50) return RestoreSurface(...);              /*0x5d96e2*/
    if (!g_glob.freeCacheCap)
        return Texture_RestoreSurface(rec, a2, (char)(a3 ? a3[0] : 0));

    // The original keys: mipKey == (32 * flags) >> 7  (i.e. bit2 of flags)
    //                    dynKey == (16 * flags) >> 7  (i.e. bit3 of flags)
    const u8 wantMip = (u8)((u8)(32 * rec.flags) >> 7);
    const u8 wantDyn = (u8)((u8)(16 * rec.flags) >> 7);

    u32 i = 0;
    FreeSurfaceEntry* e = &g_glob.freeCache[0];
    // while ( deviceKey != rec.deviceHandle
    //      || (mipKey != wantMip && !byte_14080E5)
    //      || wantDyn != dynKey )                                  /*0x5d9716*/
    while (e->deviceKey != rec.deviceHandle
           || (e->mipKey != wantMip && !g_glob.ignoreMipKey)
           || wantDyn != e->dynKey) {
        ++i;
        ++e;                                  // v5 += 20
        if (i >= g_glob.freeCacheCap)         /*0x5d9724*/
            return Texture_RestoreSurface(rec, a2, (char)(a3 ? a3[0] : 0));
    }

    // Found a reusable entry: claim it.
    e->deviceKey = 0;                         // *(v5+8) = 0            /*0x5d9741*/
    rec.frontSurface  = e->front;             // *(a1+96)  = *v5
    rec.paletteSurface= e->palette;           // *(a1+100) = *(v5+4)
    // v8 = rec.frontSurface; --dword_1406A5C;
    --g_glob.freeCacheCount;                  /*0x5d9760*/
    if (g_hooks.createSurfacePalette)
        return g_hooks.createSurfacePalette(rec, a3, a2);  // (0, v8, a3, a2)
    return 0;
}

// gilde.exe 0x5d9970 — VIBE_Texture_ReleaseSurfaces(result@eax)
void Texture_ReleaseSurfaces(TexSurfRecord& rec)
{
    // if (byte_649D70 && *rec==42 && rec[24]&&rec[25]) -> evict      /*0x5d998c*/
    // (rec[24] == +96 frontSurface, rec[25] == +100 paletteSurface,
    //  rec[17] == +68 texelMem in dword units.)
    if (g_glob.surfaceCacheEnabled && rec.tag == 42
        && rec.frontSurface && rec.paletteSurface) {
        if (g_hooks.evictAndStore) g_hooks.evictAndStore(rec);   // EvictAndStore
        rec.frontSurface = nullptr;           // v4[24] = 0
        rec.paletteSurface = nullptr;         // v4[25] = 0
    } else {
        if (rec.paletteSurface) {             // a4 = rec[25]          /*0x5d99ba*/
            if (g_hooks.releaseSurface) g_hooks.releaseSurface(rec.paletteSurface);
            rec.paletteSurface = nullptr;     // v4[25] = 0
        }
        if (rec.frontSurface) {               // a3 = rec[24]          /*0x5d99d3*/
            if (g_hooks.releaseSurface) g_hooks.releaseSurface(rec.frontSurface);
            rec.frontSurface = nullptr;       // v4[24] = 0
        }
    }
    // Common tail: free the texel buffer (v4[17] == +68) if present. /*0x5d99f5*/
    if (rec.texelMem) {
        if (g_hooks.freeDebug) g_hooks.freeDebug(rec.texelMem);
        rec.texelMem = nullptr;               // v4[17] = 0
    }
}

// gilde.exe 0x5db624 — VIBE_Texture_ReleaseSurface@<al>(a1@eax)
char Texture_ReleaseSurface(TexSurfRecord& rec)
{
    // if (!a1 || !byte_649D70 || (flags & 2)) return 0;             /*0x5db63a*/
    if (!g_glob.surfaceCacheEnabled || (rec.flags & 2) != 0)
        return 0;

    if (rec.frontSurface) {                   // v3 = *(a1+96)         /*0x5db641*/
        if (g_hooks.releaseSurface) g_hooks.releaseSurface(rec.frontSurface);
        rec.frontSurface = nullptr;
    }
    if (rec.paletteSurface) {                 // v4 = *(a1+100)        /*0x5db648*/
        if (g_hooks.releaseSurface) g_hooks.releaseSurface(rec.paletteSurface);
        rec.paletteSurface = nullptr;
    }
    // if (!*(a1+76) || *a1 == 42) return 0;                          /*0x5db665*/
    if (!rec.wrapMask || rec.tag == 42)
        return 0;
    if (g_hooks.uploadToSurface) g_hooks.uploadToSurface(rec);       // re-upload
    return 0;
}

// The exact 0x00RRGGBB -> [R,G,B] unpack from 0x5dbd1a..0x5dbd5f.
void UnpackDdrawPalette(const u32* dwords256, u8* out768)
{
    if (!dwords256 || !out768) return;
    int v12 = 0;                               // dest byte cursor
    for (int v11 = 0; v11 != 256; ++v11) {
        const u32 x = dwords256[v11];
        out768[v12]     = (u8)(x & 0xFF);              // R   /*0x5dbd25*/
        out768[v12 + 1] = (u8)((x & 0xFF00) >> 8);     // G   /*0x5dbd39*/
        out768[v12 + 2] = (u8)((x & 0xFF0000) >> 16);  // B   /*0x5dbd53*/
        v12 += 3;
    }
}

// gilde.exe 0x5dbc38 — VIBE_Texture_CapturePaletteSurface(result@eax, ...)
TexSurfRecord* Texture_CapturePaletteSurface(TexSurfRecord* rec,
                                             const u32* srcPaletteDwords,
                                             u8* palOut)
{
    if (!rec)                                  // if (!result) return result;
        return rec;

    // The original CloneRecord (0x5dbaa0) is at the boundary; the clone here is
    // the same record (we operate in place) — the field mutations below are 1:1.
    TexSurfRecord& clone = *rec;
    const int v8 = clone.width;                // *(v7+116) — width (clone field)
    clone.flags &= ~2u;                        // *(v7+104) &= ~2
    clone.frontSurface = nullptr;              // *(v7+96)  = 0
    clone.paletteSurface = nullptr;            // *(v7+100) = 0
    // *(v7+76) = (v8 - 1) | (v8*v8 - 1)                              /*0x5dbc84*/
    clone.wrapMask = (u32)((v8 - 1) | (v8 * v8 - 1));

    if (clone.tag != 42) {                     // if (v10 != 42)
        u8        palBytes[768] = {0};
        const u8* v14 = nullptr;               // palette ptr passed to LoadFromCache
        u32       v15 = 0;                      // palette entry count

        // if ((flags & 4) != 0 || (byte_14080A4 & 0x20) == 0): no readback
        if ((clone.flags & 4) != 0 || (g_glob.ddrawCaps & 0x20) == 0) {
            v14 = nullptr;
            v15 = 0;
        } else {
            // DDraw lock + palette readback (boundary). The dword->3-byte unpack
            // of the read-back palette is reconstructed exactly.
            const u32* dws = srcPaletteDwords;
            if (dws) {
                UnpackDdrawPalette(dws, palBytes);
                if (palOut) UnpackDdrawPalette(dws, palOut);
                v14 = palBytes;
                v15 = 256;
            } else {
                // No source supplied: behave as if readback produced nothing.
                v14 = nullptr;
                v15 = 0;
            }
        }
        Texture_LoadFromCache(clone, v15, v14);                       /*0x5dbd6f*/
        // The trailing Blt (vtbl+20) copy is GPU-only (boundary) — omitted.
    }
    return &clone;
}

// gilde.exe 0x5d995c — VIBE_Texture_SetBasePath@<eax>(a1@eax, a2@ecx)
namespace {
TexBasePathState g_basePath;
int DefaultVfsNormalize(const char* src, char* destBuf, int /*a2*/) {
    if (!destBuf) return 0;
    int i = 0;
    if (src) for (; i < 259 && src[i]; ++i) destBuf[i] = src[i];
    destBuf[i] = '\0';
    return 1;
}
}
TexBasePathState& TexBasePath() { return g_basePath; }
int (*g_vfsNormalizeDirPath)(const char*, char*, int) = &DefaultVfsNormalize;

int Texture_SetBasePath(const char* a1, int a2)
{
    int result = g_vfsNormalizeDirPath ? g_vfsNormalizeDirPath(a1, g_basePath.baseDir, a2) : 0;
    g_basePath.basePathSet = result;                  // dword_1406A54 = result
    return result;
}

} // namespace guild::render
