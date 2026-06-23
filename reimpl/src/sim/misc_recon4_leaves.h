#pragma once
// misc_recon4_leaves.{h,cpp} — assorted engine leaves (gilde.exe)
//
//   0x41e57c VIBE_State_Finalize            — UI-state activate/lookup
//   0x429290 VIBE_Rain_Destroy              — particle/rain double-buffer free
//   0x54f884 VIBE_DragSlot_ResetGridTable   — thieves-guild equipment grid clear
//   0x423648 VIBE_Result_Finalize           — clipped surface blit
//   0x42395c VIBE_Result_Handler_Interaction— blit wrapper (alpha-off)
//
// State and DragSlot operate on engine globals; to keep this TU self-contained
// (HARD CONSTRAINT: new files only, no edits to existing files), the global record
// banks are modeled as injectable environments. Render/alloc-coupled callees are
// routed through inert-default hooks (never faked — see project rule 8).

#include "guild/common/types.h"

namespace guild::sim {

using f32 = float;
using f64 = double;

// ===========================================================================
// 0x41e57c — VIBE_State_Finalize
// ===========================================================================
// Activates a UI "state"/font record by index, but only if the record's +60 field
// equals 7 (a type tag). On success it resolves the record via VIBE_State_Update
// (0x40e9e8) and publishes globals:
//   dword_62D244 = resolved record ptr (g_curState.recordPtr / .recordId)
//   dword_62D248 = the secondary value returned in edx (g_curState.recordEdx)
//   dword_69FFB0 = *(u16*)(rec+46)   (g_curState.lineHeight)
//   dword_62D270/62D274 = spacing pair (8,2) or (4,2) depending on the edx value
// Returns the resolved record (0 if the tag check failed).
//
// The 84-byte record bank (dword_62D204) and the resolver are injected via env.
struct StateEnv {
    // Record bank: VIBE_State_Finalize reads *(u32*)(84*idx + bank + 60).
    // We expose just the field it needs (the type tag at +60).
    int  (*recordTag60)(void* ctx, int idx) = nullptr;     // *(u32*)(84*idx+bank+60)
    // VIBE_State_Update(idx) -> (eax=recordPtr, edx=recordEdx). Returns recordPtr;
    // writes recordEdx through the out param.
    i32  (*stateUpdate)(void* ctx, int idx, i32* outEdx) = nullptr;
    // *(u16*)(recordPtr + 46)
    u16  (*recordLineHeight)(void* ctx, i32 recordPtr) = nullptr;
    void* ctx = nullptr;
    // dword_62D2B0 — the "base state index" the edx value is compared against.
    i32  baseStateIndex = 0;
};

// Outputs that the original publishes to globals; the caller owns the actual globals.
struct StateOutputs {
    i32 recordPtr = 0;   // dword_62D244
    i32 recordEdx = 0;   // dword_62D248
    u16 lineHeight = 0;  // dword_69FFB0
    i32 spacingA = 0;    // dword_62D270
    i32 spacingB = 0;    // dword_62D274
};

// Returns recordPtr (== dword_62D244) on success, 0 on tag mismatch.
// `out` (if non-null) receives the published values.
i32 StateFinalize(const StateEnv& env, int idx, StateOutputs* out);

// ===========================================================================
// 0x429290 — VIBE_Rain_Destroy
// ===========================================================================
// Frees a rain/particle object: if obj!=0, frees its +16 sub-buffer (and clears the
// slot), then frees the object itself. Uses the debug allocator VIBE_Memory_FreeDebug
// (0x43923c). Modeled with a free hook + a tiny record view.
struct RainObject {
    u8* base = nullptr;     // the object allocation (== `result`)
    u8* sub  = nullptr;     // *(u32*)(base+16)
};
struct RainFreeHooks {
    // VIBE_Memory_FreeDebug — default is a no-op marker (counts calls) so headless
    // tests can observe the free order without a real heap.
    void (*freeDebug)(void* ctx, u8* ptr) = nullptr;
    void* ctx = nullptr;
};
// Returns the original base pointer (== `result`), matching the original's eax.
u8* RainDestroy(RainObject* obj, const RainFreeHooks& hooks);

// ===========================================================================
// 0x54f884 — VIBE_DragSlot_ResetGridTable
// ===========================================================================
// Clears the thieves-guild equipment drag grid: 32 rows. Each row has a 4-dword
// header (dword_1232C30, stride 16 dwords == 64 bytes) and a parallel cell array.
//   header[0]=0, header[1..3]=-1
//   for each of the 7 cells in the row (offsets +8..+48 step 8 within the 64-byte
//   row, i.e. cells 1..7 of an 8-slot row): cellId(dword_1232C38)=-1, flag(word_1232C3C)=0
// Note the original's inner loop starts at result+8 and runs while result!=v0 where
// v0 = 48 + 64*row, i.e. it touches byte-offsets {8,16,24,32,40,48} = 6 iterations.
inline constexpr int kGridRows = 32;

struct DragGridRow {
    i32 header[4];        // dword_1232C30 row: [0]=0, [1..3]=-1
    i32 cellId[8];        // dword_1232C38 parallel, indexed by byteoff/8 (1..6 reset)
    u16 cellFlag[8];      // dword_1232C3C parallel (1..6 reset)
};

// Resets `rows` (must point to at least kGridRows entries) exactly as 0x54f884 does.
void DragSlotResetGridTable(DragGridRow* rows);

// ===========================================================================
// 0x423648 — VIBE_Result_Finalize / 0x42395c — VIBE_Result_Handler_Interaction
// ===========================================================================
// A clipped surface->surface copy. Both source (a5) and dest (a8) carry a 64-byte
// surface descriptor:
//   +12 strideBytes  +20 bitsPerPixel(byte)  +28 pixelsPtr
//   +32 ddSurfaceObj +36 clipMinY +40 clipMinX +44 clipMaxY +48 clipMaxX
//   +52 lockState    +60 decompressed-flag
// The function clamps the (x=a2,y=a1,w=v32,h=v31) dest rect and (a6,a7) source origin
// against both surfaces' clip windows, then either:
//   (A) blits via the DirectDraw/Direct3D surface object (vtable[5], the GPU path —
//       this is the rule-3 Vulkan/IGraphicsDevice boundary), or
//   (B) falls back to a row-by-row qmemcpy (the software path).
// The GENUINE, portable part is the clip arithmetic + the software-copy fallback;
// those are reconstructed 1:1. The GPU blit and the LZ decompress-state machinery
// (VIBE_DecompressState_Blob 0x423500, VIBE_Decompression_Finalize 0x4235dc) are
// routed through inert hooks.
struct Surface {
    int strideBytes = 0;   // +12
    int bppByte     = 0;   // +20 (bits per pixel; >>3 == bytes/pixel)
    u8* pixels      = nullptr; // +28
    void* ddObj     = nullptr; // +32
    int clipMinY    = 0;   // +36  (compared against a1 / a6, the Y/"row" axis)
    int clipMinX    = 0;   // +40  (compared against a2 / a7, the X/"col" axis)
    int clipMaxY    = 0;   // +44
    int clipMaxX    = 0;   // +48
    int lockState   = 0;   // +52
    int decompressed = 0;  // +60
};

struct BlitHooks {
    // Returns nonzero on GPU-blit success path taken (the original returns 0 then).
    // If gpuBlit==nullptr OR returns false, the software qmemcpy fallback runs.
    bool (*gpuBlit)(void* ctx, Surface* dst, const int dstRect[4],
                    Surface* src, const int srcRect[4], bool alpha) = nullptr;
    // dword_7626C8 == 0x80004001 (E_NOTIMPL) triggers the software fallback after a
    // GPU attempt; modeled as this flag.
    bool gpuNotImplemented = false;
    void (*decompressBlob)(void* ctx, Surface* s, int rows) = nullptr;  // 0x423500
    void (*decompressFinalize)(void* ctx, Surface* s) = nullptr;        // 0x4235dc
    void* ctx = nullptr;
};

// 0x423648. (dstX=a2, dstY=a1, dstW=v32=a3, dstH=v31=a4) is the dest rect; (srcX=a7,
// srcY=a6) the source origin. `alpha` is a9&1. Returns 1 on success, 0 if clipped to
// empty or the GPU path reported success-without-fallback.
int ResultFinalize(Surface* src, Surface* dst,
                   int dstY, int dstX, int dstW, int dstH,
                   int srcY, int srcX, bool alpha,
                   const BlitHooks& hooks);

// 0x42395c — exact wrapper: ResultFinalize(..., alpha=false).
int ResultHandlerInteraction(Surface* src, Surface* dst,
                             int dstY, int dstX, int dstW, int dstH,
                             int srcY, int srcX, const BlitHooks& hooks);

} // namespace guild::sim
