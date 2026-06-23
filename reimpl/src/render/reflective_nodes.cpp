// guild::render — REFLECTIVE-NODE SETUP, implementation. 1:1 with the Hex-Rays
// decompile of the reflective-bit writer (VIBE_Texture_LoadByName @0x5DA714) and
// the reflection-plane derivation inside VIBE_Mirror_PrepareReflectionNode
// @0x5F676C. See reflective_nodes.h for the per-address provenance.

#include "render/reflective_nodes.h"

namespace guild::render {

// ----------------------------------------------------------------------------
// gilde.exe 0x5da74f..0x5da76a — the `v85` predicate (the reflective bit5).
//
//   mov al, bl            ; al = a4 = flag2
//   sar eax, 6
//   test al, 1            ; flag2 bit6 ?
//   jz   -> v85 = 0
//   cmp byte ptr [flag0_lo], 0FFh
//   jnb  -> v85 = 0       ; (unsigned) flag0 low byte >= 0xFF  => not reflective
//   mov al, 1            ; v85 = 1
//
// The decompiler renders the second clause as
//   ((u8)flag0 != 0xFF || (flag0 & 0x10000) != 0)
// which, combined with the `jnb` (>=) test on the *byte*, is exactly
// "(u8)flag0 < 0xFF". (flag0's low byte is the only operand of the byte compare;
// the BYTE2/0x10000 bit lives above it and never affects the byte compare.)
// ----------------------------------------------------------------------------
bool ComputeReflectiveBit(u32 flag0, u32 flag2) {
    const bool highShift = ((flag2 >> 6) & 1u) != 0;       // sar al,6 / test al,1
    if (!highShift)
        return false;                                      // jz -> v85 = 0
    const u8 lo = static_cast<u8>(flag0 & 0xFFu);
    if (lo >= 0xFFu)                                        // jnb -> v85 = 0
        return false;
    return true;                                           // mov al, 1
}

// Material-level wrapper: reproduce how mesh_load.cpp step 6 assembles the loader
// args, then apply ComputeReflectiveBit.
//   flag2 = ... | (shiftHi << 6)            (only bit6 matters here)
//   flag0 low byte = present ? paletteByte : 0xFF default
bool IsMaterialReflective(bool present, u8 paletteByte, u8 shiftHi) {
    const u32 flag2 = static_cast<u32>(shiftHi) << 6;
    const u32 flag0 = present ? static_cast<u32>(paletteByte) : 0xFFu;
    return ComputeReflectiveBit(flag0, flag2);
}

// ----------------------------------------------------------------------------
// gilde.exe 0x5f68c8 — plane distance store:
//   *(a2+36) = *(a2+24)*p.x + *(a2+28)*p.y + *(a2+32)*p.z
// i.e. d = n · p, with n the camera-frame-rotated surface normal and p the
// reflective poly's first-vertex position.
// ----------------------------------------------------------------------------
MirrorPlane DeriveReflectionPlane(const float normal[3],
                                  const float pointOnSurface[3]) {
    MirrorPlane m;
    m.nx = normal[0];
    m.ny = normal[1];
    m.nz = normal[2];
    m.d  = normal[0] * pointOnSurface[0]
         + normal[1] * pointOnSurface[1]
         + normal[2] * pointOnSurface[2];   // n · p   (the +36 store)
    return m;
}

// ----------------------------------------------------------------------------
// gilde.exe 0x5f67ad..0x5f67c4 — the reflective-child scan inside
// PrepareReflectionNode:
//   while (1) { result = *v6++; if (result && (result[104] & 0x20)) break;
//               if (++v4 >= count) return; }
// Here `texRecs[i]` are the child pointers; record +104 is the flag byte.
// ----------------------------------------------------------------------------
int FindReflectiveTexture(void* const* texRecs, int count) {
    if (!texRecs)
        return -1;
    for (int i = 0; i < count; ++i) {
        void* rec = texRecs[i];                            // result = *v6++
        if (rec && (static_cast<const u8*>(rec)[104] & kReflectiveTexFlag) != 0)
            return i;                                       // break
    }
    return -1;                                              // ++v4 >= count
}

} // namespace guild::render
