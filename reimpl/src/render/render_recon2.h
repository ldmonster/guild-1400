#pragma once
#include "guild/common/types.h"
#include <cstdint>

// =============================================================================
// guild::render — render-engine helpers, recon batch 2 (render_recon2).
//
// 1:1 translations from the Hex-Rays reference (module gilde.exe, imagebase
// 0x400000) of a slice of the VIBE_Render_* engine-control / span-clip leaves.
//
// SCOPE / RULE-3 BOUNDARY
//   The original render module is half pure engine logic, half DirectDraw /
//   Direct3D / Win32-registry plumbing. Per project rule 3 (3D tech -> Vulkan)
//   and rule 6 (no other unapproved tech swaps), the device/registry wrappers
//   are NOT reconstructed here; only the pure state/flag/clip logic is. Where a
//   reconstructed function legitimately reaches a device-present or a
//   cross-module callee, that edge is routed through an installable
//   RenderRecon2Hooks struct with inert defaults (defined in the .cpp). This
//   matches the convention already used by render_leaves*.{h,cpp}.
//
// Functions translated here (all verified UNTRANSLATED / absent at write time):
//   0x5af200  VIBE_Render_ResetEngineState        (flush draw list, reset counters)
//   0x5af8e4  VIBE_Render_SetEngineEnabled        (3D-engine enable/disable state)
//   0x5b04a8  VIBE_Render_ApplyFogAndLightFlags   (fog/shadow/gamma/mip flag apply)
//   0x5b499c  VIBE_Render_PresentSceneAndClearFlags (clear obj dirty bits + present)
//   0x5d9014  VIBE_Render_WithSurfaceContext2     (span clip + palette remap)
//
// All module globals touched by these functions are owned locally as a single
// RenderRecon2State instance (one definition, this TU) to avoid ODR clashes
// with the rest of the src/** tree; cross-TU callees go through hooks.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Cross-module callee hooks (inert defaults in render_recon2.cpp; tests/
// integration install real ones). Each maps to a specific gilde.exe address.
// ---------------------------------------------------------------------------
struct RenderRecon2Hooks {
    // 0x5aef34 VIBE_Render_RadixSortDrawList(listHead, mode)
    void (*radixSortDrawList)(int listHead, int mode);
    // 0x5ae434 VIBE_Render_DrawTexturedTriangles() — hardware (D3D) flush path.
    void (*drawTexturedTriangles)();
    // 0x5aec88 VIBE_Render_RasterizeMeshList() — software raster flush path.
    void (*rasterizeMeshList)();

    // 0x5ac86c VIBE_SceneGraph_TraverseTree(rootPtr, arg, fn, kind)
    void (*sceneGraphTraverseTree)(int rootPtr, int arg, void (*fn)(), int kind);
    // 0x5f474c VIBE_Shadow_ClearAllCasters  (passed as fn to traverse, kind 64)
    void (*shadowClearAllCasters)();
    // 0x5f4880 VIBE_Shadow_ResetCasterTransforms (fn to traverse, kind 192)
    void (*shadowResetCasterTransforms)();
    // 0x5b4944 VIBE_Mesh_MarkAllFramesDirty (fn to traverse, kind 64)
    void (*meshMarkAllFramesDirty)();

    // 0x5b9f54 VIBE_TextureCache_Reset()
    int  (*textureCacheReset)();
    // 0x5ef980 VIBE_Sky_BuildDomeMesh(sky, rebuild)
    void (*skyBuildDomeMesh)(int sky, int rebuild);
    // 0x5af2e4 VIBE_Object_InvalidateCurrent(arg)
    void (*objectInvalidateCurrent)(int arg);
    // 0x5db4b8 VIBE_Texture_UploadAllRecords(a, cb)
    int  (*textureUploadAllRecords)(int a, int (*cb)(void));
    // 0x5c886c VIBE_Light_RefreshAllObjects(flags) -> byte
    u8   (*lightRefreshAllObjects)(unsigned int flags);

    // 0x5b9ef4 VIBE_Render_SetGammaTable(level)
    void (*setGammaTable)(u8 level);
    // 0x5b9e74 VIBE_Render_SetMipFilterLevel(level) -> byte
    u8   (*setMipFilterLevel)(u8 level);

    // 0x5c2a9c VIBE_Floor_RenderMinimap(floor)
    void (*floorRenderMinimap)(int floor);

    // 0x5fc554 VIBE_Gfx_RemapSurfacePalette(pixels16, table, count)
    void (*remapSurfacePalette)(u16* pixels16, int table, int count);

    // Device-present edge (rule-3 boundary): the original invokes two D3D/DDraw
    // device vtable methods (offset +36 BeginScene-style flush, +40 present)
    // through dword_64A320 from PresentSceneAndClearFlags. Both are routed here.
    //   devicePresentBegin(deviceCtx, flag)   == (*(dev+36))(dev, flag)
    //   devicePresentEnd(deviceCtx) -> int     == (*(dev+40))(dev)
    void (*devicePresentBegin)(int deviceCtx, int flag);
    int  (*devicePresentEnd)(int deviceCtx);
};

void InstallRenderRecon2Hooks(const RenderRecon2Hooks& hooks);
const RenderRecon2Hooks& GetRenderRecon2Hooks();
void ResetRenderRecon2State();  // restore globals+hooks to power-on defaults (tests)

// ---------------------------------------------------------------------------
// Recovered module state (single owning definition, this TU). Field names map
// to gilde.exe global addresses noted in the comments.
// ---------------------------------------------------------------------------
struct RenderRecon2State {
    // --- draw-list / counters (ResetEngineState 0x5af200) ---
    int viewParamA      = 0;  // dword_649DA0 (cleared each reset)
    int drawListCount   = 0;  // dword_13FC584 (returned by ResetEngineState)
    int field13FC54C    = 0;  // dword_13FC54C
    int field13FC574    = 0;  // dword_13FC574
    int field13FC4E0    = 0;  // dword_13FC4E0
    int field13FC570    = 0;  // dword_13FC570 (<- 13FC584)
    int drawListHead    = 0;  // dword_13FC770 (radix-sort list head)

    // --- engine enable / scene flags ---
    u8  engineEnabled   = 0;  // byte_649D70 (3D engine active)
    u8  sceneInit       = 0;  // byte_649D71 (scene/world initialized)
    u8  enableAllowed   = 0;  // byte_649D7C (engine may be enabled)
    int sky             = 0;  // dword_64A7C8 (sky object, 0 if none)

    // --- fog / shadow / mip flags (ApplyFogAndLightFlags 0x5b04a8) ---
    u8  fogFlags        = 0;  // byte_14080ED (low 3 bits = fog mode)
    u8  shadowQuality   = 0;  // byte_64A350

    // --- object dirty-flag table (PresentSceneAndClearFlags 0x5b499c) ---
    u32 objectCount     = 0;  // dword_1406A80 (#records)
    u8* objectTable     = nullptr; // dword_1406A84 (record base; stride 128, flag @+104)
    int floor           = 0;  // dword_64A028 (floor/minimap object, 0 if none)
    int deviceCtx       = 0;  // dword_64A320 (D3D device context handle)
    int sceneRootPtr    = 0;  // off_649D64 (scene-graph root, passed to traverse)

    // --- surface clip context (WithSurfaceContext2 0x5d9014) ---
    int clipMinY        = 0;  // dword_64A1B8 (clip top)
    int clipMaxY        = 0;  // dword_64A1C0 (clip bottom, exclusive-ish)
    int surfaceStride   = 0;  // dword_64A1C8 (pixels/row; swapped during call)
};

RenderRecon2State& Recon2State();

// ---------------------------------------------------------------------------
// Reconstructed functions.
// ---------------------------------------------------------------------------

// gilde.exe 0x5af200 — VIBE_Render_ResetEngineState
// Flush the accumulated draw list (HW textured path if engineEnabled, else the
// software raster path), then reset the per-frame counters. Returns the
// pre-reset draw-list count (dword_13FC584).
int ResetEngineState();

// gilde.exe 0x5af8e4 — VIBE_Render_SetEngineEnabled (__usercall, a1@<eax>, ret al)
// Transition the 3D engine on/off. Enabling (only when enableAllowed) resets
// shadow casters / texture cache / sky; a state change re-uploads textures and
// refreshes lighting. Returns the resulting engineEnabled byte.
u8 SetEngineEnabled(int enable);

// gilde.exe 0x5b04a8 — VIBE_Render_ApplyFogAndLightFlags (__userpurge, ret al)
//   dl=mipLevel, cl=fogMode, bl=shadowQuality, al=gammaLevel, a5=upload cb.
// Apply fog mode / shadow quality / gamma / mip filter; rebuild shadow caster
// transforms and re-upload textures when the relevant state changed.
u8 ApplyFogAndLightFlags(u8 mipLevel, char fogMode, char shadowQuality,
                         u8 gammaLevel, int (*uploadCb)(void));

// gilde.exe 0x5b499c — VIBE_Render_PresentSceneAndClearFlags (__usercall, al,sil)
//   a1=presentFlag, a2=drawMinimap. Clears the per-object "dirty" bit (+104 &
//   ~0x80) across the object table, presents the device, marks all mesh frames
//   dirty, optionally renders the floor minimap, and returns the device present
//   result code.
int PresentSceneAndClearFlags(char presentFlag, char drawMinimap);

// gilde.exe 0x5d9014 — VIBE_Render_WithSurfaceContext2 (__userpurge)
//   eax=x, edx=y, ecx=width, ebx=remapTable, a5=surfaceRec.
// Clip a horizontal run [y, y+width) against [clipMinY, clipMaxY), temporarily
// adopt the surface's stride (rec+16), and palette-remap `width` pixels at
// (rec+28) + 2*(stride*y + x). Restores the previous stride afterward.
//
// The original surface record is a raw byte block read at offsets +16 (i32
// stride) and +28 (u16* pixel base). Per types.h, the reconstruction carries
// the record by native pointer; the documented offsets are preserved here as a
// portable record view so the function is golden-testable on any host.
struct Recon2SurfaceRec {
    char _pad0[16];   // +0x00
    i32  stride;      // +0x10  (*(DWORD*)(rec+16))
    char _pad1[8];    // +0x14
    u16* pixels;      // +0x1C  (*(DWORD*)(rec+28))
};
void WithSurfaceContext2(int x, int y, int width, int remapTable,
                         const Recon2SurfaceRec* surfaceRec);

} // namespace guild::render
