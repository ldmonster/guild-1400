#include "render/terrain.h"

namespace guild::render {

// gilde.exe 0x5bbbf4 — VIBE_Floor_TileIsUniform
//   (__usercall (grid@eax, x0@edx, span@ecx, y0@ebx) -> al)  VERIFIED-1:1
//   v6 = -1 (no reference yet);  xEnd = x0+span;  yEnd = y0+span;
//   if (y0 > yEnd) return 1;                                   // cmp ebx,eax; jg
//   for (y = y0; y <= yEnd; ++y)
//     for (x = x0; x <= xEnd; ++x) {
//        idx = (mask & y)*size + (x & mask);
//        if (v6 == -1) v6 = types[idx];          // seed reference  (cmp dl,0FFh; jnz)
//        else if (v6 != types[idx]) return 0;    // mismatch        (cmp dl,[ecx+ebx]; jz)
//     }
//   return 1;
//
// CRUCIAL: v6 lives in dl, a single signed byte (char). The "no reference yet"
// sentinel 0xFF (== -1 as char) therefore COLLIDES with a real terrain-type byte
// of 0xFF: at 0x5bbc04 `mov dl,0FFh`, at 0x5bbc38 `cmp dl,0FFh`, and at 0x5bbc40
// `mov dl,[ecx+ebx]` (byte load). A 0xFF cell visited while no reference exists yet
// re-takes the seed branch (re-stores the sentinel) and so never BECOMES the
// reference. But once a real (non-0xFF) reference is seeded, a later 0xFF cell takes
// the compare branch (0x5bbc63) and mismatches (ref != 0xFF) -> returns 0. So 0xFF
// is invisible only while seeding, not afterwards. Model ref as a signed byte so
// 0xFF aliases the -1 sentinel exactly like the binary.
bool TileIsUniform(const TileGrid* grid, int x0, int span, int y0) {
    int xEnd = x0 + span;                // v7 = a3 + a2  (lea eax,[ebx+ecx]/add edi,ecx)
    int yEnd = y0 + span;                // v12 = a4 + a3
    if (y0 > yEnd) return true;          // if (a4 > a4 + a3) return 1
    signed char ref = -1;                // v6 = -1 (dl, signed byte)
    for (int y = y0; y <= yEnd; ++y) {
        for (int x = x0; x <= xEnd; ++x) {
            i32 idx = (grid->mask & y) * grid->size + (x & grid->mask);
            signed char t = (signed char)grid->types[idx]; // mov dl,[ecx+ebx]
            if (ref == -1) {             // cmp dl,0FFh; jnz
                ref = t;                 // seed (0xFF re-seeds: aliases sentinel)
            } else if (ref != t) {       // cmp dl,[ecx+ebx]; jz
                return false;            // mismatch
            }
        }
    }
    return true;
}

} // namespace guild::render
