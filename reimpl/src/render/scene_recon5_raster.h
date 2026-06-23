#pragma once
#include "guild/common/types.h"
#include <cstddef>

// =============================================================================
// guild::render — recon5 raster / snow leaves (gilde.exe, d3_engine.c / weather).
//
// Faithful 1:1 reconstruction of two self-contained, vendor-free pieces:
//
//   0x4351d8  VIBE_Render_DrawHLine           (16-bit framebuffer line plotter:
//                                              horizontal / vertical / Bresenham)
//   0x42a3e4  VIBE_Snow_GrowFlakeList          (flake-list grow: realloc + RNG init,
//                                              plus the wind/intensity update cases)
//
// COUPLED (INERT hook, see report):
//   0x4353dc  VIBE_Render_DrawLineLocked       — pure lock-wrapper: BeginFrameLock,
//             DrawHLine, then a surface-unlock dispatch through the DDraw vtable
//             (*(dword_62D57C)+128)(dword_62D57C, 0). The lock/unlock is a GPU
//             surface op (rule 3). Modelled here as DrawLineLockedHooks so the
//             real plotting math (DrawHLine) stays 1:1 and testable.
// =============================================================================
namespace guild::render {

using f32 = float;

// ---------------------------------------------------------------------------
// Framebuffer16 — the active 16-bit lock target the line plotter writes into.
// In the binary: dword_7626F0 = base byte pointer of the locked surface,
// dword_7626F8 = stride in PIXELS (the "pitch>>1" the lock path stores). The
// plotter indexes `*(WORD*)(base + 2*(x + stride*y)) = color`.
// ---------------------------------------------------------------------------
struct Framebuffer16 {
    u16* base = nullptr;   // dword_7626F0 (as a u16*; original holds the byte ptr)
    i32  stride = 0;       // dword_7626F8 (pixels per row)
};

// ---------------------------------------------------------------------------
// gilde.exe 0x4351d8 — VIBE_Render_DrawHLine (__userpurge).
// Plots a line from (x0,y0)=(a1,a2) to (x1,y1)=(a4,a3) in color `color`(a5) into
// the 16-bit framebuffer `fb`. NOTE the original's odd register packing:
//   result(eax)=x0, a2(edx)=y0, a3(ecx)=y1, a4(ebx)=x1, a5=color.
// Three branches, exactly as the original:
//   * y0 == y1            : vertical run in framebuffer terms — the code treats
//                           equal a2/a3 as a column sweep (a1..a4 over rows). It
//                           walks `result` from min(x0,x1)..max stepping +1 in the
//                           row index `stride*y0 + x`, writing 1 px per step.
//   * x0 == x1 (a1==a4)   : horizontal run — sweeps the other axis by `stride`.
//   * else                : integer Bresenham, major axis chosen by |dx| vs |dy|.
// The return value is `result` (the original returns the running x accumulator);
// reproduced verbatim.
// ---------------------------------------------------------------------------
i32 DrawHLine(Framebuffer16& fb, i32 x0, i32 y0, i32 y1, i32 x1, u16 color);

// ---------------------------------------------------------------------------
// DrawLineLockedHooks — INERT-DEFAULT injection for VIBE_Render_DrawLineLocked
// @0x4353dc. beginFrameLock returns nonzero on a successful surface lock (the
// original's VIBE_Render_BeginFrameLock); endUnlock performs the DDraw vtable
// surface-unlock that the original does for byte_762721 cases 1/3. Defaults make
// the wrapper a no-op-safe pass-through over DrawHLine for headless tests.
// ---------------------------------------------------------------------------
struct DrawLineLockedHooks {
    bool (*beginFrameLock)(void* surf) = nullptr;  // VIBE_Render_BeginFrameLock; null=>true
    void (*endUnlock)(void* surf) = nullptr;       // (*(dword_62D57C)+128)(...,0)
    void* surf = nullptr;                          // a2 (the surface object)
};

// gilde.exe 0x4353dc — VIBE_Render_DrawLineLocked. Returns the BeginFrameLock
// result (0 if the lock failed), else plots and returns 0 (original folds result
// to 0 on the unlock path and leaves the dirty-flag cleared). `lockDirty` mirrors
// dword_7626F0!=0 gate the original re-checks before unlocking — here passed in.
i32 DrawLineLocked(Framebuffer16& fb, i32 x0, i32 y0, i32 y1, i32 x1, u16 color,
                   const DrawLineLockedHooks& hooks, u8 unlockMode, bool lockDirty);

// =============================================================================
// Snow flake list (weather.c). The flake record is 40 bytes:
//   +0  float posX     +4  float posY     +8  float posZ
//   +12 float velX     +16 float swayA    +20 float swayB     (+24..+39 scratch)
// The owning state block (a1) layout the grow path touches:
//   +0   int   capacityUsed   (current populated count / high-water)
//   +4   int   capacity       (allocated flake count)
//   +8   int   activeCount    (set to capacityUsed on case 0/1)
//   +12  int   pendingKind    (a3 stashed)
//   +56  flake* flakes        (the 40-byte-record array)
//   +60  int   windX0   +64 int windX1            (case2 copies +68/+72 in)
//   +68  int   windSrcA +72  int windSrcB
//   +76  float windScaledX (= a3 * 0.001)   +80 float windScaledY (= a4 * 0.001)
//   +84  float gust        (case3: max(gust, a3*2500.0))
//   +92  int   timeBase     +96 int t0  +100 int t1  +104 int t2  +108 int t3
// =============================================================================
struct SnowFlake {
    f32 posX = 0, posY = 0, posZ = 0;   // +0/+4/+8
    f32 velX = 0, swayA = 0, swayB = 0; // +12/+16/+20
    f32 scratch[4] = {0,0,0,0};         // +24..+39 (record is 40 bytes)
};

struct SnowState {
    i32 capacityUsed = 0;  // +0
    i32 capacity = 0;      // +4
    i32 activeCount = 0;   // +8
    i32 pendingKind = 0;   // +12
    // +16..+55 unmodelled scratch
    SnowFlake* flakes = nullptr; // +56
    i32 windSrcA = 0;      // +68 (copied into +60 on case2)
    i32 windSrcB = 0;      // +72 (copied into +64 on case2)
    i32 windX0 = 0;        // +60
    i32 windX1 = 0;        // +64
    f32 windScaledX = 0;   // +76
    f32 windScaledY = 0;   // +80
    f32 gust = 0;          // +84
    i32 timeBase = 0;      // +92
    i32 t0 = 0, t1 = 0, t2 = 0, t3 = 0; // +96/+100/+104/+108
};

// RNG hook for VIBE_Util_RandNext @0x5cb8bc — returns a raw integer; the original
// scales it by 1/32768 into [0,1). Default uses an internal xorshift so golden
// vectors are deterministic when a fixed sequence is injected.
struct SnowRng {
    i32 (*next)(void* ctx) = nullptr;  // VIBE_Util_RandNext
    void* ctx = nullptr;
};

// gilde.exe 0x42a3e4 — VIBE_Snow_GrowFlakeList(state, count, kind, arg4).
// Iterates `count` times; each iteration dispatches on `kind` (a3):
//   kind 0/1: ensure capacity for `kind` flakes; if kind >= capacity, realloc to
//             kind+100 records, memcpy the old ones, RNG-init the new tail, then
//             (kind!=0) set activeCount=capacityUsed, timing window; (kind==0)
//             capacityUsed=0.
//   kind 2  : windScaledX = kind*0.001 ... (see SnowState) — wind apply.
//   kind 3  : gust = max(gust, kind*2500.0).
// Faithful realloc + per-flake RNG init is the in-scope MATH; the allocator and
// RandNext are injected. `alloc`/`free` mirror VIBE_Memory_Alloc/FreeDebug.
// ---------------------------------------------------------------------------
struct SnowAllocHooks {
    void* (*alloc)(std::size_t bytes, void* ctx) = nullptr;  // VIBE_Memory_AllocDebug
    void  (*free)(void* p, void* ctx) = nullptr;             // VIBE_Memory_FreeDebug
    void* ctx = nullptr;
};

void SnowGrowFlakeList(SnowState& st, i32 count, i32 kind, i32 arg4,
                       const SnowRng& rng, const SnowAllocHooks& mem);

} // namespace guild::render
