#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — DDraw-surface management layer of the gilde.exe texture record
// (the "ts_texture" 128-byte record), reconstructed 1:1. This is the SURFACE
// side of the record (the part the high-level src/render/texture.{h,cpp} model
// deliberately omits): the DirectDraw front/palette surface pointers (+96/+100),
// the device/format handle (+116), the flag byte (+104) and the wrap mask (+76).
//
// Reconstructed from the Hex-Rays decompile of:
//   0x5d9660  VIBE_Texture_RestoreSurface     (re-create surface; mip vs plain)
//   0x5d96c0  VIBE_Texture_LoadFromCache      (reuse a freed surface from the cache)
//   0x5d9970  VIBE_Texture_ReleaseSurfaces    (release both surfaces + texel mem)
//   0x5db624  VIBE_Texture_ReleaseSurface     (release surfaces, then re-upload)
//   0x5dbc38  VIBE_Texture_CapturePaletteSurface (clone + read back DDraw palette)
//   0x5d995c  VIBE_Texture_SetBasePath        (normalise the texture base dir)
//
// PLATFORM BOUNDARY (Rule 3): the DirectDraw surface objects are COM-like vtable
// objects. The original calls release as `(*(vtbl+8))(surface)` and palette
// readback as `(*(vtbl+80))(...)`. Those are GPU/DDraw — we do NOT reimplement
// DirectDraw. They are routed through the inert hooks declared below (defaults
// to a no-op that returns 0 and frees nothing), so the pure record/cache logic
// (which surface gets reused, which flag selects mip vs plain, the cache match
// predicate, the palette unpack 0xRRGGBB -> 3 bytes) is reconstructed exactly.
//   The Render_* entry points (CreateDynamicTexture / CreateSurfacePalette /
// ReportDDrawError) are likewise hooks. The texture CACHE of freed surfaces
// (dword_1406A50 entries of 20 bytes at dword_64A204) IS pure data and is
// reconstructed faithfully.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// A DDraw-like surface object as the record sees it: just enough to model the
// `(*(vtbl+8))(this)` release call. In the reimpl the "vtable" is our hook.
// ---------------------------------------------------------------------------
struct DDrawSurface {
    int id = 0;     // opaque surface id (nonzero == live)
};

// ---------------------------------------------------------------------------
// TexSurfRecord — the surface-relevant slice of the 128-byte ts_texture record,
// modelled by ORIGINAL byte offset so the translated field accesses read 1:1.
// (Only the fields these six functions touch are present.)
// ---------------------------------------------------------------------------
struct TexSurfRecord {
    u8            tag = 0;              // +0    record tag (42 '*' == special/clone)
    DDrawSurface* frontSurface = nullptr;  // +96   primary DDraw surface (texBase host)
    DDrawSurface* paletteSurface = nullptr;// +100  attached palette/2nd surface
    u8            flags = 0;           // +104  bit1 no-release, bit2 mip, bit3 dynamic
    int           sourceId = 0;        // +108  source/clone parent id
    void*         texelMem = nullptr;  // +68 (v4[17] == +0x44) freed by FreeDebug
    int           deviceHandle = 0;    // +116  DDraw device/format handle
    u32           wrapMask = 0;        // +76   (w-1)|(w*w-1) span wrap mask
    int           width = 0;           // +116-as-size in CapturePaletteSurface (v8)
};

// ---------------------------------------------------------------------------
// Free-surface cache entry — the 20-byte record the engine keeps at
// dword_64A204, dword_1406A50 of them, populated when a surface is evicted.
//   [+0]  freed front surface ptr   (-> record +96)
//   [+4]  freed palette surface ptr (-> record +100)
//   [+8]  device handle key (0 == empty slot)
//   [+16] mip flag key  ((32*flags)>>7)
//   [+17] dynamic flag key ((16*flags)>>7)
// ---------------------------------------------------------------------------
struct FreeSurfaceEntry {
    DDrawSurface* front = nullptr;   // +0
    DDrawSurface* palette = nullptr; // +4
    int           deviceKey = 0;     // +8  (0 == empty)
    u8            mipKey = 0;        // +16
    u8            dynKey = 0;        // +17
};

// ---------------------------------------------------------------------------
// Globals the cluster reads/writes (each carries its original address).
// ---------------------------------------------------------------------------
struct TexSurfGlobals {
    // dword_64A1FC — global "default mip" flag fed to CreateDynamicTexture
    u8  defaultMipFlag = 0;
    // dword_1406A5C — count of populated free-surface cache entries (LRU pool)
    u32 freeCacheCount = 0;
    // dword_1406A50 — capacity of the free-surface cache (entry count)
    u32 freeCacheCap = 0;
    // dword_64A204 — base of the free-surface cache array
    FreeSurfaceEntry* freeCache = nullptr;
    // byte_649D70 — master "surface cache enabled" flag
    u8  surfaceCacheEnabled = 0;
    // byte_14080E5 — "ignore mip-key mismatch" flag in the cache match predicate
    u8  ignoreMipKey = 0;
    // byte_14080A4 — DDraw caps: bit5 set == palette-readback supported
    u8  ddrawCaps = 0;
};

// ---------------------------------------------------------------------------
// Inert platform hooks (Rule 3 boundary). Replace via SetTexSurfHooks to drive a
// real backend; defaults are pure no-ops that preserve the record/cache logic.
// ---------------------------------------------------------------------------
struct TexSurfHooks {
    // VIBE_Render_CreateDynamicTexture (0x5dfed0). Creates the front+palette
    // surfaces for `record` (device, fmt, &paletteOut, &frontOut, w?, h?, mip).
    // Returns the original's char result (0 default).
    char (*createDynamicTexture)(TexSurfRecord& rec, u8 fmt, bool mip,
                                 u32 a2, char a3) = nullptr;
    // VIBE_Render_CreateSurfacePalette (0x5dfbd8): attach a 256-entry palette.
    char (*createSurfacePalette)(TexSurfRecord& rec, const u8* pal, u32 count) = nullptr;
    // VIBE_SurfaceCache_EvictAndStore (0x5d9580): stash a '*'-tagged record's
    // surfaces into the global cache. Returns its (unused) result.
    int  (*evictAndStore)(TexSurfRecord& rec) = nullptr;
    // VIBE_Memory_FreeDebug (0x43923c): free the texel buffer (+68).
    int  (*freeDebug)(void* p) = nullptr;
    // (*(vtbl+8))(surface): DDraw Release. Default: no-op returning 0.
    int  (*releaseSurface)(DDrawSurface* s) = nullptr;
    // VIBE_Texture_UploadToSurface (0x5db234): re-upload after a release.
    void (*uploadToSurface)(TexSurfRecord& rec) = nullptr;
    // VIBE_Render_ReportDDrawError (0x42e4ec).
    void (*reportError)() = nullptr;
};

void SetTexSurfHooks(const TexSurfHooks& h);
const TexSurfHooks& GetTexSurfHooks();
TexSurfGlobals& TexSurf();   // mutable access to the global state

// ---------------------------------------------------------------------------
// 0x5d9660 VIBE_Texture_RestoreSurface@<al>(rec@eax, a2@edx, a3@bl).
// Recreates the DDraw surface. mip flag = (flags&8)?0:defaultMipFlag. If
// (flags&4) -> dynamic format byte_14080C4 with (0,0); else plain byte_14080A0
// with (a2,a3). Returns CreateDynamicTexture's char result.
// ---------------------------------------------------------------------------
char Texture_RestoreSurface(TexSurfRecord& rec, u32 a2, char a3);

// ---------------------------------------------------------------------------
// 0x5d96c0 VIBE_Texture_LoadFromCache@<al>(rec@eax, a2@edx, a3@ebx).
// If the free-surface cache is enabled/non-empty, scan it for an entry whose
// deviceKey == rec.deviceHandle and whose mip/dyn keys match rec.flags; if found
// reuse that surface (move into rec, mark slot empty, dec count, attach palette
// via CreateSurfacePalette). Otherwise fall back to Texture_RestoreSurface.
// ---------------------------------------------------------------------------
char Texture_LoadFromCache(TexSurfRecord& rec, u32 a2, const u8* a3);

// ---------------------------------------------------------------------------
// 0x5d9970 VIBE_Texture_ReleaseSurfaces(rec@eax).
// If surfaceCacheEnabled && tag==42 && both surfaces live -> EvictAndStore and
// null them. Else release paletteSurface(+100) then frontSurface(+96) via the
// DDraw release hook, nulling each. Finally free the texel buffer (+68) via
// FreeDebug. (v4[17] is the +68/0x44 texel pointer.)
// ---------------------------------------------------------------------------
void Texture_ReleaseSurfaces(TexSurfRecord& rec);

// ---------------------------------------------------------------------------
// 0x5db624 VIBE_Texture_ReleaseSurface@<al>(rec@eax).
// Guard: if !rec || !surfaceCacheEnabled || (flags&2) -> return 0. Release front
// (+96) then palette (+100) via the DDraw hook, nulling each. If wrapMask(+76)
// && tag!=42 -> re-upload via UploadToSurface. Returns 0.
// ---------------------------------------------------------------------------
char Texture_ReleaseSurface(TexSurfRecord& rec);

// ---------------------------------------------------------------------------
// 0x5dbc38 VIBE_Texture_CapturePaletteSurface(rec@eax, a2,a3,a4,a5, a6).
// Clones the record (CloneRecord hook), clears its surface ptrs, recomputes the
// wrap mask = (w-1)|(w*w-1), and — if not tag 42 and the DDraw palette-readback
// cap is present and not a dynamic surface — reads back the 256-entry source
// palette via (*(vtbl+80)) and unpacks each 0x00RRGGBB dword into 3 bytes
// (R at +0, G = (x&0xFF00)>>8 at +1, B = (x&0xFF0000)>>16 at +2), then
// LoadFromCache(clone, 256, paletteBytes). Returns the clone record.
//   The DDraw lock/readback path is the boundary; the dword->3-byte palette
// UNPACK is reconstructed exactly. `palOut` (>=768 bytes) receives the unpack.
// ---------------------------------------------------------------------------
TexSurfRecord* Texture_CapturePaletteSurface(TexSurfRecord* rec,
                                             const u32* srcPaletteDwords /*256, may be null*/,
                                             u8* palOut /*>=768, may be null*/);

// Helper exposing the exact 0x00RRGGBB -> [R,G,B] unpack the original performs
// (loop over 256 dwords). out must hold >= 768 bytes.
void UnpackDdrawPalette(const u32* dwords256, u8* out768);

// ---------------------------------------------------------------------------
// 0x5d995c VIBE_Texture_SetBasePath@<eax>(a1@eax, a2@ecx).
//   result = VIBE_Vfs_NormalizeDirPath(a1, dword_62EB78, a2);
//   dword_1406A54 = result;  return result;
// VIBE_Vfs_NormalizeDirPath (0x44f88c) is file-I/O (boundary) -> hook. The
// global it writes (dword_62EB78 = base-dir buffer) and the basePathSet flag
// (dword_1406A54 = result) are modelled. Returns the hook's result (0 default).
// ---------------------------------------------------------------------------
struct TexBasePathState {
    char baseDir[260] = {0};   // dword_62EB78 normalised base-dir buffer
    int  basePathSet  = 0;     // dword_1406A54
};
TexBasePathState& TexBasePath();
// VIBE_Vfs_NormalizeDirPath(src, destBuf, a2) hook. Default: copies src into
// destBuf (truncating to 259 chars) and returns nonzero.
extern int (*g_vfsNormalizeDirPath)(const char* src, char* destBuf, int a2);
int Texture_SetBasePath(const char* a1, int a2);

} // namespace guild::render
