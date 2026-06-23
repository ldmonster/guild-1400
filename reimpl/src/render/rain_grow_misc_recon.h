#pragma once
// =============================================================================
// rain_grow_misc_recon — VIBE_Rain_GrowDropList (gilde.exe 0x4292b8).
//
//   void __cdecl VIBE_Rain_GrowDropList(float *sys, signed int opCount, ...);
//
// A variadic command processor for the rain drop list, distinct from the
// per-frame integrator in render/rain.cpp (0x429098 Create / 0x4294d4 Update).
// It walks `opCount` commands off the variadic stack; each command's first int
// is an opcode:
//   op == 0  : set head       ( sys[0] = count, raw int )
//   op == 1  : (handled implicitly — falls through with reseed/activate path)
//   op == 2  : burst spawn     (consumes a second int = `count2`)  -> seeds the
//              billboard anchor / velocity fields from two args.
//   op >= 1, op < capacity-grow path: grow the backing array by +100 records,
//              copy the existing records, free the old block, and RNG-seed the
//              newly grown slots.
//
// This is a faithful, isolated math/allocation kernel.  The original's stack
// walk is modelled here as a sequential int-argument stream (`args`,`argCount`)
// — the exact bytes the cdecl caller pushes.  Allocation is routed through the
// supplied alloc/free hooks (the original calls VIBE_Memory_AllocDebug /
// VIBE_Memory_FreeDebug with tag "d3t:RainDropList"); the RNG is the engine's
// VIBE_Util_RandNext (0x5cb8bc) supplied as a hook.
// =============================================================================
#include "guild/common/types.h"

namespace guild::render {

// One drop record = 40 bytes (0x28). Fields named by their byte offset use.
struct RainGrowDrop {
    float f00;  // +0x00
    float f04;  // +0x04
    float f08;  // +0x08
    float f0c;  // +0x0c
    float f10;  // +0x10
    float f14;  // +0x14
    u32   pad18; // +0x18
    u32   pad1c; // +0x1c
    u32   pad20; // +0x20
    u32   pad24; // +0x24
};
static_assert(sizeof(RainGrowDrop) == 40, "drop stride must be 0x28");

// Mirror of the leading fields of the rain system record touched by 0x4292b8.
struct RainGrowSys {
    // +0x00 sys[0]. Hex-Rays types a1 as float* so the decompile prints
    // `*a1 = 0.0`, but the disassembly only ever touches [ebx] with plain
    // integer moves: op==0 does `mov [ebx],eax` (eax = count), the grow path
    // reads it with `mov edi,[ebx]` (oldCount), and op==1 copies it raw with
    // `mov eax,[ebx]; mov [ebx+8],eax`. There is no fld/fild/fstp on [ebx]
    // anywhere in 0x4292b8, so head is an i32, not a float — and op==0 stores
    // `count`, never 0.0.
    i32           head;      // +0x00  (sys[0]; *(int*)ebx)
    i32           capacity;  // +0x04  ([ebx+4])
    i32           f08;       // +0x08
    i32           f0c;       // +0x0c
    RainGrowDrop* drops;     // +0x10  ([ebx+0x10])
    i32           f14;       // +0x14
    i32           f18;       // +0x18
    i32           f1c;       // +0x1c
    i32           f20;       // +0x20
    float         f24;       // +0x24
    float         f28;       // +0x28
    i32           f2c;       // +0x2c
    i32           f30;       // +0x30
    i32           f34;       // +0x34
    i32           f38;       // +0x38
    i32           f3c;       // +0x3c
};

using RainAllocFn = void* (*)(unsigned bytes, const char* tag);
using RainFreeFn  = void  (*)(void* block);
using RainRandFn  = int   (*)();

// gilde.exe 0x4292b8 — VIBE_Rain_GrowDropList.
//   `args` is the variadic int stream (opcodes + their operands), `argCount`
//   its length; `opCount` is the `a2`/arg_4 loop bound.
void Rain_GrowDropList(RainGrowSys& sys, i32 opCount, const i32* args, int argCount,
                       RainAllocFn alloc, RainFreeFn freeFn, RainRandFn rnd);

} // namespace guild::render
