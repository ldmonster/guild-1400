// real_hooks3_apply2 — companion TU to real_hooks3.cpp for the command_apply2
// He-find hook only. Glue only.
//
// command_apply2.h declares its OWN 176-byte `guild::sim::HeRecord`, which
// conflicts with he.h's full HeRecord that real_hooks3.cpp must include (for the
// HandlerTable / command builders). The two definitions cannot coexist in one
// translation unit, so the SetHeFindHook wiring is isolated here: this TU sees
// ONLY command_apply2.h's HeRecord, and reaches the shared real He pool through
// the raw `void*`-returning helper RealHeFindHandlerRaw() implemented in
// real_hooks3.cpp (which returns a handler-pool record base; both HeRecord views
// alias the same leading pool-slot bytes, so the cast is byte-faithful).
//
// This file contains NO module logic.
#include "sim/command_apply2.h"   // HeRecord (176B view) / HeFindFn / SetHeFindHook

namespace guild::sim {

// Implemented in real_hooks3.cpp (the he.h side of the wiring): find the first
// live handler-pool record whose +16 field matches `id` (the same
// VIBE_He_FindFirstHandlerByFilter selector the original He-find leaf used) and
// return its base as an opaque pointer.
void* RealHeFindHandlerRaw(i32 id);

namespace {

// command_apply2's HeFindFn is `HeRecord* (*)(i32 id)`. Adapt the raw helper's
// void* to that signature; both view the same pool-slot bytes (+0 kind, +4
// ordinal, +16 field).
HeRecord* RealHeFind(i32 id) {
    return reinterpret_cast<HeRecord*>(RealHeFindHandlerRaw(id));
}

} // namespace

// Wires command_apply2's SetHeFindHook to the real He pool. Called by
// InstallRealSimHooks3() via this thin entry so the public installer stays a
// single call (see real_hooks3.cpp note about the TU split).
void InstallRealApply2HeFind() {
    SetHeFindHook(&RealHeFind);
}

} // namespace guild::sim
