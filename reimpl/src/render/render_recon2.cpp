// render_recon2.cpp — 1:1 reconstruction of a slice of the gilde.exe render
// engine-control / span-clip leaves. See render_recon2.h for scope notes.
//
// Reference of record: Hex-Rays decompile of module gilde.exe (imagebase
// 0x400000). Device/registry edges are routed through RenderRecon2Hooks per
// project rule 3 (Vulkan tech swap) and rule 6 (no other unapproved swaps).

#include "render_recon2.h"

namespace guild::render {

// ---------------------------------------------------------------------------
// Owning state + hooks (single definition).
// ---------------------------------------------------------------------------
namespace {

RenderRecon2State g_state;

// Inert default callees. They are intentionally no-ops / identity so the
// portable/headless build links without DDraw/D3D and the pure control flow of
// the reconstructed functions can still be exercised by golden-vector tests.
void   d_radixSortDrawList(int, int) {}
void   d_drawTexturedTriangles() {}
void   d_rasterizeMeshList() {}
void   d_sceneGraphTraverseTree(int, int, void (*)(), int) {}
void   d_shadowClearAllCasters() {}
void   d_shadowResetCasterTransforms() {}
void   d_meshMarkAllFramesDirty() {}
int    d_textureCacheReset() { return 0; }
void   d_skyBuildDomeMesh(int, int) {}
void   d_objectInvalidateCurrent(int) {}
int    d_textureUploadAllRecords(int, int (*)(void)) { return 0; }
u8     d_lightRefreshAllObjects(unsigned int) { return 0; }
void   d_setGammaTable(u8) {}
u8     d_setMipFilterLevel(u8 level) { return level; }
void   d_floorRenderMinimap(int) {}
void   d_remapSurfacePalette(u16*, int, int) {}
void   d_devicePresentBegin(int, int) {}
int    d_devicePresentEnd(int) { return 0; }

RenderRecon2Hooks g_hooks = {
    d_radixSortDrawList,
    d_drawTexturedTriangles,
    d_rasterizeMeshList,
    d_sceneGraphTraverseTree,
    d_shadowClearAllCasters,
    d_shadowResetCasterTransforms,
    d_meshMarkAllFramesDirty,
    d_textureCacheReset,
    d_skyBuildDomeMesh,
    d_objectInvalidateCurrent,
    d_textureUploadAllRecords,
    d_lightRefreshAllObjects,
    d_setGammaTable,
    d_setMipFilterLevel,
    d_floorRenderMinimap,
    d_remapSurfacePalette,
    d_devicePresentBegin,
    d_devicePresentEnd,
};

} // namespace

void InstallRenderRecon2Hooks(const RenderRecon2Hooks& hooks) { g_hooks = hooks; }
const RenderRecon2Hooks& GetRenderRecon2Hooks() { return g_hooks; }
RenderRecon2State& Recon2State() { return g_state; }

void ResetRenderRecon2State() {
    g_state = RenderRecon2State{};
    g_hooks = RenderRecon2Hooks{
        d_radixSortDrawList,        d_drawTexturedTriangles,
        d_rasterizeMeshList,        d_sceneGraphTraverseTree,
        d_shadowClearAllCasters,    d_shadowResetCasterTransforms,
        d_meshMarkAllFramesDirty,   d_textureCacheReset,
        d_skyBuildDomeMesh,         d_objectInvalidateCurrent,
        d_textureUploadAllRecords,  d_lightRefreshAllObjects,
        d_setGammaTable,            d_setMipFilterLevel,
        d_floorRenderMinimap,       d_remapSurfacePalette,
        d_devicePresentBegin,       d_devicePresentEnd,
    };
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5af200 — VIBE_Render_ResetEngineState
// ---------------------------------------------------------------------------
//   dword_649DA0 = 0;
//   if (byte_649D70) { RadixSort(dword_13FC770,1); DrawTexturedTriangles(); }
//   else             { RadixSort(dword_13FC770,0); RasterizeMeshList(); }
//   dword_13FC54C=0; dword_13FC574=0; dword_13FC4E0=0;
//   dword_13FC570 = dword_13FC584;
//   dword_13FC770 = 0;
//   return dword_13FC584;
int ResetEngineState() {
    RenderRecon2State& s = g_state;

    s.viewParamA = 0;  // dword_649DA0 = 0
    if (s.engineEnabled) {
        g_hooks.radixSortDrawList(s.drawListHead, 1);
        g_hooks.drawTexturedTriangles();
    } else {
        g_hooks.radixSortDrawList(s.drawListHead, 0);
        g_hooks.rasterizeMeshList();
    }
    s.field13FC54C = 0;
    s.field13FC574 = 0;
    s.field13FC4E0 = 0;
    s.field13FC570 = s.drawListCount;  // dword_13FC570 = dword_13FC584
    s.drawListHead = 0;                // dword_13FC770 = 0
    return s.drawListCount;            // return dword_13FC584
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5af8e4 — VIBE_Render_SetEngineEnabled (ret al)
// ---------------------------------------------------------------------------
//   v2 = byte_649D70;                          (prev state)
//   if (byte_649D7C && a1) {
//       byte_649D70 = 1;
//       if (byte_649D71)
//           TraverseTree(off_649D64, 0, ShadowClearAllCasters, 64);
//       TextureCache_Reset();
//       if (dword_64A7C8) Sky_BuildDomeMesh(dword_64A7C8, 1);
//   } else byte_649D70 = 0;
//   result = byte_649D70;
//   if (v2 != byte_649D70 && byte_649D71) {
//       Object_InvalidateCurrent(0);
//       Texture_UploadAllRecords(1, 0);
//       return Light_RefreshAllObjects(1);
//   }
//   return result;
u8 SetEngineEnabled(int enable) {
    RenderRecon2State& s = g_state;

    int prev = s.engineEnabled;  // v2 = (unsigned __int8)byte_649D70
    if (s.enableAllowed && enable) {
        s.engineEnabled = 1;
        if (s.sceneInit) {
            g_hooks.sceneGraphTraverseTree(s.sceneRootPtr, 0,
                                           g_hooks.shadowClearAllCasters, 64);
        }
        g_hooks.textureCacheReset();
        if (s.sky) {
            g_hooks.skyBuildDomeMesh(s.sky, 1);
        }
    } else {
        s.engineEnabled = 0;
    }
    u8 result = s.engineEnabled;
    if (prev != static_cast<int>(s.engineEnabled)) {
        if (s.sceneInit) {
            g_hooks.objectInvalidateCurrent(0);
            g_hooks.textureUploadAllRecords(1, nullptr);
            return g_hooks.lightRefreshAllObjects(1u);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5b04a8 — VIBE_Render_ApplyFogAndLightFlags (ret al)
// ---------------------------------------------------------------------------
//   a1=dl mipLevel, a2=cl fogMode, a3=bl shadowQuality, a4=al gammaLevel,
//   a5=upload callback.
//   if ((byte_14080ED & 7) != a2 || a3 != byte_64A350) {
//       byte_14080ED = (a2 & 7) | (byte_14080ED & 0xF8);
//       TraverseTree(off_649D64, 0, Shadow_ResetCasterTransforms, 192);
//   }
//   SetGammaTable(a4);
//   result = SetMipFilterLevel(a1);
//   if (a3 != byte_64A350) {
//       TextureCache_Reset();
//       byte_64A350 = a3;
//       return Texture_UploadAllRecords(1, a5);
//   }
//   return result;
u8 ApplyFogAndLightFlags(u8 mipLevel, char fogMode, char shadowQuality,
                         u8 gammaLevel, int (*uploadCb)(void)) {
    RenderRecon2State& s = g_state;

    if ((s.fogFlags & 7) != static_cast<u8>(fogMode & 7) ||
        shadowQuality != static_cast<char>(s.shadowQuality)) {
        // byte_14080ED = a2 & 7 | byte_14080ED & 0xF8
        s.fogFlags = static_cast<u8>((static_cast<u8>(fogMode) & 7) |
                                     (s.fogFlags & 0xF8));
        g_hooks.sceneGraphTraverseTree(s.sceneRootPtr, 0,
                                       g_hooks.shadowResetCasterTransforms, 192);
    }
    g_hooks.setGammaTable(gammaLevel);
    u8 result = g_hooks.setMipFilterLevel(mipLevel);
    if (shadowQuality != static_cast<char>(s.shadowQuality)) {
        g_hooks.textureCacheReset();
        s.shadowQuality = static_cast<u8>(shadowQuality);
        return static_cast<u8>(g_hooks.textureUploadAllRecords(1, uploadCb));
    }
    return result;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5b499c — VIBE_Render_PresentSceneAndClearFlags (ret eax)
// ---------------------------------------------------------------------------
//   LOBYTE(v5) = a1;
//   v2 = 0;
//   if (dword_1406A80) {
//       v3 = 0;
//       do {
//           ++v2;
//           *(BYTE*)(v3 + dword_1406A84 + 104) &= ~0x80;
//           v3 += 128;
//       } while (v2 < dword_1406A80);
//   }
//   (*(dev+36))(dev, v5);                       // device-present begin
//   TraverseTree(off_649D64, 0, Mesh_MarkAllFramesDirty, 64);
//   if (dword_64A028 && a2) Floor_RenderMinimap(dword_64A028);
//   return (*(dev+40))(dev);                     // device-present end
int PresentSceneAndClearFlags(char presentFlag, char drawMinimap) {
    RenderRecon2State& s = g_state;

    // v5's low byte carries a1; the upper bytes are uninitialized garbage in
    // the original but only the low byte is consumed by the device call.
    int v5 = 0;
    *reinterpret_cast<unsigned char*>(&v5) = static_cast<unsigned char>(presentFlag);

    u32 v2 = 0;
    if (s.objectCount) {
        int v3 = 0;  // byte offset into the record table
        do {
            ++v2;
            if (s.objectTable) {
                s.objectTable[v3 + 104] &= static_cast<u8>(~0x80u);
            }
            v3 += 128;
        } while (v2 < s.objectCount);
    }

    g_hooks.devicePresentBegin(s.deviceCtx, v5);
    g_hooks.sceneGraphTraverseTree(s.sceneRootPtr, 0,
                                   g_hooks.meshMarkAllFramesDirty, 64);
    if (s.floor && drawMinimap) {
        g_hooks.floorRenderMinimap(s.floor);
    }
    return g_hooks.devicePresentEnd(s.deviceCtx);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5d9014 — VIBE_Render_WithSurfaceContext2
// ---------------------------------------------------------------------------
//   a1=eax x, a2=edx y, a3=ecx width, a4=ebx remapTable, a5=surfaceRec ptr.
//   if (a2 < dword_64A1B8) { a3 -= dword_64A1B8 - a2; a2 = 0; }
//   if (a2 + a3 > dword_64A1C0) a3 = dword_64A1C0 - a2 - 1;
//   v5 = dword_64A1C8;
//   dword_64A1C8 = *(DWORD*)(a5 + 16);                       // surface stride
//   Gfx_RemapSurfacePalette(
//       (WORD*)(*(DWORD*)(a5 + 28) + 2*(dword_64A1C8*a2 + a1)), a4, a3);
//   dword_64A1C8 = v5;
//
// The original treats the clip math with signed ints and does NOT re-clamp a3
// after the first adjustment (so a3 can legitimately go negative; the callee
// receives it verbatim). The surface record is read at byte offsets +16
// (stride, int) and +28 (pixel base pointer to u16).
void WithSurfaceContext2(int x, int y, int width, int remapTable,
                         const Recon2SurfaceRec* surfaceRec) {
    RenderRecon2State& s = g_state;

    int a1 = x;
    int a2 = y;
    int a3 = width;

    if (a2 < s.clipMinY) {
        a3 -= s.clipMinY - a2;  // a3 -= dword_64A1B8 - a2
        a2 = 0;
    }
    if (a2 + a3 > s.clipMaxY) {
        a3 = s.clipMaxY - a2 - 1;  // dword_64A1C0 - a2 - 1
    }

    int v5 = s.surfaceStride;          // save dword_64A1C8
    s.surfaceStride = surfaceRec->stride;  // dword_64A1C8 = *(DWORD*)(a5 + 16)

    // (WORD*)((*(DWORD*)(a5+28)) + 2 * (dword_64A1C8 * a2 + a1))
    u16* pixels = surfaceRec->pixels +
                  (static_cast<std::intptr_t>(s.surfaceStride) * a2 + a1);
    g_hooks.remapSurfacePalette(pixels, remapTable, a3);

    s.surfaceStride = v5;  // restore dword_64A1C8
}

} // namespace guild::render
