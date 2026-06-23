// office_recon_privilege.cpp — see office_recon_privilege.h.
// The only non-inline routine is the session-actor teardown loop; everything
// else is a pure predicate/table inline in the header.
#include "world/office_recon_privilege.h"

namespace guild::world {

// gilde.exe 0x49d9e0 — VIBE_Office_DestroySessionActors.
//
// Original (Hex-Rays):
//   for ( i = 0; i != 64; i = v2 + 4 ) {
//     while ( !*(int *)((char *)dword_11AAFC0 + i) || dword_6315BC ) {
//       i += 4;
//       if ( i == 64 ) return;
//     }
//     VIBE_Character_Destroy(*(int *)((char *)dword_11AAFC0 + i), a1);
//   }
//
// The byte offset i steps by 4 over a 64-byte (16-slot) table. The inner while
// skips null slots; the `|| dword_6315BC` term means once the abort flag is set
// no further actor is ever destroyed (every remaining slot is "skipped" until
// i == 64 and the function returns). We translate the byte-offset loop to slot
// indices 1:1 and return the destroy count (a faithful, observable side effect
// of the hooked VIBE_Character_Destroy calls).
int OfficeDestroySessionActors(const i32 actors[kOfficeSessionActorSlots],
                               bool abortFlag,
                               OfficeDestroyActorHook hook, void* ctx) {
    int destroyed = 0;
    int slot = 0; // i/4
    while (slot != kOfficeSessionActorSlots) {       /*0x49d9e3: i != 64*/
        // Inner skip: advance past null slots, and short-circuit entirely once
        // the abort flag is set (mirrors `|| dword_6315BC`).
        while (actors[slot] == 0 || abortFlag) {     /*0x49d9f9*/
            ++slot;                                  /*0x49d9fb*/
            if (slot == kOfficeSessionActorSlots)    /*0x49da01*/
                return destroyed;                    // early return
        }
        if (hook)
            hook(actors[slot], ctx);                 /*0x49da07: VIBE_Character_Destroy*/
        ++destroyed;
        // outer `i = v2 + 4`: v2 is the (already-advanced) slot, +4 == next slot.
        ++slot;
    }
    return destroyed;
}

} // namespace guild::world
