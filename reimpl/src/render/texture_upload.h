#pragma once
#include "guild/common/types.h"
#include "render/texture.h"

#include <vector>

// =============================================================================
// guild::render — the texture-RECORD upload / bind / instance-propagate layer.
// Faithful 1:1 reconstruction of the gilde.exe ts_texture.c GPU-side record
// machinery (the half that drives the DirectDraw / Direct3D surfaces from the
// 128-byte texture records in the global record bank dword_1406A84 / count
// dword_1406A80):
//
//   0x5db564  VIBE_Texture_BindActive          (set the live texel/palette/shift
//                                               binding from a record)
//   0x5db5f0  VIBE_Texture_ResetBinding        (clear the live binding to "white")
//   0x5db1bc  VIBE_Texture_PropagateToInstances(copy a master record's surfaces/
//                                               texels/widths to its clone records)
//   0x5db234  VIBE_Texture_UploadToSurface     (release stale GPU surfaces and
//                                               (re)load a record's texture from
//                                               the VFS into a fresh GPU surface;
//                                               then recurse into its mip clones)
//   0x5db4b8  VIBE_Texture_UploadAllRecords    (upload every active, non-clone
//                                               record; call a per-record callback)
//   0x5dba74  VIBE_Texture_RestoreIfLost       (re-create a lost dynamic surface)
//
// THE GLOBAL RECORD BANK (reconcile with texture.h / texture_asset.h)
// ---------------------------------------------------------------------------
// The original addresses records as `dword_1406A84 + 128*i` for i in
// [0, dword_1406A80). Each record is the 128-byte ts_texture.c record modelled
// by `Texture` (texture.h). This module models that bank as `TextureBank`
// (records + count + the live binding-state globals) and the per-record GPU
// surface pointers (+96/+100, omitted from `Texture` because they are the
// present layer) as a parallel `TexGpu` slot routed through `ITextureSurface`.
//
// THE GPU BOUNDARY (vtable slot documented)
// ---------------------------------------------------------------------------
// UploadToSurface releases a stale surface via the COM-style call
// `(*(void(__stdcall**)(void*))(*(_DWORD*)surface + 8))(surface)` — i.e. it
// invokes vtable slot 2 (byte offset +8) of the surface object: IUnknown::Release
// of the DirectDrawSurface. The (re)load goes through
// VIBE_Render_LoadAndStretchTexture (0x5dea50), which decodes a BMP and uploads
// it to a new surface. We abstract BOTH behind `ITextureSurface`: `release()`
// (== vtable +8) and `loadAndUpload()` (== VIBE_Render_LoadAndStretchTexture).
// Game logic stays vendor-neutral; a test supplies a counting mock.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// The GPU surface boundary. The original stored two DirectDrawSurface pointers
// per record at +96 (system/source) and +100 (video/dest) and released them via
// vtable slot 2 (byte +8). LoadAndStretchTexture decoded the BMP at `path` and
// (re)created those surfaces. We model the side effects through this interface.
// ---------------------------------------------------------------------------
class ITextureSurface {
public:
    virtual ~ITextureSurface() = default;

    // (*(vtable+8))(surface) — IDirectDrawSurface::Release. `id` is the opaque
    // surface handle previously returned by loadAndUpload (0 == none). Returns
    // the COM HRESULT-equivalent the original stashed in dword_7626D0 (0 == ok).
    virtual i32 release(u32 id) = 0;

    // gilde.exe 0x5dea50 — VIBE_Render_LoadAndStretchTexture. Decode the BMP at
    // `path` and upload a `width`x`width` texture, writing the new source/dest
    // surface handles to *srcOut/*dstOut. `stretch` selects the stretched (flags
    // bit2) vs. plain upload variant; `noColorKey`/`mipShift` mirror the v9/byte
    // arguments. Returns the achieved mip-shift byte the original stored at +124.
    virtual u8 loadAndUpload(const char* path, int width, bool stretch,
                             u8 noColorKey, u8 mipShift,
                             u32* srcOut, u32* dstOut) = 0;
};

// ---------------------------------------------------------------------------
// Per-record GPU surface slot — the +96/+100 surface handles the present layer
// owns (kept out of `Texture` because they are vendor pointers). One per bank
// record, parallel-indexed.
// ---------------------------------------------------------------------------
struct TexGpu {
    u32 srcSurface = 0;  // +96  IDirectDrawSurface (system/source) handle
    u32 dstSurface = 0;  // +100 IDirectDrawSurface (video/dest) handle
};

// ---------------------------------------------------------------------------
// Live binding state — the globals the rasterizer reads (see raster_textured.h).
// BindActive writes these; ResetBinding clears them to the "white" 1x1 default.
//   dword_1406A78  bound palette LUT pointer        (palBase)
//   unk_1406A8C    bound texel byte-array base       (texBase)
//   dword_1406A88  bound texel slot index (+76)      (group/slot)
//   dword_1406A7C  bound mip width (+116)            (texel addressing width)
//   byte_1407A91   bound width-log2 shift            (widthShift)
//   dword_64A1F8   currently-bound record index (-1 == none)
// ---------------------------------------------------------------------------
struct TextureBinding {
    const void* palBase = nullptr; // dword_1406A78
    const u8*   texBase = nullptr; // unk_1406A8C
    i32         slot    = 0;       // dword_1406A88
    i32         mipWidth = 1;      // dword_1406A7C
    u8          widthShift = 1;    // byte_1407A91
    i32         boundIndex = -1;   // dword_64A1F8 (-1 == none bound)
};

// ---------------------------------------------------------------------------
// TextureBank — the global record bank (dword_1406A84 base, dword_1406A80 count)
// plus its parallel GPU-surface slots and live binding state. Reuses `Texture`
// (texture.h) verbatim for the records.
// ---------------------------------------------------------------------------
struct TextureBank {
    std::vector<Texture> records;  // dword_1406A84 base, 128-byte stride
    std::vector<TexGpu>  gpu;      // +96/+100 surface handles (parallel)
    TextureBinding       binding;  // dword_1406A78/.../64A1F8 live binding
    u8 mipShiftGlobal = 0;         // byte_64A350  global mip shift
    u8 noTransparency = 0;         // dword_64A1FC no-colour-key default
    u8 swEnabled = 1;              // byte_649D70  software-raster path gate

    explicit TextureBank(int capacity)
        : records((size_t)capacity), gpu((size_t)capacity) {}

    bool valid(int idx) const {
        return idx >= 0 && (size_t)idx < records.size();
    }

    // gilde.exe 0x5db564 — VIBE_Texture_BindActive. If `idx` differs from the
    // currently-bound record and is non-negative: resolve clones (flags bit1 ->
    // follow +76 to the master), then publish that record's palette/texels/slot/
    // mipWidth/widthShift into the live binding. A record with slot==0 (+76)
    // binds the 1x1 "white" default instead.
    void BindActive(int idx);

    // gilde.exe 0x5db5f0 — VIBE_Texture_ResetBinding. Clear the live binding to
    // the 1x1 white default and mark nothing bound (dword_64A1F8 = 0/-1).
    void ResetBinding();

    // gilde.exe 0x5db1bc — VIBE_Texture_PropagateToInstances. For a master record
    // (not a clone, name[0] != '*'), copy its GPU surfaces (+96/+100), texel base
    // (+68), and mip/base widths (+116/+120) into every active clone record whose
    // +76 references this record's bank index.
    void PropagateToInstances(int masterIdx);

    // gilde.exe 0x5db234 — VIBE_Texture_UploadToSurface. Resolve clones, release
    // any stale +96/+100 surfaces (via dev.release == vtable+8), and — when the
    // software path is OFF (swEnabled==0) and surfaces are present or texels are
    // absent — (re)load the record's texture from the VFS into fresh surfaces
    // (dev.loadAndUpload == LoadAndStretchTexture), updating +124/+116. Then
    // propagate to clones and recurse into this record's mip-clone group.
    // `path` is the resolved BMP path (the original calls BuildBmpPath); `force`
    // is the a2 flag (force a full reload even with a live surface).
    bool UploadToSurface(int idx, bool force, const char* path,
                         ITextureSurface& dev);

    // gilde.exe 0x5db4b8 — VIBE_Texture_UploadAllRecords. Upload every active,
    // non-clone (flags bit1 clear) record; the original counts them first, then
    // uploads, calling the per-record progress callback `cb` after each. Returns
    // the number of records uploaded. `pathFor` resolves a record index -> BMP
    // path (BuildBmpPath). Clears dword_64A1F8 (the active binding) at the end.
    int UploadAllRecords(bool force, ITextureSurface& dev,
                         const char* (*pathFor)(const TextureBank&, int),
                         void (*cb)(void*) , void* cbCtx);

    // gilde.exe 0x5dba74 — VIBE_Texture_RestoreIfLost. When the software path is
    // off and the record is a stretched (flags bit2) dynamic texture, reload it.
    void RestoreIfLost(int idx, const char* path, ITextureSurface& dev);
};

} // namespace guild::render
